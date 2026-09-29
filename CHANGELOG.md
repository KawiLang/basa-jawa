# CHANGELOG — Basa Jawa

Format mengikuti [Keep a Changelog](https://keepachangelog.com/) + semver.

## [0.11.0] — Regex runtime & `Tanggal`

### Ditambahkan
- **Regex jadi nilai runtime.** `/pola/flag` tidak lagi `mboh`: polanya
  dikompilasi jadi program dan dicocokkan dengan mesin backtracking. Regex
  adalah nilai biasa — bisa masuk dhaptar, jadi properti, jadi argumen.

  ```
  tulis(/[0-9]+/g.ganti("a1b22c333", "#"));         // "a#b#c#"
  tulis(/(\d+)-(\d+)/.nilai("10-20"));              // ["10", "20"]
  tulis(/(?<tahun>\d{4})/.grup("2026", "tahun"));   // "2026"
  tulis(/^\w+@\w+$/.kabeh("budi@sari"));            // true
  ```

  Delapan method: `cocog`, `kabeh`, `ganti`, `pecah`, `nilai`, `grup`, `pola`,
  `flag`. Grup bisa diambil dengan nomor atau nama. `ganti` mendukung `$&`,
  `$0`..`$9`.

- **Anggaran langkah regex, dilaporkan sebagai galat.** Backtracking bersifat
  eksponensial dalam *waktu*: `(a+)+b` terhadap 40 huruf `a` punya 2^40 cabang,
  sementara kedalaman rekursinya cuma ~80 — jadi penjaga kedalaman tidak berguna.
  Mesin menghitung pemanggilan `match` dan menghentikan pencarian setelah
  200.000 langkah.Yang terjadi setelah itu adalah `KleruRegex` yang bisa
  ditangkap `coba`/`tangkep`, **bukan** jawaban "tidak cocok" yang diam-diam:
  jawaban yang salah lebih buruk daripada menggantung.

- **`Tanggal`** — nilai waktu, kalender proleptis Gregorian, **UTC saja**.
  Tanpa zona waktu, dengan alasan yang ditulis di `docs/tanggal.md`: tanpa basis
  data zona waktu yang andal, "jam berapa di sini" lebih sering salah daripada
  tidak dijawab.

  ```
  ana t = Tanggal.dari(2026, 9, 29, 14, 3, 7);
  tulis(t.ke_teks());          // "2026-09-29T14:03:07.000Z"
  tulis(t.nama_hari());        // "Selasa"
  tulis(t.selisih(t.tambah_hari(1)));  // 86400000
  ```

  Konstruktor global `Tanggal()` / `Tanggal(angka)` / `Tanggal("ISO")`, method
  statis `Tanggal.dari`, `Tanggal.ms`, `Tanggal.sekarang`, dan 19 method
  instans (`ke_teks`, komponen kalender, `nama_hari`, `nama_bulan`, aritmetika,
  perbandingan).

- Opcode baru: `MAKE_REGEX`, `MAKE_TANGGAL`.
- `rt::RegexProgram`, `rt::HasilRegex` (`src/rt/regexp.{h,cpp}`), `rt::Tanggal`
  (`src/rt/tanggal.{h,cpp}`).
- `VM::lempar_kleru` — galat dari kode native bisa sekarang ditangkap
  `coba`/`tangkep` seperti galat biasa.
- `NativeFnObj::sifat` — properti pada fungsi native, supaya konstruktor
  `Tanggal` punya method statis (`Tanggal.dari`).
- **Target fuzz `fuzz_regex`** (target ke-6): kompilasi pola dari input acak +
  pencocokan dengan invarian rentang hasil dan batas anggaran langkah. 330.000
  kasus lintas 11 benih, 0 crash.
- `tests/tes/regex.tes.jw` (88 assertion) dan `tests/tes/tanggal.tes.jw`
  (63 assertion).
- `docs/regex.md` dan `docs/tanggal.md`.

### Diperbaiki (bug nyata)
- **ReDoS tidak tertangani.** `(a+)+b` terhadap 40 huruf `a` menggantung
  selamanya. Penyebabnya dua: penghitung langkah hanya ada di wrapper `cocok`
  sedangkan rekursi memanggil `match` langsung (jadi tidak pernah bertambah),
  dan anggarannya di-*reset* untuk tiap posisi mulai — sehingga pola yang sama
  bisa menghabiskan anggaran berulang kali tanpa pernah habis.
- **Loop tak berujung pada `]`.** `atom()` mengembalikan node kosong untuk `]`
  **tanpa memakai karakternya**, sementara loop `sequentially()` hanya berhenti di
  `)` dan `|`. Pola seperti `x {1,2x]` (baca penghitung gagal, jadi `{` dianggap
  harf, lalu `]` nyasar) membuat parser menambah node tanpa henti sampai kehabisan
  memori. Ditemukan `fuzz_regex`; `]` sekarang harf biasa sesuai ECMAScript, dan
  `sequentially()` punya jaring pengaman yang menolak pola yang tidak maju.
- **Kelas karakter ternegosi terbalik.** `dalam_kelas()` mengembalikan `true`
  begitu rentang ditemukan, tanpa memperhitungkan negasi — jadi `[^abc]` cocok
  dengan `a`.
- **Grup 0 tidak pernah diisi.** `HasilRegex::awal[0]`/`akhir[0]` dibiarkan
  `npos`, sehingga `ganti()` menghitung panjang yang astronomical
  (`std::bad_alloc`).
- **Nama kelompok tangkap tidak pernah dikumpulkan.** Penyimpanannya ditulis ke
  vektor yang salah, jadi `grup(teks, "nama")` selalu mengembalikan seluruh
  pencocokan.
- **Flag `i` diabaikan.** `Kompilator` tidak menerima flag sama sekali, jadi
  `[a-c]/i` tidak pernah cocok dengan huruf besar.
- **Pembulatan pada komponen tanggal.** `detik_dalam_hari()` memakai
  `floor(x + 0.5)`; untuk milidetik negatif itu menghasilkan jam 24
  (`1969-12-31T24:00:00.999Z`) dan `detik()` yang meleset satu.
- **Teks ISO-8601 salah bulan diterima.** `Tanggal("2026-13-01")` menghasilkan
  tanggal (karena `Tanggal.dari` sengaja melakukan rollover) alih-alih `mboh`.
- **Galat native keluar dari program.** `Op::CALL` memakai `Status::Galat`
  langsung, bukan `unwind_galat`, jadi galat yang dilempar kode native
  (`vm.lempar_kleru`) tidak bisa ditangkap `coba`/`tangkep` — keluar dari
  program.
- **`unwind_galat` meruntuhkan frame pemanggil.** Dipanggil dari loop bytecode
  bersarang (native → balik ke kode Basa Jawa), `unwind_galat` mem-pop frame
  sampai habis, termasuk frame modul milik pemanggil. Akibatnya program berhenti
  di tengah jalan tepat setelah native gagal. Sekarang `VM::panggil` memasang
  lantai unwind (`VM::batas_unwind_`) selama pemanggilan.
- `nilai_ke_teks` untuk `Tanggal` menghasilkan `Tanggal(0)`; sekarang ISO-8601.

## [0.10.0] — Live binding modul ES

### Ditambahkan
- **Live binding.** `ekspor` variabel sekarang adalah *live binding*: importer
  mengikat sel yang sama dengan modul pengekspor, bukan salinan nilainya.
  Perubahan dari arah mana pun langsung terlihat di semua pengimpor, termasuk
  lewat rantai re-export.

  ```
  // penghitung.jw
  ana hitung = 0;
  ekspor { hitung };
  gawe naik(n) { hitung = hitung + n; bali hitung; }
  ekspor { naik };

  // pemakai.jw
  impor { hitung, naik } saka "./penghitung.jw";
  tulis(hitung);      // 0
  naik(5);
  tulis(hitung);      // 5   <- berubah, bukan salinan
  hitung = 100;
  tulis(naik(1));     // 101 <- perubahan dari importer juga terlihat
  ```

- `rt::SelObj` — satu nilai yang dibaca/tulis bersama. Dipakai untuk dua hal:
  variabel modul yang diekspor, dan pengikatan impor.
- Opcode baru: `GET_IMPORT`, `SEL_ALIAS`, `SEL_BUAT`. Opcode `GET_CELL` &
  `SET_CELL` yang sejak awal ada di `opcodes.def` sebagai *dead opcode* kini
  diimplementasikan.
- `Frame::impor_modul` — daftar modul yang dimuat statement `impor` pada frame
  itu, supaya `GET_IMPORT` tahu modul mana yang dibaca.
- `tests/tes/modul/` — berkas uji live binding tiga modul (`live.tes.jw`,
  `sumber.jw`, `perantara.jw`), termasuk pengikatan yang diteruskan lewat
  re-export.
- `docs/modules.md` — bagian "Live binding": semantik, cara kerja, dan apa yang
  perlu diperhatikan.

### Diperbaiki (bug nyata)

- **Penulisan tidak melewati alias.** `SET_CELL` menulis `sel->nilai` langsung,
  padahal sel pengikat impor diarahkan ke sel modul asal. Akibatnya importer
  bisa menulis, tapi modul asal dan pengimpor lain tidak pernah melihat
  perubahannya — live binding separuh jalan. Sekarang lewat `SelObj::tulis()`,
  yang menelusuri rantai alias sampai ke akar.
- **`Frame&` menggantung setelah `IMPORT`.** Opcode `IMPORT` menyimpan indeks
  modul pada `frames_.back()`, tapi `muat_modul()` mendorong lalu mem-pop frame
  modul. Referensi `f` sesudah itu menunjuk memori yang sudah dibebaskan.
  Gejalanya: impor siklik gagal secara tidak-deterministik. Semua akses frame
  sesudah `muat_modul` kini ditulis ulang lewat `frames_.back()`.

### Perubahan desain

Tiga hal harus berubah supaya live binding bisa bekerja; semuanya punya alasan
yang sama (urutan sel), dan urutannya berlawanan dengan yang terlihat:

1. **Sel pengikat impor dibuat paling awal**, sebelum statement apa pun
   dikompilasi. Kalau tidak, closure yang ter-hoist menangkap slot yang isinya
   `mboh` saat `CLOSURE` berjalan, dan `cari_atau_buat_upvalue` mengikat
   upvalue ke *slot stack* alih-alih sel. Begitu `impor` mengisi slot, upvalue
   itu membaca objek `SelObj` dan pemanggilannya gagal.
2. **`SEL_ALIAS`**, bukan penggantian slot: sel pengikat diarahkan ke sel
   pengekspor. Kalau slot-nya diganti, upvalue yang sudah terikat ke sel
   pengikat akan membaca objek sel yang salah.
3. **Akses variabel yang diekspor memakai `GET_CELL`/`SET_CELL`**, bukan
   `GET_LOCAL`/`SET_LOCAL`. Kompiler menandai nama itu lebih awal
   (`Compiler::tandai_sel_ekspor`), karena `ekspor { n }` boleh ditulis
   *setelah* `n` dipakai.

Statement `impor` juga dipindah: ia dijalankan setelah hoist ekspor, tapi
sebelum statement biasa. Ekspor harus lebih dulu supaya impor siklik
(`a impor b; b impor a`) menemukan fungsi yang sudah ter-hoist di modul lain.

### Verifikasi

| Preset | Hasil |
|---|---|
| `release` | build 0 warning; `ctest` 8/8 hijau; 93/93 assertion `jawa tes` |
| `asan` (ASan+LSan) | 8/8 hijau |
| `ubsan` | 8/8 hijau; 0 runtime error |
| `nonanbox` (mode nilai 16-byte) | 8/8 hijau |
| `fuzz` | 5/5 hijau |
| `--gc-stress` | 15/15 contoh emas + 93/93 assertion |

**Metrik**: 123 opcode (+`GET_IMPORT`, `SEL_ALIAS`, `SEL_BUAT`), 20.032 baris
C++, 3 berkas uji bahasa / 93 assertion.

---

## [0.9.0] — `jawa tes`: kerangka uji level bahasa

### Ditambahkan
- **`jawa tes`** — sub-perintah baru untuk menjalankan berkas uji `.jw`
  (lihat `docs/testing.md`).
  - Assertion dipasang sebagai global pada setiap berkas uji: `pratelas`
    (sama secara mendalam), `wajib_bener`, `wajib_salah`, `wajib_lempar`.
    `bener`/`salah` tidak bisa dipakai sebagai nama fungsi karena keduanya kata
    kunci.
  - Assertion yang gagal **tidak** menghentikan program: seluruh kegagalan di
    satu berkas dilaporkan sekaligus, lengkap dengan nomor baris.
  - `tulis` dialihkan ke buffer supaya keluaran program uji tidak mencampurkan
    laporan assertion.
  - Satu VM per berkas uji; global antar berkas tidak bocor.
  - `jawa bytecode` kini menampilkan asal setiap upvalue: lokal nenek moyang
    yang mana, upvalue nenek yang mana, atau global.
- **`tests/tes/`** — 2 berkas uji bahasa, 85 assertion: aritmetika, teks,
  dhaptar, operator logis, seluruh bentuk loop (`kanggo` klasik/saka,
  `nalika`, `lakoni`, `mandheg`, `terusna`), `yen`/`liyane`, closure & upvalue
  (termasuk bayangan nama 3 tingkat), kelas, `cocog`, penanganan galat,
  pipeline, dan zona mati-temporal.
- **`docs/testing.md`** — dokumentasi `jawa tes`, semantik perbandingan
  mendalam, dan batasannya.
- `ctest` menjalankan `tests/tes/` dua kali: biasa dan `--gc-stress`.
  Total 8 test per konfigurasi build.

### Diperbaiki (bug nyata)

Menemukan lima bug yang sudah lama ada. Semuanya muncul begitu ada uji bahasa
level yang sebenarnya; test C++ yang ada tidak menyentuhnya karena selalu
menguji potongan yang terlalu pendek.

| Bug | Gejala | Akar masalah |
|---|---|---|
| **Nomor baris selalu 1** | setiap pesan galat runtime & laporan assertion menunjuk `(1:1)` | opcode `NOP_LINE` ada di `opcodes.def` tapi **tidak pernah diterbitkan kompilator**, dan `vm_loop` membaca `Instruksi::baris` yang selalu `0`. `pos_sumber_` tidak pernah berubah |
| **Frame menggantung setelah galat** | setelah native memanggil balik ke bytecode lalu galat, program dilanjutkan dari bytecode fungsi yang **salah** | `jalankan_loop` yang gagal tidak mem-pop frame-nya (`gagal()` hanya menandai `galat_`), dan `VM::panggil` tidak membersihkannya. `frames_.back()` jadi frame yang salah |
| **Bayangan nama salah** | closure membaca variabel modul yang namanya sama, bukan variabel lokal fungsi | resolusi upvalue memindai **semua** nenek moyang dari luar ke dalam sekaligus, jadi fungsi paling luar menang atas induk langsung |
| **Upvalue rantai 3 tingkat kosong** | closure yang hanya meneruskan nilai upvalue menghasilkan `mboh` | fungsi perantara tidak mendaftarkan upvalue-nya sendiri, jadi `CLOSURE` tidak punya sel untuk diwariskan |
| **Tabel global dibagi semua VM** | program ke-2 (dan seterusnya) membaca objek dari heap VM sebelumnya | `stdlib.cpp` memakai `static Global` di dalam fungsi. Tidak terlihat selama satu proses hanya punya satu VM |

Bug "tabel global dibagi" baru muncul begitu ada `jawa tes` — berkas uji
membuat satu VM per berkas.

### Perubahan internal
- `stdlib::State` (tabel global & method) sekarang dimiliki `VM` lewat
  `VM::tabel_stdlib()`. `cari_metode_builtin` & `method_janji` menerima `VM&`.
- `rt::nilai_ke_teks_inspect` (dideklarasikan tapi tanpa definisi) diimplementasikan:
  teks diapit tanda kutip dan obyek datar ditampilkan lengkap dengan
  propertinya, supaya pesan kegagalan assertion bisa dibaca langsung.
- `VM::posisi_sumber()` & `VM::nama_berkas_aktif()` ditambahkan untuk laporan
  assertion.

### Verifikasi

| Preset | Hasil |
|---|---|
| `release` | build 0 warning; `ctest` 8/8 hijau; 85/85 assertion `jawa tes` |
| `asan` (ASan+LSan) | 8/8 hijau |
| `ubsan` | 8/8 hijau; 0 runtime error |
| `nonanbox` (mode nilai 16-byte) | 8/8 hijau |
| `--gc-stress` | 15/15 contoh emas + 85/85 assertion tetap identik |
| front-end | 11/11 contoh acuan ter-parse bersih |

**Metrik**: 19.567 baris C++, 119 opcode, 2 berkas uji bahasa / 85 assertion,
15 contoh emas, 11 berkas `docs/`.

---

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

## [0.8.0] — Fuzzing: 5 target, 6 bug nyata

### Ditambahkan
- **5 target fuzz** (`tests/fuzz/`, preset `fuzz`), masing-masing dengan dua
  bentuk: driver deterministik sendiri untuk GCC 12 (yang dipakai ctest), dan
  `LLVMFuzzerTestOneInput` untuk clang + libFuzzer.
  - `fuzz_lexer` — rentang token di dalam sumber, teks token tidak melebihi sumber
  - `fuzz_parser` — rentang program, `cetak_ast` menelusuri setiap node
  - `fuzz_kompilasi` — tujuan lompatan, `jumlah_slot`, indeks konstanta/nama, entri TDZ
  - `fuzz_vm` — batas langkah/frame, batas keluaran, filesystem virtual
  - `fuzz_modul` — grafik impor dipecah dari satu masukan lewat penanda `===MODUL:nama===`
- `tests/fuzz/jawa_fuzz.h` — PRNG splitmix64, mutasi enam jenis, potongan sintaks,
  program Jawa utuh yang menyasar jalur yang jarang terkena mutasi acak.
- `scripts/fuzz_jalankan.py` — runner dengan mode ctest (`--satuan`) dan mode
  campaign yang **memeriksa determinisme**: campaign sama dengan benih sama harus
  menghasilkan statistik sama, kalau tidak dianggap gagal.
- Case crash tersimpan otomatis ke `/tmp/jawa_fuzz_kasus.bin` (bisa ditimpa lewat
  `JAWA_FUZZ_KASUS`), sehingga crash yang tidak bisa ditangkap -- assertion glibc
  `malloc` -- tetap bisa direproduksi ulang.
- `docs/fuzzing.md`.
- 7 test case baru dari hasil fuzzing. Total 81 test / 152 cek.

### Diperbaiki
Enam bug nyata; tiga ditemukan langsung oleh fuzzer, tiga lagi oleh test
regresi yang ditulis berdasarkan temuan itu.
- **Regex tanpa penutup di akhir sumber melempar exception.** `lex_regex`
  menghitung flag dari `pola_akhir + 1`; regex yang tidak ketutup dan berhenti di
  `src_.size()` membuat `substr` mulai pada `size() + 1`. Masukan minimal `/b`
  (2 byte). [Ditemukan `fuzz_lexer`]
- **`gawe` tanpa nama lalu blok menyebabkan stack overflow.** Cabang
  `parse_deklarasi_fungsi` untuk `gawe` diikuti `[`/`{` memundurkan `idx_` ke
  posisi `gawe` lalu memanggil `parse_statement()` yang memanggil dirinya lagi.
  Masukan minimal `gawe* { }`. [Ditemukan `fuzz_parser`]
- **Tidak ada batas kedalaman rekursi parser.** Program dengan kurung bersarang
  menabrak stack alih-alih jadi galat. Sekarang dibatasi 160 tingkat lewat
  `Parser::RakKedalaman` (RAII) di `parse_statement` dan `parse_assignment`, dan
  melebihi batas jadi galat `S002` yang bisa dibaca. [Ditemukan `fuzz_parser`]
- **Parameter default tidak pernah berfungsi.** `ParamDeklarasi::nilai_default`
  di-parse sejak Fase 2 tapi tidak pernah dikompilasi; `gawe f(a, b = 2)` memberi
  `mboh`. Sekarang ada prolog `JUMP_IF_NOT_NULLISH` di awal badan fungsi.
  Penyimpangan: argumen bernilai `mboh` juga memakai default (D-036).
- **Parameter rest mengikat argumen biasa, bukan dhaptar sisa.**
  `Chunk::n_argumen_tetap` ada tapi tidak pernah diisi, jadi VM menyalin argumen
  ke slot parameter rest dan mendahulukan dhaptar sisa satu slot.
  `gawe f(a, ...sisa) { bali jenis(sisa); }` menghasilkan `angka`.
- **`kanggo (tetep x saka ...)` ditolak.** `parse_kanggo` hanya menerima `ana`
  sebagai pengikat loop. Bentuk `tetep` sekarang diterima juga.

### Catatan
- Tidak ada korpus crash yang tersimpan, dan `fuzz_parser` belum memeriksa rentang
  setiap node anak. Campaign terakhir yang dijalankan: 20.000 kasus x 5 target
  (100.000) plus 14.000 kasus di bawah ASan, 0 crash. Lihat `docs/fuzzing.md`.
- 5/5 `ctest` hijau pada preset `fuzz`; 6/6 hijau pada kelima build lain.

## [0.7.0] — `pilih` yang benar, zona mati-temporal, dan perbaikan GC

### Diperbaiki
- **`pilih` (switch) hanya menguji kasus pertama.** `Op::EQ` adalah perbandingan
  biasa yang mendorong boolean, tapi `Compiler::stmt_pilih` memperlakukannya
  sebagai lompatan bersyarat dan menambatkan target lompatan di akhir `pilih`.
  Akibatnya `pilih (2) { kasus 1: ...; kasus 2: ... }` mencetak isi kasus
  `1`, dan `baku:` tidak pernah berjalan. Codegen ditulis ulang; lihat
  `docs/control-flow.md` dan D-031.
- **Pola `...sisa` mengikat seluruh subjek**, bukan sisa elemennya -- bug lama
  yang juga memengaruhi `cocog`. Dipperbaiki dengan opcode baru `MARK_SPREAD`
  (D-032). Panjang subjek dengan `...sisa` sekarang cukup *minimal*.
- **`Heap::akar_semale` tidak pernah ditandai sebagai root.** Akar sementara
  kompilator (D-018) diisi `Compiler::tambah_konstanta` / `tambah_nama`, tapi
  `Heap::tandai_roots` hanya menandai `akar_scope_`. Semua konstanta dan
  nama-properti yang dibuat kompilator bisa tersapu **di tengah kompilasi** --
  `chunk_akar_` baru berlaku setelah `compile()` selesai. Gejalanya di
  `--gc-stress`: nilai `undefined` yang jauh dari sebabnya. Terbukti dengan
  AddressSanitizer `heap-use-after-free`.
- **`pradaftar_tdz` bentrok dengan target `kanggo (ana i = ...)`** (diperbaiki
  tahap yang sama: target memakai slot yang sudah dipra-daftarkan).

### Ditambahkan
- **Zona mati-temporal (TDZ)** untuk pengikat leksikal `ana` / `wonten` / `tetep`.
  `tulis(x); tetep x = 5;` sekarang melempar `KleruCakupan`, bukan mencetak
  `undefined`. Berlaku di modul, di badan fungsi, dan di dalam blok; bisa
  ditangkap `coba`/`tangkep`. Tanpa state per-frame: `Chunk::tdz_daftar`
  menyimpan ip deklarasi dan `TDZ_CHECK` membandingkannya dengan `Frame::ip`
  (D-033). Nol opcode untuk pembacaan setelah deklarasi.
- **Kasus pola pada `pilih`:** `kasus [a, b]:` dan `kasus {nama}:` memakai mesin
  `cocog` yang sama -- binding, wildcard `_`, pola bersarang, `...sisa`.
- Opcode baru: `MARK_SPREAD` (`- - -`), menandai tinggi stack sebagai awal
  elemen spread.
- `docs/control-flow.md` -- `pilih` (nilai, pola, `baku`) dan TDZ.
- `examples/pilih.jw` + `tests/golden/pilih.out` -- contoh acuan.
- 12 test case baru (`tests/unit/test_runtime.cpp`): 7 untuk `pilih`, 5 untuk TDZ.
  Total 74 test / 140 cek.

### Diketahui masih belum
- `kasus <Kelas>:` untuk pencocokan tipe di `pilih` belum ada (hanya di `cocog`).
- `for (let i...)` tidak mengikat per-iterasi (slot bersifat fungsi-wide).
- Modul belum punya *live binding*; `g.return()` / `g.next(x)` belum ada;
  `Janji.all`/`race` belum ada.

## [0.6.0] — Fase 7 (sebagian): generator lazy, tanpa fiber

Rilis kelima. 0.5.0 menutup linker modul ES; 0.6.0 menghapus penyimpangan
generator mode-eager. **Tidak ada fiber** — generator memakai continuation yang
sama dengan rantai `async` (D-028).

### Ditambahkan

**Generator LAZY** (`src/vm/vm_gen.cpp`, baru)
- `VM::panggil_generator` membuat objek `GeneratorObj`; body jalan sampai
  `metokake` pertama, lalu objek itu dikembalikan ke pemanggil.
- `VM::suspensi_generator` — `metokake` menyalin frame generator + nilai stack ke
  `Lanjutan`, menutup upvalue yang menunjuk ke rentang itu, lalu memangkas
  `frames_`/`stack_`. Pemanggil melanjutkan dari instruksi setelah `CALL`.
- `VM::lanjutkan_generator` — memulihkan continuation dan menjalankan loop lagi.
- `VM::langkah_generator` — dua tahap supaya satu `langkah` = satu `metokake`
  (nilai saat ini dilaporkan lebih dulu, dilanjutkan pada panggilan berikutnya).
- `FIBER_CREATE` / `FIBER_RESUME` **tidak pernah dipakai**.
- `OK::Generator` menggantikan `OK::Fiber` di model objek.

**API generator**
- `g.next()` -> `{ nilai, selesai }` (sifat JavaScript: saat selesai `nilai` =
  `mboh`, nilai `bali` tersedia lewat `g.bali`).
- `g.nilai`, `g.bali`, `g.selesai`, `g.jenis`, `jenis(g) == "generator"`.
- Spread `[...g]` menjalankan generator sampai habis (dengan pengaman 2^24 nilai
  -> `KleruWates`).
- `for..of` lewat `ITER_NEXT`; bisa dihentikan `mandheg` di tengah.
- Galat di body dilempar ke pemanggil `.next()`.

**Akar GC ber-scope** (`gc::ScopedRoot`)
- `Heap::akar_scope_push/pop` + `ScopedRoot` (RAII, LIFO) untuk nilai yang
  dipop dari stack lalu dipakai selama banyak alokasi.
- Dipakai di `SPREAD_PUSH`: tanpa ini generator yang sedang di-spread bisa
  tersapu di tengah.

**Akar konstanta chunk** (`VM::chunk_akar_`)
- Semua chunk program (modul utama + fungsi anak + modul terimpor) di-root
  selama program berjalan: konstanta & `nama_properti`-nya ditandai.

**Pengujian**
- 11 test unit baru: spread, lazy, tak berhingga + `mandheg`, `next()` +
  `selesai`, nilai `bali`, closure/upvalue saat suspend, galat di body, dua
  generator terpisah, generator tanpa `metokake`, dan dua test kebocoran stack
  untuk `yen`/`liyane`.
- `examples/generator.jw` diperluas jadi contoh acuan lengkap (6 baris keluaran).

### Diperbaiki (bug nyata)

| Bug | Gejala | Akar masalah |
|---|---|---|
| **Generator tak berhingga tidak bisa dipakai** | jalan sampai selesai / pengaman 2^20 | mode-eager (D-019) |
| **Nilai asing di awal spread generator** | `[undefined, 1, 2, 3]` | continuation dipulihkan dengan `stack_.resize(lan->slot_base)`; pemanggil sudah memakan slot itu, jadi terisi nilai sampah. Harus menggeser `slot_base`/`target_balas` |
| **Frame pemanggil dieksekusi dua kali saat resume** | bytecode mengulang | ambang `jalankan_loop(0)` pada resume; harus `frames_.size()` (generator) atau indeks akar rantai (async) |
| **`yen`/`nalika` membocorkan nilai kondisi di stack** | tidak terlihat, tapi loop panjang tumbuh tanpa batas | `POP` hanya ada di jalur "benar"; harus SATU `POP` di akhir yang dipakai kedua jalur |
| **Konstanta string fungsi anak bisa tersapu** | program membaca string yang salah, hanya dengan `--gc-stress` | `bersihkan_akar_sementara()` menghapus konstanta anak sebelum `FungsiObj`-nya ada (dibuat runtime saat `CLOSURE`) |
| **Generator yang di-spread bisa tersapu** | crash atau nilai salah dengan `--gc-stress` | nilai dipop dari `SPREAD_PUSH` tidak ada di root GC selama ribuan alokasi |
| **`case OK::Teks` jatuh ke blok `Generator`** | segfault saat GC | salah letak `break` saat menambah `case` baru |

**Catatan.** Bug `POP` di `yen`/`nalika` sudah ada sejak Fase 3 dan tidak
terlihat karena program pendek jarang membaca tumpukan. Bug konstanta hanya
muncul dalam mode `--gc-stress`, yang membuatnya jauh lebih sulit dicari daripada
gejalanya.

### Status verifikasi

| Preset | Hasil |
|---|---|
| `release` | build 0 warning; `ctest` 6/6 hijau; 62/62 unit test |
| `asan` (ASan+LSan) | 6/6 hijau; 12/12 contoh bersih dengan `--gc-stress` |
| `ubsan` | 6/6 hijau; 0 runtime error |
| `tsan` | 6/6 hijau; 0 data race |
| `nonanbox` (mode nilai 16-byte) | 6/6 hijau |
| `--gc-stress` | 14/14 contoh emas tetap identik |

**Metrik**: 118 opcode, 17.395 baris C++, 62 unit test / 117 cek, 14 contoh emas,
8 berkas `docs/`.

### Perubahan keputusan

- **D-019 (generator mode-eager) DICABUT** oleh D-028.
- **D-028**: generator lazy memakai continuation yang sama dengan async; fiber
  tidak pernah dipakai untuk apa pun.
- **D-029**: konstanta chunk di-root selama program berjalan.
- **D-030**: nilai yang dipop dari stack perlu akar ber-scope.

### Penyimpangan yang disengaja

- Tidak ada `g.return()` untuk memaksa generator berhenti dari luar; pemanggil
  harus melepaskannya.
- Spread generator tak berhingga menghabiskan space tanpa akhir (seperti
  JavaScript), dibatasi 2^24 nilai dengan `KleruWates`.
- `g.next(x)` menerima nilai tapi `metokake` belum punya bentuk kirim nilai
  keluar.

---

