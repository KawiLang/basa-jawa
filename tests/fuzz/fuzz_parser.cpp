// Target fuzz 2/5: parser.
//
// Invarian yang dijaga:
//   * parser tidak crash, dan selalu selesai (lintasan yang keluar dari
//     `parse_program()` tanpa menghasilkan AST maupun diagnostics berarti ada
//     bail-out yang consuming token tanpa exhausting apa pun),
//   * rentang node program menunjuk ke dalam sumber,
//   * `cetak_ast` -- yang menelusuri SETIAP node -- tidak crash dan tidak
//     menghasilkan keluaran tak terbatas (AST melingkar yang dibuat salah akan
//     terlihat sebagai stack overflow atau output tanpa akhir).
//
// Yang BELUM diperiksa di sini: rentang setiap node anak. Untuk itu perlu
// penelusur anak AST per-jenis-node yang belum ada di `ast.h`; sementara ini
// hanya rentang program yang diperiksa. Lihat `STATUS.md`.
#include "jawa_fuzz.h"

#include "parse/ast_print.h"

namespace jawa::fuzz {
namespace {

/// Batas keluaran printer. AST crafted yang membuat loop cetak akan menabrak ini.
constexpr std::size_t kMaksCetak = 8u * 1024u * 1024u;

void jalankan_parser(const std::vector<std::uint8_t>& buf, Statistik& st, std::uint64_t) {
    ++st.total;
    Hasil hasil;
    if (!tahap_lex(hasil, lihat(buf))) {
        ++st.galat_lex;
        return;
    }
    if (!tahap_parse(hasil)) {
        ++st.galat_parse;
        return;
    }

    const ast::Node* akar = hasil.program;
    if (akar->range.mulai.offset > akar->range.selesai.offset) {
        fuzz_gagal("rentang program terbalik", "offset selesai mendahului offset mulai");
    }
    if (static_cast<std::size_t>(akar->range.selesai.offset) > buf.size()) {
        fuzz_gagal("rentang program melewati sumber", "node program menunjuk posisi di luar sumber");
    }

    // `cetak_ast` menelusuri setiap node; crash di sini berarti AST rusak
    // bentuknya (mis. statement yang menunjuk dirinya sendiri).
    const std::string teks =
        parse::cetak_ast(static_cast<const ast::Program*>(akar));
    if (teks.size() > kMaksCetak) {
        fuzz_gagal("cetak AST tanpa batas", "AST crafted membuat printer berjalan tanpa akhir");
    }
    ++st.kompilasi_ok;
}

}  // namespace
}  // namespace jawa::fuzz

JAWAFUZZ_TARGET("fuzz_parser", ::jawa::fuzz::jalankan_parser)
