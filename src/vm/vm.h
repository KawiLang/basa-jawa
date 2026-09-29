// VM Basa Jawa: eksekusi bytecode stack-based.
//
// DESAIN KUNCI (lihat DECISIONS.md D-003)
// ---------------------------------------
// Seluruh eksekusi bytecode berjalan di dalam SATU loop `VM::jalankan_loop()`.
// Frame dan upvalue disimpan pada array yang dikelola VM, BUKAN pada stack
// C++. Konsekuensinya:
//   * rekursi tak hingga pada kode Basa Jawa -> `KleruRentang`, bukan crash;
//   * `metokake`/`enteni` dapat keluar-masuk loop lewat sinyal `Suspend`;
//   * reentrancy native->JS dikontrol kedalaman (`--maks-tumpukan`).
//   * rantai `async` ditunda dengan menyalin frame, bukan fiber (D-023).
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gc/heap.h"
#include "rt/object.h"
#include "rt/value.h"
#include "support/arena.h"
#include "support/source_map.h"
#include "vm/chunk.h"

namespace jawa::vm {

using rt::ArrayObj;
using rt::ClassObj;
using rt::ClosureObj;
using rt::InstanceObj;
using rt::NativeFnObj;
using rt::ObyekObj;
using rt::TeksObj;
using rt::Value;
using support::SourcePos;

class VM;
class Module;

/// Upvalue (cell) untuk closure. `open` = masih menunjuk ke slot stack frame.
///
/// Owned by VM: `open_upvalues_` menyimpan daftar sel yang masih terbuka; sel
/// yang sudah ditutup dibebaskan. Kita memakai `std::vector<Upvalue>` sebagai
/// arena agar tidak ada `new`/`delete` mentah di luar heap GC.
struct Upvalue {
    Value* lokasi = nullptr;      ///< nullptr bila sudah `close`
    Value nilai = Value::mboh();  ///< nilai bila sudah `close`
    bool open() const noexcept { return lokasi != nullptr; }
    void close() noexcept {
        if (lokasi == nullptr) return;
        nilai = *lokasi;
        lokasi = nullptr;
    }
    [[nodiscard]] Value get() const noexcept { return lokasi != nullptr ? *lokasi : nilai; }
    void set(Value v) noexcept {
        if (lokasi != nullptr) {
            *lokasi = v;
        } else {
            nilai = v;
        }
    }
};

/// Frame pemanggilan.
struct Frame {
    ClosureObj* closure = nullptr;
    const Chunk* chunk = nullptr;
    std::size_t ip = 0;          ///< instruction pointer
    std::size_t stack_base = 0;  ///< indeks nilai pertama frame
    std::size_t slot_base = 0;   ///< indeks slot lokal pertama
    int closure_idx = 0;         ///< closure yang sedang berjalan
    int baris = 0;               ///< baris terakhir untuk error

    /// Nilai `this` untuk method.
    Value this_val = Value::mboh();
    /// Panjang argumen yang dipanggil.
    std::size_t n_argumen = 0;
    /// Frame adalah generator/fiber yang bisa disuspend.
    bool suspendable = false;
    /// Indeks slot tempat hasil `RETURN` harus ditulis (dipakai `NEW` supaya
    /// instans yang sudah di-push tidak ikut hilang saat frame di-unwind).
    /// `kTanpaTarget` = hasil didorong di atas stack seperti biasa.
    static constexpr std::size_t kTanpaTarget = static_cast<std::size_t>(-1);
    /// Hasil `RETURN` dibuang; stack hanya dipangkas ke `slot_base`. Dipakai
    /// `NEW`: nilai balik konstruktor diabaikan, instans tetap di puncak.
    static constexpr std::size_t kBuangHasil = static_cast<std::size_t>(-2);
    std::size_t target_balas = kTanpaTarget;
    /// Handler `coba` aktif pada frame ini.
    struct Handler {
        std::size_t handler_tangkep = 0;
        std::size_t handler_intriguasan = 0;
        std::size_t stack_base = 0;
        std::size_t tangkep_slot = 0;
    };
    std::vector<Handler> handlers;

