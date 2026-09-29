// Unit test runtime: NaN-boxing, model objek, GC, dan eksekusi VM.
//
// Semua test memakai harness sendiri (lihat tests/harness.h) supaya proyek tetap
// bebas dependensi pihak ketiga.
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "gc/heap.h"
#include "harness.h"
#include "rt/number.h"
#include "rt/object.h"
#include "rt/string.h"
#include "rt/value.h"
#include "vm/vm.h"

using jawa::rt::Value;

namespace {

/// Jalankan program Basa Jawa dan kembalikan teks yang ditulis `tulis`.
/// Output dialihkan ke buffer VM supaya test tidak bergantung pada stdout.
std::string jalankan(std::string_view sumber) {
    std::string keluaran;
    jawa::vm::VMOptions opt;
    opt.keluaran = &keluaran;
    jawa::vm::VM mesin(opt);
    const auto status = mesin.jalankan_sumber(sumber, "<test>");
    if (status != jawa::vm::Status::Selesai) return "<galat>";
    return keluaran;
}

/// Peta path -> sumber untuk `VMOptions::baca_berkas`.
using PetaBerkas = std::map<std::string, std::string>;

/// Jalankan modul `/satu/main.jw` dengan peta berkas virtual.
std::string jalankan_modul(std::string_view sumber, const PetaBerkas& peta) {
    std::string keluaran;
    jawa::vm::VMOptions opt;
    opt.keluaran = &keluaran;
    opt.baca_berkas = [&peta](const std::string& p, std::string& keluar) {
        const auto it = peta.find(p);
        if (it == peta.end()) return false;
        keluar = it->second;
        return true;
    };
    jawa::vm::VM mesin(opt);
    if (mesin.jalankan_sumber(sumber, "/satu/main.jw") != jawa::vm::Status::Selesai) {
        return "<galat>";
    }
    return keluaran;
}

}  // namespace

// ===========================================================================
// NaN-boxing
// ===========================================================================

TEST_CASE("nilai: ukuran & trivially copyable") {
    // Ukuran mengikuti mode nilai: 8 byte dengan NaN-boxing, 16 byte dengan
    // union bertag (`JAWA_NO_NAN_BOX`, dipakai pada platform dengan LA57).
#ifdef JAWA_NO_NAN_BOX
    CHECK_EQ(sizeof(Value), std::size_t{16});
#else
    CHECK_EQ(sizeof(Value), std::size_t{8});
#endif
    CHECK(std::is_trivially_copyable_v<Value>);
}

TEST_CASE("nilai: tag dasar") {
    CHECK(Value::mboh().is_mboh());
    CHECK(Value::kosong().is_kosong());
    CHECK(Value::boolean(true).is_boole());
    CHECK(Value::boolean(true).bool_value());
    CHECK(!Value::boolean(false).bool_value());
    CHECK(Value::number(1.5).is_number());
    CHECK(Value::angka_int32(7).is_int32());
    CHECK_EQ(Value::angka_int32(7).as_i32(), 7);
}

TEST_CASE("nilai: nan punya tag tersendiri") {
    const Value n = Value::number(std::nan(""));
    CHECK(n.is_nan_angka());
    CHECK(!n.is_mboh());
    CHECK(n.is_angka());
    CHECK(std::isnan(n.as_number()));
}

TEST_CASE("nilai: objek & teks") {
    const Value t = Value::obyek(reinterpret_cast<const void*>(0x1000));
    CHECK(t.is_obyek());
    CHECK(!t.is_number());
    CHECK_EQ(t.pointer(), reinterpret_cast<const void*>(0x1000));
}

TEST_CASE("nilai: perbandingan & kunci hash") {
    CHECK(Value::number(1.0) == Value::number(1.0));
    CHECK(Value::number(1.0) != Value::number(2.0));
    CHECK(Value::number(std::nan("")).key() == Value::number(std::nan("")).key());
    CHECK(Value::mboh() == Value::mboh());
}

TEST_CASE("nilai: kebenaran (truthiness)") {
    CHECK(!jawa::rt::benar(Value::mboh()));
    CHECK(!jawa::rt::benar(Value::kosong()));
    CHECK(!jawa::rt::benar(Value::boolean(false)));
    CHECK(!jawa::rt::benar(Value::number(0.0)));
    CHECK(!jawa::rt::benar(Value::number(std::nan(""))));
    CHECK(jawa::rt::benar(Value::number(-1.0)));
    CHECK(jawa::rt::benar(Value::obyek(reinterpret_cast<const void*>(0x2000))));
}

// ===========================================================================
// Format angka
// ===========================================================================

TEST_CASE("angka: format sesuai ECMAScript") {
    CHECK_EQ(jawa::rt::number_to_string(1.0), std::string("1"));
    CHECK_EQ(jawa::rt::number_to_string(1.5), std::string("1.5"));
    CHECK_EQ(jawa::rt::number_to_string(0.1 + 0.2), std::string("0.30000000000000004"));
    CHECK_EQ(jawa::rt::number_to_string(1e21), std::string("1e+21"));
    CHECK_EQ(jawa::rt::number_to_string(-0.0), std::string("0"));
}

