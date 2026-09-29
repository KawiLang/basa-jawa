// Kompiler Basa Jawa: AST -> bytecode.
//
// Menghasilkan satu `Chunk` per fungsi. Analyzer scope (resolusi variabel,
// upvalue, TDZ) dijalankan lebih dulu sehingga bytecode tidak perlu mencari
// nama pada saat runtime.
#pragma once

#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gc/heap.h"
#include "parse/ast.h"
#include "rt/object.h"
#include "rt/value.h"
#include "support/diagnostics.h"
#include "vm/chunk.h"

namespace jawa::compile {

using rt::Value;
using support::SourcePos;
using support::SourceRange;

/// Analyzer scope + resolver upvalue untuk satu fungsi.
struct FungsiInfo {
    std::string_view nama;
    std::size_t arity = 0;
    bool variadic = false;
    bool panah = false;
    bool mengko = false;
    bool generator = false;
    bool method = false;
    bool privat = false;
    bool statis = false;
    bool constructor = false;
    bool ini_boleh = true;       ///< boleh pakai `iki`
    std::string_view getter_nama;
    std::string_view setter_nama;
    std::vector<std::string_view> nama_param;
    /// Upvalue yang diambil dari fungsi induk: (indeks di induk, nama).
    std::vector<std::pair<std::size_t, std::string_view>> ambil_upvalue;
    FungsiInfo* induk = nullptr;
};

/// Hasil kompilasi modul.
struct HasilKompilasi {
    vm::ChunkPtr modul;            ///< fungsi modul
    std::vector<vm::ChunkPtr> semua;  ///< semua chunk
    std::vector<std::string> pesan;  ///< peringatan optimizer
    bool ada_galat = false;
};

/// Kompilasi satu program. `heap` dipakai untuk alokasi objek runtime yang
/// perlu bertahan (konstanta string, nama fungsi).
class Compiler {
public:
    Compiler(gc::Heap& heap, support::DiagnosticBag& bag, std::string_view nama_berkas);

    /// Kompilasi program. Kembalikan null bila ada galat kompilasi.
    HasilKompilasi compile(const ast::Program* prog);

    /// Kompilasi ekspresi tunggal (REPL / -e).
    HasilKompilasi compile_ekspresi(const ast::Node* expr);

    /// Daftarkan fungsi global (dipakai CLI untuk `tulis` dlsb).
    void daftarkan_global(const std::string& nama, Value v) { builtin_global_[nama] = v; }

private:
    struct FungsiKonteks {
        vm::ChunkPtr chunk;
        FungsiInfo info;
        std::unordered_map<std::string, std::size_t> lokal;   ///< nama -> slot
        std::unordered_map<std::string, std::size_t> upvalue; ///< nama -> indeks upvalue
        std::size_t n_slot_terpakai = 0;
        std::size_t n_slot_maks = 0;
        bool dalam_fungsi = false;
        /// Satu loop yang sedang dikompilasi. Dipakai untuk patch `mandheg`
        /// (break) & `terusna` (continue) tanpa placeholder lompat ke 0.
        struct Loop {
            std::size_t terusna_tujuan = 0;   ///< ip awal iterasi berikutnya
            std::vector<std::size_t> patch_mandheg;
            std::vector<std::size_t> patch_terusna;
        };
        std::vector<Loop> loop;
    };

    // --- helper ---
    [[nodiscard]] FungsiKonteks& fn() { return fungsi_stack_.back(); }
    [[nodiscard]] const FungsiKonteks& fn() const { return fungsi_stack_.back(); }
    std::uint16_t emit(vm::Op op, std::uint16_t a = 0, std::uint16_t b = 0);
    std::uint16_t emit_at(std::size_t idx, vm::Op op, std::uint16_t a = 0, std::uint16_t b = 0);
    std::size_t tambah_konstanta(Value v);
    std::size_t tambah_nama(Value v);
    void patch(std::size_t idx, std::size_t tujuan);
    void patch_sebalik(std::size_t idx, std::size_t tujuan);
    std::size_t slot_baru(const std::string_view nama);
    std::size_t cari_slot(const std::string_view nama) const;
    std::size_t cari_upvalue(const std::string_view nama);
    void diagnosa(const char* kode, std::string pesan, SourceRange r, std::string saran = {});
    void diagnosa_di(const char* kode, std::string pesan, std::string saran = {});
    [[nodiscard]] std::uint32_t baris_sekarang() const;