    /// Janji yang harus diselesaikan saat frame async ini selesai
    /// (`mengko gawe f() { ... }` menghasilkan Janji).
    Value janji_async = Value::mboh();
    /// Frame ini adalah akar rantai `async` (bukan pemanggilan `mengko` bersarang).
    bool akar_async = false;
};

/// Modul: satu berkas .jw yang telah dikompilasi.
struct ModuleRecord {
    std::string nama;       ///< nama cache (path kanonik)
    std::string path;       ///< path berkas
    std::string_view sumber;
    ClosureObj* entri = nullptr;    ///< closure fungsi modul
    Value ekspor;                    ///< objek ekspor
    std::unordered_map<std::string, Value> global;  ///< variabel modul
    bool dievaluasi = false;
    /// Sedang dievaluasi (sudah ada di `VM::modul_tumpukan_`). Mencegah impor
    /// siklik: `a impor b; b impor a;` tidak menyebabkan rekursi tak hingga —
    /// impor kedua melihat ekspor yang sudah terkumpul sejauh ini.
    bool sedang = false;
    /// Impor siklik terdeteksi saat modul ini dievaluasi.
    bool impor_siklik = false;
};

/// Opsi runtime.
struct VMOptions {
    bool strict_titik_koma = false;
    bool strict_krama = false;
    bool tco = true;
    bool gc_stress = false;
    bool log_gc = false;
    bool trace = false;
    bool stat = false;
    std::size_t maks_langkah = 0;      ///< 0 = tanpa batas
    std::size_t maks_memori_mb = 0;    ///< 0 = tanpa batas
    std::size_t maks_tumpukan = 10000;  ///< kedalaman frame
    std::size_t maks_reentrancy = 64;  ///< kedalaman native->JS
    /// Bila diisi, `tulis` menulis ke buffer ini alih-alih stdout (dipakai test).
    std::string* keluaran = nullptr;
    /// Pembaca berkas untuk linker modul ES. Mengembalikan `false` bila berkas
    /// tidak ada / tidak bisa dibaca. Kosong = pakai `<fstream>`.
    ///
    /// Disuntikkan supaya unit test bisa menguji linker tanpa menyentuh
    /// sistem berkas, dan supaya embedding bisa menyediakan filesystem sendiri.
    std::function<bool(const std::string& path, std::string& keluar)> baca_berkas;
};

/// Status eksekusi.
enum class Status : uint8_t { Jalan, Selesai, Galat, Suspend };

/// Galat runtime yang dilempar lewat `uncal`.
struct GalatRuntime {
    Value nilai = Value::mboh();
    bool ada = false;
};

/// Rantai `async` yang disuspensi `entani` pada Janji yang masih menunggu.
///
/// Berisi SALINAN frame dan nilai stack, karena stack VM dipakai ulang oleh
/// kode lain sementara rantai ini tertunda. Upvalue yang menunjuk ke rentang
/// stack yang disalin ditutup lebih dulu (`Upvalue::close`), sehingga tidak ada
/// pointer menggantung setelah stack berubah.
struct Lanjutan {
    std::vector<Frame> frame;
    std::vector<Value> stack;
    /// Indeks stack saat rantai dimulai (tempat nilai hasil `entani` diletakkan).
    std::size_t slot_base = 0;
    /// Janji yang sedang ditunggu.
    rt::Value janji = rt::Value::mboh();
    bool ditolak = false;
    bool dipakai = false;
};

class VM {
public:
    explicit VM(const VMOptions& opt = {});
    ~VM();
    VM(const VM&) = delete;
    VM& operator=(const VM&) = delete;

    gc::Heap& heap() noexcept { return heap_; }
    [[nodiscard]] const gc::Heap& heap() const noexcept { return heap_; }
    [[nodiscard]] const VMOptions& opsi() const noexcept { return opt_; }

    // ------------------------------------------------------------ eksekusi
    /// Jalankan program dari sumber (kompilasi internal).
    Status jalankan_sumber(std::string_view sumber, std::string_view nama_berkas, std::string_view dir = ".");

    /// Jalankan closure yang sudah dikompilasi.
    Status jalankan(ClosureObj* entry, int n_argumen = 0);

