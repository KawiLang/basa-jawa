// Model objek Basa Jawa.
//
// Semua objek dialokasikan pada `gc::Heap` dan ditandai presisi oleh
// mark-and-sweep non-moving. Bentuk objek:
//
//   +0   : ObjHeader { kind, flags, mark }
//   +8   : payload objek (Shape* / data / native fn / ...)
//   ...  : slot inline (Nilai inline untuk Obyek/Array)
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rt/value.h"
#include "support/source_map.h"
#include "vm/chunk.h"

namespace jawa::gc {
class Heap;
}  // namespace jawa::gc

namespace jawa::vm {
class VM;
struct Upvalue;
/// Lanjutan (continuation) rantai `async` yang disuspensi; lihat `VM::suspensi_async`.
struct Lanjutan;
}  // namespace jawa::vm

namespace jawa::rt {

using Heap = ::jawa::gc::Heap;
using VM = ::jawa::vm::VM;

using support::SourcePos;
using support::SourceRange;

class FungsiObj;
class ClosureObj;
class ClassObj;
class GolonganInstance;
class JanjiObj;
class PetaObj;
class HimpunanObj;
class RegexObj;
class TanggalObj;
class KleruObj;
class SelObj;
class SimbolObj;
class BigIntObj;
class GeneratorObj;
class ArrayObj;
class TeksObj;
class ObyekOra;

class Shape;
class RegexProgram;
class Tanggal;

// ---------------------------------------------------------------------------
// Header objek
// ---------------------------------------------------------------------------

/// Jenis objek (dispatch tanpa RTTI).
enum class OK : uint8_t {
    Obyek,
    Array,
    Teks,
    Fungsi,
    Closure,
    Native,
    Golongan,      ///< konstruktor class
    Instance,      ///< instans class
    Janji,
    Peta,
    Himpunan,
    Regex,
    Tanggal,
    Kleru,
    Simbol,
    BigInt,
    Generator,     ///< hasil pemanggilan `gawe*` (lazy; lihat D-028)
    Sel,           ///< sel nilai yang dipakai live binding modul ES
    BoundFn,
    Proxy,         ///< belum didukung
    ArrayBuffer,   ///< typed array
    WeakRef,
};

const char* ok_name(OK k) noexcept;

/// Perbandingan nilai untuk kunci properti/peta.
///
/// `Value::operator==` membandingkan bit, sehingga dua `TeksObj` dengan isi
/// sama tapi berbeda alamat akan dianggap berbeda. Kunci properti/peta perlu
/// semantik "nilai sama": teks dibandingkan isinya, angka dibandingkan
/// numerik, sisanya dibandingkan bit.
[[nodiscard]] bool nilai_sama(const Value& a, const Value& b) noexcept;

/// Penanda objek.
struct ObjHeader {
    OK kind = OK::Obyek;
    std::uint8_t mark = 0;      ///< 0 = putih, 1 = hitam, 2 = abu-abu (incremental)
    std::uint8_t flags = 0;     ///< lihat ObjFlags
    std::uint16_t aban = 0;     ///<Umur penandaan (untuk generational)
};

/// Bendera objek.
enum ObjFlags : std::uint8_t {
    FlagFrozen = 1u << 0,      ///< tidak bisa diubah
    FlagSealed = 1u << 1,      ///< tidak bisa tambah properti baru
    FlagExtensible = 1u << 2,  ///< default: 1
    FlagFungsi = 1u << 3,      ///< bisa dipanggil sebagai fungsi
    FlagKelas = 1u << 4,       ///< bisa dipakai `anyar`
    FlagGenerator = 1u << 5,   ///< generator
    FlagAsync = 1u << 6,
    FlagBound = 1u << 7,
};

/// Basis semua objek.
struct Obj {
    ObjHeader h;
    virtual ~Obj() = default;  // NOLINT: virtual hanya untuk safety; dispatch utama pakai `h.kind`
};

// ---------------------------------------------------------------------------
// Bentuk / atribut properti
// ---------------------------------------------------------------------------

/// Atribut properti.
enum PropAttr : std::uint8_t {
    AttrNone = 0,
    AttrWritable = 1u << 0,
    AttrEnumerable = 1u << 1,
    AttrConfigurable = 1u << 2,
    AttrAccessor = 1u << 3,  ///< properti adalah getter/setter
    AttrPrivate = 1u << 4,
    AttrDefault = AttrWritable | AttrEnumerable | AttrConfigurable,
};

struct Properti {
    Value kunci{};       ///< Teks (interned) atau Simbol
    Value getter{};      ///< fungsi getter (atau kosong)
    Value setter{};      ///< fungsi setter (atau kosong)
    std::uint8_t attr = AttrDefault;
    std::int32_t index = -1;  ///< slot index, -1 bila tidak punya slot
};

/// Hidden class: daftar properti + peta transisi.
class Shape {
public:
    Properti* find(Value kunci) const noexcept {
        if (kunci.is_obyek() == false && kunci.is_simbol() == false) return nullptr;
        for (std::size_t i = 0; i < jumlah_; ++i) {
            if (nilai_sama(properti_[i].kunci, kunci)) return &properti_[i];
        }
        return nullptr;
    }

