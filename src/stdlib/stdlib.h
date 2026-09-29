// Pustaka standar Basa Jawa — antarmuka ke C++.
#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "rt/value.h"

namespace jawa::vm {
class VM;
}

namespace jawa::rt {
class NativeFnObj;
}

namespace jawa::vm::stdlib {

/// Daftarkan seluruh objek & fungsi bawaan ke global.
void pasang_semua(VM& vm);

/// Ambil/tulis global (dipakai VM).
rt::Value ambil_global(VM& vm, std::string_view nama);
void set_global(VM& vm, std::string_view nama, rt::Value v);

/// Panggil fungsi native.
rt::Value panggil_native(VM& vm, rt::NativeFnObj* native, rt::Value this_val, std::vector<rt::Value>& args);

/// Cari method bawaan untuk nama tertentu pada prototype (mis. "peta" pada Dhaptar).
rt::Value cari_metode_builtin(std::string_view nama, std::uint8_t jenis_objek);

}  // namespace jawa::vm::stdlib