TEST_CASE("angka: penguraian ketat") {
    double d = 0.0;
    CHECK(jawa::rt::parse_number_strict("42", d));
    CHECK_EQ(d, 42.0);
    CHECK(jawa::rt::parse_number_strict("-3.5", d));
    CHECK_EQ(d, -3.5);
    CHECK(!jawa::rt::parse_number_strict("abc", d));
    CHECK(!jawa::rt::parse_number_strict("", d));
}

TEST_CASE("angka:exactly_int32") {
    CHECK(jawa::rt::exactly_int32(5.0));
    CHECK(!jawa::rt::exactly_int32(5.5));
    CHECK(!jawa::rt::exactly_int32(1e30));
}

// ===========================================================================
// Model objek
// ===========================================================================

TEST_CASE("objek: properti lewat dict & perbandingan isi") {
    jawa::gc::Heap heap;
    auto* o = heap.alokasi<jawa::rt::ObyekObj>();
    o->h.kind = jawa::rt::OK::Obyek;
    o->shape = jawa::rt::ShapeTable::instance().kosong();
    o->slot = nullptr;
    o->jumlah_slot = 0;

    const Value k1 = Value::obyek(jawa::rt::StringTable::buat(heap, "nama"));
    const Value k2 = Value::obyek(jawa::rt::StringTable::buat(heap, "nama"));
    CHECK(k1 != k2);  // objek berbeda
    CHECK(jawa::rt::nilai_sama(k1, k2));  // tapi isinya sama

    o->define(heap, k1, Value::number(1.0), jawa::rt::AttrWritable);
    Value keluar = Value::mboh();
    CHECK(o->get(k2, keluar));
    CHECK_EQ(keluar.as_number(), 1.0);
}

TEST_CASE("peta: pasang / cari / hapus") {
    jawa::gc::Heap heap;
    auto* p = heap.alokasi<jawa::rt::PetaObj>();
    p->h.kind = jawa::rt::OK::Peta;
    const Value k = Value::number(1.0);
    CHECK(p->pasang(k, Value::number(10.0)));
    CHECK(p->pasang(k, Value::number(20.0)) == false);  // sudah ada
    const auto* e = p->cari(k);
    REQUIRE(e != nullptr);
    CHECK_EQ(e->nilai.as_number(), 20.0);
    CHECK(p->hapus(k));
    CHECK(p->cari(k) == nullptr);
}

TEST_CASE("dhaptar: dorong & perkecil") {
    jawa::gc::Heap heap;
    auto* a = heap.alokasi<jawa::rt::ArrayObj>();
    a->h.kind = jawa::rt::OK::Array;
    a->init(jawa::rt::ShapeTable::instance().kosong(), 2);
    for (int i = 0; i < 100; ++i) a->dorong(Value::number(static_cast<double>(i)));
    CHECK_EQ(a->panjang, std::size_t{100});
    CHECK_EQ(a->get(0).as_number(), 0.0);
    CHECK_EQ(a->get(99).as_number(), 99.0);
    a->perkecil();
    CHECK_EQ(a->panjang, std::size_t{100});
}

// ===========================================================================
// GC
// ===========================================================================

TEST_CASE("gc: mark & sweep membebaskan yang tak terjangkau") {
    jawa::gc::Heap heap;
    auto* akar = heap.alokasi<jawa::rt::ObyekObj>();
    akar->h.kind = jawa::rt::OK::Obyek;
    akar->shape = jawa::rt::ShapeTable::instance().kosong();
    heap.akar(Value::obyek(akar));
    const std::size_t sebelum = heap.statistik().jumlah_objek;

    for (int i = 0; i < 200; ++i) {
        (void)jawa::rt::StringTable::buat(heap, "buangan");
    }
    CHECK(heap.statistik().jumlah_objek >= sebelum);
    // Setelah koleksi, yang tersisa hanya akar + karantina alokasi terakhir
    // (lihat `Heap::kKarantina`), jadi jumlahnya TIDAK tumbuh tanpa batas.
    heap.koleksi_full();
    const std::size_t sesudah = heap.statistik().jumlah_objek;
    CHECK(sesudah <= sebelum + 128);
    for (int ronde = 0; ronde < 5; ++ronde) {
        for (int i = 0; i < 200; ++i) {
            (void)jawa::rt::StringTable::buat(heap, "buangan");
        }
        heap.koleksi_full();
    }
    CHECK(heap.statistik().jumlah_objek <= sesudah + 128);
}

TEST_CASE("gc: HandleScope menjaga nilai tetap hidup") {
    jawa::gc::Heap heap;
    jawa::gc::HandleScope scope(heap);
    Value* slot = scope.slot(Value::mboh());
    const std::size_t sebelum = heap.statistik().jumlah_objek;
    for (int i = 0; i < 200; ++i) {
        // Handle menjaga nilai lama tetap hidup; koleksi di tengah jalan harus
        // TIDAK membebaskan objek yang ditunjuk handle.
        *slot = Value::obyek(jawa::rt::StringTable::buat(heap, "tetep"));
        heap.koleksi_full();
    }
    // Karantina alokasi (lihat `Heap::kKarantina`) menahan 64 objek terakhir,
    // jadi jumlahnya dibatasi, bukan tepat 1.
    CHECK(heap.statistik().jumlah_objek <= sebelum + 65);
    CHECK(slot->is_obyek());
}

