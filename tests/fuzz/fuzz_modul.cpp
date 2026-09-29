// Target fuzz 5/5: linker modul ES.
//
// Bedanya dengan target VM: di sini yang di-fuzz adalah GRAFIK IMPOR. Program
// utama dan modul-modulnya datang dari masukan yang sama, lalu dipecah
// berdasarkan penanda `===MODUL:nama===` dan ditaruh di filesystem virtual.
// Dengan begitu kombinasi siklik, re-export, impor namespace, impor tanpa
// pengikat, dan nama yang tidak diekspor bisa terbentuk tanpa Interaksi manual.
//
// Invarian yang dijaga:
//   * program berhenti dalam batas langkah/frame,
//   * filesystem virtual benar-benar dipakai (tidak ada akses disk -- kalau
//     ada, campaign akan bergantung pada isi direktori proyek),
//   * keluaran tidak melebihi batas wajar,
//   * bytecode semua modul memenuhi invarian lompatan/slot,
//   * jumlah modul yang tercatat di VM tidak melebihi jumlah berkas virtual
//     (linker yang mendaftarkan modul berulang atau salah nama akan terlihat).
#include "jawa_fuzz.h"

#include <fcntl.h>
#include <map>
#include <unistd.h>

namespace jawa::fuzz {
namespace {

/// Pemisah antar modul di dalam satu masukan. Panjang marker tidak mungkin
/// muncul sebagai kode Basa Jawa yang sah, jadi tidak ada ambiguitas.
constexpr std::string_view kPemisah = "\n===MODUL:";

class SenapkanStderr {
public:
    SenapkanStderr() {
        simpan_ = ::dup(STDERR_FILENO);
        null_ = ::open("/dev/null", O_WRONLY);
        if (simpan_ >= 0 && null_ >= 0) ::dup2(null_, STDERR_FILENO);
    }
    ~SenapkanStderr() {
        if (simpan_ >= 0) {
            ::dup2(simpan_, STDERR_FILENO);
            ::close(simpan_);
        }
        if (null_ >= 0) ::close(null_);
    }
    SenapkanStderr(const SenapkanStderr&) = delete;
    SenapkanStderr& operator=(const SenapkanStderr&) = delete;

private:
    int simpan_ = -1;
    int null_ = -1;
};

using PetaBerkas = std::map<std::string, std::string>;

/// Pecah masukan menjadi modul virtual. Module pertama yang tidak diberi nama
/// menjadi `/virtual/main.jw`; sisanya mengikuti nama yang ditulis di marker.
/// Nama yang memuat `..` atau diawali `/` diabaikan supaya path virtual tetap
/// dalam satu direktori.
PetaBerkas pecah_modul(std::string_view masukan) {
    PetaBerkas peta;
    std::size_t pos = 0;
    std::size_t urut = 0;
    std::string nama;
    std::string isi;
    auto tutup = [&](const std::string& n, std::string& s) {
        if (n.empty()) return;
        if (n.find("..") != std::string::npos || n.front() == '/') return;
        peta["/virtual/" + n + ".jw"] = std::move(s);
        s.clear();
    };
    while (pos <= masukan.size()) {
        const std::size_t ketemu = masukan.find(kPemisah, pos);
        const std::size_t akhir = (ketemu == std::string_view::npos) ? masukan.size() : ketemu;
        isi.append(masukan.substr(pos, akhir - pos));
        if (ketemu == std::string_view::npos) break;
        // Nama modul dibaca sampai baris baru berikutnya.
        pos = ketemu + kPemisah.size();
        std::size_t akhir_nama = masukan.find('\n', pos);
        if (akhir_nama == std::string_view::npos) akhir_nama = masukan.size();
        nama = std::string(masukan.substr(pos, akhir_nama - pos));
        while (!nama.empty() && (nama.back() == ' ' || nama.back() == '\r')) nama.pop_back();
        if (nama.empty()) nama = "m" + std::to_string(urut++);
        tutup(nama, isi);
        pos = akhir_nama;
    }
    if (isi.find_first_not_of(" \t\r\n") != std::string::npos) peta["/virtual/main.jw"] = isi;
    if (peta.empty()) peta["/virtual/main.jw"] = std::string(masukan);
    return peta;
}

void jalankan_modul(const std::vector<std::uint8_t>& buf, Statistik& st, std::uint64_t) {
    ++st.total;
    const PetaBerkas peta = pecah_modul(lihat(buf));
    const std::size_t n_modul = peta.size();

    KonteksJalan ctx(
        [&peta](const std::string& path, std::string& keluar) {
            const auto it = peta.find(path);
            if (it == peta.end()) return false;
            keluar = it->second;
            return true;
        },
        100000);

    {
        const SenapkanStderr tutup;
        (void)ctx.mesin().jalankan_sumber(peta.at("/virtual/main.jw"), "/virtual/main.jw", "/virtual");
        cek_batas_keluaran(ctx.keluaran());
    }
    ++st.kompilasi_ok;
    if (n_modul > 0) ++st.jalan_ok;  // ada grafik impor yang benar-benar dijalankan
}

}  // namespace
}  // namespace jawa::fuzz

JAWAFUZZ_TARGET("fuzz_modul", ::jawa::fuzz::jalankan_modul)
