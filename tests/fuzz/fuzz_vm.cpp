// Target fuzz 4/5: VM -- sumber -> lexer -> parser -> kompilator -> loop bytecode.
//
// Yang diuji di sini jauh lebih dalam daripada target kompilator: eksekusi
// sungguhan. Invarian yang dijaga:
//   * program berhenti dalam batas langkah dan kedalaman frame (tanpa itu
//     program tak berujung akan menggantung seluruh campaign),
//   * keluaran tidak melebihi batas wajar (loop `tulis` tak berujung, atau
//     spread yang salah menghitung panjang),
//   * bytecode hasil kompilasi tetap memenuhi invarian lompatan/slot.
//
// Diagnostics dialihkan ke `/dev/null` sementara program berjalan: pada
// libFuzzer, input yang selalu gagal akan membanjiri stderr, dan pada driver
// mandiri campaign berisi ribuan galat membuat log tak terbaca.
#include "jawa_fuzz.h"

#include <fcntl.h>
#include <unistd.h>

namespace jawa::fuzz {
namespace {

/// Alihkan fd 2 ke `/dev/null` lalu pulihkan saat keluar (RAII, termasuk pada
/// jalur keluar lebih awal).
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

void jalankan_vm(const std::vector<std::uint8_t>& buf, Statistik& st, std::uint64_t) {
    ++st.total;
    // Filesystem virtual kosong: `impor` di program fuzz tidak boleh menyentuh disk.
    KonteksJalan ctx(
        [](const std::string&, std::string&) { return false; }, 100000);

    {
        const SenapkanStderr tutup;
        // `jalankan_sumber` menjalankan seluruh pipeline dan memanggil loop
        // acara, jadi async/timer ikut teruji.
        (void)ctx.mesin().jalankan_sumber(lihat(buf), "/fuzz/main.jw", "/fuzz");
        cek_batas_keluaran(ctx.keluaran());
    }
    // `Galat` maupun `Suspend` adalah hasil yang diharapkan; yang dicari adalah
    // crash atau keadaan akhir yang rusak, jadi keduanya dihitung "selesai".
    ++st.kompilasi_ok;
}

}  // namespace
}  // namespace jawa::fuzz

JAWAFUZZ_TARGET("fuzz_vm", ::jawa::fuzz::jalankan_vm)