TEST_CASE("gc: stress tidak merusak heap") {
    jawa::gc::Heap heap(/*stress=*/true);
    for (int i = 0; i < 200; ++i) {
        (void)jawa::rt::StringTable::buat(heap, "x");
    }
    // Mode stress: tiap alokasi memicu koleksi penuh; jumlah objek harus
    // tetap kecil (tidak menumpuk tanpa akar).
    CHECK(heap.statistik().jumlah_objek <= 128);
}

// ===========================================================================
// VM
// ===========================================================================

TEST_CASE("vm: hello dunia") { CHECK_EQ(jalankan("tulis(\"halo\");"), std::string("halo\n")); }

TEST_CASE("vm: aritmetika") {
    CHECK_EQ(jalankan("tulis(1 + 2 * 3);"), std::string("7\n"));
    CHECK_EQ(jalankan("tulis(2 ** 10);"), std::string("1024\n"));
    CHECK_EQ(jalankan("tulis(7 % 3);"), std::string("1\n"));
}

TEST_CASE("vm: tanpa koersi implisit (D-007)") {
    // `Angka + Teks` harus jadi galat, bukan "1a".
    jawa::vm::VMOptions opt;
    jawa::vm::VM mesin(opt);
    const auto status = mesin.jalankan_sumber("tulis(1 + \"a\");", "<test>");
    CHECK(status != jawa::vm::Status::Selesai);
}

TEST_CASE("vm: variabel & fungsi") {
    CHECK_EQ(jalankan("ana x = 4; gawe f(a) { bali a * 2; } tulis(f(x));"), std::string("8\n"));
}

TEST_CASE("vm: rekursi") {
    CHECK_EQ(jalankan("gawe fib(n) { yen (n < 2) { bali n; } bali fib(n-1) + fib(n-2); } tulis(fib(15));"),
             std::string("610\n"));
}

TEST_CASE("vm: closure & upvalue") {
    CHECK_EQ(jalankan("gawe c() { ana n = 0; bali () => ++n; } tetep f = c(); tulis(f(), f(), f());"),
             std::string("1 2 3\n"));
}

TEST_CASE("vm: loop & break/continue") {
    CHECK_EQ(jalankan("kanggo (ana i = 0; i < 5; i++) { yen (i === 1) { terusna; } "
                      "yen (i === 3) { mandheg; } tulis(i); }"),
             std::string("0\n2\n"));
}

TEST_CASE("vm: dhaptar & metode bawaan") {
    CHECK_EQ(jalankan("tulis([1,2,3].gabung(\"-\"));"), std::string("1-2-3\n"));
    CHECK_EQ(jalankan("tulis([1,2,3].saring(x => x > 1).peta(x => x * 10).gabung(\",\"));"),
             std::string("20,30\n"));
}

TEST_CASE("vm: objek & kelas") {
    CHECK_EQ(jalankan("golongan P { #n; wiwit(n) { iki.#n = n; } nampa nilai() { bali iki.#n; } } "
                      "tulis(anyar P(9).nilai);"),
             std::string("9\n"));
}

TEST_CASE("vm: pola cocog") {
    CHECK_EQ(jalankan("tulis(cocog (0) { kasus 0 => \"nol\", kasus _ => \"liyane\" })"),
             std::string("nol\n"));
    CHECK_EQ(jalankan("tulis(cocog (\"x\") { kasus 0 => \"nol\", kasus _ => \"liyane\" })"),
             std::string("liyane\n"));
}

TEST_CASE("vm: coba / tangkep") {
    // `tulis` memisahkan argumen dengan satu spasi, jadi ada dua spasi di sini.
    CHECK_EQ(jalankan("coba { uncal \"x\"; } tangkep (e) { tulis(\"ditangkep\", e); }"),
             std::string("ditangkep x\n"));
}

TEST_CASE("vm: pipeline") { CHECK_EQ(jalankan("tulis(5 |> (x => x + 1) |> (x => x * 2));"),
                                        std::string("12\n")); }

TEST_CASE("vm: generator mode-eager") {
    CHECK_EQ(jalankan("gawe* g() { metokake 1; metokake 2; } tulis([...g()]);"), std::string("[1, 2]\n"));
}

TEST_CASE("vm: template literal") {
    CHECK_EQ(jalankan("tulis(`a${1 + 1}b c`);"), std::string("a2b c\n"));
}

// ===========================================================================
// Async: Janji, suspensi, resume
// ===========================================================================

TEST_CASE("async: urutan microtask setelah kode sinkron") {
    // `enteni` menunda rantai; kode sinkron selesai lebih dulu.
    CHECK_EQ(jalankan("mengko gawe f() { enteni Wektu.tundha(1); bali 42; } "
                      "tulis('A'); const v = enteni f(); tulis(v);"),
             std::string("A\n42\n"));
}

TEST_CASE("async: pemanggilan async tanpa entani memberi Janji") {
    // `f()` mengembalikan Janji; `tulis` berjalan lebih dulu.
    CHECK_EQ(jalankan("mengko gawe f() { enteni Wektu.tundha(1); bali 1; } f(); tulis('dhisik');"),
             std::string("dhisik\n"));
}

TEST_CASE("async: chains (rantai berlapis) selesai") {
    CHECK_EQ(jalankan("mengko gawe a() { enteni Wektu.tundha(1); bali 2; } "
                      "mengko gawe b() { const x = enteni a(); bali x * 5; } "
                      "const y = enteni b(); tulis(y);"),
             std::string("10\n"));
}

