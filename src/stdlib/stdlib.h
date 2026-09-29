// Pustaka standar Basa Jawa — antarmuka ke C++.
#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "rt/object.h"
#include "rt/value.h"

namespace jawa::vm {
class VM;
}

namespace jawa::rt {
class NativeFnObj;
class ObyekObj;
}
namespace jawa::vm::stdlib {

/// Tabel global & method bawaan milik SATU VM.
///
/// Dulu kumpulan ini `static` di dalam satu fungsi, sehingga seluruh VM dalam
/// satu proses berbagi tabel yang sama. Itu-race-butir yang tidak terlihat bila
/// ada tepat satu VM per proses -- tapi `jawa tes` membuat satu VM per berkas uji,
/// dan nilainya menunjuk heap VM pertama: setelah VM itu dihancurkan, VM kedua
/// membaca pointer yang sudah dibebaskan. Karena itu `State` sekarang dimiliki
/// VM (lihat `VM::stdlib_`).
struct State {
    std::unordered_map<std::string, rt::Value> tabel;
    /// Method bawaan pada prototype: kunci "jenis:nama" -> fungsi native.
    std::unordered_map<std::string, rt::Value> metode;
    /// Method objek Janji: nama -> fungsi native (lihat `method_janji`).
    std::unordered_map<std::string, rt::NativeFn> janji_metode;
};

/// Daftarkan seluruh objek & fungsi bawaan ke global.
void pasang_semua(VM& vm);

/// Daftarkan `Peta`, `Himpunan`, dan method Janji gabungan
/// (`Janji.all`/`race`/`selesai`/`tolak`). Terpisah dari `pasang_semua`
/// supaya implementasinya bisa hidup di berkas sendiri (`stdlib_peta.cpp`).
void pasang_peta(VM& vm);

/// Daftarkan fungsi native sebagai global.
void daftarkan(VM& vm, std::string_view nama, std::size_t n_param, bool variadic, rt::NativeFn fn);
/// Daftarkan nilai konstan sebagai global.
void daftarkan_nilai(VM& vm, std::string_view nama, rt::Value v);
/// Objek dengan properti native (namespace `Matematika`, `Janji`, ...).
rt::ObyekObj* buat_namespace(VM& vm, std::string_view nama);
/// Tempel method pada prototype jenis objek tertentu (key "jenis:nama").
void pasang_prototype_metode(VM& vm, std::uint8_t jenis_objek, std::string_view nama, std::size_t n_param,
                             rt::NativeFn fn);
/// Tempel method pada satu objek (namespace).
void pasang_objek_metode(VM& vm, rt::ObyekObj* o, std::string_view nama, std::size_t n_param, rt::NativeFn fn);

/// Ambil/tulis global (dipakai VM).
rt::Value ambil_global(VM& vm, std::string_view nama);
void set_global(VM& vm, std::string_view nama, rt::Value v);

/// Panggil fungsi native.
rt::Value panggil_native(VM& vm, rt::NativeFnObj* native, rt::Value this_val, std::vector<rt::Value>& args);

/// Cari method bawaan untuk nama tertentu pada prototype (mis. "peta" pada Dhaptar).
rt::Value cari_metode_builtin(VM& vm, std::string_view nama, std::uint8_t jenis_objek);

/// Method bawaan objek Janji: nama -> fungsi native. Diisi `pasang_semua`;
/// dibaca `VM::ambil_properti` saat medal `then`/`tangkep` pada Janji.
std::unordered_map<std::string, rt::NativeFn>& method_janji(VM& vm);

}  // namespace jawa::vm::stdlib
