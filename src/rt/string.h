// Operasi string runtime Basa Jawa (UTF-8 aware).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rt/value.h"

namespace jawa::gc {
class Heap;
}  // namespace jawa::gc

namespace jawa::vm {
class VM;
}  // namespace jawa::vm

namespace jawa::rt {

using VM = ::jawa::vm::VM;
class TeksObj;
class ArrayObj;
class ObyekObj;
class PetaObj;

/// Tulis nilai apa pun ke teks (implementasi `toString`).
std::string nilai_ke_teks(VM& vm, Value v);

/// Bantu CLI: teks nilai tanpa perlu VM (dipakai `jawa bytecode`).
/// Untuk objek selain Teks mengembalikan "?" agar output ringkas.
std::string nilai_ke_teks_inspect_dummy(Value v);

/// Representasi internal (inspect) untuk REPL & diagnostik.
std::string nilai_ke_teks_inspect(VM& vm, Value v, int kedalaman = 0);

/// Nama jenis untuk `jinis` / `typeof`.
std::string_view nama_jenis(Value v);

/// `[1, 2, 3]` atau `1 + 2` (batas operator untuk pretty-print).
std::string dhaptar_ke_teks(VM& vm, ArrayObj* a, int kedalaman);

/// Peta ke `{ a: 1, b: 2 }`.
std::string peta_ke_teks(VM& vm, PetaObj* p, int kedalaman);

/// Bangun objek Teks baru.
TeksObj* buat_teks(gc::Heap& heap, std::string_view s);
TeksObj* buat_teks_dari(std::string s, gc::Heap& heap);

/// Gabung dua nilai teks/teks lain dengan aturan `+` (tanpa koersi implisit).
bool teks_gabung(VM& vm, Value a, Value b, Value& keluar);

/// Bandingkan dua string_view (untuk `==`, `<`, dsb).
int bandingkan_teks(std::string_view a, std::string_view b);

/// Apakah `s` adalah indeks numerik bulat ("0", "12").
bool indeks_bulat(std::string_view s, std::size_t& keluar);

/// Panjang dalam code point.
std::size_t panjang_code_point(std::string_view s);

/// Substring berbasis code point.
std::string_view potong_code_point(std::string_view s, std::size_t dari, std::size_t sampai);

/// Normalisasi sederhana: NFKC-lite (hanya komposisi ASCII umum). Dokumentasikan
/// cakupannya yang terbatas di docs/stdlib.md.
std::string normalisasi_sederhana(std::string_view s, std::string_view bentuk);

}  // namespace jawa::rt