    [[nodiscard]] const Properti& at(std::size_t i) const noexcept { return properti_[i]; }
    [[nodiscard]] std::size_t jumlah() const noexcept { return jumlah_; }
    [[nodiscard]] std::int32_t slot_of(std::size_t i) const noexcept { return properti_[i].index; }
    [[nodiscard]] std::size_t hash() const noexcept { return id_; }

    /// Buat shape baru = shape ini + properti (untuk transisi).
    [[nodiscard]] Shape* tambah(Value kunci, std::uint8_t attr, std::int32_t index) const;
    /// Ubah index slot pada posisi tertentu (shape baru).
    [[nodiscard]] Shape* dengan_slot(std::size_t posisi, std::int32_t index) const;

    static std::size_t jumlah_dibuat() noexcept { return jumlah_dibuat_; }

private:
    friend class ShapeTable;
    Shape() = default;
    Shape(const Shape&) = delete;
    Shape& operator=(const Shape&) = delete;
    ~Shape() = default;

    static std::size_t jumlah_dibuat_;
    std::size_t id_ = 0;
    std::size_t jumlah_ = 0;
    std::size_t kapasitas_ = 0;
    Properti* properti_ = nullptr;
    /// Link untuk GC (shape tidak di-mark; dibersihkan lewat tabel shape)
    Shape* gc_next_ = nullptr;
    bool dictionary_ = false;
};

/// Tabel shape global: shape bersifat immutable & interned (dibagikan antar objek).
class ShapeTable {
public:
    static ShapeTable& instance();

    /// Dapatkan shape dasar objek kosong.
    Shape* kosong() const noexcept { return kosong_; }
    /// Dapatkan shape dengan satu properti `constructor` (fungsi).
    Shape* fungsi() const noexcept { return fungsi_; }
    /// Transisi: tambah properti pada `dari`.
    Shape* transisi(const Shape* dari, Value kunci, std::uint8_t attr, std::int32_t index) const;

