# Status Basa Jawa

Dokumen ini adalah **catatan jujur** tentang apa yang sudah berjalan dan apa
yang belum. Tanggal: 29 September 2026.

Ringkas: front-end **dan** runtime sudah berjalan. **12 dari 12** contoh acuan
Bagian 11 menghasilkan keluaran yang persis, termasuk `asinkron.jw`
(`async`/`await`), `modul/*.jw` (ES module, dengan *live binding*), dan
`generator.jw`. Yang belum: hidden class, kalender non-Gregorian, zona waktu
lokal, pustaka standar lengkap, dan tooling (`bench`, `ubah`, LSP).

## Angka

| Metrik | Nilai |
|---|---|
| Baris kode C++ (`src/` + `tests/`) | 24.684 |
| Opcode bytecode | 132 (`GET_IMPORT`, `SEL_ALIAS`, `SEL_BUAT`, `MAKE_REGEX`, `MAKE_TANGGAL`, `DEFINE_FIELD_INIT`, `DEFINE_STATIC`, `PARAM_HADAH`, `SEL_SALIN`, `TRY_KLAUSUL`, `INSTAN_DARI`, `COCOK_TIPE` baru) |
| Target fuzz | 6 (`fuzz_lexer`, `fuzz_parser`, `fuzz_kompilasi`, `fuzz_vm`, `fuzz_modul`, `fuzz_regex`) |
| Kata kunci (baris tabel) | 52 (72 ejaan ngoko+krama) |
| Pesan diagnostik | 90 berkode + pesan galat runtime |
| Uji unit | 4 berkas (termasuk `test_fmt`), 450 cek, 133 test |
| Uji bahasa (`jawa tes`) | 13 berkas, 558 assertion (regex, `Tanggal`, live binding modul, field kelas, pengikatan per-iterasi, `coba`/`tangkep`, `pilih`, `cocog`, skop blok, field class, I/O berkas, ...) |
| Uji emas | 16 contoh keluaran persis (13 acuan + 3 modul) + 11 front-end + 32 formatter + 12 REPL |
| Build | Release, ASan, UBSan, dan mode nilai 16-byte — 11/11 `ctest` hijau di keempatnya; preset `fuzz` — 6/6 `ctest` hijau |
| Campaign fuzz terakhir | 330.000 kasus `fuzz_regex` (11 benih) + 3.000 kasus x 6 target lewat `fuzz_jalankan.py` — 0 crash |
| Dokumentasi | 13 berkas `docs/` + 4 berkas akar |

## Fase

| Fase | Isi | Status |
|---|---|---|
| 0 | Fondasi: build, `Arena`, `Result`, `SourceMap`, `DiagnosticBag`, Value, number, harness, CLI | selesai |
| 1 | Lexer | selesai |
| 2 | Parser + AST | selesai |
| 3 | Kompiler bytecode + VM + GC + pustaka standar minimum | **selesai (versi minimum)** |
| 4 | Bentuk objek: shape, hidden class, inline cache | **belum** (lihat "Yang belum") |
| 5 | Modul ES, zona mati-temporal, `super` penuh | modul ES (live binding) + TDZ + `pilih` pola selesai; `super` penuh belum |
| 6 | Event loop, Promise | **selesai** (loop acara deterministik; tanpa jam nyata) |
| 7 | Fiber, generator suspend, `metokake` non-eager | **generator selesai tanpa fiber** (D-028; `FIBER_*` tidak pernah dipakai) |
| 8 | Pustaka standar lengkap, `Tanggal`, regex, berkas | `Tanggal` (UTC) + regex (backtracking, anggaran langkah) selesai; sisanya sebagian |
| 9 | FFI, JIT, threading | belum |
| 10 | Incremental/generational GC | belum |
| 11 | Pustaka pihak ketiga, audit keamanan | belum |

## Yang bisa dilakukan sekarang

Bahasa yang berjalan penuh, termasuk:

