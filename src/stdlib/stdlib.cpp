// Pustaka standar Basa Jawa — implementasi minimal Fase 3.
//
// Tersedia sekarang (dipakai contoh acuan Bagian 11):
//   tulis, tulis_nol, tulis_kleru, Teks, Angka, Boole, pratela,
//   konsol.*, Math.*, JSON.*, Dhaptar (method inti), Teks (method inti).
//
// Modul lengkap (Tanggal, mesin regex, berkas, proses, std:*) menyusul di Fase 8.
#include "stdlib/stdlib.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>

#include "gc/heap.h"
#include "rt/number.h"
#include "rt/regexp.h"
#include "rt/tanggal.h"
#include "rt/object.h"
#include "rt/string.h"
#include "vm/vm.h"

namespace jawa::vm::stdlib {

using rt::ArrayObj;
using rt::ClassObj;
using rt::InstanceObj;
using rt::NativeFnObj;
using rt::ObyekObj;
using rt::Obj;
using rt::OK;
using rt::TeksObj;
using rt::Value;

namespace {

/// Tabel global & method bawaan milik VM yang sedang memasang.
///
/// Dulu `static` di dalam fungsi, jadi SEMUA VM dalam satu proses berbagi satu
/// tabel. `jawa tes` membuat satu VM per berkas uji, jadi nilainya akan menunjuk
/// heap VM yang sudah dihancurkan. Sekarang tabelnya milik VM (lihat
/// `stdlib::State` dan `VM::stdlib_`).
State& global(VM& vm) { return vm.tabel_stdlib(); }

Value native_teks_objek(VM& vm, std::string_view s) { return Value::obyek(rt::buat_teks(vm.heap(), s)); }

/// Daftarkan fungsi native sebagai global.
void daftarkan_impl(VM& vm, std::string_view nama, std::size_t n_param, bool variadic, rt::NativeFn fn) {
    auto* n = vm.heap().alokasi<NativeFnObj>();
    n->h.kind = OK::Native;
    n->fn = fn;
    n->nama = nama;
    n->jumlah_param = static_cast<std::uint8_t>(n_param);
    n->variadic = variadic;
    global(vm).tabel[std::string(nama)] = Value::obyek(n);
}

/// Daftarkan nilai konstan sebagai global.
void daftarkan_nilai_impl(VM& vm, std::string_view nama, Value v) { global(vm).tabel[std::string(nama)] = v; }

/// Tempel properti pada fungsi native (method statis konstruktor).
void sifat(VM& vm, NativeFnObj* f, std::string_view nama, Value v) {
    const std::pair<Value, Value> kv(Value::obyek(rt::buat_teks(vm.heap(), nama)), v);
    f->sifat.push_back(kv);
    // GC tidak menelusuri `NativeFnObj::sifat`, jadi daftarkan terpisah.
    vm.native_sifat_root().push_back(kv);
}

/// Buat fungsi native biasa (dipakai untuk method statis pada konstruktor).
Value native_baru(VM& vm, std::string_view nama, std::size_t n_param, rt::NativeFn fn) {
    auto* n = vm.heap().alokasi<NativeFnObj>();
    n->h.kind = OK::Native;
    n->fn = fn;
    n->nama = nama;
    n->jumlah_param = static_cast<std::uint8_t>(n_param);
    return Value::obyek(n);
}

/// Bantu: objek dengan properti native (namespace `Math`, `JSON`, ...).
ObyekObj* buat_namespace_impl(VM& vm, std::string_view nama) {
    ObyekObj* o = vm.buat_obyek();
    o->define(vm.heap(), native_teks_objek(vm, "nama"), native_teks_objek(vm, nama), rt::AttrDefault);
    return o;
}

// ===========================================================================
// Native: keluaran
// ===========================================================================

/// Tulis baris ke stdout, atau ke buffer VM bila `VMOptions::keluaran` diisi.
void tulis_baris(VM& vm, const std::string& baris) {
    if (vm.opsi().keluaran != nullptr) {
        *vm.opsi().keluaran += baris;
        return;
    }
    std::fwrite(baris.data(), 1, baris.size(), stdout);
}

Value native_tulis(VM& vm, Value /*this*/, std::vector<Value>& args) {
    std::string baris;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) baris += " ";
        baris += rt::nilai_ke_teks(vm, args[i]);
    }
    baris += "\n";
    tulis_baris(vm, baris);
    return Value::mboh();
}

Value native_tulis_nol(VM& vm, Value /*this*/, std::vector<Value>& args) {
    std::string baris;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) baris += ' ';
        baris += rt::nilai_ke_teks(vm, args[i]);
    }
    tulis_baris(vm, baris);
    std::fflush(stdout);
    return Value::mboh();
}

Value native_tulis_kleru(VM& vm, Value /*this*/, std::vector<Value>& args) {
    std::string baris;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) baris += " ";
        baris += rt::nilai_ke_teks(vm, args[i]);
    }
    baris += "\n";
    if (vm.opsi().keluaran != nullptr) {
        *vm.opsi().keluaran += baris;
    } else {
        std::fwrite(baris.data(), 1, baris.size(), stderr);
    }
    return Value::mboh();
}

// ===========================================================================
// Native: konversi & helper
// ===========================================================================

Value native_teks(VM& vm, Value /*this*/, std::vector<Value>& args) {
    if (args.empty()) return native_teks_objek(vm, "");
    return native_teks_objek(vm, rt::nilai_ke_teks(vm, args[0]));
}

Value native_angka(VM& vm, Value /*this*/, std::vector<Value>& args) {
    if (args.empty()) return Value::number(0.0);
    const Value& v = args[0];
    if (v.is_angka()) return v;
    if (v.is_boole()) return Value::number(v.bool_value() ? 1.0 : 0.0);
    double d = 0.0;
    const std::string s = rt::nilai_ke_teks(vm, v);
    if (rt::parse_number_strict(s, d)) return Value::number(d);
    return Value::number(std::nan(""));
}

Value native_boole(VM& /*vm*/, Value /*this*/, std::vector<Value>& args) {
    if (args.empty()) return Value::boolean(false);
    return Value::boolean(rt::benar(args[0]));
}

Value native_pratela(VM& /*vm*/, Value /*this*/, std::vector<Value>& args) {
    if (args.size() < 2) return Value::boolean(true);
    return Value::boolean(rt::benar(args[0]) == rt::benar(args[1]));
}

Value native_jenis(VM& vm, Value /*this*/, std::vector<Value>& args) {
    if (args.empty()) return native_teks_objek(vm, "mboh");
    return native_teks_objek(vm, rt::nama_jenis(args[0]));
}

Value native_metu(VM& /*vm*/, Value /*this*/, std::vector<Value>& args) {
    const int kode = args.empty() ? 0 : static_cast<int>(args[0].as_number());
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(kode);
}

// ===========================================================================
// Native: Dhaptar (method inti)
// ===========================================================================

bool earthykan(Value v) { return v.is_obyek() && v.pointer() != nullptr && static_cast<const Obj*>(v.pointer())->h.kind == OK::Array; }

ArrayObj* sb(VM& vm, Value v) {
    (void)vm;
    auto* o = static_cast<ArrayObj*>(const_cast<void*>(v.pointer()));
    return o;
}