    /// Jalankan loop bytecode untuk frame teratas (dipakai reentrancy).
    Status jalankan();
    void panggil_closure(ClosureObj* fn, int n_argumen);

    // ------------------------------------------------------------ async/entani
    /// Jalankan loop acara: tirukan antrean mikrotugas sampai habis, lalu
    /// nyalakan timer yang tersisa. Dipanggil sekali setelah kode sinkron selesai.
    /// Mengembalikan `true` bila ada pekerjaan yang menghasilkan keluaran.
    bool jalankan_loop_acara();
    /// Alokasikan Janji baru berstatus menunggu.
    rt::JanjiObj* buat_janji();
    /// Selesaikan Janji (memicu rantai `async` & `.then` yang menunggu).
    void selesaikan_janji(rt::JanjiObj* j, Value nilai, bool ditolak);
    /// Daftarkan Janji sebagai hasil pemanggilan fungsi `mengko`.
    rt::Value panggil_async(ClosureObj* fn, Value this_val, std::vector<Value>& args);
    /// Jadwalkan penyelesaian Janji pada putaran timer berikutnya (`Wektu.tundha`).
    /// Nilai Janji diisi `nilai` saat dinyalakan.
    void jadwal_timer(std::uint64_t tunda_ms, rt::JanjiObj* j, Value nilai = Value::mboh());
    /// Jadwalkan handler `.then`/`.tangkep` sebagai mikrotugas (dipakai saat
    /// Janji sudah selesai SEBELUM handler didaftarkan).
    void jadwal_mikrotugas(rt::JanjiObj* janji, Value nilai, bool ditolak, Value fungsi, Value turunan);
    /// Janji sebagai `Value` (helper untuk kode native).
    [[nodiscard]] static Value nilai_janji(rt::JanjiObj* j) noexcept {
        return j == nullptr ? Value::mboh() : Value::obyek(j);
    }

    /// Panggil fungsi dari kode native (reentrancy).
    Value panggil(Value callee, Value this_val, const std::vector<Value>& args);

    /// Whether ada galat yang belum tertangani.
    [[nodiscard]] bool ada_galat() const noexcept { return galat_.ada; }
    [[nodiscard]] const GalatRuntime& galat() const noexcept { return galat_; }
    void bersihkan_galat() noexcept { galat_ = GalatRuntime{}; }
    void lempar(Value v);

    // -------------------------------------------------------------- stack
    void dorong(Value v) { stack_.push_back(v); }
    [[nodiscard]] Value ambil() {
        Value v = stack_.back();
        stack_.pop_back();
        return v;
    }
    [[nodiscard]] Value& puncak(std::size_t dari_atas = 0) { return stack_[stack_.size() - 1 - dari_atas]; }
    [[nodiscard]] std::size_t tinggi_stack() const noexcept { return stack_.size(); }

    // ------------------------------------------------------------- objek
    /// Ambil properti dari objek (dengan rantai prototipe).
    rt::Value ambil_properti(rt::Obj* o, rt::Value kunci);
    /// `obj[kunci]`.
    void dorong_index(rt::Value obj, rt::Value kunci);
    /// `obj[kunci] = nilai`.
    void set_index_value(rt::Value obj, rt::Value kunci, rt::Value nilai);
    /// Ambil atau tulis properti class instance.
    void instance_set(InstanceObj* inst, std::string_view nama, rt::Value nilai);
    void dorong_index_helper(rt::Obj* o, rt::Value kunci);
    /// Cari getter (`nampa`) pada rantai prototipe; `true` bila ditemukan.
    bool cari_getter(rt::Value obj, rt::Value kunci, rt::Value& keluar);
    /// Cari handler `coba` terdekat; `true` bila galat tertangani.
    bool unwind_galat(rt::Value v);
    /// Buat objek kleru (`jeneng` + `pesan`) untuk dilempar lewat `uncal`.
    rt::Value buat_kleru(std::string_view jeneng, std::string pesan);

    // ---------------------------------------------------------- objek helper
    ArrayObj* buat_dhaptar(std::size_t kapasitas = 4);
    ObyekObj* buat_obyek();
    TeksObj* buat_teks(std::string_view s);
    ObyekObj* prototipe_dasar();
    ArrayObj* prototipe_dhaptar();

