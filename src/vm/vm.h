// VM Basa Jawa: eksekusi bytecode stack-based.
//
// DESAIN KUNCI (lihat DECISIONS.md D-003)
// ---------------------------------------
// Seluruh eksekusi bytecode berjalan di dalam SATU loop `VM::execute()`. Frames
// prowess dan upvalue disimpan pada array yang dikelola VM, BUKAN pada stack
// C++. Konsekuensinya:
//   * rekursi tak hingga pada kode Basa Jawa -> `KleruRentang`, bukan crash;
//   * `metokake`/`enteni` dapat keluar-masuk loop lewat sinyal `Suspend`;
//   * reentrancy native->JS dikontrol kedalaman (`--max-tumpukan`).
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
};

/// Modul: satu berkas .jw yang telah dikompilasi.
struct ModuleRecord {
    std::string_view nama;
    std::string_view path;
    std::string_view sumber;
    ClosureObj* entri = nullptr;    ///< closure fungsi modul
    Value ekspor;                    ///< objek ekspor
    std::unordered_map<std::string, Value> global;  ///< variabel modul
    bool dievaluasi = false;
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
};

/// Status eksekusi.
enum class Status : uint8_t { Jalan, Selesai, Galat, Suspend };

/// Galat runtime yang dilempar lewat `uncal`.
struct GalatRuntime {
    Value nilai = Value::mboh();
    bool ada = false;
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
    Status jalankan_loop();
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
    /// Modul yang sedang dievaluasi (untuk impor).
    ModuleRecord* modul_aktif = nullptr;
    /// Arena sementara untuk parse (dipakai `jalankan_sumber`).
    support::Arena arena_scratch_;

    /// Konteks bebas untuk embedder (REPL, native).
    void* konteks = nullptr;
};

}  // namespace jawa::vm