TEST_CASE("async: .then dijalankan sebagai microtask") {
    CHECK_EQ(jalankan("Wektu.tundha(1).then(x => tulis('nanti')); tulis('dulu');"),
             std::string("dulu\nnanti\n"));
}

TEST_CASE("async: entani di fungsi biasa ikut menunda rantai") {
    // Frame modul adalah akar rantai async, jadi `entani` di fungsi biasa pun
    // menunda; pemanggil dilanjutkan setelah Janji selesai.
    CHECK_EQ(jalankan("gawe f() { enteni Wektu.tundha(1); bali 5; } "
                      "tulis('A'); const v = enteni f(); tulis(v);"),
             std::string("A\n5\n"));
}

TEST_CASE("async: entani di tingkat modul (top-level await) boleh") {
    // `Wektu.tundha` menyelesaikan Janji tanpa nilai (setara `mboh`).
    CHECK_EQ(jalankan("tulis('A'); const v = enteni Wektu.tundha(1); tulis(jenis(v));"),
             std::string("A\nmboh\n"));
}

TEST_CASE("async: entani atas nilai biasa = identitas") {
    CHECK_EQ(jalankan("const v = enteni 5; tulis(v);"), std::string("5\n"));
}

TEST_CASE("async: .tangkep menangkap penolakan fungsi mengko") {
    // Galat di dalam `mengko` menjadi PENOLAKAN Janji, bukan galat program, dan
    // `.tangkep` dipanggil sebagai mikrotugas SETELAH kode sinkron selesai.
    CHECK_EQ(jalankan("mengko gawe f() { enteni Wektu.tundha(1); uncal 'bose'; } "
                      "f().tangkep(e => tulis('nolak: ', e)); tulis('dhisik');"),
             std::string("dhisik\nnolak:  bose\n"));
}

TEST_CASE("async: .then berantai") {
    CHECK_EQ(jalankan("Wektu.tundha(1).then(x => x).then(x => tulis(jenis(x))); tulis('dhisik');"),
             std::string("dhisik\nmboh\n"));
}

TEST_CASE("async: enteni atas Janji ditolak melempar galat yang bisa ditangkap") {
    CHECK_EQ(jalankan("coba { mengko gawe f() { enteni Wektu.tundha(1); uncal 'bose'; } "
                      "const v = enteni f(); } tangkep (e) { tulis('ditangkep: ', e); }"),
             std::string("ditangkep:  bose\n"));
}

TEST_CASE("async: timer dinyalakan menurut urutan tunda") {
    // `tunda_ms` menentukan urutan, bukan urutan pemanggilan.
    CHECK_EQ(jalankan("Wektu.tundha(30).then(x => tulis('A')); "
                      "Wektu.tundha(10).then(x => tulis('B')); tulis('dhisik');"),
             std::string("dhisik\nB\nA\n"));
}

// ===========================================================================
// Modul ES: impor, ekspor, siklus, dan penanganan galat
//
// Modul disuplai lewat `VMOptions::baca_berkas` (peta path -> sumber), jadi
// test tidak menyentuh sistem berkas.
// ===========================================================================

// ===========================================================================
// Generator LAZY (`gawe*` + `metokake`)
// ===========================================================================

TEST_CASE("generator: spread menghasilkan semua hasil") {
    CHECK_EQ(jalankan("gawe* cacah(n) { kanggo (ana i = 1; i <= n; i++) metokake i; }\n"
                      "tulis([...cacah(5)]);\n"),
             std::string("[1, 2, 3, 4, 5]\n"));
}

TEST_CASE("generator: lazy -- body jalan sampai metokake pertama") {
    // Sisi kanan `tulis` dievaluasi lebih dulu, tapi body generator sudah
    // sampai `metokake` pertama SAAT dipanggil (bukan nanti). Baris "A" tetap
    // tercetak sebelum "1" karena `metokake` mengembalikan kendali.
    CHECK_EQ(jalankan("gawe* g() { metokake 1; metokake 2; }\n"
                      "const x = g();\n"
                      "tulis('A');\n"
                      "tulis(x.next().nilai);\n"
                      "tulis(x.next().nilai);\n"),
             std::string("A\n1\n2\n"));
}

TEST_CASE("generator: tak berhingga bisa dipakai dan dihentikan") {
    // Generator tak berhingga dulu mustahil: mode-eager berjalan sampai selesai.
    CHECK_EQ(jalankan("gawe* tak_henti() { ana i = 0; nalika (bener) { metokake i; i = i + 1; } }\n"
                      "ana n = 0;\n"
                      "kanggo (ana x saka tak_henti()) {\n"
                      "  yen (n >= 4) mandheg;\n"
                      "  tulis(x);\n"
                      "  n = n + 1;\n"
                      "}\n"),
             std::string("0\n1\n2\n3\n"));
}

TEST_CASE("generator: next() mengembalikan {nilai, selesai}") {
    CHECK_EQ(jalankan("gawe* g() { metokake 1; }\n"
                      "const x = g();\n"
                      "tulis(x.next().selesai);\n"
                      "tulis(x.next().selesai);\n"),
             std::string("false\ntrue\n"));
}

