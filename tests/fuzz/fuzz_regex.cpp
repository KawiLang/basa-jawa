// Target fuzz 6/6: mesin regex.
//
// Ini kode yang paling muda dan paling rawan: parser pola selain biasa
// (nested group, shorthand di dalam kelas, rentang terbalik, kuantifier
// possessif), dan pencocokan backtracking dengan continuation yang bisa
// memakai stack tanpa batas.
//
// Invarian yang dijaga:
//   * pola yang gagal kompilasi menghasilkan `nullptr` + pesan, BUKAN program
//     setengah jadi yang nanti bisa crash saat dicocokkan,
//   * `cari`/`kabeh`/`ganti`/`semua` tidak keluar dari memori untuk input apa
//     pun, dan tidak menggantung (anggaran langkah harus bekerja),
//   * rentang hasil selalu di dalam `subjek`, dan monoton,
//   * `kelompok(i)` tidak pernah menghasilkan sub-teks di luar jangkauan.
#include "jawa_fuzz.h"

#include "rt/regexp.h"

namespace jawa::fuzz {
namespace {

/// Jumlah langkah maksimum per pemanggilan, supaya campaign tidak menggantung
/// pada pola patologis. campaigned juga harus menguji bahwa batas itu bekerja.
constexpr std::size_t kAnggaranKecil = 2000;

void periksa_hasil(const rt::HasilRegex& h, std::string_view subjek) {
    if (h.awal.size() != h.akhir.size()) {
        std::fprintf(stderr, "REGEX: ukuran awal != ukuran akhir\n");
        std::abort();
    }
    for (std::size_t i = 0; i < h.awal.size(); ++i) {
        const std::size_t a = h.awal[i];
        const std::size_t b = h.akhir[i];
        if (a == std::string::npos || b == std::string::npos) continue;
        if (a > b || b > subjek.size()) {
            std::fprintf(stderr, "REGEX: rentang di luar subjek (%zu, %zu, len %zu)\n", a, b, subjek.size());
            std::abort();
        }
        // `kelompok` harus aman untuk semua indeks.
        (void)h.kelompok(subjek, i);
    }
    if (!h.awal.empty()) {
        (void)h.kelompok(subjek, 0);
        (void)h.kelompok(subjek, h.awal.size() + 3);  // di luar jangkauan
    }
}

void jalankan_regex(const std::vector<std::uint8_t>& buf, Statistik& st) {
    ++st.total;
    // Pola & flag diambil dari potongan buffer yang berbeda, supaya keduanya bisa
    // berubah-ubah sendiri dan tidak selalu berpasangan.
    const std::size_t belah = buf.empty() ? 0 : buf.size() / 2;
    const std::string pola(reinterpret_cast<const char*>(buf.data()), belah);
    std::string_view flag(reinterpret_cast<const char*>(buf.data() + belah),
                          std::min<std::size_t>(3, buf.size() - belah));

    std::string pesan;
    auto program = rt::RegexProgram::kompilasi(pola, flag, pesan);
    if (program == nullptr) {
        // Pola tidak sah: `pesan` harus terisi, dan tidak boleh ada program.
        if (pesan.empty()) {
            std::fprintf(stderr, "REGEX: kompilasi gagal tanpa pesan\n");
            std::abort();
        }
        ++st.galat_lex;
        return;
    }
    ++st.kompilasi_ok;

    // Anggaran sengaja dikecilkan supaya pola patologis (justru yang ingin
    // diuji) tidak membuat campaign lambat -- sekaligus menguji bahwa batas
    // langkah itu benar-benar menghentikan pencarian.
    program->set_anggaran_langkah(kAnggaranKecil);

    // Subjek dari sisa buffer, dibersihkan dari byte yang bisa complicate
    // pencocokan UTF-8 (mesin regex-nya byte-oriented, jadi ini memang perlu).
    std::string subjek;
    for (std::size_t i = belah * 2 % buf.size(); i < buf.size() && subjek.size() < 64; ++i) {
        if (buf[i] != 0) subjek.push_back(static_cast<char>(buf[i]));
    }
    if (subjek.empty()) subjek = "aaab";

    rt::HasilRegex h;
    if (program->cari(subjek, 0, h)) {
        periksa_hasil(h, subjek);
        if (!h.cocok) {
            std::fprintf(stderr, "REGEX: cari() true tapi h.cocok false\n");
            std::abort();
        }
    }
    h = {};
    if (program->kabeh(subjek, h)) periksa_hasil(h, subjek);
    h = {};

    const std::string hasil_ganti = program->ganti(subjek, "<$0|$&|$1>");
    if (hasil_ganti.size() > subjek.size() * 64 + 4096) {
        std::fprintf(stderr, "REGEX: ganti() menghasilkan %zu byte dari subjek %zu byte\n",
                     hasil_ganti.size(), subjek.size());
        std::abort();
    }
    const auto semua = program->semua(subjek);
    if (semua.size() > subjek.size() + 1) {
        std::fprintf(stderr, "REGEX: semua() mengembalikan %zu hasil dari subjek %zu byte\n",
                     semua.size(), subjek.size());
        std::abort();
    }
    for (const rt::HasilRegex& x : semua) periksa_hasil(x, subjek);

    // Budget besar harus tetap benar untuk pola yang normal.
    program->set_anggaran_langkah(2000000);
    rt::HasilRegex h2;
    if (program->cari(subjek, 0, h2) && h2.cocok) periksa_hasil(h2, subjek);
}

}  // namespace

void jalankan_regex_target(const std::vector<std::uint8_t>& buf, Statistik& st, std::uint64_t) {
    jalankan_regex(buf, st);
}

}  // namespace jawa::fuzz

JAWAFUZZ_TARGET("fuzz_regex", ::jawa::fuzz::jalankan_regex_target)
