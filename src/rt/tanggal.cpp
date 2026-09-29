// `Tanggal` — implementasi kalender proleptis Gregorian (UTC).
#include "rt/tanggal.h"

#include <chrono>
#include <cmath>
#include <cstdio>

namespace jawa::rt {
namespace {

constexpr double kMsPerHari = 86400000.0;

/// Hari sejak epoch (1970-01-01) untuk tanggal kalender. Algoritma
/// "days from civil" (Howard Hinnant) -- eksak untuk seluruh rentang int64.
int64_t hari_sCivil(int64_t y, int64_t m, int64_t d) noexcept {
    y -= m <= 2 ? 1 : 0;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;                                  // [0, 399]
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  // [0, 365]
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;         // [0, 146096]
    return era * 146097 + doe - 719468;
}

/// Kebalikan dari `hari_sCivil`.
void civil_dariHari(int64_t z, int64_t& y, int64_t& m, int64_t& d) noexcept {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;                                       // [0, 146096]
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0, 399]
    const int64_t yy = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);                // [0, 365]
    const int64_t mp = (5 * doy + 2) / 153;                                     // [0, 11]
    d = doy - (153 * mp + 2) / 5 + 1;                                           // [1, 31]
    m = mp + (mp < 10 ? 3 : -9);                                                // [1, 12]
    y = yy + (m <= 2 ? 1 : 0);
}

std::string_view pad2(int v) {
    static thread_local char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d", v);
    return buf;
}

std::string_view pad3(int v) {
    static thread_local char buf[8];
    std::snprintf(buf, sizeof(buf), "%03d", v);
    return buf;
}

/// Bentukkan `std::string` dari `string_view` hasil pad (yang menunjuk buffer
/// thread-local).
void tempel(std::string& keluar, std::string_view sv) { keluar.append(sv); }

std::string_view pad4_ke(int64_t y) {
    // 32 byte: cukup untuk '-' + 19 digit (int64) + padding nol + terminator.
    static thread_local char buf[32];
    const int64_t sy = y < 0 ? -y : y;
    if (y < 0) std::snprintf(buf, sizeof(buf), "-%04lld", static_cast<long long>(sy));
    else std::snprintf(buf, sizeof(buf), "%04lld", static_cast<long long>(sy));
    return buf;
}

}  // namespace

int64_t Tanggal::tahun() const noexcept {
    int64_t y = 0;
    int64_t m = 0;
    int64_t d = 0;
    civil_dariHari(static_cast<std::int64_t>(std::floor(ms_ / kMsPerHari)), y, m, d);
    return y;
}

int Tanggal::bulan() const noexcept {
    int64_t y = 0;
    int64_t m = 0;
    int64_t d = 0;
    civil_dariHari(static_cast<std::int64_t>(std::floor(ms_ / kMsPerHari)), y, m, d);
    return static_cast<int>(m);
}

int Tanggal::hari() const noexcept {
    int64_t y = 0;
    int64_t m = 0;
    int64_t d = 0;
    civil_dariHari(static_cast<std::int64_t>(std::floor(ms_ / kMsPerHari)), y, m, d);
    return static_cast<int>(d);
}

int Tanggal::hari_dalam_minggu() const noexcept {
    // 1970-01-01 = Kamis (4).
    const auto hari = static_cast<int64_t>(std::floor(ms_ / kMsPerHari));
    int64_t h = (hari + 4) % 7;
    if (h < 0) h += 7;
    return static_cast<int>(h);
}

int Tanggal::detik_dalam_hari() const noexcept {
    // `ms_` negatif harus dibulatkan ke BAWAH, bukan dipotong ke nol -- kalau
    // dipotong, -1 ms akan terhitung sebagai 0 (yaitu tengah malam, bukan
    // 23:59:59.999). Setelah `hari` di-floor, sisanya sudah di [0, 86400000),
    // jadi pemotongan ke bawah di sini benar.
    const double hari = std::floor(ms_ / kMsPerHari);
    const double sisa_ms = ms_ - hari * kMsPerHari;
    return static_cast<int>(std::floor(sisa_ms / 1000.0));
}
int Tanggal::jam() const noexcept { return detik_dalam_hari() / 3600; }
int Tanggal::menit() const noexcept { return (detik_dalam_hari() / 60) % 60; }
int Tanggal::detik_dalam_menit() const noexcept { return detik_dalam_hari() % 60; }
int Tanggal::milidetik_dalam_detik() const noexcept {
    const double hari = std::floor(ms_ / kMsPerHari);
    const double sisa_ms = ms_ - hari * kMsPerHari;
    return static_cast<int>(std::floor(sisa_ms)) % 1000;
}

std::string Tanggal::ke_tanggal() const {
    std::string keluar;
    tempel(keluar, pad4_ke(tahun()));
    keluar += '-';
    tempel(keluar, pad2(bulan()));
    keluar += '-';
    tempel(keluar, pad2(hari()));
    return keluar;
}

std::string Tanggal::ke_waktu() const {
    std::string keluar;
    tempel(keluar, pad2(jam()));
    keluar += ':';
    tempel(keluar, pad2(menit()));
    keluar += ':';
    tempel(keluar, pad2(detik_dalam_menit()));
    keluar += '.';
    tempel(keluar, pad3(milidetik_dalam_detik()));
    keluar += 'Z';
    return keluar;
}

std::string Tanggal::ke_teks() const { return ke_tanggal() + "T" + ke_waktu(); }

std::string_view Tanggal::nama_hari(int h) {
    static constexpr std::string_view kNama[7] = {
        "Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu"};
    if (h < 0 || h > 6) return {};
    return kNama[h];
}

std::string_view Tanggal::nama_bulan(int b) {
    // Indeks 0 tidak dipakai; bulan 1..12 memakai indeks yang sama.
    static constexpr std::string_view kNama[13] = {
        "",          "Januari", "Februari",  "Maret",  "April",   "Mei",
        "Juni",      "Juli",    "Agustus",   "September", "Oktober", "November",
        "Desember"};
    if (b < 1 || b > 12) return {};
    return kNama[b];
}

Tanggal Tanggal::dari(int64_t tahun, int bulan, int hari, int jam, int menit, double detik) noexcept {
    // Normalisasi bulan & hari ke rentang sebenarnya (rollover).
    int64_t y = tahun;
    int64_t m = bulan;
    y += (m - 1) / 12;
    m = (m - 1) % 12 + 1;
    if (m <= 0) {
        m += 12;
        y -= 1;
    }
    const int64_t dasar = hari_sCivil(y, m, 1) + (hari - 1);
    const double ms = static_cast<double>(dasar) * kMsPerHari + static_cast<double>(jam) * 3600000.0 +
                      static_cast<double>(menit) * 60000.0 + detik * 1000.0;
    return Tanggal(ms);
}

Tanggal Tanggal::sekarang() noexcept {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return Tanggal(static_cast<double>(ms));
}

}  // namespace jawa::rt