TEST_CASE("generator: nilai balik 'bali' tidak jadi hasil") {
    // `next()` setelah selesai melaporkan `mboh` (sifat JavaScript); nilai
    // `bali` tersedia lewat properti `bali`.
    CHECK_EQ(jalankan("gawe* g() { metokake 1; bali 99; }\n"
                      "const x = g();\n"
                      "x.next();\n"
                      "tulis(jenis(x.next().nilai));\n"
                      "tulis(x.bali);\n"),
             std::string("mboh\n99\n"));
}

TEST_CASE("generator: closure & upvalue bertahan saat suspend") {
    CHECK_EQ(jalankan("gawe* ngitung(mulai) {\n"
                      "  ana i = mulai;\n"
                      "  nalika (bener) { metokake i; i = i + 1; }\n"
                      "}\n"
                      "const x = ngitung(10);\n"
                      "tulis(x.next().nilai);\n"
                      "tulis(x.next().nilai);\n"
                      "tulis(x.next().nilai);\n"),
             std::string("10\n11\n12\n"));
}

TEST_CASE("generator: galat di body dilempar ke pemanggil next()") {
    CHECK_EQ(jalankan("gawe* g() { metokake 1; uncal 'bose'; }\n"
                      "const x = g();\n"
                      "coba { x.next(); x.next(); } tangkep (e) { tulis('ditangkep'); }\n"),
             std::string("ditangkep\n"));
}

TEST_CASE("generator: dua generator tak saling ganggu") {
    CHECK_EQ(jalankan("gawe* a() { metokake 1; metokake 2; }\n"
                      "gawe* b() { metokake 'x'; metokake 'y'; }\n"
                      "const x = a();\n"
                      "const y = b();\n"
                      "tulis(x.next().nilai);\n"
                      "tulis(y.next().nilai);\n"
                      "tulis(x.next().nilai);\n"
                      "tulis(y.next().nilai);\n"),
             std::string("1\nx\n2\ny\n"));
}

TEST_CASE("generator: yang tanpa metokake langsung selesai") {
    // `mboh_` bukan `mboh`: `mboh` adalah kata kunci `undefined`.
    CHECK_EQ(jalankan("gawe* tanpa_yield() { bali 1; }\n"
                      "tulis(jenis(tanpa_yield().next().selesai));\n"),
             std::string("boole\n"));
}

TEST_CASE("kondisi: `yen` tidak membocorkan nilai di stack") {
    // Setiap `yen` pernah membocorkan nilai kondisi pada jalur false, sehingga
    // loop panjang tumbuh di stack tanpa batas. 50.000 iterasi cukup untuk
    // melewati batas stack JVM biasa kalau bocorannya masih ada.
    CHECK_EQ(jalankan("ana n = 0;\n"
                      "nalika (n < 50000) {\n"
                      "  n = n + 1;\n"
                      "  yen (n > 2) { yen (n > 3) { yen (n > 4) { } } }\n"
                      "}\n"
                      "tulis('n=', n);\n"),
             std::string("n= 50000\n"));
}

TEST_CASE("kondisi: `liyane` juga tidak membocorkan nilai") {
    CHECK_EQ(jalankan("ana x = 0;\n"
                      "kanggo (ana i = 0; i < 1000; i = i + 1) {\n"
                      "  yen (i % 2 === 0) { x = x + 1; } liyane { x = x - 1; }\n"
                      "}\n"
                      "tulis('x=', x);\n"),
             std::string("x= 0\n"));
}

// ===========================================================================
// `pilih` (switch) — nilai, pola, dan `baku`
// ===========================================================================

TEST_CASE("pilih: kasus nilai diuji berurutan") {
    // `Op::EQ` adalah perbandingan biasa; dulu `stmt_pilih` memperlakukannya
    // sebagai lompatan, sehingga hanya `kasus` PERTAMA yang pernah dicek.
    CHECK_EQ(jalankan("pilih (2) { kasus 1: tulis('satu'); kasus 2: tulis('dua'); kasus 3: tulis('tiga'); }\n"),
             std::string("dua\n"));
    CHECK_EQ(jalankan("pilih (1) { kasus 1: tulis('satu'); kasus 2: tulis('dua'); }\n"),
             std::string("satu\n"));
    CHECK_EQ(jalankan("pilih (9) { kasus 1: tulis('satu'); kasus 2: tulis('dua'); }\n"
                      "tulis('selesai');\n"),
             std::string("selesai\n"));
}

TEST_CASE("pilih: `baku` jadi cadangan") {
    CHECK_EQ(jalankan("pilih (9) { kasus 1: tulis('satu'); baku: tulis('lain'); }\n"),
             std::string("lain\n"));
    // `baku` tidak jatuh kalau ada kasus yang cocok.
    CHECK_EQ(jalankan("pilih (1) { kasus 1: tulis('satu'); baku: tulis('lain'); }\n"),
             std::string("satu\n"));
}

TEST_CASE("pilih: pola dhaptar dengan binding") {
    CHECK_EQ(jalankan("pilih ([1, 2]) { kasus [a, b]: tulis('a=', a, 'b=', b); }\n"),
             std::string("a= 1 b= 2\n"));
    CHECK_EQ(jalankan("pilih ([1, [2, 3]]) { kasus [1, [2, x]]: tulis('nested ', x); }\n"),
             std::string("nested  3\n"));
    CHECK_EQ(jalankan("pilih ([9, 9]) { kasus [1, 2]: tulis('salah'); baku: tulis('baku'); }\n"),
             std::string("baku\n"));
}

