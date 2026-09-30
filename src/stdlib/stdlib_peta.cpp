// Pustaka standar: `Peta`, `Himpunan`, dan method Janji gabungan.
//
// `PetaObj` & `HimpunanObj` sudah ada sebagai kelas runtime (open addressing di
// `src/rt/object.cpp`) tapi tidak pernah bisa dibuat dari kode Basa Jawa:
// tidak ada constructor global, dan `VM::ambil_properti` tidak punya cabang
// untuk keduanya -- jadi `jenis(peta)` pun tidak pernah mengembalikan "peta".
//
// Berkas ini sengaja terpisah dari `stdlib.cpp` yang sudah panjang; tidak ada
// header baru karena semua fungsi di sini dipasang lewat
// `pasang_prototype_metode`/`daftarkan` yang menerima pointer fungsi biasa.
#include <cstddef>
#include <vector>

#include "rt/object.h"
#include "rt/string.h"
#include "stdlib/stdlib.h"
#include "vm/vm.h"

namespace jawa::vm::stdlib {
namespace {

using jawa::vm::VM;
using rt::ArrayObj;
using rt::OK;
using rt::HimpunanObj;
using rt::JanjiObj;
using rt::PetaObj;
using rt::Value;

/// `PetaObj` pada `v`, atau `nullptr`.
PetaObj* peta_dari(Value v) {
    if (!v.is_obyek() || v.pointer() == nullptr) return nullptr;
    auto* o = static_cast<rt::Obj*>(v.mutable_pointer());
    return (o != nullptr && o->h.kind == OK::Peta) ? static_cast<PetaObj*>(o) : nullptr;
}

/// `HimpunanObj` pada `v`, atau `nullptr`.
HimpunanObj* himpunan_dari(Value v) {
    if (!v.is_obyek() || v.pointer() == nullptr) return nullptr;
    auto* o = static_cast<rt::Obj*>(v.mutable_pointer());
    return (o != nullptr && o->h.kind == OK::Himpunan) ? static_cast<HimpunanObj*>(o) : nullptr;
}

/// `JanjiObj` pada `v`, atau `nullptr`.
JanjiObj* janji_dari_peta(Value v) {
    if (!v.is_obyek() || v.pointer() == nullptr) return nullptr;
    auto* o = static_cast<rt::Obj*>(v.mutable_pointer());
    return (o != nullptr && o->h.kind == OK::Janji) ? static_cast<JanjiObj*>(o) : nullptr;
}

/// Argumen ke-`i`, atau `mboh`.
Value arg(std::vector<Value>& a, std::size_t i) { return i < a.size() ? a[i] : Value::mboh(); }

/// Dhaptar kosong berisi `n` elemen.
ArrayObj* kosong(VM& vm, std::size_t n) {
    ArrayObj* a = vm.buat_dhaptar(n);
    a->panjang = n;
    for (std::size_t i = 0; i < n; ++i) a->elemen[i] = Value::mboh();
    return a;
}

/// Salin entri aktif `p` ke `keluar` (mengikut urutan sisip).
std::size_t isi_aktif(const PetaObj& p, ArrayObj* keluar) {
    std::size_t n = 0;
    for (const auto& e : p.entri) {
        if (e.dihapus) continue;
        if (keluar != nullptr && n < keluar->panjang) keluar->elemen[n] = e.kunci;
        ++n;
    }
    return n;
}

// ===========================================================================
// Peta
// ===========================================================================

/// `Peta()` atau `Peta(peta_lain)` / `Peta({a: 1})`.
Value peta_baru(VM& vm, Value /*this*/, std::vector<Value>& args) {
    PetaObj* p = vm.buat_peta();
    if (args.empty()) return Value::obyek(p);
    if (PetaObj* asal = peta_dari(args[0])) {
        for (const auto& e : asal->entri) {
            if (!e.dihapus) p->pasang(e.kunci, e.nilai);
        }
    } else if (rt::Obj* o = args[0].is_obyek() ? static_cast<rt::Obj*>(args[0].mutable_pointer()) : nullptr;
               o != nullptr && o->h.kind == OK::Obyek) {
        auto* ob = static_cast<rt::ObyekObj*>(o);
        ob->each_properti([&](const Value& k, const Value& v) { p->pasang(k, v); });
    }
    return Value::obyek(p);
}

Value peta_get(VM&, Value this_val, std::vector<Value>& args) {
    PetaObj* p = peta_dari(this_val);
    if (p == nullptr) return Value::mboh();
    if (PetaObj::Entri* e = p->cari(arg(args, 0))) return e->nilai;
    return args.size() >= 2 ? args[1] : Value::mboh();
}

Value peta_set(VM&, Value this_val, std::vector<Value>& args) {
    PetaObj* p = peta_dari(this_val);
    if (p == nullptr) return this_val;
    p->pasang(arg(args, 0), arg(args, 1));
    return this_val;  // bisa dirantai: `peta.set("a", 1).set("b", 2)`
}

Value peta_hapus(VM&, Value this_val, std::vector<Value>& args) {
    PetaObj* p = peta_dari(this_val);
    return Value::boolean(p != nullptr && p->hapus(arg(args, 0)));
}

Value peta_ada(VM&, Value this_val, std::vector<Value>& args) {
    PetaObj* p = peta_dari(this_val);
    return Value::boolean(p != nullptr && p->cari(arg(args, 0)) != nullptr);
}

Value peta_kosong(VM&, Value this_val, std::vector<Value>&) {
    PetaObj* p = peta_dari(this_val);
    return Value::boolean(p == nullptr || p->jumlah_aktif == 0);
}

Value peta_bersih(VM&, Value this_val, std::vector<Value>&) {
    PetaObj* p = peta_dari(this_val);
    if (p == nullptr) return Value::mboh();
    p->entri.clear();
    p->tabel.clear();
    p->jumlah_aktif = 0;
    return Value::mboh();
}

Value peta_kunci(VM& vm, Value this_val, std::vector<Value>&) {
    PetaObj* p = peta_dari(this_val);
    ArrayObj* a = kosong(vm, p == nullptr ? 0 : p->jumlah_aktif);
    if (p == nullptr) return Value::obyek(a);
    a->panjang = isi_aktif(*p, a);
    return Value::obyek(a);
}

Value peta_nilai(VM& vm, Value this_val, std::vector<Value>&) {
    PetaObj* p = peta_dari(this_val);
    ArrayObj* a = kosong(vm, p == nullptr ? 0 : p->jumlah_aktif);
    if (p == nullptr) return Value::obyek(a);
    std::size_t n = 0;
    for (const auto& e : p->entri) {
        if (!e.dihapus) a->elemen[n++] = e.nilai;
    }
    a->panjang = n;
    return Value::obyek(a);
}

Value peta_entri(VM& vm, Value this_val, std::vector<Value>&) {
    PetaObj* p = peta_dari(this_val);
    ArrayObj* a = kosong(vm, p == nullptr ? 0 : p->jumlah_aktif);
    if (p == nullptr) return Value::obyek(a);
    std::size_t n = 0;
    for (const auto& e : p->entri) {
        if (e.dihapus) continue;
        ArrayObj* pasangan = kosong(vm, 2);
        pasangan->elemen[0] = e.kunci;
        pasangan->elemen[1] = e.nilai;
        a->elemen[n++] = Value::obyek(pasangan);
    }
    a->panjang = n;
    return Value::obyek(a);
}

Value peta_akeh(VM& vm, Value this_val, std::vector<Value>& args) {
    PetaObj* p = peta_dari(this_val);
    ArrayObj* a = kosong(vm, p == nullptr ? 0 : p->jumlah_aktif);
    if (p == nullptr) return Value::obyek(a);
    const bool mau_kunci = !args.empty() && args[0].is_obyek() &&
                           static_cast<rt::Obj*>(args[0].mutable_pointer())->h.kind == OK::Teks &&
                           rt::nilai_sama(args[0], Value::obyek(rt::buat_teks(vm.heap(), "kunci")));
    std::size_t n = 0;
    for (const auto& e : p->entri) {
        if (e.dihapus) continue;
        a->elemen[n++] = mau_kunci ? e.kunci : e.nilai;
    }
    a->panjang = n;
    return Value::obyek(a);
}

// ===========================================================================
// Himpunan
// ===========================================================================

/// `Himpunan()` / `Himpunan([1, 2, 2])` (duplikat diabaikan) / `Himpunan(peta)`.
Value himpunan_baru(VM& vm, Value /*this*/, std::vector<Value>& args) {
    HimpunanObj* h = vm.buat_himpunan();
    if (args.empty()) return Value::obyek(h);
    if (HimpunanObj* asal = himpunan_dari(args[0])) {
        for (const auto& e : asal->isi.entri) {
            if (!e.dihapus) h->isi.pasang(e.kunci, e.kunci);
        }
        return Value::obyek(h);
    }
    if (rt::Obj* o = args[0].is_obyek() ? static_cast<rt::Obj*>(args[0].mutable_pointer()) : nullptr;
        o != nullptr && o->h.kind == OK::Array) {
        auto* a = static_cast<ArrayObj*>(o);
        for (std::size_t i = 0; i < a->panjang; ++i) h->isi.pasang(a->elemen[i], a->elemen[i]);
        return Value::obyek(h);
    }
    h->isi.pasang(args[0], args[0]);
    return Value::obyek(h);
}

Value himpunan_tambah(VM&, Value this_val, std::vector<Value>& args) {
    HimpunanObj* h = himpunan_dari(this_val);
    if (h == nullptr) return this_val;
    h->isi.pasang(arg(args, 0), arg(args, 0));
    return this_val;
}

Value himpunan_hapus(VM&, Value this_val, std::vector<Value>& args) {
    HimpunanObj* h = himpunan_dari(this_val);
    return Value::boolean(h != nullptr && h->isi.hapus(arg(args, 0)));
}

Value himpunan_ada(VM&, Value this_val, std::vector<Value>& args) {
    HimpunanObj* h = himpunan_dari(this_val);
    return Value::boolean(h != nullptr && h->isi.cari(arg(args, 0)) != nullptr);
}

Value himpunan_kosong(VM&, Value this_val, std::vector<Value>&) {
    HimpunanObj* h = himpunan_dari(this_val);
    return Value::boolean(h == nullptr || h->isi.jumlah_aktif == 0);
}

Value himpunan_bersih(VM&, Value this_val, std::vector<Value>&) {
    HimpunanObj* h = himpunan_dari(this_val);
    if (h == nullptr) return Value::mboh();
    h->isi.entri.clear();
    h->isi.tabel.clear();
    h->isi.jumlah_aktif = 0;
    return Value::mboh();
}

Value himpunan_ke_dhaptar(VM& vm, Value this_val, std::vector<Value>&) {
    HimpunanObj* h = himpunan_dari(this_val);
    ArrayObj* a = kosong(vm, h == nullptr ? 0 : h->isi.jumlah_aktif);
    if (h == nullptr) return Value::obyek(a);
    a->panjang = isi_aktif(h->isi, a);
    return Value::obyek(a);
}

// ===========================================================================
// Janji.all / Janji.race / Janji.selesai / Janji.tolak
// ===========================================================================

/// Bangun Janji gabungan dari `daftar`.
///
/// `semua == true`  -> `Janji.all`: selesai setelah semua selesai, hasilnya
///                     dhaptar dengan nilai masing-masing pada indeksnya.
/// `semua == false` -> `Janji.race`: selesai pada Janji pertama yang selesai.
///
/// Nilai biasa (bukan Janji) diperlakukan sebagai Janji yang sudah selesai,
/// sama seperti ECMAScript (`Promise.all([1, Promise.resolve(2)])`).
/// Janji yang sudah ditolak langsung menolak Janji gabungan.
Value janji_gabung(VM& vm, std::vector<Value>& args, bool semua) {
    JanjiObj* hasil = vm.buat_janji();
    const Value hasil_v = Value::obyek(hasil);
    ArrayObj* daftar = nullptr;
    if (!args.empty()) {
        if (rt::Obj* o = args[0].is_obyek() ? static_cast<rt::Obj*>(args[0].mutable_pointer()) : nullptr;
            o != nullptr && o->h.kind == OK::Array) {
            daftar = static_cast<ArrayObj*>(o);
        }
    }
    const std::size_t n = daftar == nullptr ? 0 : daftar->panjang;
    ArrayObj* kumpulan = kosong(vm, semua ? n : 1);
    if (semua) {
        for (std::size_t i = 0; i < n; ++i) kumpulan->elemen[i] = Value::mboh();
    }
    if (n == 0) {
        vm.selesaikan_janji(hasil, semua ? Value::obyek(kumpulan) : Value::mboh(), false);
        return hasil_v;
    }

    // Entri penggumpalan dipasang ke SETIAP Janji sumber. `sisa_kumpul` di
    // entri pertama adalah HOLDOFF: kalau Janji ke-0 selesai duluan, sisa
    // Janji yang belum selesai dihitung dari situ.
    std::size_t belum_selesai = 0;
    for (std::size_t i = 0; i < n; ++i) {
        JanjiObj* j = janji_dari_peta(daftar->elemen[i]);
        if (j == nullptr) {
            // Nilai biasa: sudah selesai dengan dirinya sendiri.
            if (semua) kumpulan->elemen[i] = daftar->elemen[i];
            continue;
        }
        if (j->status == rt::JanjiStatus::Gagal) {
            vm.selesaikan_janji(hasil, j->hasil, true);
            return hasil_v;
        }
        if (j->status == rt::JanjiStatus::Slamet) {
            if (semua) kumpulan->elemen[i] = j->hasil;
            continue;
        }
        ++belum_selesai;
    }
    if (!semua) belum_selesai = 1;  // `race` hanya butuh satu
    if (belum_selesai == 0) {
        vm.selesaikan_janji(hasil, semua ? Value::obyek(kumpulan) : Value::mboh(), false);
        return hasil_v;
    }
    for (std::size_t i = 0; i < n; ++i) {
        JanjiObj* j = janji_dari_peta(daftar->elemen[i]);
        if (j == nullptr || j->status != rt::JanjiStatus::Menunggu) continue;
        JanjiObj::Then th;
        th.kumpulan_tujuan = hasil;
        th.kumpulan = kumpulan;
        th.nama_indeks = i;
        th.sisa_kumpul = belum_selesai;
        th.kumpul_semua = semua;
        j->then_daftar.push_back(th);
    }
    return hasil_v;
}

Value janji_all(VM& vm, Value /*this*/, std::vector<Value>& args) { return janji_gabung(vm, args, true); }

Value janji_race(VM& vm, Value /*this*/, std::vector<Value>& args) { return janji_gabung(vm, args, false); }

/// `Janji.anySelesai([...])`: seperti `all`, tapi TIDAK PERNAH ditolak.
///
/// Setiap slot hasilnya dibungkus: `{nilai: ...}` kalau Janjinya selesai, atau
/// `{galat: ...}` kalau ditolak. Berguna saat beberapa Janji boleh gagal tapi
/// program tetap mau berjalan setelah semuanya selesai.
Value janji_semua_selesai(VM& vm, Value /*this*/, std::vector<Value>& args) {
    JanjiObj* hasil = vm.buat_janji();
    const Value hasil_v = Value::obyek(hasil);
    ArrayObj* daftar = nullptr;
    if (!args.empty()) {
        if (rt::Obj* o = args[0].is_obyek() ? static_cast<rt::Obj*>(args[0].mutable_pointer()) : nullptr;
            o != nullptr && o->h.kind == OK::Array) {
            daftar = static_cast<ArrayObj*>(o);
        }
    }
    const std::size_t n = daftar == nullptr ? 0 : daftar->panjang;
    ArrayObj* kumpulan = kosong(vm, n);
    std::size_t belum = 0;
    for (std::size_t i = 0; i < n; ++i) {
        JanjiObj* j = janji_dari_peta(daftar->elemen[i]);
        if (j == nullptr) {
            ObyekObj* bungkus = vm.buat_obyek();
            bungkus->define(vm.heap(), Value::obyek(rt::buat_teks(vm.heap(), "nilai")),
                            daftar->elemen[i], rt::AttrDefault);
            kumpulan->elemen[i] = Value::obyek(bungkus);
            continue;
        }
        if (j->status == rt::JanjiStatus::Menunggu) {
            ++belum;
            continue;
        }
        const bool ditolak = j->status == rt::JanjiStatus::Gagal;
        ObyekObj* bungkus = vm.buat_obyek();
        bungkus->define(vm.heap(), Value::obyek(rt::buat_teks(vm.heap(), ditolak ? "galat" : "nilai")),
                        j->hasil, rt::AttrDefault);
        kumpulan->elemen[i] = Value::obyek(bungkus);
    }
    if (belum == 0) {
        vm.selesaikan_janji(hasil, Value::obyek(kumpulan), false);
        return hasil_v;
    }
    for (std::size_t i = 0; i < n; ++i) {
        JanjiObj* j = janji_dari_peta(daftar->elemen[i]);
        if (j == nullptr || j->status != rt::JanjiStatus::Menunggu) continue;
        JanjiObj::Then th;
        th.kumpulan_tujuan = hasil;
        th.kumpulan = kumpulan;
        th.nama_indeks = i;
        th.sisa_kumpul = belum;
        th.kumpul_semua = true;
        th.kumpulan_tolak_ditahan = true;
        j->then_daftar.push_back(th);
    }
    return hasil_v;
}

/// `Janji.selesai(v)` -- Janji yang langsung selesai. Berguna untuk menulis
/// fungsi `mengko` tanpa `enteni` di dalamnya.
Value janji_selesai(VM& vm, Value /*this*/, std::vector<Value>& args) {
    JanjiObj* j = vm.buat_janji();
    vm.selesaikan_janji(j, arg(args, 0), false);
    return Value::obyek(j);
}

/// `Janji.tolak(e)` -- Janji yang langsung ditolak.
Value janji_tolak(VM& vm, Value /*this*/, std::vector<Value>& args) {
    JanjiObj* j = vm.buat_janji();
    vm.selesaikan_janji(j, arg(args, 0), true);
    return Value::obyek(j);
}

}  // namespace

// ===========================================================================
// Pemasangan
// ===========================================================================

void pasang_peta(VM& vm) {
    daftarkan(vm, "Peta", 0, false, peta_baru);
    daftarkan(vm, "Himpunan", 0, false, himpunan_baru);

    const auto PETA = static_cast<std::uint8_t>(OK::Peta);
    const auto HIMPUNAN = static_cast<std::uint8_t>(OK::Himpunan);
    pasang_prototype_metode(vm, PETA, "get", 2, peta_get);
    pasang_prototype_metode(vm, PETA, "set", 2, peta_set);
    pasang_prototype_metode(vm, PETA, "hapus", 1, peta_hapus);
    pasang_prototype_metode(vm, PETA, "ada", 1, peta_ada);
    pasang_prototype_metode(vm, PETA, "kosong", 0, peta_kosong);
    pasang_prototype_metode(vm, PETA, "bersih", 0, peta_bersih);
    pasang_prototype_metode(vm, PETA, "kunci", 0, peta_kunci);
    pasang_prototype_metode(vm, PETA, "nilai", 0, peta_nilai);
    pasang_prototype_metode(vm, PETA, "entri", 0, peta_entri);
    pasang_prototype_metode(vm, PETA, "akeh", 1, peta_akeh);

    pasang_prototype_metode(vm, HIMPUNAN, "tambah", 1, himpunan_tambah);
    pasang_prototype_metode(vm, HIMPUNAN, "hapus", 1, himpunan_hapus);
    pasang_prototype_metode(vm, HIMPUNAN, "ada", 1, himpunan_ada);
    pasang_prototype_metode(vm, HIMPUNAN, "kosong", 0, himpunan_kosong);
    pasang_prototype_metode(vm, HIMPUNAN, "bersih", 0, himpunan_bersih);
    pasang_prototype_metode(vm, HIMPUNAN, "ke_dhaptar", 0, himpunan_ke_dhaptar);

    // `Janji` sebagai objek: `Janji.all(...)` dll. Didaftarkan sebagai namespace
    // supaya method statisnya bisa diambil lewat `ambil_properti` `OK::Obyek`.
    ObyekObj* janji = buat_namespace(vm, "Janji");
    pasang_objek_metode(vm, janji, "all", 1, janji_all);
    pasang_objek_metode(vm, janji, "race", 1, janji_race);
    pasang_objek_metode(vm, janji, "anySelesai", 1, janji_semua_selesai);
    pasang_objek_metode(vm, janji, "selesai", 1, janji_selesai);
    pasang_objek_metode(vm, janji, "tolak", 1, janji_tolak);
    daftarkan_nilai(vm, "Janji", Value::obyek(janji));
}

}  // namespace jawa::vm::stdlib