    // --------------------------------------------------------------- modul
    void daftarkan_modul(std::string_view nama, std::string_view path, std::string_view sumber);
    [[nodiscard]] ModuleRecord* cari_modul(std::string_view nama) const;
    Value ambil_global(std::string_view nama);
    void set_global(std::string_view nama, Value v);
    /// Linker modul ES: muat, kompilasi, dan evaluasi modul, lalu kembalikan
    /// objek ekspornya. `dari_dir` = direktori modul pengimpor (untuk path relatif).
    Value muat_modul(std::string_view spesifikasi, std::string_view dari_dir);
    /// Resolusi spesifikasi modul -> path kanonik (kunci cache).
    static std::string selesaikan_path(std::string_view spesifikasi, std::string_view dari_dir);
    /// Kompilasi sumber menjadi closure modul. Menulis diagnostik ke stderr
    /// dan mengembalikan `nullptr` bila gagal.
    ClosureObj* kompilasi_modul(std::string_view sumber, std::string_view nama_berkas,
                                std::string_view dir);
    /// Evaluasi body modul di frame-nya sendiri lalu kembalikan objek ekspornya.
    /// Mengembalikan `mboh` bila modul gagal.
    Value evaluasi_modul(ModuleRecord* rec);

    // --------------------------------------------------------- error/trace
    std::string jejak_stack() const;
    void set_posisi_sumber(std::string_view berkas, SourcePos pos);
    /// Simpan string agar `string_view` yang dialingkatkan ke heap tetap hidup.
    std::string_view singsan(std::string_view s) {
        singsahan_teks_.emplace_back(s);
        return singsahan_teks_.back();
    }

    // ------------------------------------------------------------- utilitas
    [[nodiscard]] std::size_t langkah() const noexcept { return langkah_; }
    void reset_langkah() noexcept { langkah_ = 0; }
    void registrasikan_root_visitor(std::function<void(gc::RootVisitor&)> fn);

public:
    /// Status galat terakhir (dipakai loop eksekusi & kode native).
    GalatRuntime galat_;

private:
    friend struct LoopAkses;
    /// Loop bytecode dengan penjaga kedalaman eksplisit. `kedalaman_nol` = 0
    /// menjalankan sampai `frames_` kosong (dipakai resume rantai `async`).
    Status jalankan_loop(std::size_t kedalaman_awal);
    /// Suspend rantai `async` saat ini karena `entani` Janji yang menunggu.
    /// Mengembalikan `true` bila rantai berhasil disimpan.
    bool suspensi_async(rt::JanjiObj* j);
    /// Pulihkan rantai `async` yang ditunda lalu jalankan sampai selesai.
    void lanjutkan_async(Lanjutan* lan, Value hasil, bool ditolak);
    /// Jalankan satu mikrotugas dari antrean; `false` bila antrean kosong.
    bool jalankan_mikrotugas();
    bool cek_batas_langkah();
    [[nodiscard]] Frame& frame_sekarang() { return frames_.back(); }
    void reset_stack();
    void panggil_objek(Value callee, Value this_val, std::vector<Value>& args);
    friend struct LoopAkses;
    void mulai_frame(ClosureObj* fn, Value this_val, std::vector<Value>& args,
                    std::size_t target_balas = static_cast<std::size_t>(-1));
    /// Alokasikan instans kosong untuk sebuah class.
    /// Jalankan generator (`gawe*`) mode-eager: kumpulkan hasil `metokake`.
    void jalankan_generator_eager(ClosureObj* fn, rt::Value this_val, std::vector<rt::Value>& args);
    rt::ClassObj* kelas_dari(rt::Value v) const;
    rt::InstanceObj* instans_baru(rt::ClassObj* kls);
    /// Tutup semua upvalue terbuka pada frame yang baru selesai.
    void tutup_upvalue_frame(std::size_t stack_base);
    /// Sel upvalue untuk slot stack tertentu (dibuat bila belum ada).
    Upvalue* cari_atau_buat_upvalue(std::size_t stack_index);