Value native_dhaptar_tambah(VM& vm, Value this_val, std::vector<Value>& args) {
    if (!earthykan(this_val)) return Value::mboh();
    ArrayObj* a = sb(vm, this_val);
    for (const Value& v : args) a->dorong(v, &vm.heap());
    return Value::number(static_cast<double>(a->panjang));
}

Value native_dhaptar_pop(VM& vm, Value this_val, std::vector<Value>& /*args*/) {
    if (!earthykan(this_val) || sb(vm, this_val)->panjang == 0) return Value::mboh();
    ArrayObj* a = sb(vm, this_val);
    const Value v = a->elemen[a->panjang - 1];
    --a->panjang;
    return v;
}

Value native_dhaptar_gabung(VM& vm, Value this_val, std::vector<Value>& args) {
    if (!earthykan(this_val)) return Value::mboh();
    ArrayObj* a = sb(vm, this_val);
    std::string bagian = args.empty() ? "," : rt::nilai_ke_teks(vm, args[0]);
    std::string out;
    for (std::size_t i = 0; i < a->panjang; ++i) {
        if (i > 0) out += bagian;
        out += rt::nilai_ke_teks(vm, a->elemen[i]);
    }
    return native_teks_objek(vm, out);
}

Value native_dhaptar_peta(VM& vm, Value this_val, std::vector<Value>& args) {
    if (!earthykan(this_val) || args.empty()) return Value::mboh();
    ArrayObj* a = sb(vm, this_val);
    ArrayObj* keluar = vm.buat_dhaptar(a->panjang);
    for (std::size_t i = 0; i < a->panjang; ++i) {
        std::vector<Value> satu{a->elemen[i]};
        keluar->dorong(vm.panggil(args[0], Value::mboh(), satu), &vm.heap());
    }
    return Value::obyek(keluar);
}

Value native_dhaptar_saring(VM& vm, Value this_val, std::vector<Value>& args) {
    if (!earthykan(this_val) || args.empty()) return Value::mboh();
    ArrayObj* a = sb(vm, this_val);
    ArrayObj* keluar = vm.buat_dhaptar(a->panjang);
    for (std::size_t i = 0; i < a->panjang; ++i) {
        std::vector<Value> satu{a->elemen[i]};
        if (rt::benar(vm.panggil(args[0], Value::mboh(), satu))) keluar->dorong(a->elemen[i], &vm.heap());
    }
    return Value::obyek(keluar);
}

Value native_dhaptar_saben(VM& vm, Value this_val, std::vector<Value>& args) {
    if (!earthykan(this_val) || args.empty()) return Value::mboh();
    ArrayObj* a = sb(vm, this_val);
    for (std::size_t i = 0; i < a->panjang; ++i) {
        std::vector<Value> satu{a->elemen[i]};
        if (!rt::benar(vm.panggil(args[0], Value::mboh(), satu))) return Value::boolean(false);
    }
    return Value::boolean(true);
}

Value native_dhaptar_kurangi(VM& vm, Value this_val, std::vector<Value>& args) {
    if (!earthykan(this_val) || args.empty()) return Value::mboh();
    ArrayObj* a = sb(vm, this_val);
    if (a->panjang == 0) return Value::mboh();
    Value awal = a->elemen[0];
    for (std::size_t i = 1; i < a->panjang; ++i) {
        std::vector<Value> dua{awal, a->elemen[i]};
        awal = vm.panggil(args[0], Value::mboh(), dua);
    }
    return awal;
}

Value native_dhaptar_da(VM& vm, Value this_val, std::vector<Value>& args) {
    if (!earthykan(this_val) || args.empty()) return Value::mboh();
    ArrayObj* a = sb(vm, this_val);
    if (a->panjang == 0) return Value::mboh();
    return a->elemen[a->panjang - 1];
}

// ===========================================================================
// Native: Teks (method inti)
// ===========================================================================

bool earthy_teks(Value v) { return v.is_obyek() && v.pointer() != nullptr && static_cast<const Obj*>(v.pointer())->h.kind == OK::Teks; }

std::string_view str(Value v) { return static_cast<const TeksObj*>(v.pointer())->str(); }

Value native_teks_dawa(VM& /*vm*/, Value this_val, std::vector<Value>& /*args*/) {
    if (!earthy_teks(this_val)) return Value::number(0.0);
    return Value::number(static_cast<double>(static_cast<const TeksObj*>(this_val.pointer())->dawa()));
}

Value native_teks_huruf_gedhe(VM& vm, Value this_val, std::vector<Value>& /*args*/) {
    if (!earthy_teks(this_val)) return this_val;
    std::string s = std::string(str(this_val));
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return native_teks_objek(vm, s);
}

Value native_teks_huruf_kecil(VM& vm, Value this_val, std::vector<Value>& /*args*/) {
    if (!earthy_teks(this_val)) return this_val;
    std::string s = std::string(str(this_val));
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return native_teks_objek(vm, s);
}

Value native_teks_pangkas(VM& vm, Value this_val, std::vector<Value>& args) {
    if (!earthy_teks(this_val) || args.empty()) return this_val;
    const std::size_t a = static_cast<std::size_t>(args[0].as_number());
    const std::size_t b = args.size() > 1 ? static_cast<std::size_t>(args[1].as_number()) : str(this_val).size();
    if (a > b || a >= str(this_val).size()) return native_teks_objek(vm, "");
    return native_teks_objek(vm, str(this_val).substr(a, b - a));
}

Value native_teks_ganti(VM& vm, Value this_val, std::vector<Value>& args) {
    if (!earthy_teks(this_val) || args.size() < 2) return this_val;
    const std::string s(str(this_val));
    const std::string cari = rt::nilai_ke_teks(vm, args[0]);
    const std::string ganti = rt::nilai_ke_teks(vm, args[1]);
    std::string out;
    if (cari.empty()) return native_teks_objek(vm, s);
    std::size_t pos = 0;
    for (;;) {
        const std::size_t ketemu = s.find(cari, pos);
        if (ketemu == std::string::npos) { out += s.substr(pos); break; }
        out += s.substr(pos, ketemu - pos);
        out += ganti;
        pos = ketemu + cari.size();
    }
    return native_teks_objek(vm, out);
}

Value native_teks_pecah(VM& vm, Value this_val, std::vector<Value>& args) {
    if (!earthy_teks(this_val) || args.empty()) return Value::mboh();
    const std::string s(str(this_val));
    const std::string pisah = rt::nilai_ke_teks(vm, args[0]);
    ArrayObj* keluar = vm.buat_dhaptar();
    if (pisah.empty()) {
        for (std::size_t i = 0; i < s.size(); ++i) keluar->dorong(native_teks_objek(vm, std::string(1, s[i])), &vm.heap());
        return Value::obyek(keluar);
    }
    std::size_t pos = 0;
    for (;;) {
        const std::size_t ketemu = s.find(pisah, pos);
        if (ketemu == std::string::npos) {
            keluar->dorong(native_teks_objek(vm, s.substr(pos)), &vm.heap());
            break;
        }
        keluar->dorong(native_teks_objek(vm, s.substr(pos, ketemu - pos)), &vm.heap());
        pos = ketemu + pisah.size();
    }
    return Value::obyek(keluar);
}