    void daftarkan(Shape* s) const;  // untuk sweep (tabel mutable)

private:
    ShapeTable();
    mutable std::vector<Shape*> semua_;
    Shape* kosong_ = nullptr;
    Shape* fungsi_ = nullptr;
    std::vector<std::vector<Shape*>> bucket_;

};

/// Kunci interned (Value Teks dengan flag interned) -> id.
class StringTable {
public:
    static StringTable& instance();
    /// Intern string; kembalikan Value Teks yang sama untuk isi sama.
    Value intern(std::string_view s);
    /// Buat TeksObj baru (tidak di-intern), dipakai untuk hasil runtime.
    [[nodiscard]] static TeksObj* buat(Heap& heap, std::string_view s);
    /// Cari tanpa membuat.
    [[nodiscard]] Value cari(std::string_view s) const noexcept;
    [[nodiscard]] std::size_t jumlah() const noexcept { return masuk_.size(); }
    /// Bersihkan entri mati (dipanggil sweep GC).
    void bersihkan_mati(std::vector<TeksObj*>& hidup);
    void daftarkan_semua(std::vector<TeksObj*>& keluar) const;
    void reset();

private:
    StringTable();
    std::vector<TeksObj*> masuk_;
    std::unordered_map<std::string, TeksObj*> peta_;
    bool aktif_ = false;
};

// ---------------------------------------------------------------------------
// Teks
// ---------------------------------------------------------------------------
class TeksObj : public Obj {
public:
    static constexpr OK kKind = OK::Teks;

    [[nodiscard]] const std::string& str() const noexcept { return data_; }
    [[nodiscard]] std::size_t dawa() const noexcept { return panjang_cp_; }
    [[nodiscard]] std::size_t dawa_bita() const noexcept { return data_.size(); }

    void hitung_panjang();
    std::string_view slice(std::size_t byte_mulai, std::size_t byte_akhir) const noexcept {
        return std::string_view(data_).substr(byte_mulai, byte_akhir - byte_mulai);
    }
    /// Indeks byte untuk code point ke-`i` (dari awal).
    [[nodiscard]] std::size_t offset_code_point(std::size_t i) const;

    void hash();
    [[nodiscard]] std::size_t hash_nilai() const noexcept { return hash_; }

    /// Gabung rope-like: untuk hasil `+` panjang kita tetap konkret (dokumentasi).
    static TeksObj* gabung(Heap& heap, const std::string& a, const std::string& b);

private:
    std::string data_;
    std::size_t panjang_cp_ = 0;
    mutable std::size_t hash_ = 0;
    mutable bool hash_valid_ = false;
    bool interned_ = false;
    friend class StringTable;
    friend TeksObj* buat_teks_dari(std::string, ::jawa::gc::Heap&);
    friend TeksObj* gabung(::jawa::gc::Heap&, const std::string&, const std::string&);
};

// ---------------------------------------------------------------------------
// Obyek biasa
// ---------------------------------------------------------------------------
class ObyekObj : public Obj {
public:
    static constexpr OK kKind = OK::Obyek;

    Shape* shape = nullptr;
    Value* slot = nullptr;      ///< slot inline (nullptr bila dictionary mode)
    std::size_t jumlah_slot = 0;
    /// Dictionary mode: pasangan (kunci, nilai) bila `slot == nullptr`.
    std::vector<std::pair<Value, Value>> dict;
    /// Pasangan accessor: kunci -> {nampa (getter), setel (setter)}.
    struct Aksesor {
        Value kunci;
        Value getter = Value::mboh();
        Value setter = Value::mboh();
    };
    std::vector<Aksesor> aksesor;
    Value prototipe = Value::mboh();

    ~ObyekObj() override { ::operator delete(static_cast<void*>(slot)); }

