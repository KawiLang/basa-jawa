// Target fuzz 1/5: lexer.
//
// Invarian yang dijaga: lexer tidak boleh crash, tidak boleh menghasilkan token
// dengan rentang di luar sumber, dan tidak boleh decoding teks yang lebih panjang
// dari masukannya. Loop tanpa batas pada masukan yang sama akan terlihat sebagai
// hangup, bukan sebagai laporan.
#include "jawa_fuzz.h"

namespace jawa::fuzz {
namespace {

/// Rentang setiap token harus berada di dalam sumber. Ini menangkap lexer yang
/// salah menghitung panjang, atau yang memotong di batas UTF-8 sehingga batasnya
/// jatuh di tengah karakter.
void periksa_rentang(const lex::TokenList& token, std::size_t panjang_sumber) {
    for (const lex::Token& t : token.token) {
        if (t.range.mulai.offset > t.range.selesai.offset) {
            fuzz_gagal("rentang token terbalik", "offset selesai mendahului offset mulai");
        }
        if (static_cast<std::size_t>(t.range.selesai.offset) > panjang_sumber) {
            fuzz_gagal("rentang token melewati sumber", "lexer menghitung panjang keliru");
        }
        if (t.nilai_teks.size() > panjang_sumber) {
            fuzz_gagal("teks token melebihi sumber", "lexer mendekode lebih dari input");
        }
    }
}

void jalankan_lexer(const std::vector<std::uint8_t>& buf, Statistik& st, std::uint64_t) {
    ++st.total;
    const std::string_view sumber = lihat(buf);

    Hasil hasil;
    const bool ok = tahap_lex(hasil, sumber);
    periksa_rentang(hasil.token, sumber.size());

    if (!ok) {
        ++st.galat_lex;
        return;
    }
    // Target ini berhenti di lexer. Pakai `galat_parse` sebagai penghitung
    // "selesai tanpa crash" supaya kolom statistik tidak ambigu.
    ++st.galat_parse;
}

}  // namespace
}  // namespace jawa::fuzz

JAWAFUZZ_TARGET("fuzz_lexer", ::jawa::fuzz::jalankan_lexer)
