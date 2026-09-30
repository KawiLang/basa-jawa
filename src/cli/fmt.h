// `jawa fmt` — antarmuka pemformat kode.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace jawa::cli {

/// Opsi `jawa fmt`.
struct FormatOptions {
    /// `--cek`: jangan menulis, hanya laporkan apakah perlu diformat.
    bool cek = false;
    /// Lebar satu tingkat indentasi (spasi).
    std::size_t lebar_indent = 4;
    /// Parse ulang hasil format untuk memastikan programnya utuh. Menyalakan
    /// ini adalah jaring pengaman: kalau formatter punya bug, hasilnya
    /// ditolak, bukan ditulis ke berkas.
    bool cek_sintaks = true;
};

/// Hasil satu pemformatan.
struct HasilFormat {
    std::string teks;       ///< hasil format (kosong kalau `--cek`)
    bool berubah = false;   ///< hasil berbeda dari masukan
    bool galat = false;     ///< format gagal; lihat `pesan`
    std::string pesan;       ///< alasan kegagalan
    std::uint32_t baris = 0;      ///< baris terkait pada kegagalan
};

/// Format satu program. Kembalikan `true` kalau tidak ada kegagalan.
///
/// Tidak pernah melempar galatsyntax: kalau sumber tidak bisa di-lex, `hasil`
/// diisi `galat = true` dengan pesan yang siap ditampilkan.
bool format_sumber(std::string_view sumber, std::string_view nama_berkas, const FormatOptions& opsi,
                   HasilFormat& hasil);

}  // namespace jawa::cli
