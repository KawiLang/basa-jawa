// Mesin regex Basa Jawa: pencocokan backtracking.
//
// Dipakai untuk nilai runtime `/pola/flag`. Grammar-nya mengikuti ECMAScript
// sebagai subset: apa yang dipakai di dokumentasi ini ada, dan yang tidak ada
// menghasilkan galat kompilasi -- bukan perilaku diam-diam.
//
// Grammar:
//
//   alternasi    := sequentially ('|' sequentially)*
//   sequentially := atom kuantifier*
//   kuantifier   := ('*' | '+' | '?' | '{' n (',' m?)? '}')
//                   disusul '?' (malu / lazy)
//   atom         := '(' grup ')' | '[' kelas ']' | '.' | jangkar | escape | harf
//   grup         := '?:' non-penangkap | '?<nama>' bernama | polos (penangkap)
//
// Yang didukung: `. * + ? {n,m}` (dengan `?` untuk lazy), `() [] | ^ $`,
// `\d \D \w \W \s \S \b \B \n \t \r \f \v \0 \xHH \uHHHH \cX`, rentang kelas
// karakter dengan negasi, kelompok bernama, dan flag `g i m s y u`.
//
// Yang TIDAK didukung, dan menghasilkan galat: lookahead/lookbehind,
// backreference, kuantifier possessif, dan nama kelompok yang sama dua kali.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace jawa::rt {

/// Hasil satu kali pencocokan.
struct HasilRegex {
    bool cocok = false;
    /// Rentang byte sub-teks yang cocok, per kelompok. Indeks 0 = keseluruhan
    /// pencocokan; 1.. = kelompok tangkap. Kelompok yang tidak ikut cocok punya
    /// `awal == std::string::npos`.
    std::vector<std::size_t> awal;
    std::vector<std::size_t> akhir;

    /// Sub-teks kelompok ke-`i` (0 = keseluruhan). String kosong kalau tidak
    /// ikut cocok.
    [[nodiscard]] std::string_view kelompok(std::string_view subjek, std::size_t i) const;
    /// Jumlah kelompok tangkap (tanpa menghitung kelompok 0).
    [[nodiscard]] std::size_t jumlah_kelompok() const { return awal.empty() ? 0 : awal.size() - 1; }
};

/// Program regex yang sudah dikompilasi. Dimiliki bersama lewat
/// `std::shared_ptr` (lihat `RegexObj::program`).
class RegexProgram {
public:
    /// Kompilasi `pola`. Mengembalikan program, atau `nullptr` dengan pesan
    /// galat (Jawa, tanpa prefix "Kleru") di `galat_keluar`.
    static std::shared_ptr<RegexProgram> kompilasi(std::string_view pola, std::string_view flag,
                                                  std::string& galat_keluar);

    /// Validasi flag saja.
    static bool cek_flag(std::string_view flag, std::string& galat_keluar);

    /// Cari kecocokan pertama pada atau setelah `dari`. `hasil` diisi walau tidak
    /// cocok (`hasil.cocok == false`).
    ///
    /// Mengembalikan `false` BUKAN hanya karena tidak cocok: lihat
    /// `batas_terlampaui()`. Pola patologis seperti `(a+)+b` memakai
    /// waktu EKSPONENSIAL (bukan kedalaman rekursi yang besar), jadi pencocok punya
    /// anggaran langkah. Melampaui anggaran berarti "tidak tahu", bukan "tidak
    /// cocok" -- dan pemanggil wajib membedakannya.
    bool cari(std::string_view subjek, std::size_t dari, HasilRegex& hasil) const;

    /// Cocok sebagai keseluruhan string (anchored di kedua ujung).
    /// Pengecekan batas berlaku sama seperti `cari`.
    bool kabeh(std::string_view subjek, HasilRegex& hasil) const;

    /// Ganti semua kecocokan. `ganti` boleh memuat `$&` (seluruh kecocokan) dan
    /// `$0`..`$9` (kelompok tangkap).
    [[nodiscard]] std::string ganti(std::string_view subjek, std::string_view ganti,
                                   std::size_t jumlah_maks = static_cast<std::size_t>(-1)) const;

    /// Semua kecocokan (untuk `pecah`).
    [[nodiscard]] std::vector<HasilRegex> semua(std::string_view subjek,
                                               std::size_t jumlah_maks = static_cast<std::size_t>(-1)) const;

    /// True kalau pencarian terakhir dihentikan karena anggaran langkah habis
    /// (pola terlalu rumit). Berlaku sampai panggilan `cari`/`kabeh` berikutnya.
    [[nodiscard]] bool batas_terlampaui() const noexcept { return batas_terlampaui_; }

    [[nodiscard]] bool global() const noexcept { return flag_global_; }
    [[nodiscard]] bool abaikan_besar_kecil() const noexcept { return flag_i_; }
    [[nodiscard]] bool multibaris() const noexcept { return flag_m_; }
    [[nodiscard]] bool titik_semu() const noexcept { return flag_s_; }
    [[nodiscard]] bool lengket() const noexcept { return flag_y_; }
    [[nodiscard]] const std::string& pola() const noexcept { return pola_; }
    [[nodiscard]] const std::string& flag() const noexcept { return flag_; }
    [[nodiscard]] std::size_t jumlah_kelompok() const noexcept { return jumlah_kelompok_; }
    /// Nama kelompok tangkap yang bernama, sejajar dengan nomor kelompok.
    [[nodiscard]] const std::vector<std::string>& nama_kelompok() const noexcept { return nama_kelompok_; }

    /// Anggaran langkah per pemanggilan `cari`/`kabeh`. 200.000 cukup untuk
    /// teks normal beberapa KB pada pola wajar, dan masih memberi margin untuk
    /// pola patologis yang sah -- tapi tetap batas yang tidak bisa dipercaya
    /// seratus persen. Bisa diubah pemanggil untuk pola yang memang mahal.
    void set_anggaran_langkah(std::size_t n) noexcept { anggaran_langkah_ = n; }
    [[nodiscard]] std::size_t anggaran_langkah() const noexcept { return anggaran_langkah_; }

    /// Pohon program (lihat `regexp.cpp`). Publik supaya implementasi
    /// pencocokan di `.cpp` bisa memakainya tanpa friend.
    struct Node;

private:
    RegexProgram() = default;

    std::string pola_;
    std::string flag_;
    bool flag_global_ = false;
    bool flag_i_ = false;
    bool flag_m_ = false;
    bool flag_s_ = false;
    bool flag_y_ = false;
    std::size_t jumlah_kelompok_ = 0;
    std::size_t anggaran_langkah_ = 200000;
    mutable bool batas_terlampaui_ = false;
    std::vector<std::string> nama_kelompok_;
    std::shared_ptr<Node> akar;
};

}  // namespace jawa::rt
