// AST Basa Jawa.
//
// Node immutable setelah parse, dialokasikan di arena, dan selalu membawa
// `SourceRange`. Dispatch lewat `enum class NodeKind` (tanpa RTTI).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "support/arena.h"
#include "support/source_map.h"

namespace jawa::ast {

using support::SourcePos;
using support::SourceRange;

// ---------------------------------------------------------------------------
// Alias (dipakai di beberapa tempat)
// ---------------------------------------------------------------------------
using Teks = std::string_view;
using Nam = std::string_view;

/// Jenis node (dispatch tanpa RTTI).
enum class NK : uint8_t {
    // ekspresi
    Nomor,
    BigIntLit,
    TeksLit,
    TemplateLit,
    RegexLit,
    CendoKasih,
    ArrayLit,
    ObjectLit,
    Fungsi,
    RefIdent,   // rujukan pengenal
    AksesProperti,
    AksesIndeks,
    Panggilan,
   _anyar,
    Unary,
    Biner,
    Logika,
    Penugasan,
    Kondisional,
    Pembaruan,
    Rangkaian,     // x |> f
    TemplateTag,   // tag`...`
    CocogExpr,
    ImporDinamis,  // impor("...")
    ThisExpr,
    SuperExpr,
    KunciPrivat,   // #nama sebagai ekspresi (tidak boleh)
    // pola
    PolaNilai,
    PolaAlternatif,
    PolaDhaptar,
    PolaObjek,
    PolaTipe,
    // statement
    Program,
    EkspresiStmt,
    DeklarasiVar,
    Blok,
    YenStmt,
    NalikaStmt,
    LakoniStmt,
    KanggoStmt,
    KanggoOfStmt,
    KanggoInStmt,
    KanggoEnteniStmt,
    PilihStmt,
    KasusKlap,
    BaliStmt,
    MandhegStmt,
    TerusnaStmt,
    UncalStmt,
    CobaStmt,
    TangkepKlausul,
    PungkasanKlausul,
    GolonganDeklarasi,
    MetodeDeklarasi,
    EksporDeklarasi,
    ImporDeklarasi,
    EksporDefault,
    LabelStmt,
    KosongStmt,
    DebuggerStmt,
    // definisi
    FungsiDeklarasi,
    ParamDeklarasi,
    // tipe (anotasi bertahap)
    TipeAnotasi,
    TipeUnion,
    TipeArray,
    TipeTuple,
    TipeObjek,
    TipeFungsi,
    TipeReferensi,
    TipeOpsional,
    AntarmukaDeklarasi,
    AliasTipe,
    // accessor & properti
    PropertyAccessor,
    PropertiObj,
    ElemenArr,
    FieldKelas,
    KasusKocog,
    PropertiPola,
    Pola,             // semua jenis pola
    PropertiTipe,
};

/// Jenis operator biner (cerminan presedensi Bagian 3.4).
enum class BinOp : uint8_t {
    Tambah, Kurang, Kali, Bagi, Modulo, Pangkat,
    Lt, Le, Gt, Ge, Eq, Ne,
    EqKetat, NeKetat,   ///< `===` / `!==`
    instanceSaka,
    ing,
    BitDan, BitXor, BitOr,
    GeserKiri, GeserKanan, GeserKananTanpaTanda,
    Lan, Utawa, Nullish,
    Pipeline,
    Koma,
};

enum class UnOp : uint8_t { Neg, Pos, Ora, BitNot, PlusPlus, MinusMinus, Jinis, Busak, Entani, Metokake };

enum class LogOp : uint8_t { Lan, Utawa, Nullish };

enum class AssignOp : uint8_t {
    Set, Tambah, Kurang, Kali, Bagi, Modulo, Pangkat,
    GeserKiri, GeserKanan, GeserKananTanpaTanda,
    BitDan, BitOr, BitXor,
    Lan, Utawa, Nullish,
};

const char* binop_name(BinOp op) noexcept;
const char* unop_name(UnOp op) noexcept;
const char* assignop_name(AssignOp op) noexcept;

// ---------------------------------------------------------------------------
// Node dasar
// ---------------------------------------------------------------------------
struct Node {
    NK kind = NK::Program;
    SourceRange range;
};

using NodePtr = Node*;  // arena-allocated

// ---------------------------------------------------------------------------
// Literal & ekspresi dasar
// ---------------------------------------------------------------------------
/// Nilai dasar. `jenis_nilai` membedakan angka biasa dari `kosong`/`mboh`/
/// dan konstanta `DuduAngka`/`Tak_Wates` yang harus dibedakan kompilator.
struct NomorLit : Node {
    static constexpr NK kKind = NK::Nomor;
    enum class Bentuk : uint8_t { Angka, Kosong, Mboh, NaN, Infinity, Bener, Salah };
    double nilai = 0.0;
    Nam teks_mentah;
    Bentuk bentuk = Bentuk::Angka;
};

struct BigIntLit : Node {
    static constexpr NK kKind = NK::BigIntLit;
    Nam digit;          ///< digit radix (tanpa `n`)
    int radix = 10;
};

struct TeksLit : Node {
    static constexpr NK kKind = NK::TeksLit;
    std::string nilai;  ///< sudah di-decode escape
};

/// Satu bagian template: cooked atau ekspresi.
struct TemplateBagian {
    bool ekspresi = false;
    std::string teks;             ///< untuk cooked
    NodePtr ekspresi_node = nullptr;  ///< untuk ekspresi
    SourceRange range;
};

struct TemplateLit : Node {
    static constexpr NK kKind = NK::TemplateLit;
    std::vector<TemplateBagian> bagian;
    bool ada_tag = false;
    NodePtr tag = nullptr;  ///< untuk template bertag
};

struct RegexLit : Node {
    static constexpr NK kKind = NK::RegexLit;
    std::string pola;
    std::string flag;
};

/// Elemen dhaptar: nilai atau spread.
struct ElemenArr : Node {
    static constexpr NK kKind = NK::ElemenArr;
    NodePtr nilai = nullptr;
    bool spread = false;
};

struct ArrayLit : Node {
    static constexpr NK kKind = NK::ArrayLit;
    std::vector<NodePtr> elemen;
};

/// Properti object literal.
struct PropertiObj : Node {
    static constexpr NK kKind = NK::PropertiObj;
    enum class Jenis : uint8_t { Nilai, Metode, Accessor, Spread, Shorthand, Komputat } jenis = Jenis::Nilai;
    NodePtr kunci = nullptr;       ///< ekspresi kunci (untuk Nilai/Komputat)
    Nam kunci_nama;               ///< nama statis (Nilai/Shorthand/Accessor/Metode)
    NodePtr nilai = nullptr;       ///< nilai / method body holder
    NodePtr computed = nullptr;   ///< untuk Spread: ekspresi yang disebar
    bool getter = false;          ///< accessor: true = nampa
    bool setter = false;
    // tipe anotasi
    NodePtr tipe = nullptr;
    bool privat = false;
};

struct ObjectLit : Node {
    static constexpr NK kKind = NK::ObjectLit;
    std::vector<NodePtr> properti;  ///< PropertiObj*
};

// ---------------------------------------------------------------------------
// Fungsi
// ---------------------------------------------------------------------------
struct ParamDeklarasi : Node {
    static constexpr NK kKind = NK::ParamDeklarasi;
    Nam nama;
    NodePtr nilai_default = nullptr;   ///< parameter default
    NodePtr tipe = nullptr;
    bool rest = false;                ///< `...nama`
    bool destructuring = false;        ///< nama sebenarnya adalah pola
    NodePtr pola = nullptr;            ///< bila destructuring
};

struct FungsiDeklarasi : Node {
    static constexpr NK kKind = NK::FungsiDeklarasi;
    Nam nama;                 ///< kosong bila anonymous / arrow
    std::vector<NodePtr> param;  ///< ParamDeklarasi*
    NodePtr awak = nullptr;    ///< Blok*
    bool panah = false;        ///< arrow function
    bool mengko = false;       ///< async
    bool generator = false;    ///< `gawe*`
    bool ekspresi_badan = false;  ///< arrow dengan badan ekspresi
    NodePtr badan_ekspresi = nullptr;  ///< badan arrow tanpa blok
    bool metode = false;       ///< method di class/object
    Nam method_nama;          ///< nama getter/setter/constructor
    bool privat = false;
    bool statis = false;
    bool getter = false;
    bool setter = false;
    NodePtr tipe_bali = nullptr;
    std::vector<NodePtr> tipe_param;
};

/// Rujukan nama (identifier). Semua pemakaian nama di ekspresi memakai node ini.
struct RefIdent : Node {
    static constexpr NK kKind = NK::RefIdent;
    Nam nama;
};

// ---------------------------------------------------------------------------
// Akses & panggilan
// ---------------------------------------------------------------------------
struct AksesProperti : Node {
    static constexpr NK kKind = NK::AksesProperti;
    NodePtr objek = nullptr;
    Nam nama;
    NodePtr komputat = nullptr;  ///< bila `obj[k]`
    bool opsional = false;       ///< `?.`
    bool privat = false;         ///< `obj.#nama`
};

struct Panggilan : Node {
    static constexpr NK kKind = NK::Panggilan;
    NodePtr callee = nullptr;
    std::vector<NodePtr> argumen;
    bool spread_pada_call = false;
    bool opsional = false;
    Nam type_args_label;  ///< tidak dipakai (anotasi tipe ada di deklarasi)
};

struct AnyarExpr : Node {
    static constexpr NK kKind = NK::_anyar;
    NodePtr konstruktor = nullptr;
    std::vector<NodePtr> argumen;
    bool ada_argumen = false;
};

struct UnaryExpr : Node {
    static constexpr NK kKind = NK::Unary;
    UnOp op = UnOp::Neg;
    NodePtr operand = nullptr;
};

struct BinerExpr : Node {
    static constexpr NK kKind = NK::Biner;
    BinOp op = BinOp::Tambah;
    NodePtr kiri = nullptr;
    NodePtr kanan = nullptr;
};

struct LogikaExpr : Node {
    static constexpr NK kKind = NK::Logika;
    LogOp op = LogOp::Lan;
    NodePtr kiri = nullptr;
    NodePtr kanan = nullptr;
};

struct PenugasanExpr : Node {
    static constexpr NK kKind = NK::Penugasan;
    AssignOp op = AssignOp::Set;
    NodePtr target = nullptr;
    NodePtr nilai = nullptr;
    bool mashed = false;  ///< destructuring: `[a,b] = c`
};

struct KondisionalExpr : Node {
    static constexpr NK kKind = NK::Kondisional;
    NodePtr kondisi = nullptr;
    NodePtr bila_benar = nullptr;
    NodePtr bila_salah = nullptr;
};

struct PembaruanExpr : Node {
    static constexpr NK kKind = NK::Pembaruan;
    UnOp op = UnOp::PlusPlus;  // PlusPlus / MinusMinus
    NodePtr target = nullptr;
    bool prefiks = true;
};

struct RangkaianExpr : Node {
    static constexpr NK kKind = NK::Rangkaian;
    NodePtr kiri = nullptr;
    NodePtr kanan = nullptr;
};

struct ThisExpr : Node {
    static constexpr NK kKind = NK::ThisExpr;};
struct SuperExpr : Node {
    static constexpr NK kKind = NK::SuperExpr;};
struct KunciPrivatExpr : Node {
    static constexpr NK kKind = NK::KunciPrivat; Nam nama; };

struct ImporDinamisExpr : Node {
    static constexpr NK kKind = NK::ImporDinamis;
    NodePtr spesifikasi = nullptr;
};

// ---------------------------------------------------------------------------
// Pola (cocog)
// ---------------------------------------------------------------------------
struct Pola : Node {
    static constexpr NK kKind = NK::Pola;
    enum class Jenis : uint8_t {
        Wildcard,      // _
        Nama,          // x (binding)
        Literal,       // 0, "a", bener
        Alternatif,    // a | b
        Dhaptar,       // [a, b, ...sisa]
        Objek,         // {jeneng, umur: x, ...liyane}
        Tipe,          // teks_x: Teks
        Ekspresi,      // pola-seeded (nilai > 5)
    } jenis = Jenis::Wildcard;
    NodePtr nilai = nullptr;         // Literal/Ekspresi/nama
    std::vector<NodePtr> alternatif;  // Alternatif
    std::vector<NodePtr> elemen;      // Dhaptar
    std::vector<NodePtr> properti;    // Objek (PropertiPola)
    NodePtr sisanya = nullptr;       // rest
    NodePtr tipe = nullptr;          // Tipe
    NodePtr penjaga = nullptr;       // `yen (...)`
    Nam nama;                        // nama binding (Nama/Tipe)
    bool ada_nama = false;           // pola alias: `nilai as nama` (tidak dipakai)
};

struct PropertiPola : Node {
    static constexpr NK kKind = NK::PropertiPola;
    NodePtr kunci = nullptr;   ///< ekspresi kunci
    NodePtr pola = nullptr;    ///< Pola*
    Nam nama;                  ///< shorthand: pola = Nama dengan nama ini
    bool spread = false;
    bool rest = false;         ///< `...liyane`
};

struct CocogExpr : Node {
    static constexpr NK kKind = NK::CocogExpr;
    NodePtr subjek = nullptr;
    std::vector<NodePtr> kasus;  ///< KasusKocog*
};

struct KasusKocog : Node {
    static constexpr NK kKind = NK::KasusKocog;
    NodePtr pola = nullptr;
    NodePtr nilai = nullptr;   ///< nilai yang dihasilkan
};

// ---------------------------------------------------------------------------
// Statement
// ---------------------------------------------------------------------------
struct Program : Node {
    static constexpr NK kKind = NK::Program;
    std::vector<NodePtr> body;
    std::string_view nama_berkas;
    bool punya_moment_awal = false;  // top-level await
};

struct EkspresiStmt : Node {
    static constexpr NK kKind = NK::EkspresiStmt;
    NodePtr ekspresi = nullptr;
};

struct DeklarasiVarStmt : Node {
    static constexpr NK kKind = NK::DeklarasiVar;
    Nam jeneng;
    NodePtr nilai = nullptr;
    bool tetep = false;        ///< const
    bool destruktur = false;
    NodePtr pola = nullptr;    ///< bila destruktur
    NodePtr tipe = nullptr;
    std::vector<NodePtr> deklarator_lain;  ///< `ana a = 1, b = 2`
};

struct BlokStmt : Node {
    static constexpr NK kKind = NK::Blok;
    std::vector<NodePtr> body;
};

struct YenStmt : Node {
    static constexpr NK kKind = NK::YenStmt;
    NodePtr kondisi = nullptr;
    NodePtr lalu = nullptr;   ///< BlokStmt* atau EkspresiStmt* (tanpa blok)
    NodePtr liyane = nullptr;  ///< YenStmt* atau BlokStmt*
    bool ada_liyane = false;
};

struct NalikaStmt : Node {
    static constexpr NK kKind = NK::NalikaStmt;
    NodePtr kondisi = nullptr;
    NodePtr awak = nullptr;
};

struct LakoniStmt : Node {
    static constexpr NK kKind = NK::LakoniStmt;
    NodePtr awak = nullptr;
    NodePtr kondisi = nullptr;  ///< bisa null (do-while)
};

struct KanggoStmt : Node {
    static constexpr NK kKind = NK::KanggoStmt;
    NodePtr inisialisasi = nullptr;  ///< DeklarasiVarStmt* atau EkspresiStmt* (null)
    NodePtr kondisi = nullptr;
    NodePtr pembaruan = nullptr;  ///< PembaruanExpr* atau EkspresiStmt*
    NodePtr awak = nullptr;
};

struct KanggoOfStmt : Node {
    static constexpr NK kKind = NK::KanggoOfStmt;
    NodePtr target = nullptr;  ///< DeklarasiVarStmt* (dhaptar destruktur)
    NodePtr iterable = nullptr;
    NodePtr awak = nullptr;
    bool enteni = false;  ///< `kanggo enteni`
};

struct KanggoInStmt : Node {
    static constexpr NK kKind = NK::KanggoInStmt;
    NodePtr target = nullptr;
    NodePtr objek = nullptr;
    NodePtr awak = nullptr;
    bool enteni = false;
};

struct PilihStmt : Node {
    static constexpr NK kKind = NK::PilihStmt;
    NodePtr subjek = nullptr;
    std::vector<NodePtr> kasus;  ///< KasusKlap*
};

struct KasusKlap : Node {
    static constexpr NK kKind = NK::KasusKlap;
    /// Nilai yang dibandingkan dengan `==`. `null` kalau kasusnya `baku:` atau
    /// memakai pola.
    NodePtr test = nullptr;
    /// Pola (`kasus [1, 2]:` / `kasus {jenis: "kucing"}:`). `null` kalau kasusnya
    /// membandingkan nilai biasa. Tidak pernah keduanya terisi.
    NodePtr pola = nullptr;
    std::vector<NodePtr> body;
    bool fallthrough_eksplisit = false;
};

struct BaliStmt : Node {
    static constexpr NK kKind = NK::BaliStmt; NodePtr nilai = nullptr; };
struct MandhegStmt : Node {
    static constexpr NK kKind = NK::MandhegStmt; Nam label = {}; };
struct TerusnaStmt : Node {
    static constexpr NK kKind = NK::TerusnaStmt; Nam label = {}; };
struct UncalStmt : Node {
    static constexpr NK kKind = NK::UncalStmt; NodePtr nilai = nullptr; };

struct TangkepKlausul : Node {
    static constexpr NK kKind = NK::TangkepKlausul;
    Nam binding;                 ///< kosong = catch tanpa binding
    bool ada_binding = false;
    NodePtr body = nullptr;      ///< BlokStmt*
    NodePtr tipe = nullptr;
};

struct PungkasanKlausul : Node {
    static constexpr NK kKind = NK::PungkasanKlausul;
    NodePtr body = nullptr;
};

struct CobaStmt : Node {
    static constexpr NK kKind = NK::CobaStmt;
    NodePtr blok = nullptr;
    std::vector<NodePtr> tangkep;  ///< TangkepKlausul*
    NodePtr pungkasan = nullptr;  ///< klausa `pungkasan` (PungkasanKlausul*)
};

/// Satu entri `ekspor { lokal minangka ekspor }`.
struct EksporSpesifikasi {
    Nam lokal;
    Nam ekspor;
};

struct EksporDeklarasi : Node {
    static constexpr NK kKind = NK::EksporDeklarasi;
    Nam nama;         ///< nama yang diekspor (kosong bila ekspor daftar/deklarasi)
    bool deklarasi_lengkap = false;  ///< `ekspor gawe f() {}`
    NodePtr deklarasi = nullptr;     ///< statement yang diekspor
    std::vector<EksporSpesifikasi> daftar;  ///< `ekspor {a, b minangka c}`
    std::string_view modul;          ///< `saka "..."` (re-export)
    bool ada_modul = false;
    bool default_ekspor = false;     ///< `ekspor baku ...`
};

struct ImporSpesifikasi : Node {
    Nam impor;             ///< nama lokal (atau nama yang diimpor utawa alias)
    Nam sumber;            ///< nama asli di modul
    Nam alias;             ///< `minangka X`
};

struct ImporDeklarasi : Node {
    static constexpr NK kKind = NK::ImporDeklarasi;
    std::vector<ImporSpesifikasi> daftar;
    std::string_view modul;  ///< string path / "std:xxx" / "native:..."
    bool ada_modul = false;
    bool ada_namespace = false;  ///< `impor * minangka M`
    Nam alias_namespace;
    NodePtr tipe = nullptr;
};

struct EksporDefaultStmt : Node {
    static constexpr NK kKind = NK::EksporDeklarasi; NodePtr deklarasi = nullptr; };

struct LabelStmt : Node {
    static constexpr NK kKind = NK::LabelStmt;
    Nam label;
    NodePtr awak = nullptr;
};

struct KosongStmt : Node {
    static constexpr NK kKind = NK::KosongStmt;};
struct DebuggerStmt : Node {
    static constexpr NK kKind = NK::DebuggerStmt;};

// ---------------------------------------------------------------------------
// Class
// ---------------------------------------------------------------------------
struct PropertyAccessorDeklarasi : Node {
    static constexpr NK kKind = NK::PropertyAccessor;
    Nam nama;
    bool getter = false;
    bool setter = false;
    bool statis = false;
    bool privat = false;
    bool komputat = false;
    NodePtr kunci = nullptr;
    NodePtr fungsi = nullptr;  ///< FungsiDeklarasi*
};

struct MetodeDeklarasi : Node {
    static constexpr NK kKind = NK::MetodeDeklarasi;
    NodePtr fungsi = nullptr;  ///< FungsiDeklarasi*
    bool statis = false;
    bool privat = false;
};

struct FieldKelas : Node {
    static constexpr NK kKind = NK::FieldKelas;
    Nam nama;
    bool privat = false;
    bool statis = false;
    NodePtr nilai = nullptr;
    NodePtr tipe = nullptr;
    bool komputat = false;
    NodePtr kunci = nullptr;
};

struct GolonganDeklarasi : Node {
    static constexpr NK kKind = NK::GolonganDeklarasi;
    Nam nama;
    NodePtr induk = nullptr;   ///< ekspresi
    std::vector<NodePtr> badan;  ///< FieldKelas*, MetodeDeklarasi*, PropertyAccessorDeklarasi*
    std::vector<NodePtr> statis_blok;  ///< statement blok statis
};

// ---------------------------------------------------------------------------
// Anotasi tipe bertahap
// ---------------------------------------------------------------------------
struct TipeAnotasi : Node {
    static constexpr NK kKind = NK::TipeAnotasi;
    std::string_view nama;    // "angka", "teks", "dhaptar", dst
};

struct TipeUnion : Node {
    static constexpr NK kKind = NK::TipeUnion;
    std::vector<NodePtr> varian;  ///< Tipe*
};

struct TipeArray : Node {
    static constexpr NK kKind = NK::TipeArray;
    NodePtr elemen = nullptr;
};

struct TipeTuple : Node {
    static constexpr NK kKind = NK::TipeTuple;
    std::vector<NodePtr> elemen;
};

struct TipeObjek : Node {
    static constexpr NK kKind = NK::TipeObjek;
    std::vector<NodePtr> properti;  ///< PropertiTipe*
};

struct PropertiTipe : Node {
    static constexpr NK kKind = NK::PropertiTipe;
    Nam nama;
    NodePtr tipe = nullptr;
    bool opsional = false;
};

struct TipeFungsi : Node {
    static constexpr NK kKind = NK::TipeFungsi;
    std::vector<NodePtr> parameter;
    NodePtr bali = nullptr;
};

struct TipeReferensi : Node {
    static constexpr NK kKind = NK::TipeReferensi;
    Nam nama;
    std::vector<NodePtr> argumen;
};

struct TipeOpsional : Node {
    static constexpr NK kKind = NK::TipeOpsional;
    NodePtr dasar = nullptr;
};

struct AntarmukaDeklarasi : Node {
    static constexpr NK kKind = NK::AntarmukaDeklarasi;
    Nam nama;
    std::vector<NodePtr> badan;  ///< MetodeDeklarasi* / FieldKelas*
};

struct AliasTipe : Node {
    static constexpr NK kKind = NK::AliasTipe;
    Nam nama;
    NodePtr tipe = nullptr;
};

}  // namespace jawa::ast
