// Target fuzz 3/5: kompilator.
//
// Invarian yang dijaga pada bytecode yang dihasilkan:
//   * setiap lompatan menunjuk instruksi yang ada (label di akhir kode sah),
//   * `jumlah_slot` cukup untuk slot tertinggi yang dipakai `GET_LOCAL` /
//     `SET_LOCAL` / `DEF_LOCAL` / `TDZ_CHECK`,
//   * `KONSTAN` dan `GET_PROP` tidak menunjuk indeks di luar pool-nya,
//   * setiap entri `tdz_daftar` menunjuk ip di dalam kode.
//
// Ini target yang paling sering menemukan bug nyata: patch lompatan yang salah
// tidak terlihat dari program yang kecil, tapi muncul di program yang besar.
#include "jawa_fuzz.h"

namespace jawa::fuzz {
namespace {

void jalankan_kompilasi(const std::vector<std::uint8_t>& buf, Statistik& st, std::uint64_t) {
    ++st.total;
    KonteksJalan ctx;  // heap kompilator = heap VM (konstanta string hidup di sana)

    Hasil hasil;
    if (!tahap_lex(hasil, lihat(buf))) {
        ++st.galat_lex;
        return;
    }
    if (!tahap_parse(hasil)) {
        ++st.galat_parse;
        return;
    }
    if (!tahap_kompilasi(hasil, ctx.mesin().heap())) {
        return;
    }
    periksa_semua_chunk(hasil.kompilasi.semua);
    if (hasil.kompilasi.modul != nullptr) {
        periksa_lompatan(*hasil.kompilasi.modul);
        periksa_slot(*hasil.kompilasi.modul);
    }
    ++st.kompilasi_ok;
}

}  // namespace
}  // namespace jawa::fuzz

JAWAFUZZ_TARGET("fuzz_kompilasi", ::jawa::fuzz::jalankan_kompilasi)
