// Parser Basa Jawa: recursive descent + Pratt untuk ekspresi.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ast.h"
#include "lex/lexer.h"
#include "support/arena.h"
#include "support/diagnostics.h"

namespace jawa::parse {

using ast::Node;
using ast::NodePtr;
using support::SourcePos;
using support::SourceRange;

/// Catatan penting: anggota data dideklarasikan SEBELUM method inline-nya.
/// Bila method inline memakai anggota yang dideklarasikan belakangan, pencarian
/// nama terjadi pada titik definisi dan GCC tidak menemukan `operator[]`-nya.
class Parser {
public:
    Parser(lex::TokenList& token, support::Arena& arena, support::DiagnosticBag& bag, std::string_view nama_berkas);

    /// Parse program lengkap. AST selalu non-null; periksa diagnostik.
    ast::NodePtr parse_program();

    /// Parse satu ekspresi (untuk REPL & opsi -e).
    ast::NodePtr parse_ekspresi_tunggal();

    [[nodiscard]] const support::DiagnosticBag& diagnostics() const noexcept { return bag_; }
    [[nodiscard]] bool ada_galat() const noexcept { return bag_.ada_galat(); }

private:
    // ------------------------------------------------------------- data
    lex::TokenList& token_;
    support::Arena& arena_;
    support::DiagnosticBag& bag_;
    std::string_view nama_berkas_;
    std::size_t idx_ = 0;
    int kedalaman_ = 0;

    // ------------------------------------------------------------- token
    const lex::Token& saat() const noexcept { return token_.token[idx_]; }
    const lex::Token& peek(int n) const noexcept;
    lex::Tok jenis(int n = 0) const noexcept;
    [[nodiscard]] lex::Tok jenis_sekarang() const noexcept { return token_.token[idx_].jenis; }
    /// Boleh menjadi nama properti setelah `.`? selain `pengenal`, kata kunci
    /// juga boleh: dalam Basa Jawa banyak nama method yang sama dengan kata
    /// kunci (`nampa`, `tangkep`, `bali`, `jenis`, `saka`, ...).
    static bool boleh_adi_properti(lex::Tok t) noexcept {
        return t == lex::Tok::Ident || lex::is_reserved_keyword(t) || lex::is_contextual_keyword(t);
    }
    [[nodiscard]] bool cek(lex::Tok t) const noexcept { return jenis_sekarang() == t; }
    [[nodiscard]] bool cek(int n, lex::Tok t) const noexcept { return jenis(n) == t; }
    bool makan(lex::Tok t) noexcept;
    void lewati_asi() noexcept;
    void diagnosa(const char* kode, std::string pesan, SourceRange r, std::string saran = {});
    void diagnosa_di(const char* kode, std::string pesan, std::string saran = {});
    void aspek_ke_close(lex::Tok t, const char* kode, std::string_view apa);

    // ---------------------------------------------------------- statement
    ast::NodePtr parse_statement();
    ast::NodePtr parse_blok();
    ast::NodePtr parse_yen();
    ast::NodePtr parse_nalika();
    ast::NodePtr parse_lakoni();
    ast::NodePtr parse_kanggo();
    ast::NodePtr parse_pilih();
    ast::NodePtr parse_coba();
    ast::NodePtr parse_golongan();
    ast::NodePtr parse_ekspor();
    ast::NodePtr parse_impor();
    ast::NodePtr parse_deklarasi_fungsi();
    ast::NodePtr parse_antarmuka();
    ast::NodePtr parse_field_kelas();
    ast::NodePtr parse_metode_kelas();
    ast::NodePtr parse_accessor_kelas();
    ast::NodePtr parse_labeled();

    // ---------------------------------------------------------- ekspresi
    ast::NodePtr parse_ekspresi();
    ast::NodePtr parse_assignment();
    ast::NodePtr parse_kondisional();
    ast::NodePtr parse_pipeline();
    ast::NodePtr parse_logika_or();
    ast::NodePtr parse_logika_and();
    ast::NodePtr parse_bit_or();
    ast::NodePtr parse_bit_xor();
    ast::NodePtr parse_bit_and();
    ast::NodePtr parse_equality();
    ast::NodePtr parse_relasional();
    ast::NodePtr parse_shift();
    ast::NodePtr parse_additif();
    ast::NodePtr parse_multiplikatif();
    ast::NodePtr parse_pangkat();
    ast::NodePtr parse_unary();
    ast::NodePtr parse_postfix();
    ast::NodePtr parse_call_member();
    /// Lanjutkan rantai akses member/panggilan dari `e` (`.x`, `[i]`, `(...)`).
    /// Dipisah dari `parse_call_member` supaya `anyaar Foo().x` bisa dirangkai.
    ast::NodePtr lanjut_member(ast::NodePtr e, std::size_t m);
    /// Rantai akses TANPA pemanggilan, untuk konstruktor `anyaar Foo.bar(1)`.
    /// Kalau pemanggilan ikut dimakan, `anyaar Foo(1).x` akan salah ditafsirkan
    /// sebagai `anyaar (Foo(1).x)`.
    ast::NodePtr parse_konstruktor();
    ast::NodePtr parse_primary();
    ast::NodePtr parse_literal();
    ast::NodePtr parse_objek_literal();
    ast::NodePtr parse_array_literal();
    ast::NodePtr parse_template();
    ast::NodePtr parse_cocog();
    ast::NodePtr parse_pola();
    ast::NodePtr parse_pola_alternatif();
    ast::NodePtr parse_literal_primer();

    // ------------------------------------------------------------- helper
    /// Alokasikan node di arena, set `kind` dari `T::kKind`, lalu isi rentang.
    template <class T>
    T* buat(SourceRange r) {
        T* n = arena_.create<T>();
        n->kind = T::kKind;
        n->range = r;
        return n;
    }
    SourceRange rentang_dari(std::size_t mulai) const;
    void sinkronisasi_statement();
    void tipe_annotation(Node*& keluar);
    void parse_params(ast::FungsiDeklarasi* fn);
    void parse_argumen(std::vector<NodePtr>& keluar, std::size_t mulai);
    bool coba_arrow(NodePtr& keluar);
    /// Apakah token bisa dipakai sebagai NAMA pengenal di posisi binding/kunci?
    /// Meng terima `Ident` dan kata kunci apa pun (lihat DECISIONS.md D-013).
    [[nodiscard]] bool token_bisa_nama() const noexcept;
    [[nodiscard]] bool cek_nama(int n = 0) const noexcept;
    std::string_view ambil_nama();
    ast::NodePtr parse_tipe();
};

}  // namespace jawa::parse