    void init(Shape* s, std::size_t n_slot);
    Properti* cari(Value kunci) noexcept;
    bool define(Heap& heap, Value kunci, Value nilai, std::uint8_t attr);
    bool get(Value kunci, Value& keluar) const noexcept;
    bool set(Heap& heap, Value kunci, Value nilai);
    bool hapus(Value kunci);
    /// Daftar/temukan accessor. `getter == false` berarti seter.
    Aksesor* cari_aksesor(Value kunci) noexcept;
    void pasang_aksesor(Value kunci, Value fn, bool getter);
    void ke_dictionary(Heap& heap);
    void kompak();
    [[nodiscard]] bool dictionary() const noexcept { return slot == nullptr; }
    /// Enumerasi seluruh properti (bukan accessor) untuk disalin, mis. ke
    /// `Peta`. Bentuk dictionary maupun inline menghasilkan urutan yang sama
    /// (urutan definisi).
    template <class F>
    void each_properti(F&& f) const {
        if (slot != nullptr) {
            if (shape != nullptr) {
                for (std::size_t i = 0; i < shape->jumlah(); ++i) {
                    const Properti& p = shape->at(i);
                    if (p.index < 0 || static_cast<std::size_t>(p.index) >= jumlah_slot) continue;
                    if ((p.attr & AttrAccessor) != 0) continue;
                    f(p.kunci, slot[p.index]);
                }
            }
            return;
        }
        for (const auto& kv : dict) f(kv.first, kv.second);
    }
};

// ---------------------------------------------------------------------------
// Dhaptar
// ---------------------------------------------------------------------------
class ArrayObj : public Obj {
public:
    static constexpr OK kKind = OK::Array;

    Value* elemen = nullptr;
    std::size_t panjang = 0;
    std::size_t kapasitas = 0;
    Shape* shape = nullptr;   ///< untuk properti tambahan
    ObyekObj* ekstra = nullptr;  ///< properti non-indeks
    Value prototipe = Value::mboh();
    bool hole_free = true;    ///< Basa Jawa melarang hole

    ~ArrayObj() override { ::operator delete(static_cast<void*>(elemen)); }