TEST_CASE("pilih: pola obyek & wildcard") {
    CHECK_EQ(jalankan("pilih ({jenis: 'kucing'}) {"
                      " kasus {jenis: 'kucing'}: tulis('kucing');"
                      " baku: tulis('lain'); }\n"),
             std::string("kucing\n"));
    CHECK_EQ(jalankan("pilih ([1, 2, 3]) { kasus [_, 2, _]: tulis('ada 2'); baku: tulis('lain'); }\n"),
             std::string("ada 2\n"));
}

TEST_CASE("pilih: pola rest mengikat sisa, bukan seluruh subjek") {
    CHECK_EQ(jalankan("pilih ([1, 2, 3]) { kasus [a, ...sisa]: tulis(a, sisa); }\n"),
             std::string("1 [2, 3]\n"));
    // Panjang subjek boleh lebih panjang dari yang ditulis di pola.
    CHECK_EQ(jalankan("pilih ([1, 2, 3, 4]) { kasus [a, b, ...sisa]: tulis(a, b, sisa); }\n"),
             std::string("1 2 [3, 4]\n"));
    // ...dan sisa boleh kosong kalau panjangnya pas.
    CHECK_EQ(jalankan("pilih ([1, 2]) { kasus [a, b, ...sisa]: tulis(a, b, sisa); }\n"),
             std::string("1 2 []\n"));
    // ...tapi subjek tidak boleh lebih pendek dari bagian tetap.
    // Tidak ada yang dicetak kalau tidak ada kasus yang cocok.
    CHECK_EQ(jalankan("pilih ([1]) { kasus [a, b, ...sisa]: tulis('tidak cocok'); }\n"),
             std::string(""));
}

TEST_CASE("pilih: dipakai di dalam fungsi") {
    CHECK_EQ(jalankan("gawe f(x) { pilih (x) { kasus 0: bali 'nol'; kasus 1: bali 'satu';"
                      " baku: bali 'lain'; } }\n"
                      "tulis(f(0), f(1), f(7));\n"),
             std::string("nol satu lain\n"));
}

TEST_CASE("pilih: pola dipakai tanpa binding — nilai yang terikat tidak bocor ke luar") {
    CHECK_EQ(jalankan("pilih ([1, 2, 3]) { kasus [a, ...sisa]: ; }\n"
                      "tulis('selesai');\n"),
             std::string("selesai\n"));
}

// ===========================================================================
// Zona mati-temporal (TDZ)
// ===========================================================================

TEST_CASE("tdz: baca sebelum deklarasi = galat") {
    jawa::vm::VMOptions opt;
    jawa::vm::VM mesin(opt);
    CHECK(mesin.jalankan_sumber("tulis(x); tetep x = 5;", "<test>") == jawa::vm::Status::Galat);
}

TEST_CASE("tdz: baca setelah deklarasi tidak apa-apa") {
    CHECK_EQ(jalankan("tetep x = 5; tulis(x);\n"), std::string("5\n"));
}

TEST_CASE("tdz: berlaku di dalam fungsi") {
    jawa::vm::VMOptions opt;
    jawa::vm::VM mesin(opt);
    CHECK(mesin.jalankan_sumber("gawe f() { tulis(y); tetep y = 1; } f();", "<test>") ==
          jawa::vm::Status::Galat);
}

TEST_CASE("tdz: galat bisa ditangkap `coba`") {
    CHECK_EQ(jalankan("coba { tulis(z); } tangkep (e) { tulis('ditangkep'); } tetep z = 1;\n"),
             std::string("ditangkep\n"));
    CHECK_EQ(jalankan("coba { pilih (1) { kasus 1: tulis(w); } }"
                      " tangkep (e) { tulis('ditangkep'); } tetep w = 9;\n"),
             std::string("ditangkep\n"));
}

TEST_CASE("tdz: pengikut loop tetap jalan") {
    CHECK_EQ(jalankan("ana n = 0;\n"
                      "nalika (n < 3) { ana i = n; n = n + 1; }\n"
                      "tulis('n=', n);\n"),
             std::string("n= 3\n"));
    CHECK_EQ(jalankan("kanggo (ana i = 0; i < 3; i = i + 1) { tulis(i); }\n"),
             std::string("0\n1\n2\n"));
}

// ===========================================================================
// Regression test dari hasil fuzzing (lihat `docs/fuzzing.md`)
// ===========================================================================
// Dua bug di bawah ditemukan oleh `fuzz_lexer` dan `fuzz_parser`, bukan oleh
// test yang ditulis orang. Test ini sengaja memakai INPUT PERSIS dari kasus
// yang ditemukan, supaya regresinya tidak bisa lolos diam-diam.

// Bug 1: `lex_regex` menghitung flag dari `pola_akhir + 1`. Untuk regex yang
// tidak ketutup dan berhenti tepat di akhir sumber, `pola_akhir == src_.size()`,
// sehingga `substr` mulai pada `size() + 1` dan melempar `std::out_of_range`.
// Masukan minimal: `/b` (2 byte).
TEST_CASE("lexer: regex tanpa penutup di akhir sumber tidak melempar") {
    for (const char* src : {"/b", "a/[/", "/", "a+/b", "/ab", "a/[/]/"}) {
        jawa::vm::VMOptions opt;
        jawa::vm::VM mesin(opt);
        // Yang penting: tidak melempar exception. Status boleh apa saja --
        // galat diagnostik L008 adalah hasil yang diharapkan.
        (void)mesin.jalankan_sumber(src, "<test>");
    }
}

