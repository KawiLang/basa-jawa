// `jawa tes` — implementasi kerangka uji.
//
// Setiap berkas uji dieksekusi di VM sendiri (satu VM per berkas) supaya global
// yang dideklarasikan program uji tidak bocor ke berkas lain. Assertion dipasang
// sebagai native function yang mencatat hasilnya ke `KonteksUji` milik CLI
// (dicapai lewat `VM::konteks`).
//
// Assertion TIDAK menghentikan program: `jawa tes` memasang `tulis` ke buffer,
// sehingga program uji bisa mencetak untuk debug tanpa mengotorkan keluaran
// runner, dan seluruh assertion di sebuah berkas dilaporkan sekaligus.
#include "cli/tes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "rt/object.h"
#include "rt/string.h"
#include "stdlib/stdlib.h"

namespace jawa::cli {
namespace {

namespace fs = std::filesystem;

using rt::ArrayObj;
using rt::KleruObj;
using rt::NativeFn;
using rt::NativeFnObj;
using rt::Obj;
using rt::ObyekObj;
using rt::OK;
using rt::Properti;
using rt::TeksObj;
using rt::Value;
using support::SourcePos;
using vm::VM;

// ---------------------------------------------------------------------------
// Konteks uji
// ---------------------------------------------------------------------------

/// Dikirim lewat `VM::konteks` supaya native function bisa mencatat kegagalan
/// tanpa perlu tabel global bersama.
struct KonteksUji {
    bool warna = false;
    std::size_t assertion = 0;
    std::size_t gagal = 0;
    /// Assertion yang gagal, dicetak setelah program selesai.
    std::vector<std::string> laporan;
};

KonteksUji* konteks(VM& vm) { return static_cast<KonteksUji*>(vm.konteks); }

void tulis_stderr(std::string_view s) {
    std::fwrite(s.data(), 1, s.size(), stderr);
    std::fputc('\n', stderr);
}

std::string merah(const std::string& s, bool warna) { return warna ? "\x1b[31m" + s + "\x1b[0m" : s; }
std::string hijau(const std::string& s, bool warna) { return warna ? "\x1b[32m" + s + "\x1b[0m" : s; }
std::string abu(const std::string& s, bool warna) { return warna ? "\x1b[90m" + s + "\x1b[0m" : s; }

/// `"12:5"` — posisi sumber sebagai awalan laporan.
///
/// Angka dikonversi lebih dulu ke `std::string`, lalu dirangkai. Menyusun
/// `"(" + std::to_string(a) + ":" + std::to_string(b) + ")"` dalam satu
/// ekspresi memicu false positive `-Wrestrict` pada GCC 12.
std::string awalan_posisi(VM& vm) {
    const SourcePos pos = vm.posisi_sumber();
    std::string baris = std::to_string(pos.baris);
    std::string kolom = std::to_string(pos.kolom);
    std::string keluar = "(";
    keluar += baris;
    keluar += ":";
    keluar += kolom;
    keluar += ")";
    return keluar;
}

void catat_gagal(VM& vm, const std::string& pesan) {
    KonteksUji* k = konteks(vm);
    if (k == nullptr) return;
    ++k->gagal;
    // Prefix posisi sumber: tanpa ini, assertion yang gagal di berkas 200 baris
    // hanya bisa dicari secara manual. `awalan_posisi` dipisah dari `pesan`
    // supaya tidak ada penyusunan `std::string`+rantai yang memicu peringatan
    // `-Wrestrict` (false positive) pada GCC 12.
    std::string baris = awalan_posisi(vm);
    baris += " ";
    baris += pesan;
    k->laporan.push_back(std::move(baris));
}

void catat_lolos(VM& vm) {
    if (KonteksUji* k = konteks(vm); k != nullptr) ++k->assertion;
}

// ---------------------------------------------------------------------------
// Perbandingan mendalam
// ---------------------------------------------------------------------------

/// Bandingkan dua nilai secara mendalam untuk `pratelas`.
///
/// `rt::nilai_sama()` hanya membandingkan bit untuk objek non-teks, jadi dua
/// `TeksObj` berbeda alamat dianggap berbeda. Assertion membandingkan ISI, jadi
/// teks, dhaptar, dan obyek dibandingkan rekursif.
bool sama_mendalam(VM& vm, const Value& a, const Value& b, int kedalaman);

/// Kunci-teks dari sebuah `Value` Teks, atau string kosong.
std::string_view sebagai_teks(const Value& v) {
    if (!v.is_obyek() || v.pointer() == nullptr) return {};
    if (static_cast<const Obj*>(v.pointer())->h.kind != OK::Teks) return {};
    return static_cast<const TeksObj*>(v.pointer())->str();
}

bool sama_dhaptar(VM& vm, const ArrayObj* a, const ArrayObj* b, int kedalaman) {
    if (a->panjang != b->panjang) return false;
    for (std::size_t i = 0; i < a->panjang; ++i) {
        if (!sama_mendalam(vm, a->elemen[i], b->elemen[i], kedalaman + 1)) return false;
    }
    return true;
}

/// Kunci/value yang bisa dibaca dari `ObyekObj`, baik slot inline maupun dict.
std::vector<std::pair<std::string, Value>> baca_properti(const ObyekObj* o) {
    std::vector<std::pair<std::string, Value>> keluar;
    if (o->slot != nullptr && o->shape != nullptr) {
        for (std::size_t i = 0; i < o->shape->jumlah(); ++i) {
            const Properti& p = o->shape->at(i);
            if (p.index < 0) continue;
            const auto idx = static_cast<std::size_t>(p.index);
            if (idx >= o->jumlah_slot) continue;
            const std::string_view kunci = sebagai_teks(p.kunci);
            if (kunci.empty()) continue;  // kunci non-teks: abaikan
            keluar.emplace_back(std::string(kunci), o->slot[idx]);
        }
    } else {
        for (const auto& [kunci, nilai] : o->dict) {
            const std::string_view k = sebagai_teks(kunci);
            if (k.empty()) continue;
            keluar.emplace_back(std::string(k), nilai);
        }
    }
    std::sort(keluar.begin(), keluar.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return keluar;
}

bool sama_obyek(VM& vm, const ObyekObj* a, const ObyekObj* b, int kedalaman) {
    const auto pa = baca_properti(a);
    const auto pb = baca_properti(b);
    if (pa.size() != pb.size()) return false;
    for (std::size_t i = 0; i < pa.size(); ++i) {
        if (pa[i].first != pb[i].first) return false;
        if (!sama_mendalam(vm, pa[i].second, pb[i].second, kedalaman + 1)) return false;
    }
    return true;
}

bool sama_mendalam(VM& vm, const Value& a, const Value& b, int kedalaman) {
    if (kedalaman > 32) return true;  // pengaman terhadap rekursi tak berujung
    // Nilai dasar lebih dulu: `NaN` tidak pernah sama dengan dirinya sendiri
    // lewat `==`, tapi `pratelas(DuduAngka, DuduAngka)` harus lulus.
    if (a.is_angka() && b.is_angka()) {
        const double x = a.as_number();
        const double y = b.as_number();
        if (std::isnan(x) && std::isnan(y)) return true;
        return x == y;
    }
    if (a.is_boole() && b.is_boole()) return a.bool_value() == b.bool_value();
    if (a.is_kosong() && b.is_kosong()) return true;
    if (a.is_mboh() && b.is_mboh()) return true;
    if (!a.is_obyek() || !b.is_obyek()) return rt::nilai_sama(a, b);
    const Obj* oa = static_cast<const Obj*>(a.pointer());
    const Obj* ob = static_cast<const Obj*>(b.pointer());
    if (oa == nullptr || ob == nullptr) return oa == ob;
    if (oa->h.kind != ob->h.kind) return false;
    switch (oa->h.kind) {
        case OK::Teks:
            return sebagai_teks(a) == sebagai_teks(b);
        case OK::Array:
            return sama_dhaptar(vm, static_cast<const ArrayObj*>(oa), static_cast<const ArrayObj*>(ob), kedalaman);
        case OK::Obyek:
            return sama_obyek(vm, static_cast<const ObyekObj*>(oa), static_cast<const ObyekObj*>(ob), kedalaman);
        case OK::Kleru: {
            // Dua galat dianggap sama bila nama & pesannya sama.
            const auto* ka = static_cast<const KleruObj*>(oa);
            const auto* kb = static_cast<const KleruObj*>(ob);
            return ka->jeneng == kb->jeneng && ka->pesan == kb->pesan;
        }
        default:
            return rt::nilai_sama(a, b);
    }
}

/// Format nilai untuk pesan kegagalan (dibatasi agar keluaran tetap ringkas).
std::string teks_nilai(VM& vm, const Value& v) {
    std::string s = rt::nilai_ke_teks_inspect(vm, v, 0);
    if (s.size() > 240) s = s.substr(0, 240) + "…";
    return s;
}

/// Pesan opsional (argumen terakhir pada assertion yang mendukungnya).
std::string pesan_opsional(const std::vector<Value>& args, std::size_t indeks) {
    if (args.size() <= indeks) return {};
    return std::string(sebagai_teks(args[indeks]));
}

void tambah_pesan(std::string& pesan, const std::string& tambahan) {
    if (!tambahan.empty()) pesan += ": " + tambahan;
}

/// Sisipkan "label: <nilai>" sebagai baris baru pada laporan.
///
/// Baris baru disisipkan sebagai operasi terpisah, bukan lewat
/// `pesan += "\n  label: " + nilai`: GCC 12 melaporkan `-Wrestrict` (false
/// positive) pada susunan `std::string` yang merantai temporary ke literal.
void tambah_baris(std::string& pesan, const char* label, const std::string& nilai) {
    pesan += "\n";
    pesan += label;
    pesan += ": ";
    pesan += nilai;
}

// ---------------------------------------------------------------------------
// Assertion
// ---------------------------------------------------------------------------

Value native_pratelas(VM& vm, Value /*this*/, std::vector<Value>& args) {
    catat_lolos(vm);
    if (args.size() < 2) {
        catat_gagal(vm, "pratelas() butuh minimal 2 argumen: (nilai, harapan)");
        return Value::mboh();
    }
    if (sama_mendalam(vm, args[0], args[1], 0)) return Value::mboh();
    std::string pesan = "pratelas gagal";
    tambah_pesan(pesan, pesan_opsional(args, 2));
    tambah_baris(pesan, "      entuk", teks_nilai(vm, args[0]));
    tambah_baris(pesan, "    harapan", teks_nilai(vm, args[1]));
    catat_gagal(vm, pesan);
    return Value::mboh();
}

Value native_wajib_bener(VM& vm, Value /*this*/, std::vector<Value>& args) {
    catat_lolos(vm);
    if (!args.empty() && rt::benar(args[0])) return Value::mboh();
    std::string pesan = args.empty() ? "wajib_bener() butuh 1 argumen" : "nilai kudu `bener`";
    if (!args.empty()) {
        tambah_pesan(pesan, pesan_opsional(args, 1));
        tambah_baris(pesan, "      entuk", teks_nilai(vm, args[0]));
    }
    catat_gagal(vm, pesan);
    return Value::mboh();
}

Value native_wajib_salah(VM& vm, Value /*this*/, std::vector<Value>& args) {
    catat_lolos(vm);
    if (!args.empty() && !rt::benar(args[0])) return Value::mboh();
    std::string pesan = args.empty() ? "wajib_salah() butuh 1 argumen" : "nilai kudu `salah`";
    if (!args.empty()) {
        tambah_pesan(pesan, pesan_opsional(args, 1));
        tambah_baris(pesan, "      entuk", teks_nilai(vm, args[0]));
    }
    catat_gagal(vm, pesan);
    return Value::mboh();
}

/// Jalankan `fn`, laporkan apakah ia melempar, lalu bersihkan kondisi galat
/// supaya assertion berikutnya tidak terbawa errornya.
bool panggil_dan_tangkap(VM& vm, const Value& fn) {
    vm.bersihkan_galat();
    std::vector<Value> kosong;
    vm.panggil(fn, Value::mboh(), kosong);
    const bool melempar = vm.ada_galat();
    if (melempar) vm.bersihkan_galat();
    return melempar;
}

Value native_wajib_lempar(VM& vm, Value /*this*/, std::vector<Value>& args) {
    catat_lolos(vm);
    if (args.empty()) {
        catat_gagal(vm, "wajib_lempar() butuh 1 argumen: (fungsi)");
        return Value::mboh();
    }
    if (panggil_dan_tangkap(vm, args[0])) return Value::mboh();
    std::string pesan = "fungsi kudu melempar";
    tambah_pesan(pesan, pesan_opsional(args, 1));
    catat_gagal(vm, pesan);
    return Value::mboh();
}

// ---------------------------------------------------------------------------
// Pemasangan bawaan
// ---------------------------------------------------------------------------

void pasang(VM& vm, std::string_view nama, std::size_t n_param, rt::NativeFn fn) {
    auto* n = vm.heap().alokasi<NativeFnObj>();
    n->h.kind = OK::Native;
    n->fn = fn;
    n->nama = vm.singsan(nama);
    n->jumlah_param = static_cast<std::uint8_t>(n_param);
    vm::stdlib::set_global(vm, nama, Value::obyek(n));
}

void pasang_bawaan_tes(VM& vm) {
    // Nama fungsi TIDAK boleh sama dengan kata kunci: `bener` & `salah` adalah
    // keyword, jadi penamaan memakai awalan `wajib_` (harus) agar tidak bentrok.
    pasang(vm, "pratelas", 2, native_pratelas);
    pasang(vm, "wajib_bener", 1, native_wajib_bener);
    pasang(vm, "wajib_salah", 1, native_wajib_salah);
    pasang(vm, "wajib_lempar", 1, native_wajib_lempar);
}

}  // namespace

// ---------------------------------------------------------------------------
// Kumpulan berkas
// ---------------------------------------------------------------------------

namespace {

bool akhiran_tes(const std::string& nama) {
    constexpr std::string_view kAkhiran = ".tes.jw";
    return nama.size() > kAkhiran.size() && nama.compare(nama.size() - kAkhiran.size(), kAkhiran.size(), kAkhiran) == 0;
}

/// `langsung` = berkas di root yang dipanggil pengguna. Berkas `.jw` biasa di
/// root ikut diambil, tapi tidak di subdirektori — supaya `jawa tes tests/`
/// tidak menjalankan berkas pendukung yang kebetulan ada di sana.
void kumpulkan_rekursif(const fs::path& dir, bool langsung, std::vector<std::string>& keluar) {
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    if (ec) return;
    std::vector<fs::path> anak;
    for (const fs::directory_entry& e : it) anak.push_back(e.path());
    std::sort(anak.begin(), anak.end());  // keluaran deterministik lintas OS
    for (const fs::path& p : anak) {
        if (fs::is_directory(p, ec)) {
            kumpulkan_rekursif(p, false, keluar);
            continue;
        }
        if (p.extension() != ".jw") continue;
        if (akhiran_tes(p.filename().string()) || langsung) keluar.push_back(p.string());
    }
}

}  // namespace

std::vector<std::string> kumpulkan_berkas_tes(const std::string& akar) {
    std::vector<std::string> keluar;
    std::error_code ec;
    if (!fs::exists(akar, ec)) return keluar;
    if (fs::is_regular_file(akar, ec)) return {akar};
    kumpulkan_rekursif(fs::path(akar), true, keluar);
    return keluar;
}

// ---------------------------------------------------------------------------
// Menjalankan satu berkas
// ---------------------------------------------------------------------------

bool jalankan_berkas_tes(const std::string& path, const vm::VMOptions& opsi, bool warna, RingkasanTes& ringkas) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        tulis_stderr(merah("KleruCLI [C003] Gagal maca berkas uji: " + path, warna));
        ++ringkas.berkas_gagal;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string kode = ss.str();

