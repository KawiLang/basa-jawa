# CHANGELOG — Basa Jawa

Format mengikuti [Keep a Changelog](https://keepachangelog.com/) + semver.

## [0.1.0] — Fase 0, 1, dan 2 (front-end; runtime belum ada)

### Ditambahkan
- **Fase 0 — Fondasi**
  - `PLAN.md`, `DECISIONS.md`, `CHANGELOG.md`, `CMakeLists.txt`, `CMakePresets.json`
    (presets `debug`, `release`, `relwithdebinfo`, `asan`, `ubsan`, `tsan`, `coverage`,
    `fuzz`, `nonanbox`), `.clang-format`, `.clang-tidy`.
  - `src/support/`: `Arena` (bump-pointer, RAII), `Result<T,E>` (tanpa exception yang
    keluar dari front-end), `SourcePos`/`SourceRange`/`LineMap`, `DiagnosticBag` +
    katalog pesan dwibahasa `messages.def`.
  - `src/rt/value.h`: `Value` 8-byte NaN-boxing + mode fallback tagged-union 16-byte
    (`JAWA_NO_NAN_BOX`).
  - `src/rt/number.h`: parse angka ketat, format terpendek round-trip, `toRadix`,
    `toFixed`, `toPrecision`, `toExponential`.
  - Harness test sendiri (tanpa Catch2/GTest): `TEST_CASE`, `SECTION`, `CHECK*`,
    `REQUIRE*`, `CHECK_THROWS*`; 21 test case / 201 cek.
  - CLI `jawa` dengan sub-perintah: `versi`, `bantuan`, `token`, `cek`.
- **Fase 1 — Lexer**
  - Semua token: identifier Unicode (termasuk blok aksara Jawa U+A980–A9DF), angka
    (desimal, `0x`/`0o`/`0b`, separator `_`, BigInt `n`), teks dengan escape lengkap
    (`\n \t \r \b \f \v \0 \\ \' \" \` \xHH \uXXXX \u{X…}`), template literal
    multibaris/bersarang, regex dengan flag `gimsuy`, private name `#nama`.
  - Kata kunci **ngoko & krama** dari satu sumber `src/lex/keywords.def`
    (tabel hash FNV-1a + open addressing, isi kedua ejaan).
  - Disambiguasi regex vs pembagian berbasis token sebelumnya.
  - Flag ASI `baris_baru_sebelum` pada setiap token.
  - shebang, BOM UTF-8, dan teardown UTF-8 tidak valid (diagnostik, bukan crash).
  - Mode `--ketat-krama` (tolak campuran ngoko+krama).
  - CLI `jawa token` mencetak daftar token.

- **Fase 2 — Parser & AST**
  - AST lengkap dengan `SourceRange`, `static constexpr NK kKind` per node,
    dialokasikan di `Arena` (yang menjalankan destructor dengan benar).
  - Parser 5 berkas, 0 warning: statement, ekspresi (precedence climbing),
    arrow function (cover grammar), `golongan` (privat/statis/accessor/`wiwit`/
    `induk`/generator/`mengko`), modul (impor/ekspor/dinamis/re-export),
    `cocog` + pola, anotasi tipe bertahap.
  - Error recovery via sinkronisasi statement; ≤ 50 diagnostik per berkas.
  - `jawa ast` mencetak pohon AST.
  - 13 test case / 33 cek.
- **CLI**
  - `jawa versi`, `jawa bantuan`, `jawa token`, `jawa cek`, `jawa ast`, `jawa -e`.
  - `scripts/cek_contoh.py` memverifikasi 11 contoh acuan Bagian 11 ter-parse
    bersih (semua lulus).
- **Perbaikan bug yang ditemukan sanitizer**
  - use-after-free: `Lexer` mengembalikan `string_view` ke `std::vector` miliknya
    (di-*invalidate* saat reallocasi). Penyimpanan dipindah ke `std::deque` dan
    `TokenList` kini memiliki string-nya sendiri.
  - kebocoran: `Arena` tidak menjalankan destructor, sehingga buffer
    `std::vector`/`std::string` di node AST bocor. Arena kini mencatat dan
    menjalankan destructor secara terbalik.

### Status verifikasi
| Preset | Hasil |
|---|---|
| `release` | build 0 warning; `ctest` 2/2 hijau |
| `asan` (ASan+LSan) | 2/2 hijau; 0 kebocoran, 0 use-after-free |
| `ubsan` | 2/2 hijau; 0 runtime error |

(Tabel di atas untuk 0.1.0; tabel verifikasi 0.3.0 ada di `STATUS.md`.)

### Catatan
- Toolchain yang tersedia di mesin build: GCC 12.2.1 (bukan GCC 13). Semua fitur
  C++23 yang dipakai (`std::expected`, `std::print`) memiliki polyfill/alternatif
  C++20; lihat `DECISIONS.md` § Deviasi.
- Pada rilis ini runtime belum ada; program hanya bisa diperiksa sintaksnya
  (`jawa cek`). Runtime tiba pada 0.3.0 (lihat di bawah).

---

## [0.3.0] — Fase 3: kompilator bytecode, VM, GC, pustaka standar

Rilis kedua. 0.1.0 mencakup front-end (lexer + parser + AST); 0.3.0
menambahkan seluruh runtime: kompilator bytecode, VM, model objek, GC, dan
pustaka standar minimum.

### Ditambahkan

**Kompiler (AST → bytecode)**
- Resolusi scope: `emit_baca_nama`/`emit_tulis_nama` (lokal → upvalue → global),
  slot 0 dicadangkan untuk `this` di modul maupun fungsi.
- Upvalue ala Lox: `cari_upvalue` mencatat nama, resolusi dilakukan setelah body
  selesai. Nama yang ternyata global memakai sel upvalue `null` + nama di
  `Chunk::upvalue`, sehingga `GET_UPVAL` jatuh ke tabel global.
- `terusna` pada `kanggo` melompat ke bagian PEMBARUAN (bukan ke kondisi).
- Hoisting deklarasi fungsi: slot dialokasikan sebelum body dikompilasi, sehingga
  rekursi bekerja.
- Pernyataan penugasan bernilai (`x = 1` bernilai `1`).
- Ekspansi pola `cocog` ke opcode biasa (`IS_ARRAY`, `IS_OBJECT`, `SEQ`) +
  `DEF_LOCAL` untuk binding.
- Cek tipe bertahap pada parameter & nilai balik → `CEK_TIPE` / `KleruTipe`.
- Spread dhaptar: `SPREAD_PUSH` + `MAKE_ARRAY_SPREAD` (jumlah dihitung saat
  runtime karena panjang iterable tidak diketahui kompilasi).

**VM**
- Satu loop bytecode dengan frame eksplisit; pemanggilan JS→JS tanpa rekursi C++.
- Closure + upvalue (sel terbuka & tertutup) dengan `std::deque` stack.
- Kelas: `CLASS`, `DEFINE_METHOD`, `DEFINE_ACCESSOR`, `DEFINE_FIELD`, `NEW`,
  `GET_SUPER`/`SET_SUPER`, pewarisan method/accessor/konstruktor, field & method
  `statis`, getter `nampa` dijalankan saat dibaca.
- `coba`/`tangkep`/`intrigasan` dengan tabel handler per frame dan unwinder.
- `cocog` dengan pola angka, dhaptar, objek, nama, wildcard, penjaga.
- Iterator (`ITER_INIT`/`ITER_NEXT`) untuk dhaptar, teks, dan objek.
- Pipeline `|>`, `??` (`JUMP_IF_NOT_NULLISH`), short-circuit dengan lompatan
  bersyarat yang hanya MEMBATAS (peek).
- `===` / `!==` terpisah dari `==` / `!=`.
- Galat: `KleruObj` dengan `jeneng`, `pesan`, `sebab`, `jejak`, `baris`, `kolom`.
  Membaca properti `mboh`/`kosong` melempar galat seperti `TypeError`.
- Batas langkah, tumpukan, dan memori sebagai galat bahasa (bukan crash).

**GC**
- Karantina 64 alokasi terakhir (D-017) — memperbaiki use-after-free yang
  ditemukan ASan pada mode stress.
- Akar sementara untuk objek yang dibuat kompilator (D-018).
- Akuntansi memori mencakup isi `std::string` dan pertumbuhan buffer dhaptar,
  sehingga `--maks-memori` akhirnya bisa terpicu.

**Model objek**
- `nilai_sama()` untuk kunci: teks dibandingkan isinya, angka secara numerik.
- `ObyekObj::aksesor` untuk getter/setter; `ClassObj` memisahkan field instance
  dari nilai statis.
- Field privat: slot bernama `#x` (D-020).

**CLI**
- Perintah `bytecode` (cetak chunk rekursif beserta konstanta, nama, upvalue).
- Opsi runtime: `--gc-stress`, `--log-gc`, `--ketat-titik-koma`, `--ketat-krama`,
  `--maks-langkah`, `--maks-tumpukan`, `--maks-memori`.
- Perbaikan: `-e` kini berlaku untuk `run` (sebelumnya selalu jatuh ke `cek`).

**Pustaka standar**
- `jenis`, `StdAksara.angka_jawa` / `angka_arab` (blok U+A9D0..U+A9D9, minus
  U+A9CA).
- `Teks` 8 method, `Dhaptar` 8 method, `.dawa` pada dhaptar & teks.
- `nilai_ke_teks` mencetak `DuduAngka` untuk NaN.
- `VMOptions::keluaran` untuk mengarahkan `tulis` ke buffer (dipakai unit test).

**Dokumentasi**
- `README.md`, `docs/grammar.ebnf`, `docs/bytecode.md`, `docs/object-model.md`,
  `docs/gc.md`, `docs/stdlib.md`.
- `STATUS.md` diperbarui: 10 dari 11 contoh acuan menghasilkan keluaran persis.

### Status verifikasi

| Preset | Hasil |
|---|---|
| `release` | build 0 warning; `ctest` 6/6 hijau |
| `asan` (ASan+LSan) | 6/6 hijau; 0 kebocoran, 0 use-after-free |
| `ubsan` | 6/6 hijau; 0 runtime error |
| `nonanbox` (mode nilai 16-byte) | 6/6 hijau |
| `--gc-stress` | 10/10 contoh emas tetap identik |

**Pengujian**
- `tests/unit/test_runtime.cpp` (30 test, 80 cek): NaN-boxing, format angka,
  model objek, GC, eksekusi VM.
- `scripts/cek_golden.py` (+ `--gc-stress`) membandingkan keluaran `jawa run`
  dengan `tests/golden/*.out`.
- Uji `coba`/`tangkep`, `cocog`, generator, pipeline, tipe bertahap.

### Diperbaiki (bug nyata yang ditemukan saat Fase 3)

| Bug | Gejala | Akar masalah |
|---|---|---|
| NaN-boxing salah masking | semua nilai terbaca sebagai double | penanda bit 51 bertabrakan dengan `double` |
| Rekursi tak berhingga | segfault native | loop bersarang memakai stack C++ |
| Nilai setelah panggilan salah | argumen salah dibaca | loop bersarang lanjut ke frame induk |
| Closure baca sampah | nilai acak / double free | `std::vector` merealokasi pointer upvalue |
| Objek & array rusak | `undefined` / crash | `GET_PROP`/`DEFINE` salah jumlah pop |
| Teks kehilangan spasi | `${x} ngeluarke` → `ngeluarke` | spasi dilewati di dalam template literal |
| GC hidup hilang | tidak ada keluaran | `RootVisitor::visit` null |
| `anyaar Foo(1).x` salah | konstruktor jadi hasil panggilan | rantai akses memakan `(` |
| `--maks-memori` mati | tidak pernah terpicu | akuntansi hanya `sizeof(T)`, tidak termasuk isi string & buffer dhaptar |
| Mode nilai 16-byte rusak | build gagal / ODR | `JAWA_NO_NAN_BOX` hanya PRIVATE pada pustaka; test & CLI melihat definisi `Value` berbeda |

### Belum (lihat `STATUS.md`)
- `async`/`await` + event loop (contoh `asinkron.jw`).
- Generator suspend yang sesungguhnya (saat ini mode-eager).
- Hidden class & inline cache yang sesungguhnya.
- Linker modul ES, `Tanggal`, regex runtime, berkas, proses.

## [0.4.0] — Fase 6: async/await, Janji, dan loop acara

Rilis ketiga. 0.3.0 menyelesaikan runtime; 0.4.0 menutup Fase 6 sehingga
**semua 11 contoh acuan Bagian 11** menghasilkan keluaran yang persis.

### Ditambahkan

**Async tanpa fiber (D-023)**
- `src/vm/vm_async.cpp` (baru): Janji, penjadwalan, suspensi, dan resume.
- `struct Lanjutan` (continuation): menyalin frame + nilai stack, bukan stack
  C++ terpisah. Pemulihan menyalin balik lalu menjalankan loop bytecode lagi.
- `AWAIT` di loop VM:
  - Janji sudah `Slamet` -> memakai `hasil` langsung (tanpa suspend).
  - Janji `Gagal` -> melempar `hasil` sebagai galat (bisa ditangkap `coba`).
  - Janji `Menunggu` -> menunda seluruh rantai `async`; pemanggil melanjutkan
    dari instruksi setelah `CALL`.
  - Nilai selain Janji -> identitas (`enteni 5` -> `5`).
- Upvalue yang menunjuk ke stack yang disalin ditutup lebih dulu
  (`Upvalue::close`), supaya tidak ada pointer menggantung.
- `VM::panggil_async` untuk fungsi `mengko`: Janji didorong lebih dulu, frame
  dijalankan dengan `Frame::kBuangHasil` supaya Janji tetap menjadi hasil
  pemanggilan.
- Galat di dalam `mengko` menjadi **penolakan Janji** (`unwind_galat` championed
  jalur ini), bukan galat program. Penolakan tak tertangani akhirnya menjadi
  galat program, seperti unhandled rejection.
- `Frame::janji_async` & `Frame::akar_async`; `VM::dasar_async_` menandai akar
  rantai async yang sedang berjalan.
- Top-level `enteni` ditandai lewat `Chunk::await_tingkat_modul` (diisi
  kompiler), sehingga frame modul hanya menjadi akar rantai bila memang perlu.

**Loop acara (deterministik)**
- Antrean mikrotugas: penyelesaian Janji dan handler `.then`/`.tangkep`.
- Antrean timer: `Wektu.tundha(ms)`, diurutkan `tunda_ms` lalu urutan pemanggilan.
- `VM::jalankan_loop_acara` dijalankan sekali setelah kode sinkron selesai;
  mikrotugas selalu mendahului timer (meniru ECMAScript).
- Akar GC untuk antrean mikrotugas, timer, dan rantai yang disuspensi
  (termasuk closure & upvalue frame yang ditunda).

**Pustaka standar**
- `Wektu.tundha(ms)` dan `Wektu.teka()`.
- Method Janji: `then`, `tangkep`, `jenis` (`nunggu`/`slamet`/`gagal`), `hasil`.
  `then`/`tangkep` pada Janji yang sudah selesai tetap menjadwalkan handler
  sebagai mikrotugas (bukan memanggil langsung).

**Perbaikan parser**
- `mengko gawe f() { ... }` di tingkat statement kini menjadi DEKLARASI
  (sebelumnya `EkspresiStmt` berisi closure, sehingga `f` tidak pernah menjadi
  slot lokal dan `GET_GLOBAL "f"` mengembalikan `mboh`).
- Kata kunci boleh menjadi nama properti setelah `.` dan `?.` (D-024).
  Contoh: `.tangkep` (`.catch`), `.nampa`/`.nyetel`, `.bali`, `.jenis`, `.saka`.

**Dokumentasi**
- `docs/async.md` (baru): arsitektur continuation, loop acara, model Janji,
  galat async, dan penyimpangan dari JavaScript.
- `docs/bytecode.md`: `AWAIT` dipindahkan dari "belum diimplementasikan".
- `STATUS.md`, `DECISIONS.md` (D-023, D-024), `README.md` diperbarui.

### Diperbaiki (bug nyata yang ditemukan saat Fase 6)

| Bug | Gejala | Akar masalah |
|---|---|---|
| `mengko gawe f()` tak bisa dipanggil | `Ora bisa nelep nilai: dudu fungsi` | di-parse sebagai ekspresi, `f` bukan slot lokal |
| Rantai async tidak pernah dilanjutkan | `dhisik` saja, `42` hilang | `selesaikan_janji` mengosongkan `lanjutan_vm` sebelum mikrotugas memakainya |
| Loop acara tanpa henti | proses menggantung | `jalankan_mikrotugas` selalu `true` walau antrean kosong; timer tidak pernah dinyalakan |
| Handler `.tangkep` tak terpanggil | galat langsung|Program mati sebelum handler dijadwalkan; Janji yang sudah ditolak tidak menjadwalkan handler |
| Handler menerima Janji, bukan nilai | `nolak: Janji { <pending> }` | `t.nilai` diisi `turunan` pada jalur penolakan |
| `dhisik` tercetak setelah `42` | urutan acuan terbalik | frame modul selalu jadi akar async; perlu penanda `await_tingkat_modul` |
| `.tangkep` gagal di-parse | `Ngarep-arep jeneng properti sawise titik.` | kata kunci ditolak sebagai nama properti |

### Status verifikasi

| Preset | Hasil |
|---|---|
| `release` | build 0 warning; `ctest` 6/6 hijau |
| `asan` (ASan+LSan) | 6/6 hijau; 11/11 contoh bersih dengan `--gc-stress` |
| `ubsan` | 6/6 hijau; 0 runtime error |
| `tsan` | 6/6 hijau; 0 data race |
| `nonanbox` (mode nilai 16-byte) | 6/6 hijau |
| `--gc-stress` | 11/11 contoh emas tetap identik |

**Pengujian**
- `tests/unit/test_runtime.cpp`: 42 test / 92 cek (dari 30/80). Test baru:
  urutan microtask, Janji tanpa `enteni`, rantai berlapis, `.then`,
  `.then` berantai, `.tangkep` atas penolakan, `enteni` atas Janji ditolak,
  penolakan tertangkap `coba`, urutan timer, top-level `enteni`, `enteni` di
  fungsi biasa, getter/setter kelas.
- `tests/golden/asinkron.out` (baru) — contoh acuan ke-11 kini punya golden file.
- `scripts/cek_golden.py`: `SKIP` dikosongkan; 11/11 wajib cocok.

### Penyimpangan yang disengaja

- Pemanggil fungsi `mengko` menunggu sampai fungsi itu selesai, jadi
  `tulis(f())` mencetak Janji yang masih `nunggu` (lihat `docs/async.md`).
- `Wektu.tundha` hanya mengurutkan timer; tidak menunggu ms sungguhan.

---

## [0.5.0] — Fase 5: linker modul ES

Rilis keempat. 0.4.0 menutup async/await; 0.5.0 membuat program multi-berkas
benar-benar jalan. `impor`/`ekspor` sebelumnya di-parse dengan benar tetapi
`IMPORT`/`EXPORT` di VM hanya pushing objek kosong.

### Ditambahkan

**Linker modul** (`src/vm/linker.cpp`, baru)
- `VM::muat_modul`: resolusi path relatif terhadap direktori modul pengimpor,
  baca, kompilasi (lex → parse → compile), evaluasi, dan kembalikan objek ekspor.
- `VM::selesaikan_path` menormalkan path (`./` dibuang, `/` dirapatkan) sehingga
  kunci cache konsisten.
- `VM::kompilasi_modul` & `VM::evaluasi_modul` dipisah dari `jalankan_sumber`
  supaya alur boot modul utama dan modul terimpor memakai kode yang sama.
- `VMOptions::baca_berkas` — pembaca berkas yang bisa diinjeksi, supaya unit
  test menguji linker tanpa menyentuh sistem berkas dan embedder bisa
  menyuplai filesystem sendiri. Default-nya `<fstream>`.
- Cache per path kanonik: modul dievaluasi satu kali walau diimpor berkali-kali.
- Impor siklik: modul yang sedang dievaluasi mengembalikan objek ekspor
  parsial-nya, bukan menggantung.
- Cakupan global per modul via `VM::modul_tumpukan_`; `modul_aktif` adalah
  puncak tumpukan.
- `pos_berkas_` disimpan & dipulihkan di sekitar evaluasi modul, supaya pesan
  galat dan jejak stack menunjuk modul yang benar.
- Akar GC untuk seluruh `ModuleRecord` (entri, objek ekspor, `global`).

**Opcode baru**
- `GET_EXPORT` (grup `modul`): membaca nama dari objek ekspor. Berbeda dengan
  `GET_PROP`, nama yang tidak ada adalah `KleruModul` yang mencantumkan daftar
  nama yang diekspor — impor salah ketik adalah kesalahan program, bukan nilai
  kosong (D-027).

**Kompilator**
- `ekspor`: deklarasi dikompilasi normal lalu nilainya dibaca ulang ke objek
  ekspor. Berlaku seragam untuk `gawe`, `golongan`, dan `tetep`/`ana`.
- `ekspor baku <ekspresi>` menyimpan NILAI ekspresi (bukan statement-nya, yang
  akan menambah `POP`).
- `ekspor { a, b minangka c }` (+ `saka "mod"` untuk re-export), dengan pemisah
  opsional (koma, titik koma, atau spasi).
- `ekspor { x }` boleh mendahului deklarasi: entri ditunda ke akhir modul.
- Hoisting ekspor (D-026): fungsi/kelas yang diekspor dibuat di awal modul;
  slot pengikat impor dialokasikan lebih dulu.
- `impor`: `DUP` per nama, `GET_EXPORT`, `DEF_LOCAL`, lalu `POP` modulnya.
  (Bug lama: objek ekspor hanya ada satu di stack, jadi `GET_PROP` untuk nama
  kedua dan seterusnya membaca kekosongan.)

**Parser**
- `impor "path"` tanpa pengikat (impor untuk efek samping).
- `impor NAMA saka "modul"` = impor ekspor `baku` ke nama lokal (sesuai
  `daftar_impor` di `docs/grammar.ebnf`).
- `baku` diterima sebagai nama di daftar impor `impor { baku }`.
- Pemisah opsional di daftar impor (koma/titik koma/tanpa pemisah).
- Kata kunci boleh jadi nama properti setelah `.` dan `?.` (D-024) — ini yang
  membuat `.tangkep` (`.catch`) pada Janji bisa ditulis.

**Contoh & pengujian**
- `examples/modul/{matematika,bentuk,alat,utama}.jw` — contoh acuan modul ES
  dengan impor, alias, ekspor `baku`, dan re-export.
- `tests/golden/modul/*.out` + `scripts/cek_golden.py` kini memindai
  `examples/modul/`.
- 9 test unit baru di `tests/unit/test_runtime.cpp`, memakai filesystem virtual:
  impor nama/alias/namespace, ekspor `baku`, impor efek samping, evaluasi
  satu kali, impor bersarang tiga tingkat, impor siklik, re-export, ekspor
  mendahului deklarasi, kelas yang diekspor, dan tiga jenis galat impor
  (nama tidak diekspor / berkas hilang / galat di dalam modul).

### Diperbaiki (bug nyata yang ditemukan saat Fase 5)

| Bug | Gejala | Akar masalah |
|---|---|---|
| Impor kedua & seterusnya `undefined` | hanya nama pertama yang terbaca | objek ekspor tidak di-`DUP` per nama; `GET_PROP` juga mengabaikan kunci di stack |
| `ekspor` di modul utama gagal | "ekspor mung bisa digunakake ing modul" | `jalankan_sumber` tidak mendorong modul utama ke tumpukan modul & tidak membuat objek ekspornya |
| Body modul tidak pernah jalan | ekspor selalu kosong | ambang `jalankan_loop` memakai indeks STACK, bukan jumlah FRAME |
| Fungsi ter-hoist memanggil `undefined` | "dudu fungsi" saat impor siklik | upvalue ter-capture sebagai nama global karena slot impor belum dialokasikan |
| Nama salah ketik diam-diam | `undefined` tanpa penjelasan | impor memakai `GET_PROP`; butuh opcode khusus (`GET_EXPORT`) |
| `ekspor { a, b }` gagal tanpa koma | galat sintaks | parser mewajibkan koma sebagai pemisah |
| Galat impor tidak bisa ditangkap | program berhenti | `muat_modul` memakai `galat_.ada` langsung, bukan `unwind_galat` |

### Status verifikasi

| Preset | Hasil |
|---|---|
| `release` | build 0 warning; `ctest` 6/6 hijau; 51/51 unit test |
| `asan` (ASan+LSan) | 6/6 hijau; 14/14 contoh emas bersih dengan `--gc-stress` |
| `ubsan` | 6/6 hijau; 0 runtime error |
| `tsan` | 6/6 hijau; 0 data race |
| `nonanbox` (mode nilai 16-byte) | 6/6 hijau |
| `--gc-stress` | 14/14 contoh emas tetap identik |

**Metrik**: 118 opcode, 16.811 baris C++, 51 unit test / 106 cek,
14 contoh emas, 7 berkas `docs/`.

### Penyimpangan yang disengaja

- **Bukan live binding**: nilai yang diedarkan adalah nilai saat statement
  `ekspor` dievaluasi. Perubahan `const` di modul asal tidak terlihat importer.
- Hanya deklarasi fungsi/kelas yang bisa di-bootstrap lewat siklus impor.
- Path modul belum me-resolve `..` dan tidak mengikuti symlink.

---