- Semua bentuk statement: `yen`/`liyane`/`liyane yen`, `nalika`, `lakoni`, `kanggo`
  (klassik, `saka`, `ing`), `pilih`/`kasus`, `coba`/`tangkep`/`intrigasan`, `uncal`,
  `mandheg`, `terusna`, `bali`, `debugger`.
- Fungsi deklaratif, arrow (`(a, b) => a + b`, `x => x`), parameter default & rest.
- Closure, upvalue (sel terbuka & tertutup), rekursi, `tindo` tak hingga
  menghasilkan `KleruRentang`.
- Kelas: privat `#`, konstruktor `wiwit`, getter/setter `nampa`/`setel`, `turunan`,
  `induk` (super method & setter), field & method `statis`, blok statis.
- Pola `cocog`: literal, `dhaptar`, `obyek`, nama, wildcard, alternatif `|`,
  penjaga `yen`, rest `...`.
- Operator pipeline `|>`, `??`, `lan`/`utawa` (short-circuit), `saka`/`ing`.
- `bener`/`salah`/`kosong`/`mboh` sebagai tipe tersendiri.
- Anotasi tipe bertahap pada parameter & nilai balik → `KleruTipe`.
- `gawe*` generator LAZY: `metokake` menunda generator (bukan menjalankan
  sampai habis), jadi generator tak berhingga bisa dipakai. `.next()` ->
  `{nilai, selesai}`, spread `[...]`, `for..of`, dan `mandheg` di tengah.
- `mengko gawe` (async) + `enteni` (await) dengan loop acara deterministik:
  Janji, `.then`/`.tangkep`, `Wektu.tundha`, top-level `enteni`, dan galat
  yang menjadi penolakan Janji. Tanpa fiber (lihat `docs/async.md`).
- Template literal, termasuk tag & bersarang.
- GC mark-and-sweep presisi dengan akar lengkap; `--gc-stress` bersih.
- CLI: `run`, `cek`, `token`, `ast`, `bytecode`, `tes`, `versi`, `bantuan`, `-e`.
- `jawa tes` — kerangka uji level bahasa: assertion (`pratelas`, `wajib_bener`,
  `wajib_salah`, `wajib_lempar`) dipasang sebagai global, seluruh kegagalan di
  satu berkas dilaporkan sekaligus, laporan menyebut nomor baris. Lihat
  `docs/testing.md`.
- **Regex runtime**: `/pola/flag` jadi nilai, bukan `mboh`. Mesin backtracking
  dengan kelompok tangkap, kelompok bernama, kelas karakter, shorthand, flag
  `g i m s y u`, dan **anggaran langkah** yang melaporkan pola patologis
  (`(a+)+b`) sebagai galat yang bisa ditangkap, bukan menggantung dan bukan
  diam-diam menjawab "tidak cocok". Lihat `docs/regex.md`.
- **`Tanggal`**: kalender proleptis Gregorian **UTC saja** (tanpa zona waktu --
  alasannya di `docs/tanggal.md`), konversi ke/dari teks ISO-8601, komponen
  kalender, dan aritmetika `tambah_ms`/`tambah_hari`/`selisih`.
- Fuzzing 6 target (`preset fuzz`) dengan driver deterministik yang bisa jalan
  tanpa libFuzzer; sudah menemukan 7 bug nyata, lihat `docs/fuzzing.md`.
- Modul ES: `impor` (nama / alias / namespace / `baku` / tanpa pengikat) dan
  `ekspor` (deklarasi / `baku` / daftar nama / re-export), termasuk impor
  siklik antar-modul dan **live binding** (variabel yang diekspor dibagi lewat
  sel yang sama).
- `pilih` dengan kasus nilai (`==`), kasus **pola** (`[a, ...sisa]`,
  `{nama}`, wildcard `_`, pola bersarang), dan `baku` sebagai cadangan.