TEST_CASE("lexer: regex tanpa penutup dilaporkan sebagai galat") {
    jawa::vm::VMOptions opt;
    jawa::vm::VM mesin(opt);
    CHECK(mesin.jalankan_sumber("const r = /abc", "<test>") == jawa::vm::Status::Galat);
}

// Penjaga: perbaikan di atas tidak boleh merusak regex yang sah maupun program
// biasa yang tidak memakai regex sama sekali. (Regex sebagai NILAI runtime
// belum ada -- itu Fase 8; yang diuji di sini hanya tokenisasi.)
TEST_CASE("lexer: regex ketutup dan pembagian tetap jalan") {
    CHECK_EQ(jalankan("const a = 'abc';\ntulis(jenis(a));\n"), std::string("teks\n"));
    CHECK_EQ(jalankan("tulis(10 / 2, 7 % 3);\n"), std::string("5 1\n"));
}

// Bug 2: `parse_deklarasi_fungsi` untuk `gawe` diikuti `[` atau `{` memundurkan
// `idx_` ke posisi `gawe` lalu memanggil `parse_statement()`, yang memanggil
// `parse_deklarasi_fungsi()` lagi -- rekursi tak berujung yang meledakkan stack.
// Masukan minimal: `gawe* { }`.
TEST_CASE("parser: `gawe` tanpa nama lalu blok tidak menyebabkan stack overflow") {
    for (const char* src : {"gawe* { metokake 1; }", "gawe { metokake 1; }",
                            "gawe [a, b](x) { bali x; }", "gawe {a}(x) { bali x; }",
                            "gawe mengko * { enteni 1; }"}) {
        jawa::vm::VMOptions opt;
        jawa::vm::VM mesin(opt);
        // Hasil yang diharapkan: galat diagnostik, bukan crash.
        CHECK(mesin.jalankan_sumber(src, "<test>") == jawa::vm::Status::Galat);
    }
}

// Bug 2b: tanpa batas kedalaman, program dengan kurung bersarang jauh akan tetap
// menabrak stack. Dengan batas, ia jadi galat biasa yang bisa dibaca.
TEST_CASE("parser: nestoring ekspresi dibatasi") {
    std::string dalam(400, '(');
    dalam += "1";
    dalam.append(400, ')');
    jawa::vm::VMOptions opt;
    jawa::vm::VM mesin(opt);
    CHECK(mesin.jalankan_sumber(dalam, "<test>") == jawa::vm::Status::Galat);
}

TEST_CASE("parser: nestoring wajar masih boleh") {
    // Penjaga: batasnya 160 tingkat, jadi ekspresi yang wajar tidak terpengaruh.
    std::string dalam(40, '(');
    dalam += "1";
    dalam.append(40, ')');
    jawa::vm::VMOptions opt;
    jawa::vm::VM mesin(opt);
    CHECK(mesin.jalankan_sumber(dalam, "<test>") != jawa::vm::Status::Galat);
}

// Penjaga kedua: bentuk `gawe` yang SAH (dengan nama) tidak boleh ikut terpengaruh
// oleh jalur pemulihan di atas.
TEST_CASE("parser: `gawe` bernama tetap berfungsi seperti biasa") {
    CHECK_EQ(jalankan("gawe f(a, b = 2, ...sisa) { bali a + b + sisa.dawa; }\n"
                      "tulis(f(1), f(1, 5, 9, 9));\n"),
             std::string("3 8\n"));
    CHECK_EQ(jalankan("gawe* g() { metokake 1; metokake 2; }\n"
                      "kanggo (tetep x saka g()) { tulis(x); }\n"),
             std::string("1\n2\n"));
}

TEST_CASE("modul: impor nama, alias, dan namespace") {
    const PetaBerkas peta{
        {"/satu/util.jw", "ekspor tetep K = 42;\nekspor gawe tambah(a, b) { bali a + b; }\n"},
    };
    CHECK_EQ(jalankan_modul("impor { K, tambah } saka \"./util.jw\";\ntulis(tambah(K, 8));\n", peta),
             std::string("50\n"));
    CHECK_EQ(jalankan_modul("impor { K minangka N, tambah minangka jumlah } saka \"./util.jw\";\n"
                            "tulis(jumlah(N, 1));\n",
                            peta),
             std::string("43\n"));
    CHECK_EQ(jalankan_modul("impor * minangka U saka \"./util.jw\";\ntulis(U.tambah(1, 2));\n", peta),
             std::string("3\n"));
}

TEST_CASE("modul: ekspor default dan impor untuk efek samping") {
    const PetaBerkas peta{
        {"/satu/a.jw", "tulis(\"A\");\nekspor tetep N = 1;\n"},
        {"/satu/b.jw", "ekspor baku \"bawaan\";\n"},
    };
    CHECK_EQ(jalankan_modul("impor \"./a.jw\";\nimpor Utama saka \"./b.jw\";\ntulis(Utama);\n", peta),
             std::string("A\nbawaan\n"));
}