    // --- statement ---
    void statement(const ast::Node* n);
    /// `sudah_hoist` = deklarasi fungsi/kelas ini sudah dibuat di awal modul
    /// (hoisting ekspor), jadi badan statement cukup mengekspornya.
    void statement(const ast::Node* n, bool sudah_hoist);
    void stmt_blok(const ast::BlokStmt* n);
    /// `awak` bisa berupa blok ATAU satu statement (`kanggo (...) stmt;`).
    void stmt_awak(const ast::Node* n);
    void stmt_yen(const ast::YenStmt* n);
    void stmt_nalika(const ast::NalikaStmt* n);
    void stmt_lakoni(const ast::LakoniStmt* n);
    void stmt_kanggo(const ast::KanggoStmt* n);
    void stmt_kanggo_of(const ast::KanggoOfStmt* n);
    void stmt_pilih(const ast::PilihStmt* n);
    void stmt_coba(const ast::CobaStmt* n);
    void stmt_golongan(const ast::GolonganDeklarasi* n);
    void stmt_ekspor(const ast::EksporDeklarasi* n, bool sudah_hoist);
    void stmt_impor(const ast::ImporDeklarasi* n);
    /// `ekspor { x, y }` yang ditunda sampai akhir modul (lihat `Compiler::compile`).
    struct EksporTunda {
        std::string_view lokal;
        std::string_view ekspor;
    };
    /// Deklarasi yang dibungkus `ekspor`, atau `nullptr`.
    [[nodiscard]] static const ast::Node* deklarasi_ekspor(const ast::Node* n);
    /// Nama yang diekspor deklarasi itu (kosong bila tidak bernama).
    [[nodiscard]] static std::string_view nama_deklarasi(const ast::Node* d);
    /// Slot untuk pengikat impor. Dialokasikan lebih awal di `compile()` supaya
    /// fungsi yang di-hoist capture slot, bukan nama global (lihat catatan
    /// impor siklik di `Compiler::compile`).
    std::size_t slot_impor(const std::string_view nama);
    void deklarasi_fungsi(const ast::FungsiDeklarasi* n, bool eksport);

    // --- ekspresi ---
    void ekspresi(const ast::Node* n);
    void eks_biner(const ast::BinerExpr* n);
    void eks_panggilan(const ast::Panggilan* n);
    void eks_fungsi(const ast::FungsiDeklarasi* n);
    void eks_objek(const ast::ObjectLit* n);
    void eks_dhaptar(const ast::ArrayLit* n);
    void eks_template(const ast::TemplateLit* n);
    void eks_cocog(const ast::CocogExpr* n);
    /// Susun uji pola: menyisakan `bener`/`salah` di stack; bila cocok, nama
    /// yang di-binding ditulis ke slot lokalnya.
    void susun_pola(const ast::Pola* p, std::size_t s_subj, std::vector<std::size_t>& lompat_gagal);
    /// Pola dengan subjek di PUNCAK stack (pola anak dari `[a, b]` / `{x: p}`).
    void susun_pola_stack(const ast::Pola* p, std::vector<std::size_t>& lompat_gagal);
    /// Slot untuk nama pola: pakai yang ada kalau sudah pernah dialingokasikan.
    std::size_t slot_pola(std::string_view nama);
    void eks_destructur(const ast::Node* target, const ast::Node* nilai, bool deklarasi);
    /// Baca nama: lokal -> upvalue -> global (satu helper, konsisten di semua
    /// konteks sehingga rekursi & closure memakai jalur yang sama).
    void emit_baca_nama(std::string_view nama);
    /// Tulis nama: lokal -> upvalue -> global.
    void emit_tulis_nama(std::string_view nama);
    /// Seperti `emit_tulis_nama`, tetapi untuk konteks yang nilai di stack
    /// harus hilang (statement biasa). Untuk global kita tambahkan `POP`.
    void emit_tulis_nama_statement(std::string_view nama);

    // --- optimise ---
    bool konstan(const ast::Node* n, Value& keluar);

    gc::Heap& heap_;
    support::DiagnosticBag& bag_;
    std::string_view nama_berkas_;
    std::deque<FungsiKonteks> fungsi_stack_;  // alamat stabil
    std::unordered_map<std::string, Value> builtin_global_;
    std::unordered_map<std::string, std::size_t> global_slot_;
    std::vector<std::string_view> global_nama_;
    std::vector<vm::ChunkPtr> semua_chunk_;
    std::size_t optimise_aktif_ = 0;  ///< 0 = nonaktif
    bool ada_fungsi_ = false;
    /// `ekspor { ... }` yang menunggu akhir modul. Hanya berlaku untuk modul.
    std::vector<EksporTunda> ekspor_tunda_;
    /// Slot pengikat impor yang sudah dialokasikan di awal modul.
    std::unordered_map<std::string, std::size_t> impor_slot_;
};

}  // namespace jawa::compile