- Zona mati-temporal: `ana`/`wonten`/`tetep` tidak bisa dibaca sebelum
  deklarasinya dievaluasi, di modul maupun di badan fungsi, dan galatnya bisa
  ditangkap `coba`. Lihat `docs/control-flow.md`.

## Yang BELUM (jujur)

### 1. Bentuk objek belum: tidak ada hidden class/inline cache

`Shape` & `ShapeTable` ada dan punya transisi, tapi `VM::buat_obyek()` memakai
mode dictionary langsung. Properti dibaca dengan `ObyekObj::dict` +
`rt::nilai_sama()`. Fungsional & benar, tapi **bukan** yang dimaksud spesifikasi
(hidden class + inline cache). Lihat `docs/object-model.md`.

Field privat juga bukan privat sungguhan: disimpan sebagai slot biasa bernama
`#x`; privasi dijaga kompilator.

### 2. Modul ES: live binding selesai, modul bawaan belum

`impor`/`ekspor` sudah berfungsi penuh (lihat `docs/modules.md`): impor nama,
alias, namespace, ekspor `baku`, re-export, impor bersarang, impor siklik,
penanganan galat, dan **live binding** — variabel yang diekspor disimpan
sebagai sel yang dibagi, sehingga perubahan dari arah mana pun langsung terlihat
di semua pengimpor (termasuk lewat rantai re-export). Yang belum:

- Belum ada modul bawaan (`std:...`); tidak ada bundling, tidak ada peta
  alias nama berkas.
- **`Peta`/`Himpunan`/`Janji.all` SELESAI (tahap 6).** `PetaObj` &
  `HimpunanObj` sudah ada sebagai kelas runtime sejak lama tapi tidak pernah
  bisa dibuat dari kode Basa Jawa — tidak ada constructor global dan
  `VM::ambil_properti` tidak punya cabangnya. Sekarang bisa, plus
  `Janji.all`/`race`/`selesai`/`tolak`. Lihat `docs/stdlib.md`.
- **`anyaar` pada fungsi native SELESAI (tahap 6).** `NEW` hanya menangani
  `Golongan`, jadi `anyaar Tanggal(0)` menghasilkan `mboh`. Sekarang
  `anyaar` pada native/closure diperlakukan sebagai pemanggilan biasa.

### 3. Pustaka standar: regex & `Tanggal` sudah, sisanya belum

Ada: `tulis`, `Teks`, `Angka`, `Boole`, `jenis`, `Matematika`, `Dhaptar`
(9 method), `Teks` (8 method), `StdAksara`, `JSON.gawe_teks` (stub),
`Wektu` (`tundha`, `teka`), method Janji (`then`, `tangkep`, `jenis`, `hasil`),
regex runtime (8 method, lihat `docs/regex.md`), dan `Tanggal` (konstruktor + 3
method statis + 19 method instans, lihat `docs/tanggal.md`).

Yang **hanya ada sebagian**, dan sebaiknya dibaca sebagai batasan nyata:

- **Regex byte-oriented.** `.` dan kelas karakter mencocokkan byte, bukan titik
  kode, jadi `/^.$/` tidak cocok `"é"`. Flag `u` diterima dan disimpan tapi belum
  mengubah apa pun. Lookahead/lookbehind, backreference, dan kuantifier
  possessif ditolak eksplisit. Tidak ada mode `n` dan tidak ada `\p{...}`.
- **`Tanggal` tanpa zona waktu.** Tidak ada konversi ke/from waktu lokal, tidak
  ada daylight saving, tidak ada kalender selain Gregorian (Rejrah/Saka dan
  Hijriah tidak ada).