Value native_teks_termasuk(VM& vm, Value this_val, std::vector<Value>& args) {
    if (!earthy_teks(this_val) || args.empty()) return Value::boolean(false);
    return Value::boolean(str(this_val).find(rt::nilai_ke_teks(vm, args[0])) != std::string_view::npos);
}

Value native_teks_mulainya_dengan(VM& vm, Value this_val, std::vector<Value>& args) {
    if (!earthy_teks(this_val) || args.empty()) return Value::boolean(false);
    const std::string_view s = str(this_val);
    return Value::boolean(s.substr(0, std::min(s.size(), rt::nilai_ke_teks(vm, args[0]).size())) ==
                          rt::nilai_ke_teks(vm, args[0]));
}

// ===========================================================================
// Native: Matematika
// ===========================================================================

using MathFn = double (*)(double);

Value m1(VM& /*vm*/, MathFn f, std::vector<Value>& args) {
    if (args.empty()) return Value::number(std::nan(""));
    return Value::number(f(args[0].as_number()));
}

Value mmin(VM& /*vm*/, Value, std::vector<Value>& args) {
    if (args.size() < 2) return Value::number(std::nan(""));
    return Value::number(std::fmin(args[0].as_number(), args[1].as_number()));
}

Value mmaks(VM& /*vm*/, Value, std::vector<Value>& args) {
    if (args.size() < 2) return Value::number(std::nan(""));
    return Value::number(std::fmax(args[0].as_number(), args[1].as_number()));
}

Value m_pangkat(VM& /*vm*/, Value, std::vector<Value>& args) {
    if (args.size() < 2) return Value::number(std::nan(""));
    return Value::number(std::pow(args[0].as_number(), args[1].as_number()));
}

#define JAWA_MATH1(NAMA, FN)                                        \
    Value math_##NAMA(VM& vm, Value, std::vector<Value>& args) {   \
        return m1(vm, FN, args);                                  \
    }
JAWA_MATH1(mutlak, std::fabs)
JAWA_MATH1(lantai, std::floor)
JAWA_MATH1(bunder, std::ceil)
JAWA_MATH1(akar, std::sqrt)
JAWA_MATH1(sin, std::sin)
JAWA_MATH1(cos, std::cos)
JAWA_MATH1(tan, std::tan)
JAWA_MATH1(log, std::log)
JAWA_MATH1(log2, std::log2)
JAWA_MATH1(log10, std::log10)
JAWA_MATH1(exp, std::exp)
JAWA_MATH1(tanda, [](double x) -> double { return std::signbit(x) ? 1.0 : -1.0; })
JAWA_MATH1(trunc, std::trunc)
#undef JAWA_MATH1

Value math_factorial(VM& /*vm*/, Value, std::vector<Value>& args) {
    if (args.empty()) return Value::number(1.0);
    const double x = args[0].as_number();
    if (x < 0 || x != std::floor(x)) return Value::number(std::nan(""));
    double hasil = 1.0;
    for (double i = 2.0; i <= x; i += 1.0) hasil *= i;
    return Value::number(hasil);
}

// ===========================================================================
// StdAksara: konversi angka <-> aksara
// ===========================================================================

/// Aksara Jawa untuk digit 0..9 (blok U+A9D0 "JAVANESE LETTER CA" s.d.
/// U+A9D9, dipakai sebagai angka seperti pada Aksara Bali & Thai).
constexpr char32_t kAksaraJawa[10] = {0xA9D0, 0xA9D1, 0xA9D2, 0xA9D3, 0xA9D4,
                                     0xA9D5, 0xA9D6, 0xA9D7, 0xA9D8, 0xA9D9};