    void init(Shape* s, std::size_t kap);
    /// `heap` opsional: bila diisi, pertumbuhan buffer `elemen` ikut dihitung
    /// pada `--maks-memori` (isi `elemen` di luar sizeof objek).
    void dorong(Value v, Heap* heap = nullptr);
    void set(std::size_t i, Value v, Heap* heap = nullptr);
    [[nodiscard]] Value get(std::size_t i) const noexcept { return i < panjang ? elemen[i] : Value::mboh(); }
    void perkecil(Heap* heap = nullptr);
    void ensure(std::size_t perlu, Heap* heap = nullptr);
};

// ---------------------------------------------------------------------------
// Fungsi
// ---------------------------------------------------------------------------

/// Objek fungsi bytecode.
class FungsiObj : public Obj {
public:
    static constexpr OK kKind = OK::Fungsi;
    ::jawa::vm::ChunkPtr kode;
    std::string_view nama;
    std::uint8_t jumlah_param = 0;
    bool variadic = false;
    bool arrow = false;         ///< `iki` lexical
    bool method = false;
    bool generator = false;
    bool async = false;
    bool strict_krama = false;
    bool tail_recursive = false;
    std::vector<std::string_view> nama_param;
    std::vector<::jawa::vm::ChunkPtr> children;  ///< fungsi anak (nested)
    std::vector<FungsiObj*> upvalue_def;           ///< definisi upvalue
    std::vector<std::string_view> upvalue_name;
    Value prototipe = Value::mboh();
    FungsiObj* home_object = nullptr;  ///< untuk `super`
    bool is_class_ctor = false;
    bool strict_super = false;
};

/// Native function (C++).
/// Tanda tangan fungsi native: (VM, `this`, argumen) -> nilai balik.
using NativeFn = Value (*)(VM& vm, Value this_val, std::vector<Value>& args);

class NativeFnObj : public Obj {
public:
    static constexpr OK kKind = OK::Native;
    NativeFn fn = nullptr;
    std::string_view nama;
    std::uint8_t jumlah_param = 0;
    bool variadic = false;
    Value prototipe = Value::mboh();
    /// Properti yang ditempel pada fungsi native-nya -- misalnya method statis
    /// pada konstruktor `Tanggal` (`Tanggal.dari`, `Tanggal.sekarang`).
    ///
    /// `Value` tidak punya `std::hash`, jadi vektor Pasangan (linear) yang
    /// dipakai; jumlahnya sedikit dan tidak pernah di-hot loop. Kunci & nilai
    /// harus di-root GC; `VM::daftarkan_root_visitor` yang melakukannya.
    std::vector<std::pair<Value, Value>> sifat;
};

class ClosureObj : public Obj {
public:
    static constexpr OK kKind = OK::Closure;
    FungsiObj* fungsi = nullptr;
    /// Cell upvalue. Tipe `Upvalue*` (bukan `Value`) supaya `GET_UPVAL` bisa
    /// membaca lewat indireksi; lihat `vm::Upvalue`.
    std::vector<void*> upvalue;
    std::string_view nama;
    Value prototipe = Value::mboh();
};

class BoundFnObj : public Obj {
public:
    static constexpr OK kKind = OK::BoundFn;
    Value target;
    Value this_val;
    std::vector<Value> bound;
};

// ---------------------------------------------------------------------------
// Class (constructor) & instans
// ---------------------------------------------------------------------------
class ClassObj : public Obj {
public:
    static constexpr OK kKind = OK::Golongan;
    std::string_view nama;
    Value prototipe = Value::mboh();   ///< objek prototipe (method, accessor, field statis)
    Value induk = Value::mboh();       ///< class induk
    Value konstruktor = Value::mboh(); ///< fungsi constructor
    Value wiwit_pabrik = Value::mboh();
    /// Inisialisasi field instance (`y = <ekspresi>`). Closure tanpa parameter
    /// (selain `this`) yang menjalankan `iki.<field> = <init>` berurutan.
    /// Dijalankan di `NEW` sebelum konstruktor, dari induk ke anak.
    Value inisial_field = Value::mboh();
    std::vector<Value> field_statis;  ///< nilai field instance (indeks sama dgn nama_field)
    std::vector<std::string_view> nama_field;
    /// Method & field statis (`statis` pada deklarasi class).
    std::vector<Value> nilai_statis;
    std::vector<std::string_view> nama_statis;
    /// Slot privat: objek privat per class (supaya nama sama antar class tidak bentrok).
    Value privat = Value::mboh();
    std::string_view module_asal;
};

class InstanceObj : public Obj {
public:
    static constexpr OK kKind = OK::Instance;
    ClassObj* kelas = nullptr;
    std::vector<Value> slot;   ///< field instance
    std::vector<std::string_view> nama_slot;
    Value prototipe = Value::mboh();
    Shape* shape = nullptr;
};

// ---------------------------------------------------------------------------
// Janji (Promise)
// ---------------------------------------------------------------------------
enum class JanjiStatus : uint8_t { Menunggu, Slamet, Gagal };

class JanjiObj : public Obj {
public:
    static constexpr OK kKind = OK::Janji;
    JanjiStatus status = JanjiStatus::Menunggu;
    Value hasil = Value::mboh();
    /// Callback tersimpan: [fungsi_slamet, fungsi_tolak] per_then.
    struct Then {
        Value on_slamet;
        Value on_tolak;
        Value asli;  ///< promise asal (untuk thenable)
        /// `Janji.all`/`Janji.race`: kalau `kumpulan_tujuan` terisi, entri ini
        /// BUKAN handler `.then`, melainkan penggumpalan: saat Janji ini selesai,
        /// nilainya ditulis ke `kumpulan[nama_indeks]` lalu `sisa_kumpul`
        /// dikurangi. `kumpulan`/`kumpulan_tujuan` dimiliki VM dan di-root
        /// sebagai akar GC.
        rt::JanjiObj* kumpulan_tujuan = nullptr;
        ArrayObj* kumpulan = nullptr;
        std::size_t nama_indeks = 0;
        /// Jumlah Janji yang belum selesai (hanya untuk `all`; `race` memakai 1).
        std::size_t sisa_kumpul = 0;
        /// `true` untuk `Janji.all` (tunggu semua), `false` untuk `Janji.race`.
        bool kumpul_semua = true;
    };
    std::vector<Then> then_daftar;
    std::vector<Value> tangkap_daftar;   ///< .tangkep
    std::vector<Value> intriguasan_daftar;  ///< .pungkasan
    bool ditangani = false;
    /// Rantai `async` yang menunggu Janji ini selesai (lihat `VM::suspensi_async`).
    /// Tipe `jawa::vm::Lanjutan*` (non-owning; dimiliki VM) supaya lapisan `rt`
    /// tidak perlu tahu bentuk continuation.
    std::vector<jawa::vm::Lanjutan*> lanjutan_vm;
};

// ---------------------------------------------------------------------------
// Generator
// ---------------------------------------------------------------------------

/// Status generator. `Jalan` berarti body belum selesai (suspended di
/// `metokake`, atau sedang berjalan).
enum class GeneratorStatus : uint8_t { Jalan, Selesai, Gagal };

/// Hasil pemanggilan fungsi `gawe*`.
///
/// LAZY: body fungsi baru berjalan sampai `metokake` pertama saat objek ini
/// dibuat. Tiap `metokake` menunda seluruh frame generator (disalin ke
/// `jawa::vm::Lanjutan`) dan mengembalikan kendali ke pemanggil. Generator tak
/// berhingga karena itu bisa dipakai (lihat D-028).
class GeneratorObj : public Obj {
public:
    static constexpr OK kKind = OK::Generator;
    GeneratorStatus status = GeneratorStatus::Jalan;
    /// Nilai `metokake` terakhir.
    Value nilai = Value::mboh();
    /// Nilai balik `bali` (bukan hasil `metokake`); valid setelah selesai.
    Value nilai_bali = Value::mboh();
    /// Galat yang dilempar body kalau `status == Gagal`.
    Value galat = Value::mboh();
    /// Continuation yang disuspensi (pemilik: VM, non-owning).
    jawa::vm::Lanjutan* lanjutan = nullptr;
    /// Closure fungsi generator (dipegang supaya bisa di-root & dipamerkan).
    Value fungsi = Value::mboh();
    /// Body sudah pernah sampai `metokake`? `false` = belum ada hasil sama sekali.
    bool dimulai = false;
    /// Nilai di `nilai` sudah dilaporkan ke pemanggil. Panggilan `langkah`
    /// berikutnya baru melanjutkan generator. Ini yang membuat tiap `next()`
    /// menghasilkan tepat satu `metokake` (sifat JavaScript).
    bool sudah_dibaca = false;
    /// Nilai yang diberikan pemanggil lewat `.next(nilai)`.
    Value kirim = Value::mboh();
    /// Dampar nilai balik yang diminta pemanggil lewat `.next(nilai)`.
    bool kirim_pakai = false;
};

// ---------------------------------------------------------------------------
// Peta & Himpunan
// ---------------------------------------------------------------------------
class PetaObj : public Obj {
public:
    static constexpr OK kKind = OK::Peta;
    /// Slot terbuka: (kunci, nilai, hash) untuk iterasi stabil.
    struct Entri {
        Value kunci;
        Value nilai;
        bool dihapus = false;
    };
    std::vector<Entri> entri;
    std::vector<std::size_t> tabel;  ///< index entri; 0xFFFF = kosong
    std::size_t jumlah_aktif = 0;
    Value prototipe = Value::mboh();