Sudah ditambahkan pada tahap 6-7: `Peta` (11 method, termasuk iterasi `kanggo ... saka`
yang menghasilkan pasangan), `Himpunan` (7 method, iterable), dan
`Janji.all`/`race`/`selesai`/`tolak`/`anySelesai`. Pada tahap 12: I/O berkas
(`Berkas` + 6 method, `baca_berkas`/`tulis_berkas`/`ada_berkas`/`ukuran_berkas`/
`hapus_berkas`, dan `ada_direktori`/`dadi_direktori`/`hapus_direktori`), blocking
& sinkron.

Belum: proses, I/O async, `Janji.bungkus`, dan format tanggal bebas selain
ISO-8601.
Lihat `docs/stdlib.md`, `docs/regex.md`, `docs/tanggal.md`, `docs/async.md`.

### 4. Lima bug yang ditemukan lewat uji tahap 7 (SEMUA sudah diperbaiki)

Tahap 7 menambah `tests/tes/upvalue.tes.jw`. Semuanya **bug stack**, bukan bug
nilai: program sering tetap "jalan", hanya dengan hasil yang salah atau berhenti
lebih awal. Uji lama tidak menangkapnya karena semua yang terpengaruh berada
di dalam badan fungsi atau loop bersarang -- dua bentuk yang belum pernah diuji.

1. **`emit_tulis_nama_statement` tidak konsisten antar opcode.** `SET_LOCAL`
   consuming (`v - >`), tapi `SET_CELL`, `SET_UPVAL`, dan `SET_GLOBAL`
   menyisakan nilainya (`v - > v`). `POP` ditambahkan hanya untuk `SET_GLOBAL`,
   jadi jalur upvalue/sel menumpuk satu nilai per penugasan.
2. **`DUP` ganda di jalur upvalue/global.** Penugasan adalah ekspresi
   (`x = 1` bernilai 1), jadi pemanggil menambahkan `DUP`. Padahal `SET_UPVAL`
   sudah menyisakan nilai -- hasilnya satu nilai lebih per penugasan. Sekarang
   `emit_tulis_nama` dijamin efek bersih `v - -` untuk semua jalur.
3. **Jalur keluar `kanggo ... saka` kurang satu `POP`.** `ITER_NEXT` mendorong
   hasil DAN flag; `JUMP_IF_FALSE` hanya mengintip. Jadi ada 4 nilai di stack
   (flag, hasil, indeks, iterable) dan hanya 3 yang dibuang. Sisanya menutupi
   nilai `ITER_NEXT` milik loop **luar**, jadi loop bersarang hanya menghasilkan
   separuh iterasi: `kanggo (a saka [1,2]) { kanggo (b saka [1,2]) { ... } }`
   memberi 2 baris, bukan 4.
4. **`mandheg`/`terusna` melewati `POP` kondisi `yen`/`nalika`.** Kedua statement
   itu menaruh satu `POP` di akhir yang dipakai kedua jalur, jadi nilai kondisi
   tetap di stack sepanjang badan. `mandheg` melompatinya, dan `POP` di jalur
   keluar loop memakan nilai yang salah.
5. **`coba_arrow` memakan `{` dua kali.** `makan(Tok::LBrace)` sudah mengonsumsi
   `{`, lalu `parse_blok()` memanggil `aspek_ke_close(Tok::LBrace)` yang juga
   mengonsumsi. Akibatnya statement pertama di badan arrow selalu gagal:
   `(v) => { kanggo (...) { ... } }` menghasilkan `Ngarep-arep "{" nanging nemu
   "kanggo"`. Arrow dengan badan blok praktis tidak bisa dipakai.

Tiga bug kecil lain ikut tertutup:

- **`kasus _:` pada `pilih`** dibaca sebagai perbandingan dengan variabel `_`,
  bukan wildcard, jadi `pilih` diam-diam tidak menjalankan body apa pun.
- **`Peta`/`Himpunan` tidak bisa diiterasi** dengan `kanggo ... saka`
  (`ITER_NEXT` tidak punya cabangnya), dan entri yang sudah dihapus membuat
  iterasi berhenti di tengah, bukan dilewati.
