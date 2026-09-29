# Status Basa Jawa

Dokumen ini adalah **catatan jujur** tentang apa yang sudah berjalan dan apa
yang belum. Tanggal: 29 September 2026.

Ringkas: front-end **dan** runtime sudah berjalan. **12 dari 12** contoh acuan
Bagian 11 menghasilkan keluaran yang persis, termasuk `asinkron.jw`
(`async`/`await`), `modul/*.jw` (ES module), dan `generator.jw`. Yang belum:
hidden class, *live binding* modul, pustaka standar lengkap, dan tooling
(REPL, `fmt`, `bench`).

## Angka

| Metrik | Nilai |
|---|---|
| Baris kode C++ (`src/` + `tests/`) | 17.808 |
| Opcode bytecode | 119 (`MARK_SPREAD` baru; `GET_EXPORT`) |
| Kata kunci (baris tabel) | 52 (72 ejaan ngoko+krama) |
| Pesan diagnostik | 106 |
| Uji unit | 3 berkas, 140 cek, 74 test |
| Uji emas | 15 contoh keluaran persis (12 acuan + 3 modul) + 11 front-end |
| Build | Release, ASan, UBSan, TSan, dan mode nilai 16-byte — 6/6 `ctest` hijau di kelimanya |
| Dokumentasi | 9 berkas `docs/` + 4 berkas akar |

## Fase

| Fase | Isi | Status |
|---|---|---|
| 0 | Fondasi: build, `Arena`, `Result`, `SourceMap`, `DiagnosticBag`, Value, number, harness, CLI | selesai |
| 1 | Lexer | selesai |
| 2 | Parser + AST | selesai |
| 3 | Kompiler bytecode + VM + GC + pustaka standar minimum | **selesai (versi minimum)** |
| 4 | Bentuk objek: shape, hidden class, inline cache | **belum** (lihat "Yang belum") |
| 5 | Modul ES, zona mati-temporal, `super` penuh | modul ES + TDZ + `pilih` pola selesai; `super` penuh belum |
| 6 | Event loop, Promise | **selesai** (loop acara deterministik; tanpa jam nyata) |
| 7 | Fiber, generator suspend, `metokake` non-eager | **generator selesai tanpa fiber** (D-028; `FIBER_*` tidak pernah dipakai) |
| 8 | Pustaka standar lengkap, `Tanggal`, regex, berkas | sebagian |
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
- CLI: `run`, `cek`, `token`, `ast`, `bytecode`, `versi`, `bantuan`, `-e`.
- Modul ES: `impor` (nama / alias / namespace / `baku` / tanpa pengikat) dan
  `ekspor` (deklarasi / `baku` / daftar nama / re-export), termasuk impor
  siklik antar-modul.
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

### 2. Modul ES: berfungsi, tapi bukan live binding & belum ada modul bawaan

`impor`/`ekspor` sudah berfungsi penuh (lihat `docs/modules.md`): impor nama,
alias, namespace, ekspor `baku`, re-export, impor bersarang, impor siklik, dan
penanganan galat. Yang belum:

- **Bukan live binding** — nilai yang diedarkan adalah nilai saat statement
  `ekspor` dievaluasi. Perubahan `const` di modul asal tidak terlihat importer.
- Modul hanya boleh ber-hoist lewat deklarasi fungsi/kelas. Dua modul yang
  saling mengimpor `const` akan melihat `mboh`.
- Belum ada modul bawaan (`std:...`); tidak ada bundling, tidak ada peta
  alias nama berkas.

### 3. Pustaka standar minimum

Ada: `tulis`, `Teks`, `Angka`, `Boole`, `jenis`, `Matematika`, `Dhaptar`
(9 method), `Teks` (8 method), `StdAksara`, `JSON.gawe_teks` (stub),
`Wektu` (`tundha`, `teka`), dan method Janji (`then`, `tangkep`, `jenis`, `hasil`).
Belum: `Tanggal`, regex runtime, `Peta`/`Himpunan` komprehensif, berkas, proses,
`Janji.all`/`race`/`anySelesai`, dan I/O async.
Lihat `docs/stdlib.md` dan `docs/async.md`.

### 4. Bagian lain yang belum

- `pilih`: kasus **pola** hanya untuk `[...]` dan `{...}` (mesin yang sama
  dengan `cocog`). `kasus <Kelas>:` untuk pencocokan tipe belum ada, dan
  `pilih` sebagai ekspresi pun belum. Lihat `docs/control-flow.md`.
- Hanya `tangkep` pertama yang dipakai sebagai handler; seleksi berdasarkan
  tipe kleru belum ada (kompilator memberi peringatan S504).
- `for (let i ...)` mengikat per-fungsi, bukan per-iterasi, jadi `i` di akhir
  loop adalah nilai iterasi terakhir (slot kompilator bersifat fungsi-wide).
- `Wektu.tundha` hanya mengurutkan timer, tidak menunggu ms sungguhan
  (loop acara deterministik, lihat `docs/async.md`).
- Pemanggil fungsi `mengko` menunggu sampai fungsi itu selesai; `tulis(f())`
  mencetak Janji yang masih `nunggu`, bukan hasil akhirnya (penyimpangan dari
  JavaScript, disengaja — lihat `docs/async.md`).
- `AksesProperti` pada `Instance` mencari field secara linear.
- Error runtime tidak membawa jejak stack sumber (hanya nama fungsi).
- Batas instruksi 16-bit: fungsi dengan > 65535 instruksi tidak didukung.
- Tanpa debugger, tanpa REPL, tanpa LSP, tanpa formatter.

## Cara memverifikasi

```bash
export PATH=/opt/rh/gcc-toolset-12/root/usr/bin:$PATH

# 1. Build (release)
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build/release

# 2. Unit + uji emas
ctest --test-dir build/release --output-on-failure

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
```

**Catatan lingkungan.** Mesin build ini punya 2 GB RAM dan 3 inti. Build
sanitizer + Debug harus memakai `-j 1` (`cmake --build build/ubsan -j 1`);
dengan paralel penuh, `cc1plus` untuk `stdlib.cpp` dibunuh OOM. Build release
aman dengan paralel penuh.

Hasil terakhir yang tercatat: **6/6 `ctest` hijau di Release, ASan, UBSan, dan
mode nilai 16-byte**; 10/10 contoh emas cocok termasuk mode `--gc-stress`; 11/11
contoh acuan ter-parse bersih.

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
