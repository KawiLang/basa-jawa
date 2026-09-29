# Status Basa Jawa

Dokumen ini adalah **catatan jujur** tentang apa yang sudah berjalan dan apa
yang belum. Tanggal: 29 September 2026.

Ringkas: front-end **dan** runtime sudah berjalan. 10 dari 11 contoh acuan
Bagian 11 menghasilkan keluaran yang benar. `async`/`await` dan event loop
belum ada.

## Angka

| Metrik | Nilai |
|---|---|
| Baris kode C++ (`src/` + `tests/`) | 15.445 |
| Opcode bytecode | 119 |
| Kata kunci (baris tabel) | 52 (72 ejaan ngoko+krama) |
| Pesan diagnostik | 106 |
| Uji unit | 3 berkas, 80 cek, 30 test |
| Uji emas | 10 contoh keluaran persis + 11 front-end |
| Build | Release, ASan, UBSan, dan mode nilai 16-byte — 6/6 `ctest` hijau di keempatnya |
| Dokumentasi | 5 berkas `docs/` + 4 berkas akar, 1.952 baris |

## Fase

| Fase | Isi | Status |
|---|---|---|
| 0 | Fondasi: build, `Arena`, `Result`, `SourceMap`, `DiagnosticBag`, Value, number, harness, CLI | selesai |
| 1 | Lexer | selesai |
| 2 | Parser + AST | selesai |
| 3 | Kompiler bytecode + VM + GC + pustaka standar minimum | **selesai (versi minimum)** |
| 4 | Bentuk objek: shape, hidden class, inline cache | **belum** (lihat "Yang belum") |
| 5 | Modul ES, zona mati-temporal, `super` penuh | sebagian |
| 6 | Event loop, Promise | belum |
| 7 | Fiber, generator suspend, `metokake` non-eager | belum |
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
- `gawe*` generator + `metokake` + spread `[...]`.
- Template literal, termasuk tag & bersarang.
- GC mark-and-sweep presisi dengan akar lengkap; `--gc-stress` bersih.
- CLI: `run`, `cek`, `token`, `ast`, `bytecode`, `versi`, `bantuan`, `-e`.

## Yang BELUM (jujur)

### 1. `async`/`await` + event loop — contoh `asinkron.jw` gagal

```
$ build/release/jawa run examples/asinkron.jw
KleruJinis [R002] Ora bisa nelep nilai: dudu fungsi.   (Wektu.tundha belum ada)
```

Yang perlu: `JanjiObj` (sudah ada di model objek), antrean microtask, `AWAIT`
nyata (suspensi frame), dan `Wektu`/`Timer`. Butuh fiber (Fase 7) agar
`suspensi` tidak memakai rekursi C++.

### 2. Generator mode-eager

`gawe* g() { ... }` dijalankan **sampai selesai**; semua hasil `metokake`
dikumpulkan menjadi Dhaptar. Benar untuk generator berhingga, salah untuk
generator tak berhingga. Ada pengaman 2²⁰ hasil agar program tidak menggantung
tanpa pemberitahuan. Lazy generator butuh fiber.

### 3. Bentuk objek belum: tidak ada hidden class/inline cache

`Shape` & `ShapeTable` ada dan punya transisi, tapi `VM::buat_obyek()` memakai
mode dictionary langsung. Properti dibaca dengan `ObyekObj::dict` +
`rt::nilai_sama()`. Fungsional & benar, tapi **bukan** yang dimaksud spesifikasi
(hidden class + inline cache). Lihat `docs/object-model.md`.

Field privat juga bukan privat sungguhan: disimpan sebagai slot biasa bernama
`#x`; privasi dijaga kompilator.

### 4. Modul ES baru di-parse, belum di-link

`impor`/`ekspor` bisa di-parse (AST benar) tetapi `IMPORT`/`EXPORT` di VM
belum menyelesaikan modul. Program multi-berkas tidak jalan.

### 5. Pustaka standar minimum

Ada: `tulis`, `Teks`, `Angka`, `Boole`, `jenis`, `Matematika`, `Dhaptar`
(9 method), `Teks` (8 method), `StdAksara`, `JSON.gawe_teks` (stub).
Belum: `Tanggal`, regex runtime, `Peta`/`Himpunan` komprehensif, berkas, proses.
Lihat `docs/stdlib.md`.

### 6. Bagian lain yang belum

- `pilih` (`switch`) hanya menguji kesamaan nilai; `kasus` dengan pola
  destruktur dan `baku` perlu diperluas.
- Hanya `tangkep` pertama yang dipakai sebagai handler; seleksi berdasarkan
  tipe kleru belum ada (kompilator memberi peringatan S504).
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