    void rehash();
    Entri* cari(Value kunci) noexcept;
    bool pasang(Value kunci, Value nilai);
    bool hapus(Value kunci);
};

class HimpunanObj : public Obj {
public:
    static constexpr OK kKind = OK::Himpunan;
    PetaObj isi;
    Value prototipe = Value::mboh();
};

// ---------------------------------------------------------------------------
// Regex & Tanggal
// ---------------------------------------------------------------------------
class RegexObj : public Obj {
public:
    static constexpr OK kKind = OK::Regex;
    std::string pola;
    std::string flag;
    /// Program regex yang sudah dikompilasi. `shared_ptr` ke tipe buram;
    /// lihat `rt/regexp.h`.
    std::shared_ptr<RegexProgram> program;
    std::size_t last_index = 0;
    bool global = false;
    bool abaikan_besar_kecil = false;
    bool multibaris = false;
    bool titik_semu = false;
    bool lengket = false;
};

class TanggalObj : public Obj {
public:
    static constexpr OK kKind = OK::Tanggal;
    double milidetik = 0.0;  ///< ms sejak epoch UTC
};

// ---------------------------------------------------------------------------
// Sel (live binding modul ES)
// ---------------------------------------------------------------------------

/// Satu nilai yang dibaca dan ditulis bersama oleh modul pengekspor dan semua
/// modul pengimpor-nya.
///
/// Ini yang membuat `ekspor` menjadi **live binding**: importer menyimpan sel,
/// bukan salinan nilainya. Modul pengekspor menulis lewat sel yang sama, jadi
/// perubahan di sana langsung terlihat di importer -- persis seperti
/// `export let counter` di ECMAScript.
///
/// `alias` dipakai untuk pengikatan impor. Sel pengikat dibuat lebih dulu
/// (sebelum statement `impor` dijalankan) supaya closure yang ter-hoist bisa
/// menangkapnya; begitu modulnya benar-benar diimpor, sel itu diarahkan ke sel
/// milik modul pengekspor. Tanpa ini, closure ter-hoist akan menangkap slot
/// stack yang isinya nanti diganti -- dan upvalue-nya membaca objek `SelObj`,
/// bukan isinya.
///
/// Nama pengikat impor pada modul pengimpor adalah slot lokal yang berisi
/// `SelObj` ini; opcode `GET_CELL`/`SET_CELL` menerjemahkan baca/tulis menjadi
/// akses sel.
class SelObj : public Obj {
public:
    static constexpr OK kKind = OK::Sel;
    Value nilai = Value::mboh();
    SelObj* alias = nullptr;  ///< bila tidak-null: nilai sebenarnya ada di sini