    VMOptions opt_;
    gc::Heap heap_;
    /// Stack nilai. WAJIB `std::deque` (bukan `vector`): sel upvalue menyimpan
    /// POINTER ke slot stack, dan `deque` menjamin alamat elemen existing tidak
    /// berubah saat `push_back` (lihat `cari_atau_buat_upvalue`).
    std::deque<Value> stack_;
    std::vector<Frame> frames_;
    /// Sel upvalue terbuka (urutan tidak menurun menurut indeks stack).
    std::vector<Upvalue*> open_upvalues_;
    /// Target `metokake` untuk generator mode-eager (lihat `panggil_objek`).
    std::vector<rt::ArrayObj*> tumbles_yield_;
    /// Indeks stack tempat spread terakhir dimulai (lihat `SPREAD_PUSH`).
    std::vector<std::size_t> spread_base_;
    /// Arena sel upvalue yang sudah ditutup (dimiliki VM, dibersihkan penuh).
    std::vector<std::unique_ptr<Upvalue>> sel_tutup_;
    // ------------------------------------------------------------- async
    /// Indeks frame akar rantai `async` yang sedang berjalan; `kTanpaAsync`
    /// bila tidak ada rantai aktif.
    static constexpr std::size_t kTanpaAsync = static_cast<std::size_t>(-1);
    std::size_t dasar_async_ = kTanpaAsync;
    /// Rantai `async` yang disuspensi (pemilik; Janji hanya menyimpan pointer).
    std::vector<std::unique_ptr<Lanjutan>> lanjutian_;
    /// Antrean mikrotugas: Janji yang selesai dijadwalkan di sini agar berjalan
    /// setelah seluruh kode sinkron selesai (sifat "microtask" ECMAScript).
    struct Mikrotugas {
        enum class Jenis : uint8_t { LanjutAsync, HandlerThen };
        Jenis jenis = Jenis::HandlerThen;
        rt::JanjiObj* janji = nullptr;  ///< Janji yang baru selesai
        Value nilai = Value::mboh();   ///< nilai hasil / alasan penolakan
        bool ditolak = false;
        /// `LanjutAsync`: rantai `async` yang menunggu.
        std::vector<Lanjutan*> lanjutan;
        /// `HandlerThen`: handler `.then`/`.tangkep` + Janji turunannya.
        Value fungsi = Value::mboh();
        Value turunan = Value::mboh();
    };
    std::deque<Mikrotugas> antrean_mikrotugas_;
    /// Timer (`Wektu.tundha`): diurutkan berdasarkan tunda, dinyalakan setelah
    /// seluruh mikrotugas habis.
    struct Timer {
        std::uint64_t tunda_ms = 0;
        std::uint64_t urutan = 0;
        rt::JanjiObj* janji = nullptr;
        Value nilai = Value::mboh();
    };
    std::vector<Timer> pekerja_;
    std::uint64_t urutan_timer_ = 0;
    std::unordered_map<std::string, ModuleRecord*> modul_;
    std::deque<ModuleRecord> modul_store_;
    std::size_t langkah_ = 0;
    int reentrancy_ = 0;
    std::string pos_berkas_;
    /// Penyimpanan string milik VM supaya `string_view` objek kleru tetap hidup.
    std::deque<std::string> singsahan_teks_;
    SourcePos pos_sumber_;
    bool dalam_reentrancy_ = false;
    std::vector<std::function<void(gc::RootVisitor&)>> root_visitor_;
    /// Cache prototype (root GC; bukan `static` supaya bebas bila ada >1 VM).
    Value proto_dasar_ = Value::mboh();
    Value proto_dhaptar_ = Value::mboh();

public:
    /// Tumpukan modul yang sedang dievaluasi. Modul A yang mengimpor B
    /// dievaluasi sementara A masih berjalan, jadi setiap modul punya cakupan
    /// global sendiri; `modul_aktif` adalah puncak tumpukan ini.
    std::vector<ModuleRecord*> modul_tumpukan_;
    /// Modul aktif = puncak tumpukan; `nullptr` bila di luar modul.
    ModuleRecord* modul_aktif = nullptr;
    /// Arena sementara untuk parse (dipakai `jalankan_sumber`).
    support::Arena arena_scratch_;

    /// Konteks bebas untuk embedder (REPL, native).
    void* konteks = nullptr;
};

}  // namespace jawa::vm