- **Janji turunan `.then` bisa tersapu GC.** `Then::asli` tidak di-root setelah
  `then_daftar` dikosongkan, dan `panggil` di dalam `jalankan_mikrotugas` bisa
  memicu koleksi. (ASan: heap-use-after-free; hanya muncul di bawah
  `--gc-stress`.)

### 5. Bagian lain yang belum

- **`cocog` bertipe SELESAI (tahap 5).** `kasus n: Tipe => ...` sekarang benar
  diuji: nama builtin (`teks`, `angka`, `dhaptar`, `peta`, ...) lewat opcode
  `COCOK_TIPE`, class kapital (`Kucing`) lewat `INSTAN_DARI` (serta seluruh
  induknya), plus `Tipe | U`, `T?`, `dhaptar<T>`, dan `{ ... }`. Yang
  **tetap** tidak dinilai: referensi generics (`Janji<angka>`) dan tipe fungsi.
- `pilih`: kasus **pola** hanya untuk `[...]` dan `{...}` (mesin yang sama
  dengan `cocog`), dan `pilih` sebagai ekspresi belum. Kasus **tipe**
  (`kasus <Kelas>:`) **sudah ada** (D-039) — kenali dari huruf kapital, dan
  cocok untuk instans class itu maupun semua induknya. Lihat
  `docs/control-flow.md`.
- ~~Pola `cocog` bertipe belum dievaluasi~~ — **SELESAI (tahap 5)**, lihat di atas.
- **`tangkep` bertipe & `pungkasan` SELESAI (tahap 3).** Banyak klausula
  `tangkep` dengan seleksi tipe kleru (`tangkep (Kleru)`, `tangkep (e:
  KleruJenis)`), klausula yang tidak cocok meneruskan galat ke `coba` luar, dan
  `pungkasan` jalan di ketiga jalur. Lihat D-038 & `docs/control-flow.md`.
  Yang **tetap** belum: `pungkasan` juga belum jalan kalau `uncal` di dalam
  `pungkasan` (galat dari `pungkasan` menggantikan galat asli tanpa
  penggabungan).
- **Kasus tipe `pilih` & `bali`/`pungkasan` SELESAI (tahap 4).**
  `kasus <Kelas>:` mengenali pencocokan tipe lewat opcode `INSTAN_DARI`
  (menelusuri rantai `induk`), dan `bali` di dalam `coba` kini menyalakan
  `pungkasan` lebih dulu. Lihat D-039 & `docs/control-flow.md`.
- **Pengikatan per-iterasi `kanggo` SELESAI (tahap 2).** `kanggo (ana i = ...)`
  dan `kanggo (ana x saka ...)` mengikat per-iterasi lewat `SelObj` +
  `SEL_SALIN` (D-037), jadi closure tiap iterasi melihat nilai iterasinya
  sendiri.
- **Skop blok SELESAI (tahap 9, D-040).** Pengikut leksikal punya masa hidup
  bloknya: dua blok sibling, dua loop, atau dua kasus `pilih` boleh memakai nama
  yang sama tanpa saling berebut slot. Pengikut di dalam loop memakai sel
  per-iterasi seperti variabel loop. Dua pengikut dengan nama sama di skop yang
  SAMA kini jadi galat `S401` (d dulu ditoleransi pelan-pelan, dan variabelnya
  membeku di nilai deklarasi pertama). Pembacaan nama yang hanya ada di skop
  yang sudah tertutup jatuh ke global -- bukan galat kompilasi, sesuai cara
  bahasa ini memperlakukan nama yang tidak dikenal.
- **Parameter default: `mboh` vs argumen hilang SELESAI (tahap 2).** Opcode
  `PARAM_HADAH` membaca `Frame::n_argumen` (D-036), jadi `f(mboh)` tidak lagi
  tertukar dengan `f()`.