    /// Sel yang benar-benar menyimpan nilai.
    ///
    /// Rantai alias bisa lebih dari satu tingkat: re-export
    /// (`ekspor { y } saka "./tiga.jw"`) meneruskan sel milik modul lain, dan
    /// modul pengimpor mengarahkan selnya ke sel itu -- jadi `a -> b -> c`.
    /// Karena itu penelusuran dibuat sampai ke akar, bukan satu tingkat.
    [[nodiscard]] SelObj* sel_asli() noexcept {
        SelObj* s = this;
        for (int d = 0; d < 16 && s->alias != nullptr; ++d) s = s->alias;
        return s;
    }
    [[nodiscard]] Value baca() const {
        return const_cast<SelObj*>(this)->sel_asli()->nilai;
    }
    void tulis(Value v) { sel_asli()->nilai = v; }
};

// ---------------------------------------------------------------------------
// Kleru
// ---------------------------------------------------------------------------
class KleruObj : public Obj {
public:
    static constexpr OK kKind = OK::Kleru;
    std::string_view jeneng;
    std::string pesan;
    Value sebab = Value::mboh();
    std::vector<std::string> jejak;
    SourcePos pos;
    std::string_view berkas;
};

// ---------------------------------------------------------------------------
// Simbol & BigInt
// ---------------------------------------------------------------------------
class SimbolObj : public Obj {
public:
    static constexpr OK kKind = OK::Simbol;
    std::string deskripsi;
    std::size_t id = 0;
};

class BigIntObj : public Obj {
public:
    static constexpr OK kKind = OK::BigInt;
    /// Big integer sign-magnitude base 2^32 (little-endian limb).
    std::vector<std::uint32_t> limb;
    bool negatif = false;
};

}  // namespace jawa::rt
