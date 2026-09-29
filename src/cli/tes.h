// `jawa tes` — kerangka uji untuk program Basa Jawa.
//
// Setiap berkas `.jw` adalah satu berkas uji. Penulis cukup menulis perbandingan
// di tubuh program; `jawa tes` memasang sejumlah fungsi bawaan sebagai global,
// menjalankan berkasnya, lalu melaporkan assertion yang gagal.
//
// Fungsi bawaan (lihat `pasang_bawaan_tes`):
//
//   pratelas(nilai, harapan, pesan?)  nilai harus sama secara mendalam
//   wajib_bener(nilai, pesan?)       nilai harus bernilai `bener`
//   wajib_salah(nilai, pesan?)       nilai harus bernilai `salah`
//   wajib_lempar(fn, pesan?)         `fn` harus melempar nilai apa pun
//
// Nama fungsi TIDAK boleh sama dengan kata kunci. `bener` dan `salah` adalah
// keyword, jadi penamaan assertion memakai awalan `wajib_` (harus).
//
// Assertion yang gagal TIDAK menghentikan program: seluruh kegagalan di satu
// berkas dilaporkan sekaligus, lalu `jawa tes` keluar dengan kode bukan nol.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "vm/vm.h"

namespace jawa::cli {

/// Hasil sekumpulan berkas uji.
struct RingkasanTes {
    std::size_t berkas = 0;         ///< jumlah berkas yang dijalankan
    std::size_t assertion = 0;      ///< jumlah assertion yang dievaluasi
    std::size_t gagal = 0;          ///< jumlah assertion yang gagal
    std::size_t berkas_gagal = 0;   ///< berkas yang gagal dimuat/dijalankan
};

/// Jalankan satu berkas uji. Mengembalikan `true` bila tidak ada kegagalan.
/// `warna` menentukan penggunaan escape ANSI pada keluaran.
bool jalankan_berkas_tes(const std::string& path, const vm::VMOptions& opsi, bool warna, RingkasanTes& ringkas);

/// Kumpulkan berkas uji di bawah `akar` secara rekursif.
///
/// Suffiks `.tes.jw` selalu dikenali di semua tingkat. Berkas `.jw` biasa hanya
/// diambil bila berada langsung di `akar`, supaya `jawa tes tests/` tidak ikut
/// menjalankan berkas pendukung. Hasil terurut agar keluaran deterministik.
std::vector<std::string> kumpulkan_berkas_tes(const std::string& akar);

}  // namespace jawa::cli