TEST_CASE("modul: dievaluasi satu kali walau diimpor berkali-kali") {
    const PetaBerkas peta{{"/satu/sisi.jw", "tulis(\"sekali\");\nekspor tetep N = 7;\n"}};
    CHECK_EQ(jalankan_modul("impor { N } saka \"./sisi.jw\";\n"
                            "impor { N minangka M } saka \"./sisi.jw\";\n"
                            "tulis(N, M);\n",
                            peta),
             std::string("sekali\n7 7\n"));
}

TEST_CASE("modul: impor bersarang tiga tingkat") {
    const PetaBerkas peta{
        {"/satu/a.jw", "ekspor tetep D = 3;\n"},
        {"/satu/b.jw", "impor { D } saka \"./a.jw\";\nekspor tetep C = D + 1;\n"},
    };
    CHECK_EQ(jalankan_modul("impor { C } saka \"./b.jw\";\ntulis(C);\n", peta), std::string("4\n"));
}

TEST_CASE("modul: impor siklik a<->b tidak menggantung") {
    const PetaBerkas peta{
        {"/satu/a.jw", "impor { g } saka \"./b.jw\";\nekspor gawe f() { bali \"f:\" + g(); }\n"},
        {"/satu/b.jw", "impor { f } saka \"./a.jw\";\nekspor gawe g() { bali \"g\"; }\n"},
    };
    CHECK_EQ(jalankan_modul("impor { f } saka \"./a.jw\";\ntulis(f());\n", peta), std::string("f:g\n"));
}

TEST_CASE("modul: ekspor daftar, alias, dan re-export") {
    const PetaBerkas peta{
        {"/satu/dasar.jw", "tetep x = 5; tetep w = 6;\nekspor { x minangka y, w };\n"},
        {"/satu/tengah.jw", "impor { y } saka \"./dasar.jw\";\nekspor { y minangka z };\n"},
    };
    CHECK_EQ(jalankan_modul("impor { y, w } saka \"./dasar.jw\";\ntulis(y, w);\n", peta),
             std::string("5 6\n"));
    CHECK_EQ(jalankan_modul("impor { z } saka \"./tengah.jw\";\ntulis(z);\n", peta), std::string("5\n"));
}

TEST_CASE("modul: ekspor boleh ditulis sebelum deklarasi") {
    const PetaBerkas peta{{"/satu/a.jw", "ekspor { N };\ntetep N = 8;\n"}};
    CHECK_EQ(jalankan_modul("impor { N } saka \"./a.jw\";\ntulis(N);\n", peta), std::string("8\n"));
}

TEST_CASE("modul: galat impor bisa ditangkap pemanggil") {
    // Tiga jenis kegagalan, semuanya harus jadi galat biasa (bukan crash)
    // yang bisa ditangkap `coba`/`tangkep` di modul pemanggil.
    struct Kasus {
        const char* nama;
        PetaBerkas peta;
        std::string_view sumber;
    };
    const std::vector<Kasus> kasus{
        {"nama tidak diekspor",
         {{"/satu/a.jw", "ekspor gawe ada() { bali 1; }\n"}},
         "coba { impor { zzz } saka \"./a.jw\"; }\ntangkep (e) { tulis('ditangkep'); }\n"},
        {"berkas hilang",
         {},
         "coba { impor { a } saka \"./hilang.jw\"; }\ntangkep (e) { tulis('ditangkep'); }\n"},
        {"galat di dalam modul",
         {{"/satu/a.jw", "ekspor gawe momok() { uncal \"mbocah\"; }\n"}},
         "coba { impor { momok } saka \"./a.jw\"; momok(); }\ntangkep (e) { tulis('ditangkep'); }\n"},
    };
    for (const Kasus& k : kasus) {
        CHECK_EQ(jalankan_modul(k.sumber, k.peta), std::string("ditangkep\n"));
    }
}

TEST_CASE("modul: kelas & closure diekspor sebagai nilai, bukan disalin") {
    const PetaBerkas peta{
        {"/satu/a.jw",
         "golongan Kotak { #s; wiwit(s) { iki.#s = s; } nampa sisi() { bali iki.#s; } }\n"
         "ekspor { Kotak };\n"},
    };
    // `sisi` adalah accessor `nampa`, jadi `k.sisi` sudah bernilai (memanggil
    // `k.sisi()` akan memanggil HASIL getter, seperti di JavaScript).
    CHECK_EQ(jalankan_modul("impor { Kotak } saka \"./a.jw\";\n"
                            "const k = anyar Kotak(4);\n"
                            "tulis(k.sisi);\n",
                            peta),
             std::string("4\n"));
}

TEST_CASE("kelas: getter & setter (nampa / nyetel)") {

    CHECK_EQ(jalankan("golongan P { #x; wiwit(x) { iki.#x = x; } "
                      "nampa v() { bali iki.#x; } nyetel v(n) { iki.#x = n; } } "
                      "const p = anyar P(1); p.v = 9; tulis(p.v);"),
             std::string("9\n"));
}

TEST_CASE("vm: batas frame rekursif menghasilkan galat") {
    jawa::vm::VMOptions opt;
    opt.maks_tumpukan = 200;
    jawa::vm::VM mesin(opt);
    // Rekursi tak hingga harus menghasilkan KleruRentang, bukan crash.
    const auto status = mesin.jalankan_sumber("gawe f() { bali f(); } f();", "<test>");
    CHECK(status == jawa::vm::Status::Galat);
}