    // `tulis` diarahkan ke buffer: keluaran program uji tidak boleh tercampur
    // dengan laporan assertion. Program uji tetap bisa mencetaknya sendiri.
    vm::VMOptions o = opsi;
    std::string keluaran;
    o.keluaran = &keluaran;

    vm::VM mesin(o);
    KonteksUji k;
    k.warna = warna;
    mesin.konteks = &k;
    pasang_bawaan_tes(mesin);

    std::printf("%s %s\n", abu("uji", warna).c_str(), path.c_str());
    const std::string dir = fs::path(path).parent_path().string();
    const vm::Status s = mesin.jalankan_sumber(kode, path, dir.empty() ? "." : dir);

    ++ringkas.berkas;
    ringkas.assertion += k.assertion;
    ringkas.gagal += k.gagal;

    bool ok = true;
    if (s != vm::Status::Selesai) {
        std::printf("  %s berkas gagal dijalankan\n", merah("x", warna).c_str());
        std::fflush(stdout);
        const std::string jejak = mesin.jejak_stack();
        std::fputs(jejak.c_str(), stderr);
        std::fputc('\n', stderr);
        ++ringkas.berkas_gagal;
        ok = false;
    }
    for (const std::string& baris : k.laporan) {
        std::printf("  %s %s\n", merah("x", warna).c_str(), baris.c_str());
    }
    if (ok && k.laporan.empty()) {
        std::printf("  %s %zu assertion lulus\n", hijau("v", warna).c_str(), k.assertion);
    }
    std::fflush(stdout);
    return ok && k.laporan.empty();
}

}  // namespace jawa::cli