- **Galat aritmetika bisa ditangkap (tahap 3).** `1 + "a"` dulu menulis
  `galat_.ada` langsung, tanpa `unwind_galat`, sehingga `coba` tidak pernah
  menangkapnya — termasuk dari dalam fungsi yang dipanggil. Sekarang
  membentuk `KleruObj` dulu (D-038). Galat internal VM (instruksi rusak,
  langkah maksimum) sengaja tetap tidak bisa ditangkap.
- `Wektu.tundha` hanya mengurutkan timer, tidak menunggu ms sungguhan
  (loop acara deterministik, lihat `docs/async.md`).
- Pemanggil fungsi `mengko` menunggu sampai fungsi itu selesai; `tulis(f())`
  mencetak Janji yang masih `nunggu`, bukan hasil akhirnya (penyimpangan dari
  JavaScript, disengaja — lihat `docs/async.md`).
- `AksesProperti` pada `Instance` mencari field secara linear.
- Error runtime tidak membawa jejak stack sumber (hanya nama fungsi).
- Batas instruksi 16-bit: fungsi dengan > 65535 instruksi tidak didukung.
- Tanpa debugger, tanpa LSP, tanpa `jawa ubah` (refactor), tanpa `jawa bench`,
  tanpa embedding API (`include/` belum ada -- `CMakeLists.txt` sudah menyiapkan
  `install(DIRECTORY include/)` tapi direktorinya belum dibuat). `jawa tes`,
  `jawa fmt`, dan `jawa repl` **sudah ada** (`docs/testing.md`, `docs/fmt.md`,
  `docs/repl.md`).
- Tidak ada korpus crash fuzzing yang tersimpan, dan `fuzz_parser` belum
  memeriksa rentang setiap node anak (butuh penelusur AST per-jenis-node).
  Lihat `docs/fuzzing.md`.
- **Field kelas berinisialisasi SELESAI (tahap 1).** `y = <ekspresi>`,
  `#x = <ekspresi>`, dan `statis x = <ekspresi>` kini dievaluasi: instance
  lewat closure `inisial_field` (`DEFINE_FIELD_INIT`, dijalankan di `NEW`
  sebelum `wiwit`, induk-ke-anak), statis lewat `DEFINE_STATIC` sekali saat
  definisi. Catatan lama di bawah ini sudah tidak berlaku — diverifikasi
  `tests/tes/bahasa.tes.jw` (+7 assertion) + `--gc-stress` + ASan/UBSan:
  - ~~`golongan A { #x = 1; ... }` -> `iki` undefined~~ (salah ketik `anyaar`
    di laporan; kata kuncinya `anyar`).
  - ~~field publik -> stack overflow~~ (tidak terreproduksi pada `165cae9`;
    yang nyata: inisialisasi diabaikan -> `mboh`).
  - ~~getter tidak terdaftar~~ (tidak terreproduksi; `nampa` bekerja).
- **Bug arena laten DIPERBAIKI (tahap 1).** `Arena::allocate` memakai offset
  blok lama untuk blok baru, menulis lewat akhir blok bila berkas cukup besar
  untuk butuh blok kedua (meledak tepat saat `bahasa.tes.jw` + 5 baris field
  ditambahkan; ASan: `unknown-crash` di `Arena::create`). Kini offset blok
  baru selalu 0, dan `bytes_used_` ikut dihitung.
- ~~Parameter default memakai `JUMP_IF_NOT_NULLISH`~~ — **SELESAI (tahap 2)**,
  lihat Opcode `PARAM_HADAH` di atas.
- Skor cakupan belum diukur (preset `coverage` ada, tapi belum ada ambang
  90% yang dijaga).

## Cara memverifikasi

