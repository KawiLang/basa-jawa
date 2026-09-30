// `jawa repl` — lingkup baca-evaluasi-cetak.
//
// Yang membuatnya repot adalah bentuk program: setiap baris diurai, dikompilasi,
// dan dijalankan sebagai modul BARU. Modul punya frame sendiri, jadi pengikut
// `ana x = 1` pada modul itu tidak akan ada pada baris berikutnya -- kecuali
// penyimpanannya dipindah ke global, yang milik VM dan bertahan antar evaluasi
// (lihat `Compiler::set_repl`).
//
// Dua keputusan lain:
//
//  - **Nilai ekspresi dicetak.** Baris yang hanya berisi ekspresi dibungkus
//    `tulis(<ekspresi>)` sebelum dikompilasi. Ini murah dan tidak perlu API
//    nilai-kembalian baru di VM. Baris yang berupa statement (diakhiri `;` atau
//    diawali kata kunci deklarasi) tidak dibungkus, jadi `tulis(1);` tetap
//    mencetak tepat sekali.
//
//  - **Masukan beberapa baris ditunda.** Kurung kurawal/kurung siku/kurung
//    kurung yang belum seimbang berarti pengguna belum selesai mengetik, jadi
//    baris berikutnya adalah kelanjutan. String yang belum ditutup ikut
//    memanggil kelanjutan. Penentuannya memakai lexer, sehingga `{` di dalam
//    teks atau komentar tidak dihitung.
//
// Keluaran program (`tulis`) langsung ke stdout -- tidak ditangkap. REPL harus
// menampilkan keluaran secepatnya, dan menangkap lalu memutuskan kapan
// menampilkannya justru menambah penundaan tanpa manfaat.
//
// Mode non-interaktif (stdin bukan terminal) tidak mencetak prompt dan berhenti
// di akhir masukan -- supaya REPL bisa dipakai di dalam uji.
#pragma once

#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace jawa::cli {

/// Hasil satu evaluasi baris REPL.
struct HasilRepl {
    bool keluar = false;      ///< pengguna meminta keluar
    bool dievaluasi = false;  ///< ada yang benar-benar dijalankan
    bool ada_galat = false;   ///< kompilasi atau runtime gagal
};

/// Hasil pemeriksaan kelengkapan satu masukan REPL.
struct Kelengkapan {
    bool lengkap = true;
    std::string alasan;  ///< kenapa belum lengkap (untuk pesan galat)
};

/// Apakah `sumber` sudah cukup untuk dievaluasi.
///
/// Belum lengkap kalau ada kurung/kurung kurawal yang belum tertutup, atau
/// string/template literal/komentar blok yang belum ditutup. Memakai lexer
/// supaya `{` di dalam string atau komentar tidak ikut dihitung.
Kelengkapan cek_kelengkapan(std::string_view sumber);

/// Sarana yang dipakai REPL untuk mengevaluasi satu potongan atau satu perintah
/// (yang diawali `:`).
using Evaluator = std::function<HasilRepl(std::string_view baris)>;

/// Jalankan satu sesi REPL.
///
/// `pemasok_baris` mengembalikan baris berikutnya tanpa baris baru di akhir, atau
/// `std::nullopt` pada akhir masukan. `interaktif` mengatur apakah prompt dan
/// kelanjutannya dicetak.
///
/// Mengembalikan jumlah evaluasi yang gagal, jadi bisa dipakai sebagai kode
/// keluar proses.
int jalankan_repl(std::function<std::optional<std::string>()> pemasok_baris, bool interaktif,
                  std::ostream& keluar, std::ostream& galat, const Evaluator& evaluasi);

/// Cetak daftar perintah REPL.
void cetak_bantuan_repl(std::ostream& keluar);

}  // namespace jawa::cli
