// `Tanggal` — nilai waktu Basa Jawa.
//
// Menyimpan milidetik sejak epoch UTC (1970-01-01T00:00:00Z) sebagai `double`,
// jadi rentang ± 2^53 milidetik (± 285.000 tahun) -- jauh lebih dari cukup.
//
// Kalender memakai kalender proleptis Gregorian (regel Leibniz 1582), sama
// seperti ECMAScript: aturan 100/400 untuk tahun abad. UTC diasumsikan tanpa
// zona waktu; konversi ke/dari waktu lokal tidak tersedia.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace jawa::rt {

/// Nilai `Tanggal`: milidetik sejak epoch UTC.
class Tanggal {
public:
    constexpr Tanggal() = default;
    explicit constexpr Tanggal(double ms) : ms_(ms) {}

    [[nodiscard]] constexpr double milidetik() const noexcept { return ms_; }
    [[nodiscard]] constexpr std::int64_t milidetik_bulat() const noexcept {
        return static_cast<std::int64_t>(ms_);
    }
    [[nodiscard]] constexpr std::int64_t detik() const noexcept { return static_cast<std::int64_t>(ms_) / 1000; }

    // --- medan kalender (UTC) ---
    /// Tahun (bisa negatif / sebelum epoch).
    [[nodiscard]] int64_t tahun() const noexcept;
    /// Bulan 1..12.
    [[nodiscard]] int bulan() const noexcept;
    /// Tanggal dalam bulan 1..31.
    [[nodiscard]] int hari() const noexcept;
    /// Hari dalam minggu: 0 = Minggu .. 6 = Sabtu.
    [[nodiscard]] int hari_dalam_minggu() const noexcept;
    [[nodiscard]] int jam() const noexcept;
    [[nodiscard]] int menit() const noexcept;
    [[nodiscard]] int detik_dalam_menit() const noexcept;
    [[nodiscard]] int milidetik_dalam_detik() const noexcept;
    /// Detik sejak tengah malam hari itu (0..86399).
    [[nodiscard]] int detik_dalam_hari() const noexcept;

    /// ISO 8601, contoh `2026-09-29T14:03:07.123Z`.
    [[nodiscard]] std::string ke_teks() const;
    /// Hanya tanggal: `2026-09-29`.
    [[nodiscard]] std::string ke_tanggal() const;
    /// Hanya waktu: `14:03:07.123`.
    [[nodiscard]] std::string ke_waktu() const;

    /// Nama hari & bulan dalam Bahasa Jawa.
    [[nodiscard]] static std::string_view nama_hari(int hari_dalam_minggu);
    [[nodiscard]] static std::string_view nama_bulan(int bulan);

    // --- kontruksi statis ---
    /// Dari komponen kalender (UTC). Bulan 1..12. Hari di luar rentang bulan
    /// di-rollover (mis. 31 Februari -> 3 Maret), seperti `Date` di ECMAScript.
    [[nodiscard]] static Tanggal dari(int64_t tahun, int bulan, int hari, int jam = 0, int menit = 0,
                                       double detik = 0.0) noexcept;
    /// Waktu sekarang (ms sejak epoch UTC).
    [[nodiscard]] static Tanggal sekarang() noexcept;

    // --- aritmetika ---
    /// Tambah ms.
    [[nodiscard]] constexpr Tanggal tambah_ms(double ms) const noexcept { return Tanggal(ms_ + ms); }
    [[nodiscard]] Tanggal tambah_hari(double hari) const noexcept { return tambah_ms(hari * 86400000.0); }
    /// Selisih dalam ms (`*sebenar - ini`).
    [[nodiscard]] constexpr double selisih_ms(const Tanggal& lain) const noexcept {
        return lain.ms_ - ms_;
    }
    [[nodiscard]] bool sebelum(const Tanggal& lain) const noexcept { return ms_ < lain.ms_; }
    [[nodiscard]] bool sesudah(const Tanggal& lain) const noexcept { return ms_ > lain.ms_; }
    [[nodiscard]] bool sama_dengan(const Tanggal& lain) const noexcept { return ms_ == lain.ms_; }

private:
    double ms_ = 0.0;
};

}  // namespace jawa::rt
