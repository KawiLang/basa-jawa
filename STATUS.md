# Status Basa Jawa

Dokumen ini adalah **catatan jujur** tentang apa yang sudah berjalan dan apa
yang belum. Tanggal: 29 September 2026.

Ringkas: front-end **dan** runtime sudah berjalan. **12 dari 12** contoh acuan
Bagian 11 menghasilkan keluaran yang persis, termasuk `asinkron.jw`
(`async`/`await`), `modul/*.jw` (ES module, dengan *live binding*), dan
`generator.jw`. Yang belum: hidden class, kalender non-Gregorian, zona waktu
lokal, pustaka standar lengkap, dan tooling (REPL, `fmt`, `bench`).

## Angka

| Metrik | Nilai |
|---|---|
| Baris kode C++ (`src/` + `tests/`) | 21.260 |
| Opcode bytecode | 128 (`GET_IMPORT`, `SEL_ALIAS`, `SEL_BUAT`, `MAKE_REGEX`, `MAKE_TANGGAL`, `DEFINE_FIELD_INIT`, `DEFINE_STATIC`, `PARAM_HADAH`, `SEL_SALIN` baru) |
| Target fuzz | 6 (`fuzz_lexer`, `fuzz_parser`, `fuzz_kompilasi`, `fuzz_vm`, `fuzz_modul`, `fuzz_regex`) |
| Kata kunci (baris tabel) | 52 (72 ejaan ngoko+krama) |
| Pesan diagnostik | 90 berkode + pesan galat runtime |
| Uji unit | 3 berkas, 152 cek, 81 test |
| Uji bahasa (`jawa tes`) | 5 berkas, 284 assertion (regex, `Tanggal`, live binding modul, field kelas, pengikatan per-iterasi, ...) |
| Uji emas | 15 contoh keluaran persis (12 acuan + 3 modul) + 11 front-end |
| Build | Release, ASan, UBSan, dan mode nilai 16-byte — 8/8 `ctest` hijau di ketiganya; preset `fuzz` — 6/6 `ctest` hijau |
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

Belum: `Peta`/`Himpunan` komprehensif, berkas, proses, `Janji.all`/`race`/
`anySelesai`, I/O async, dan format tanggal bebas selain ISO-8601.
Lihat `docs/stdlib.md`, `docs/regex.md`, `docs/tanggal.md`, `docs/async.md`.

### 4. Bagian lain yang belum

- `pilih`: kasus **pola** hanya untuk `[...]` dan `{...}` (mesin yang sama
  dengan `cocog`). `kasus <Kelas>:` untuk pencocokan tipe belum ada, dan
  `pilih` sebagai ekspresi pun belum. Lihat `docs/control-flow.md`.
- Hanya `tangkep` pertama yang dipakai sebagai handler; seleksi berdasarkan
  tipe kleru belum ada (kompilator memberi peringatan S504).
- **Pengikatan per-iterasi `kanggo` SELESAI (tahap 2).** `kanggo (ana i = ...)`
  dan `kanggo (ana x saka ...)` mengikat per-iterasi lewat `SelObj` +
  `SEL_SALIN` (D-037), jadi closure tiap iterasi melihat nilai iterasinya
  sendiri. Yang **tetap** belum ada: skop blok, sehingga dua pengikat dengan
  nama sama di loop berbeda dalam satu fungsi masih saling berebut slot.
- **Parameter default: `mboh` vs argumen hilang SELESAI (tahap 2).** Opcode
  `PARAM_HADAH` membaca `Frame::n_argumen` (D-036), jadi `f(mboh)` tidak lagi
  tertukar dengan `f()`.
- `Wektu.tundha` hanya mengurutkan timer, tidak menunggu ms sungguhan
  (loop acara deterministik, lihat `docs/async.md`).
- Pemanggil fungsi `mengko` menunggu sampai fungsi itu selesai; `tulis(f())`
  mencetak Janji yang masih `nunggu`, bukan hasil akhirnya (penyimpangan dari
  JavaScript, disengaja — lihat `docs/async.md`).
- `AksesProperti` pada `Instance` mencari field secara linear.
- Error runtime tidak membawa jejak stack sumber (hanya nama fungsi).
- Batas instruksi 16-bit: fungsi dengan > 65535 instruksi tidak didukung.
- Tanpa debugger, tanpa REPL, tanpa LSP, tanpa formatter, tanpa `jawa fmt` /
  `jawa ubah`, tanpa `jawa bench`, tanpa embedding API (`include/` belum ada --
  `CMakeLists.txt` sudah menyiapkan `install(DIRECTORY include/)` tapi
  direktorinya belum dibuat). `jawa tes` **sudah ada** (`docs/testing.md`).
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
- Parameter default memakai `JUMP_IF_NOT_NULLISH`, jadi argumen yang sengaja
  dilewatkan sebagai `mboh` juga akan digantikan nilai default. Membedakan
  keduanya butuh opcode baru yang membaca `Frame::n_argumen`.
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
mode nilai 16-byte**; 15/15 contoh emas cocok termasuk mode `--gc-stress`; 11/11
contoh acuan ter-parse bersih; 284/284 assertion `jawa tes` lulus.

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