void encode_utf8(char32_t cp, std::string& keluar) {
    if (cp <= 0x7F) {
        keluar.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        keluar.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        keluar.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        keluar.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        keluar.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        keluar.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

Value native_aksara_angka_jawa(VM& vm, Value /*this*/, std::vector<Value>& args) {
    if (args.empty()) return native_teks_objek(vm, "");
    const double x = args[0].as_number();
    const bool negatif = x < 0;
    const long long n = static_cast<long long>(negatif ? -x : x);
    std::string aksara;
    if (negatif) encode_utf8(0xA9CA, aksara);  // tanda minus aksara Jawa
    const std::string digit = std::to_string(n);
    for (char c : digit) encode_utf8(kAksaraJawa[static_cast<std::size_t>(c - '0')], aksara);
    return native_teks_objek(vm, aksara);
}

Value native_aksara_angka_arab(VM& vm, Value /*this*/, std::vector<Value>& args) {
    if (args.empty()) return Value::number(0.0);
    std::string s(rt::nilai_ke_teks(vm, args[0]));
    bool negatif = false;
    std::size_t i = 0;
    if (i < s.size() && s[i] == '-') {
        negatif = true;
        i += 1;
    } else if (i < s.size() && static_cast<unsigned char>(s[i]) >= 0x80) {
        // Tanda minus aksara Jawa U+A9CA; kode point lain dianggap digit/error.
        std::size_t len = 1;
        const auto b0 = static_cast<unsigned char>(s[i]);
        if (b0 >= 0xF0) {
            len = 4;
        } else if (b0 >= 0xE0) {
            len = 3;
        } else if (b0 >= 0xC0) {
            len = 2;
        }
        char32_t cp = len == 4 ? (b0 & 0x07u) : (len == 3 ? (b0 & 0x0Fu) : (b0 & 0x1Fu));
        for (std::size_t k = 1; k < len && i + k < s.size(); ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
        }
        if (cp == 0xA9CA) {
            negatif = true;
            i += std::min(len, s.size() - i);
        }
    }
    long long n = 0;
    for (; i < s.size();) {
        char32_t cp = 0;
        const auto b0 = static_cast<unsigned char>(s[i]);
        std::size_t len = 1;
        if (b0 >= 0xF0) { len = 4; cp = b0 & 0x07u; }
        else if (b0 >= 0xE0) { len = 3; cp = b0 & 0x0Fu; }
        else if (b0 >= 0xC0) { len = 2; cp = b0 & 0x1Fu; }
        for (std::size_t k = 1; k < len && i + k < s.size(); ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
        }
        i += len;
        if (cp < 0xA9D0 || cp > 0xA9D9) return Value::number(std::nan(""));
        n = n * 10 + static_cast<long long>(cp - 0xA9D0);
    }
    return Value::number(static_cast<double>(negatif ? -n : n));
}


// ===========================================================================
// Janji (Promise) & Wektu (timer)
// ===========================================================================

/// Ambil Janji dari nilai; `nullptr` bila bukan Janji.
rt::JanjiObj* janji_dari(Value v) {
    if (!v.is_obyek()) return nullptr;
    auto* j = static_cast<rt::JanjiObj*>(v.mutable_pointer());
    return (j != nullptr && j->h.kind == OK::Janji) ? j : nullptr;
}

/// Daftarkan handler pada Janji. Bila Janji sudah selesai, handler langsung
/// dijadwalkan sebagai mikrotugas (meniru `then` pada promise yang sudah
/// settled: handler tetap terpanggil, tapi asynchronously).
Value daftarkan_then(VM& vm, rt::JanjiObj* j, Value on_slamet, Value on_tolak) {
    if (!on_slamet.is_obyek() && !on_tolak.is_obyek()) return Value::mboh();
    rt::JanjiObj* turunan = vm.buat_janji();
    const Value tv = Value::obyek(turunan);
    if (j->status == rt::JanjiStatus::Menunggu) {
        j->then_daftar.push_back(rt::JanjiObj::Then{on_slamet, on_tolak, tv});
        return tv;
    }
    const bool ditolak = j->status == rt::JanjiStatus::Gagal;
    const Value f = ditolak ? on_tolak : on_slamet;
    if (f.is_obyek()) vm.jadwal_mikrotugas(j, ditolak ? j->hasil : j->hasil, ditolak, f, tv);
    return tv;
}

/// `.then(f)`: kembalikan Janji baru yang selesai setelah `f` dijalankan.
Value native_janji_then(VM& vm, Value this_val, std::vector<Value>& args) {
    auto* j = janji_dari(this_val);
    if (j == nullptr) return this_val;
    const Value f = args.empty() ? Value::mboh() : args[0];
    return daftarkan_then(vm, j, f, f);
}

/// `.tangkep(f)`: handler penolakan (jalur `Gagal`).
Value native_janji_tangkep(VM& vm, Value this_val, std::vector<Value>& args) {
    auto* j = janji_dari(this_val);
    if (j == nullptr) return this_val;
    const Value f = args.empty() ? Value::mboh() : args[0];
    return daftarkan_then(vm, j, Value::mboh(), f);
}

/// `Wektu.tundha(ms)`: Janji yang selesai pada putaran timer berikutnya.
Value native_wektu_tundha(VM& vm, Value /*this*/, std::vector<Value>& args) {
    const std::uint64_t ms = args.empty() ? 0
                                           : static_cast<std::uint64_t>(args[0].as_number() < 0
                                                                            ? 0
                                                                            : args[0].as_number());
    rt::JanjiObj* j = vm.buat_janji();
    // `setTimeout` menyelesaikan Janji tanpa nilai (setara `mboh`).
    vm.jadwal_timer(ms, j, Value::mboh());
    return Value::obyek(j);
}

/// `Wektu.teka()`: antrean macrotask kosong (sinkron, selalu selesai).
Value native_wektu_teka(VM& vm, Value /*this*/, std::vector<Value>& args) {
    rt::JanjiObj* j = vm.buat_janji();
    vm.selesaikan_janji(j, args.empty() ? native_teks_objek(vm, "teka") : args[0], false);
    return Value::obyek(j);
}

// ===========================================================================
// Native: JSON
// ===========================================================================

Value json_teks(VM& vm, Value /*this*/, std::vector<Value>& args) {
    if (args.empty()) return native_teks_objek(vm, "null");
    return native_teks_objek(vm, rt::nilai_ke_teks(vm, args[0]));
}

}  // namespace

// ===========================================================================
// API publik
// ===========================================================================

namespace {
std::unordered_map<std::string, Value>& metode_bawaan(VM& vm) { return global(vm).metode; }
}  // namespace

std::unordered_map<std::string, rt::NativeFn>& method_janji(VM& vm) { return global(vm).janji_metode; }

Value ambil_global(VM& vm, std::string_view nama) {
    State& g = global(vm);
    auto it = g.tabel.find(std::string(nama));
    if (it == g.tabel.end()) return Value::mboh();
    return it->second;
}

void set_global(VM& vm, std::string_view nama, Value v) { global(vm).tabel[std::string(nama)] = v; }

Value cari_metode_builtin(VM& vm, std::string_view nama, std::uint8_t jenis_objek) {
    const std::string kunci = std::to_string(static_cast<int>(jenis_objek)) + ":" + std::string(nama);
    auto& m = metode_bawaan(vm);
    auto it = m.find(kunci);
    if (it == m.end()) return Value::mboh();
    return it->second;
}

Value panggil_native(VM& vm, NativeFnObj* native, Value this_val, std::vector<Value>& args) {
    if (native->fn == nullptr) return Value::mboh();
    return native->fn(vm, this_val, args);
}

// ===========================================================================
// Native: regex runtime
// ===========================================================================
//
// Pola `/pola/flag` sudah jadi objek `RegexObj` oleh opcode `MAKE_REGEX`. Yang
// dikembalikan di sini adalah hasil pencocokan sebagai nilai biasa: boolean
// untuk `cocog`/`kabeh`, teks untuk `ganti`, dhaptar untuk `pecah`.

namespace {

/// `RegexObj` pada `this_val`, atau `nullptr` kalau bukan regex.
rt::RegexObj* obj_regex(Value v) {
    if (!v.is_obyek() || v.pointer() == nullptr) return nullptr;
    auto* o = const_cast<rt::Obj*>(static_cast<const rt::Obj*>(v.pointer()));
    if (o->h.kind != OK::Regex) return nullptr;
    return static_cast<rt::RegexObj*>(o);
}

/// `TanggalObj` pada `this_val`, atau `nullptr`.
rt::TanggalObj* obj_tanggal(Value v) {
    if (!v.is_obyek() || v.pointer() == nullptr) return nullptr;
    auto* o = const_cast<rt::Obj*>(static_cast<const rt::Obj*>(v.pointer()));
    if (o->h.kind != OK::Tanggal) return nullptr;
    return static_cast<rt::TanggalObj*>(o);
}

/// Teks dari sebuah nilai: `TeksObj` dipinjam langsung (tanpa alokasi), nilai
/// lain dikonversi lewat `nilai_ke_teks` lalu disimpan di buffer thread-local.
std::string_view arg_teks_bersih(Value v) {
    if (v.is_obyek() && v.pointer() != nullptr) {
        const auto* o = static_cast<const rt::Obj*>(v.pointer());
        if (o->h.kind == OK::Teks) return static_cast<const rt::TeksObj*>(o)->str();
    }
    static thread_local std::string buf;
    buf = rt::nilai_ke_teks_inspect_dummy(v);
    return buf;
}

/// Teks argumen ke-`i`, atau string kosong.
std::string_view arg_teks(const std::vector<Value>& args, std::size_t i) {
    if (i >= args.size()) return {};
    return arg_teks_bersih(args[i]);
}

/// Lempar galat kalau pencarian regex dihentikan anggaran langkah.
///
/// Ini PENTING: pola patologis seperti `(a+)+b` membuat backtracking eksponensial
/// dalam waktu. Tanpa anggaran langkah, program bisa menggantung selamanya; tapi
/// jawaban "tidak cocok" yang salah sama buruknya dengan menggantung -- pemanggil
/// akan menyimpulkan tidak ada kecocokan padahal mesinnya menyerah. Jadi anggaran
/// habis dilaporkan sebagai galat yang bisa ditangkap `coba`/`tangkep`.
bool cek_anggaran(VM& vm, const rt::RegexObj* r) {
    if (r == nullptr || r->program == nullptr || !r->program->batas_terlampaui()) return false;
    vm.lempar_kleru("KleruRegex",
                    "Pola regex /" + r->pola + "/ terlalu rumit: mesinnya menyerah setelah " +
                        std::to_string(r->program->anggaran_langkah()) +
                        " langkah. Pola semacam (a+)+b bisa czasnya berlipat ganda; "
                        "tulis ulang polanya supaya tidak ada kuantifier bersarang di atas "
                        "kelompok yang bisa diulang.");
    return true;
}

ArrayObj* dhaptar_baru(VM& vm, std::size_t n) {
    auto* a = vm.buat_dhaptar(n);
    a->panjang = n;
    for (std::size_t i = 0; i < n; ++i) a->elemen[i] = Value::mboh();
    return a;
}


}  // namespace

/// `/pola/.cocog(teks)` -- true kalau ada kecocokan di mana saja.
Value native_regex_cocog(VM& vm, Value this_val, std::vector<Value>& args) {
    rt::RegexObj* r = obj_regex(this_val);
    if (r == nullptr || r->program == nullptr) return Value::boolean(false);
    rt::HasilRegex h;
    const bool cocok = r->program->cari(arg_teks(args, 0), 0, h) && h.cocok;
    if (cek_anggaran(vm, r)) return Value::mboh();
    return Value::boolean(cocok);
}

/// `/pola/.kabeh(teks)` -- true kalau teks SELURUH-nya cocok (anchor dua ujung).
Value native_regex_kabeh(VM& vm, Value this_val, std::vector<Value>& args) {
    rt::RegexObj* r = obj_regex(this_val);
    if (r == nullptr || r->program == nullptr) return Value::boolean(false);
    rt::HasilRegex h;
    const bool cocok = r->program->kabeh(arg_teks(args, 0), h) && h.cocok;
    if (cek_anggaran(vm, r)) return Value::mboh();
    return Value::boolean(cocok);
}

/// `/pola/.ganti(teks, pengganti)` -- ganti semua kecocokan. `$&` = seluruh
/// kecocokan, `$1`..`$9` = kelompok tangkap.
Value native_regex_ganti(VM& vm, Value this_val, std::vector<Value>& args) {
    rt::RegexObj* r = obj_regex(this_val);
    if (r == nullptr || r->program == nullptr) return native_teks_objek(vm, "");
    const std::string hasil = r->program->ganti(arg_teks(args, 0), arg_teks(args, 1));
    if (cek_anggaran(vm, r)) return Value::mboh();
    return native_teks_objek(vm, hasil);
}

/// `/pola/.pecah(teks)` -- dhaptar; tiap elemen adalah dhaptar
/// `[seluruh_cocokan, kelompok1, kelompok2, ...]`.
Value native_regex_pecah(VM& vm, Value this_val, std::vector<Value>& args) {
    rt::RegexObj* r = obj_regex(this_val);
    auto* luar = dhaptar_baru(vm, 0);
    if (r == nullptr || r->program == nullptr) return Value::obyek(luar);
    const std::string_view subjek = arg_teks(args, 0);
    const auto hasil = r->program->semua(subjek);
    if (cek_anggaran(vm, r)) return Value::mboh();
    for (const rt::HasilRegex& h : hasil) {
        const std::size_t n = h.awal.size();
        auto* baris = dhaptar_baru(vm, n);
        for (std::size_t i = 0; i < n; ++i) {
            baris->elemen[i] = native_teks_objek(vm, std::string(h.kelompok(subjek, i)));
        }
        luar->dorong(Value::obyek(baris), &vm.heap());
    }
    return Value::obyek(luar);
}

/// `/pola/.nilai(teks)` -- dhaptar kelompok tangkap dari kecocokan PERTAMA
/// saja (tanpa kelompok 0). Kalau tidak ada kecocokan: dhaptar kosong.
Value native_regex_nilai(VM& vm, Value this_val, std::vector<Value>& args) {
    rt::RegexObj* r = obj_regex(this_val);
    // Tanpa kecocokan: dhaptar KOSONG, bukan dhaptar berisi `mboh`. Pemanggil
    //_then/sedang menghitung panjangnya akan lebih mudah begitu.
    auto* luar = dhaptar_baru(vm, 0);
    if (r == nullptr || r->program == nullptr) return Value::obyek(luar);
    const std::string_view subjek = arg_teks(args, 0);
    rt::HasilRegex h;
    if (!r->program->cari(subjek, 0, h) || !h.cocok) {
        if (cek_anggaran(vm, r)) return Value::mboh();
        return Value::obyek(luar);
    }
    const std::size_t ngrup = r->program->jumlah_kelompok();
    auto* isi = dhaptar_baru(vm, ngrup);
    for (std::size_t i = 1; i <= ngrup; ++i) {
        isi->elemen[i - 1] = native_teks_objek(vm, std::string(h.kelompok(subjek, i)));
    }
    return Value::obyek(isi);
}

/// `/pola/.grup(teks, kelompok)` -- teks kelompok tangkap. `kelompok` boleh
/// nomor (1-based; 0 = seluruh pencocokan) ATAU nama untuk kelompok bernama.
/// Kelompok yang tidak ikut cocok menghasilkan teks kosong.
Value native_regex_grup(VM& vm, Value this_val, std::vector<Value>& args) {
    rt::RegexObj* r = obj_regex(this_val);
    if (r == nullptr || r->program == nullptr) return native_teks_objek(vm, "");
    const std::string_view subjek = arg_teks(args, 0);
    rt::HasilRegex h;
    if (!r->program->cari(subjek, 0, h) || !h.cocok) {
        if (cek_anggaran(vm, r)) return Value::mboh();
        return native_teks_objek(vm, "");
    }
    std::size_t nomor = 0;
    if (args.size() > 1) {
        if (args[1].is_angka()) {
            nomor = static_cast<std::size_t>(args[1].as_number());
        } else {
            const std::string_view nama = arg_teks(args, 1);
            // `nama_kelompok_` sejajar dengan nomor kelompok tangkap.
            const auto& daftar = r->program->nama_kelompok();
            for (std::size_t i = 0; i < daftar.size(); ++i) {
                if (daftar[i] == nama) {
                    nomor = i + 1;
                    break;
                }
            }
        }
    }
    return native_teks_objek(vm, std::string(h.kelompok(subjek, nomor)));
}

Value native_regex_pola(VM& vm, Value this_val, std::vector<Value>&) {
    rt::RegexObj* r = obj_regex(this_val);
    return native_teks_objek(vm, r != nullptr ? r->pola : std::string());
}

Value native_regex_flag(VM& vm, Value this_val, std::vector<Value>&) {
    rt::RegexObj* r = obj_regex(this_val);
    return native_teks_objek(vm, r != nullptr ? r->flag : std::string());
}

// ===========================================================================
// Native: `Tanggal`
// ===========================================================================
//
// Nilai `Tanggal` menyimpan milidetik sejak epoch UTC. Waktu lokal tidak
// dipakai: tanpa basis zona waktu yang andal, "jam berapa di sini" lebih
// sering salah daripada tidak dijawab. Yang tersedia adalah kalender UTC.

/// `Tanggal()` = sekarang, `Tanggal(angka)` = dari ms sejak epoch,
/// `Tanggal("ISO")` = dari teks `YYYY-MM-DD[THH:MM[:SS[.sss]][Z]]`.
/// Teks yang tidak bisa diparse menghasilkan `mboh` (seperti `Teks("x")`).
Value native_tanggal_buat(VM& vm, Value, std::vector<Value>& args) {
    if (args.empty()) {
        const rt::Tanggal sekarang = rt::Tanggal::sekarang();
        auto* o = vm.heap().alokasi<rt::TanggalObj>();
        o->h.kind = OK::Tanggal;
        o->milidetik = sekarang.milidetik();
        return Value::obyek(o);
    }
    if (args[0].is_angka()) {
        auto* o = vm.heap().alokasi<rt::TanggalObj>();
        o->h.kind = OK::Tanggal;
        o->milidetik = args[0].as_number();
        return Value::obyek(o);
    }
    return vm.buat_tanggal_dari_teks(arg_teks_bersih(args[0]));
}

/// `Tanggal.dari(tahun, bulan, hari, jam, menit, detik)` -- dari komponen
/// kalender UTC. Bulan 1..12; hari di luar rentang bulan di-rollover.
Value native_tanggal_dari(VM& vm, Value, std::vector<Value>& args) {
    if (args.size() < 3 || !args[0].is_angka() || !args[1].is_angka() || !args[2].is_angka()) {
        return Value::mboh();
    }
    const int jam = args.size() > 3 && args[3].is_angka() ? static_cast<int>(args[3].as_number()) : 0;
    const int menit = args.size() > 4 && args[4].is_angka() ? static_cast<int>(args[4].as_number()) : 0;
    const double detik = args.size() > 5 && args[5].is_angka() ? args[5].as_number() : 0.0;
    const rt::Tanggal t = rt::Tanggal::dari(static_cast<std::int64_t>(args[0].as_number()),
                                             static_cast<int>(args[1].as_number()),
                                             static_cast<int>(args[2].as_number()), jam, menit, detik);
    auto* o = vm.heap().alokasi<rt::TanggalObj>();
    o->h.kind = OK::Tanggal;
    o->milidetik = t.milidetik();
    return Value::obyek(o);
}

/// `Tanggal.ms(n)` -- dari milidetik sejak epoch.
Value native_tanggal_ms(VM& vm, Value, std::vector<Value>& args) {
    if (args.empty() || !args[0].is_angka()) return Value::mboh();
    auto* o = vm.heap().alokasi<rt::TanggalObj>();
    o->h.kind = OK::Tanggal;
    o->milidetik = args[0].as_number();
    return Value::obyek(o);
}

/// `Tanggal.sekarang()` -- waktu sekarang.
Value native_tanggal_sekarang(VM& vm, Value, std::vector<Value>&) {
    const rt::Tanggal t = rt::Tanggal::sekarang();
    auto* o = vm.heap().alokasi<rt::TanggalObj>();
    o->h.kind = OK::Tanggal;
    o->milidetik = t.milidetik();
    return Value::obyek(o);
}

/// Bangun objek `Tanggal` dari milidetik (helper internal).
Value tanggal_dari_ms(VM& vm, double ms) {
    auto* o = vm.heap().alokasi<rt::TanggalObj>();
    o->h.kind = OK::Tanggal;
    o->milidetik = ms;
    return Value::obyek(o);
}

/// `this_val` sebagai `rt::Tanggal`, atau epoch kalau bukan objek tanggal.
rt::Tanggal tang(Value this_val) {
    const rt::TanggalObj* o = obj_tanggal(this_val);
    return o != nullptr ? rt::Tanggal(o->milidetik) : rt::Tanggal(0.0);
}

Value native_tanggal_ke_teks(VM& vm, Value this_val, std::vector<Value>&) {
    return native_teks_objek(vm, tang(this_val).ke_teks());
}
Value native_tanggal_ke_tanggal(VM& vm, Value this_val, std::vector<Value>&) {
    return native_teks_objek(vm, tang(this_val).ke_tanggal());
}
Value native_tanggal_ke_waktu(VM& vm, Value this_val, std::vector<Value>&) {
    return native_teks_objek(vm, tang(this_val).ke_waktu());
}
Value native_tanggal_tahun(VM&, Value this_val, std::vector<Value>&) {
    return Value::number(static_cast<double>(tang(this_val).tahun()));
}
Value native_tanggal_bulan(VM&, Value this_val, std::vector<Value>&) {
    return Value::number(static_cast<double>(tang(this_val).bulan()));
}
Value native_tanggal_hari(VM&, Value this_val, std::vector<Value>&) {
    return Value::number(static_cast<double>(tang(this_val).hari()));
}
Value native_tanggal_jam(VM&, Value this_val, std::vector<Value>&) {
    return Value::number(static_cast<double>(tang(this_val).jam()));
}
Value native_tanggal_menit(VM&, Value this_val, std::vector<Value>&) {
    return Value::number(static_cast<double>(tang(this_val).menit()));
}
Value native_tanggal_detik(VM&, Value this_val, std::vector<Value>&) {
    return Value::number(static_cast<double>(tang(this_val).detik_dalam_menit()));
}
Value native_tanggal_milidetik(VM&, Value this_val, std::vector<Value>&) {
    return Value::number(static_cast<double>(tang(this_val).milidetik_dalam_detik()));
}
Value native_tanggal_hari_dalam_minggu(VM&, Value this_val, std::vector<Value>&) {
    return Value::number(static_cast<double>(tang(this_val).hari_dalam_minggu()));
}
Value native_tanggal_nama_hari(VM& vm, Value this_val, std::vector<Value>&) {
    return native_teks_objek(vm, std::string(rt::Tanggal::nama_hari(tang(this_val).hari_dalam_minggu())));
}
Value native_tanggal_nama_bulan(VM& vm, Value this_val, std::vector<Value>&) {
    return native_teks_objek(vm, std::string(rt::Tanggal::nama_bulan(tang(this_val).bulan())));
}
Value native_tanggal_ms_inst(VM&, Value this_val, std::vector<Value>&) {
    return Value::number(tang(this_val).milidetik());
}
Value native_tanggal_tambah_ms(VM& vm, Value this_val, std::vector<Value>& args) {
    if (args.empty() || !args[0].is_angka()) return Value::mboh();
    return tanggal_dari_ms(vm, tang(this_val).milidetik() + args[0].as_number());
}
Value native_tanggal_tambah_hari(VM& vm, Value this_val, std::vector<Value>& args) {
    if (args.empty() || !args[0].is_angka()) return Value::mboh();
    return tanggal_dari_ms(vm, tang(this_val).tambah_hari(args[0].as_number()).milidetik());
}
/// `t.selisih(lain)` -- selisih dalam milidetik (`lain - t`).
Value native_tanggal_selisih(VM&, Value this_val, std::vector<Value>& args) {
    const rt::TanggalObj* o = args.empty() ? nullptr : obj_tanggal(args[0]);
    if (o == nullptr) return Value::mboh();
    return Value::number(o->milidetik - tang(this_val).milidetik());
}
Value native_tanggal_sebelum(VM&, Value this_val, std::vector<Value>& args) {
    const rt::TanggalObj* o = args.empty() ? nullptr : obj_tanggal(args[0]);
    return Value::boolean(o != nullptr && tang(this_val).sebelum(rt::Tanggal(o->milidetik)));
}
Value native_tanggal_sesudah(VM&, Value this_val, std::vector<Value>& args) {
    const rt::TanggalObj* o = args.empty() ? nullptr : obj_tanggal(args[0]);
    return Value::boolean(o != nullptr && tang(this_val).sesudah(rt::Tanggal(o->milidetik)));
}
Value native_tanggal_sama(VM&, Value this_val, std::vector<Value>& args) {
    const rt::TanggalObj* o = args.empty() ? nullptr : obj_tanggal(args[0]);
    return Value::boolean(o != nullptr && tang(this_val).sama_dengan(rt::Tanggal(o->milidetik)));
}

// ===========================================================================
// Pasang
// ===========================================================================

namespace {
void pasang_prototype_metode_impl(VM& vm, std::uint8_t jenis_objek, std::string_view nama, std::size_t n_param,
                                  rt::NativeFn fn) {
    auto* n = vm.heap().alokasi<NativeFnObj>();
    n->h.kind = OK::Native;
    n->fn = fn;
    n->nama = nama;
    n->jumlah_param = static_cast<std::uint8_t>(n_param);
    metode_bawaan(vm)[std::to_string(static_cast<int>(jenis_objek)) + ":" + std::string(nama)] = Value::obyek(n);
}

void pasang_objek_metode_impl(VM& vm, ObyekObj* o, std::string_view nama, std::size_t n_param, rt::NativeFn fn) {
    auto* n = vm.heap().alokasi<NativeFnObj>();
    n->h.kind = OK::Native;
    n->fn = fn;
    n->nama = nama;
    n->jumlah_param = static_cast<std::uint8_t>(n_param);
    o->define(vm.heap(), native_teks_objek(vm, std::string(nama)), Value::obyek(n), rt::AttrDefault);
}
}  // namespace

// Wrapper publik (dipakai `stdlib_peta.cpp`).
void daftarkan(VM& vm, std::string_view nama, std::size_t n_param, bool variadic, rt::NativeFn fn) {
    daftarkan_impl(vm, nama, n_param, variadic, fn);
}
void daftarkan_nilai(VM& vm, std::string_view nama, Value v) { daftarkan_nilai_impl(vm, nama, v); }
ObyekObj* buat_namespace(VM& vm, std::string_view nama) { return buat_namespace_impl(vm, nama); }
void pasang_prototype_metode(VM& vm, std::uint8_t jenis_objek, std::string_view nama, std::size_t n_param,
                             rt::NativeFn fn) {
    pasang_prototype_metode_impl(vm, jenis_objek, nama, n_param, fn);
}
void pasang_objek_metode(VM& vm, ObyekObj* o, std::string_view nama, std::size_t n_param, rt::NativeFn fn) {
    pasang_objek_metode_impl(vm, o, nama, n_param, fn);
}

void pasang_semua(VM& vm) {
    // Tabel global & method bawaan adalah root GC: tanpa ini, koleksi pertama
    // akan membebaskan seluruh objek native.
    vm.registrasikan_root_visitor([&vm](gc::RootVisitor& rv) {
        for (const auto& kv : global(vm).tabel) rv.rooted(kv.second);
        for (const auto& kv : global(vm).metode) rv.rooted(kv.second);
    });

    // --- global fungsi ---
    daftarkan(vm, "tulis", 0, true, native_tulis);
    daftarkan(vm, "tulis_nol", 0, true, native_tulis_nol);
    daftarkan(vm, "tulis_kleru", 0, true, native_tulis_kleru);
    daftarkan(vm, "Teks", 1, false, native_teks);
    daftarkan(vm, "Angka", 1, false, native_angka);
    daftarkan(vm, "Boole", 1, false, native_boole);
    daftarkan(vm, "pratela", 2, false, native_pratela);
    daftarkan(vm, "jenis", 1, false, native_jenis);
    daftarkan(vm, "metu", 1, false, native_metu);

    // --- konstanta global ---
    daftarkan_nilai(vm, "DuduAngka", Value::number(std::nan("")));
    daftarkan_nilai(vm, "Tak_Wates", Value::number(HUGE_VAL));
    daftarkan_nilai(vm, "globalIki", Value::obyek(vm.buat_obyek()));

    // --- konsol ---
    ObyekObj* konsol = buat_namespace(vm, "konsol");
    pasang_objek_metode(vm, konsol, "cathet", 0, native_tulis);
    pasang_objek_metode(vm, konsol, "log", 0, native_tulis);
    pasang_objek_metode(vm, konsol, "informasi", 0, native_tulis);
    pasang_objek_metode(vm, konsol, "peringatan", 0, native_tulis_kleru);
    pasang_objek_metode(vm, konsol, "kleru", 0, native_tulis_kleru);
    pasang_objek_metode(vm, konsol, "debug", 0, native_tulis);
    daftarkan_nilai(vm, "konsol", Value::obyek(konsol));

    // --- Matematika ---
    ObyekObj* math = buat_namespace(vm, "Matematika");
    pasang_objek_metode(vm, math, "mutlak", 1, math_mutlak);
    pasang_objek_metode(vm, math, "lantai", 1, math_lantai);
    pasang_objek_metode(vm, math, "bunder", 1, math_bunder);
    pasang_objek_metode(vm, math, "akar", 1, math_akar);
    pasang_objek_metode(vm, math, "sin", 1, math_sin);
    pasang_objek_metode(vm, math, "cos", 1, math_cos);
    pasang_objek_metode(vm, math, "tan", 1, math_tan);
    pasang_objek_metode(vm, math, "log", 1, math_log);
    pasang_objek_metode(vm, math, "log2", 1, math_log2);
    pasang_objek_metode(vm, math, "log10", 1, math_log10);
    pasang_objek_metode(vm, math, "exp", 1, math_exp);
    pasang_objek_metode(vm, math, "tanda", 1, math_tanda);
    pasang_objek_metode(vm, math, "trunc", 1, math_trunc);
    pasang_objek_metode(vm, math, "pangkat", 2, m_pangkat);
    pasang_objek_metode(vm, math, "paling_kecil", 2, mmin);
    pasang_objek_metode(vm, math, "paling_besar", 2, mmaks);
    pasang_objek_metode(vm, math, "faktorial", 1, math_factorial);
    math->define(vm.heap(), native_teks_objek(vm, "PI"), Value::number(3.14159265358979323846), rt::AttrDefault);
    math->define(vm.heap(), native_teks_objek(vm, "E"), Value::number(2.71828182845904523536), rt::AttrDefault);
    math->define(vm.heap(), native_teks_objek(vm, "LN2"), Value::number(0.693147180559945309), rt::AttrDefault);
    math->define(vm.heap(), native_teks_objek(vm, "LN10"), Value::number(2.302585092994045684), rt::AttrDefault);
    math->define(vm.heap(), native_teks_objek(vm, "SQRT2"), Value::number(1.414213562373095048), rt::AttrDefault);
    daftarkan_nilai(vm, "Matematika", Value::obyek(math));

    // --- JSON (minimal) ---
    ObyekObj* json = buat_namespace(vm, "JSON");
    pasang_objek_metode(vm, json, "gawe_teks", 1, json_teks);
    daftarkan_nilai(vm, "JSON", Value::obyek(json));



    // --- StdAksara ---
    ObyekObj* aksara = buat_namespace(vm, "StdAksara");
    pasang_objek_metode(vm, aksara, "angka_jawa", 1, native_aksara_angka_jawa);
    pasang_objek_metode(vm, aksara, "angka_arab", 1, native_aksara_angka_arab);
    daftarkan_nilai(vm, "StdAksara", Value::obyek(aksara));

    // --- Wektu (timer) ---
    ObyekObj* wektu = buat_namespace(vm, "Wektu");
    pasang_objek_metode(vm, wektu, "tundha", 1, native_wektu_tundha);
    pasang_objek_metode(vm, wektu, "teka", 0, native_wektu_teka);
    daftarkan_nilai(vm, "Wektu", Value::obyek(wektu));

    // --- method Janji ---
    method_janji(vm)[std::string("then")] = native_janji_then;
    method_janji(vm)[std::string("tangkep")] = native_janji_tangkep;

    // --- Peta, Himpunan, Janji.all/race/selesai/tolak (lihat stdlib_peta.cpp) ---
    pasang_peta(vm);

    // --- prototype Dhaptar & Teks (method bawaan) ---
    const std::uint8_t ARR = static_cast<std::uint8_t>(OK::Array);
    const std::uint8_t TEK = static_cast<std::uint8_t>(OK::Teks);
    pasang_prototype_metode(vm, ARR, "tambah", 1, native_dhaptar_tambah);
    pasang_prototype_metode(vm, ARR, "copot", 0, native_dhaptar_pop);
    pasang_prototype_metode(vm, ARR, "gabung", 1, native_dhaptar_gabung);
    pasang_prototype_metode(vm, ARR, "peta", 1, native_dhaptar_peta);
    pasang_prototype_metode(vm, ARR, "saring", 1, native_dhaptar_saring);
    pasang_prototype_metode(vm, ARR, "saben", 1, native_dhaptar_saben);
    pasang_prototype_metode(vm, ARR, "kurangi", 1, native_dhaptar_kurangi);
    pasang_prototype_metode(vm, ARR, "nilai", 0, native_dhaptar_da);
    pasang_prototype_metode(vm, TEK, "dawa", 0, native_teks_dawa);
    pasang_prototype_metode(vm, TEK, "huruf_gedhe", 0, native_teks_huruf_gedhe);
    pasang_prototype_metode(vm, TEK, "huruf_kecil", 0, native_teks_huruf_kecil);
    pasang_prototype_metode(vm, TEK, "pangkas", 2, native_teks_pangkas);
    pasang_prototype_metode(vm, TEK, "ganti", 2, native_teks_ganti);
    pasang_prototype_metode(vm, TEK, "pecah", 1, native_teks_pecah);
    pasang_prototype_metode(vm, TEK, "termasuk", 1, native_teks_termasuk);
    pasang_prototype_metode(vm, TEK, "mulainya_dengan", 1, native_teks_mulainya_dengan);

    // --- prototype Regex ---
    const std::uint8_t REG = static_cast<std::uint8_t>(OK::Regex);
    pasang_prototype_metode(vm, REG, "cocog", 1, native_regex_cocog);
    pasang_prototype_metode(vm, REG, "kabeh", 1, native_regex_kabeh);
    pasang_prototype_metode(vm, REG, "ganti", 2, native_regex_ganti);
    pasang_prototype_metode(vm, REG, "pecah", 1, native_regex_pecah);
    pasang_prototype_metode(vm, REG, "nilai", 1, native_regex_nilai);
    pasang_prototype_metode(vm, REG, "grup", 2, native_regex_grup);
    pasang_prototype_metode(vm, REG, "pola", 0, native_regex_pola);
    pasang_prototype_metode(vm, REG, "flag", 0, native_regex_flag);

    // --- `Tanggal`: konstruktor global + method statis ---
    auto* ctor = vm.heap().alokasi<NativeFnObj>();
    ctor->h.kind = OK::Native;
    ctor->fn = native_tanggal_buat;
    ctor->nama = "Tanggal";
    ctor->jumlah_param = 0;
    ctor->variadic = true;
    sifat(vm, ctor, "nama", native_teks_objek(vm, "Tanggal"));
    sifat(vm, ctor, "dari", native_baru(vm, "dari", 6, native_tanggal_dari));
    sifat(vm, ctor, "ms", native_baru(vm, "ms", 1, native_tanggal_ms));
    sifat(vm, ctor, "sekarang", native_baru(vm, "sekarang", 0, native_tanggal_sekarang));
    daftarkan_nilai(vm, "Tanggal", Value::obyek(ctor));

    // --- prototype Tanggal ---
    const std::uint8_t TGL = static_cast<std::uint8_t>(OK::Tanggal);
    pasang_prototype_metode(vm, TGL, "ke_teks", 0, native_tanggal_ke_teks);
    pasang_prototype_metode(vm, TGL, "ke_tanggal", 0, native_tanggal_ke_tanggal);
    pasang_prototype_metode(vm, TGL, "ke_waktu", 0, native_tanggal_ke_waktu);
    pasang_prototype_metode(vm, TGL, "tahun", 0, native_tanggal_tahun);
    pasang_prototype_metode(vm, TGL, "bulan", 0, native_tanggal_bulan);
    pasang_prototype_metode(vm, TGL, "hari", 0, native_tanggal_hari);
    pasang_prototype_metode(vm, TGL, "jam", 0, native_tanggal_jam);
    pasang_prototype_metode(vm, TGL, "menit", 0, native_tanggal_menit);
    pasang_prototype_metode(vm, TGL, "detik", 0, native_tanggal_detik);
    pasang_prototype_metode(vm, TGL, "milidetik", 0, native_tanggal_milidetik);
    pasang_prototype_metode(vm, TGL, "hari_dalam_minggu", 0, native_tanggal_hari_dalam_minggu);
    pasang_prototype_metode(vm, TGL, "nama_hari", 0, native_tanggal_nama_hari);
    pasang_prototype_metode(vm, TGL, "nama_bulan", 0, native_tanggal_nama_bulan);
    pasang_prototype_metode(vm, TGL, "ms", 0, native_tanggal_ms_inst);
    pasang_prototype_metode(vm, TGL, "tambah_ms", 1, native_tanggal_tambah_ms);
    pasang_prototype_metode(vm, TGL, "tambah_hari", 1, native_tanggal_tambah_hari);
    pasang_prototype_metode(vm, TGL, "selisih", 1, native_tanggal_selisih);
    pasang_prototype_metode(vm, TGL, "sebelum", 1, native_tanggal_sebelum);
    pasang_prototype_metode(vm, TGL, "sesudah", 1, native_tanggal_sesudah);
    pasang_prototype_metode(vm, TGL, "sama_dengan", 1, native_tanggal_sama);
}

}  // namespace jawa::vm::stdlib