```bash
export PATH=/opt/rh/gcc-toolset-12/root/usr/bin:$PATH

# 1. Build (release)
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build/release

# 2. Unit + uji emas + uji bahasa
ctest --test-dir build/release --output-on-failure
jawa tes tests/tes/

# 3. Sanitizer: ASan + UBSan harus sama-sama hijau
cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=Debug -DJAWA_SANITIZER=address -DCMAKE_CXX_COMPILER=g++
cmake --build build/asan && ctest --test-dir build/asan --output-on-failure

cmake -S . -B build/ubsan -DCMAKE_BUILD_TYPE=Debug -DJAWA_SANITIZER=undefined -DCMAKE_CXX_COMPILER=g++
cmake --build build/ubsan && ctest --test-dir build/ubsan --output-on-failure

# 4b. Mode nilai 16-byte (tanpa NaN-boxing; untuk platform dengan LA57)
cmake -S . -B build/nonanbox -DCMAKE_BUILD_TYPE=Release -DJAWA_NAN_BOXING=OFF -DCMAKE_CXX_COMPILER=g++
cmake --build build/nonanbox && ctest --test-dir build/nonanbox --output-on-failure

# 4. GC stress: seluruh contoh harus tetap sama
python3 scripts/cek_golden.py --gc-stress

# 5. Front-end: 11 contoh acuan ter-parse bersih
python3 scripts/cek_contoh.py

# 6. Kode sumber bebas karakter terlarang
python3 tools/check_sumber.py

# Fuzzing (butuh preset `fuzz`)
cmake --preset fuzz -DCMAKE_CXX_COMPILER=g++
cmake --build build/fuzz -j 1
python3 scripts/fuzz_jalankan.py --batas 20000
```

**Catatan lingkungan.** Mesin build ini punya 2 GB RAM dan 3 inti. Build
sanitizer + Debug harus memakai `-j 1` (`cmake --build build/ubsan -j 1`);
dengan paralel penuh, `cc1plus` untuk `stdlib.cpp` dibunuh OOM. Build release
aman dengan paralel penuh.

Hasil terakhir yang tercatat: **8/8 `ctest` hijau di Release, ASan, UBSan, dan
mode nilai 16-byte**; 16/16 contoh emas cocok termasuk mode `--gc-stress`; 11/11
contoh acuan ter-parse bersih; 558/558 assertion `jawa tes` lulus.

## Pelajaran rekayasa

Tujuh pelajaran yang menghasilkan bug nyata di proyek ini:

1. **NaN-boxing butuh penanda yang tidak bisa ditabrak.** Bit 51 + tag 3-bit
   tabrakan dengan `double` biasa. Penanda harus bit tanda. Respek uint8_t
   `kTagMask` yang hanya menutupi sebagian medan.
2. **Rekursi C++ di jalur panas berbahaya.** Loop bytecode yang memanggil
   `jalankan_loop()` secara bersarang membuat pemanggilan JS→JS memakai stack
   C++; rekursi tak hingga jadi crash native. Sekarang frame eksplisit.
3. **`std::vector` untuk stack merusak upvalue.** `push_back` merealokasi
   memori, sehingga pointer ke slot stack (sel upvalue) menggantung. Pakai
   `std::deque`.
4. **Alokasi memicu koleksi sebelum pemanggil sempat menginisialisasi.** Mode
   stress menyingkapnya. Karantina 64 alokasi terakhir menyelesaikannya.
5. **Nilai yang belum di-root mudah terlupa.** Konstanta kompilasi, tabel
   stdlib, prototype, dan handle native — semuanya harus punya akar; satu yang
   lupa membuat program salah, bukan crash.
6. **Ekspresi penugasan harus bernilai.** `x = 1` bernilai `1`;
   `SET_PROP` sudah menyisakan nilai sehingga tidak perlu `DUP` tambahan.
   `GET_PROP`/`DEFINE` yang salah jumlah pop menghasilkan program yang
   "bekerja" tapi salah.
7. **Perbedaan simbolik vs bitwise pada kunci.** `Value::operator==` membandingkan
   bit; dua `TeksObj` dengan isi sama adalah berbeda. Kunci properti perlu
   `rt::nilai_sama()`.
