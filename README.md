# Basa Jawa (`jawa`, `.jw`)

Bahasa pemrograman dengan sintaks JavaScript dan **seluruh** kata kunci, API,
serta pesan galat dalam Bahasa Jawa. Diterjemahkan ke C++20/23, tanpa dependensi
pustaka pihak ketiga.

```jawa
tetep jeneng = "Budi";
tulis(`Halo, ${jeneng}!`);
```

```
Halo, Budi!
```

## Status

Front-end **dan** runtime (kompiler bytecode + VM + GC + async +
pustaka standar dasar) sudah berjalan. **11 dari 11** contoh acuan Bagian 11
menghasilkan keluaran yang persis. Lihat [`STATUS.md`](STATUS.md) untuk
daftar **jelas** apa yang sudah selesai dan apa yang belum.

| Komponen | Status |
|---|---|
| Lexer (Unicode, template, regex, escape) | selesai |
| Parser + AST (kelas, pola, modul, tipe bertahap) | selesai |
| Kompiler → bytecode | selesai |
| VM (fungsi, closure, kelas, pola, try/catch, generator) | selesai |
| GC mark-and-sweep presisi | selesai |
| Pustaka standar | sebagian (`tulis`, `Teks`, `Matematika`, `Dhaptar`, `Teks`, `StdAksara`, `Wektu`, `JSON` minimum) |
| Modul ES (`impor`/`ekspor`) | parsir ada, linker belum |
| async/await + loop acara | selesai (deterministik, tanpa fiber — `docs/async.md`) |
| Regex runtime, `Tanggal`, berkas, proses | belum |

## Membangun

```bash
export PATH=/opt/rh/gcc-toolset-12/root/usr/bin:$PATH   # GCC 12.2.1
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build/release
```

Butuh **CMake ≥ 3.20** dan compiler **C++20** (direkomendasikan GCC 13 / Clang 16 /
MSVC 19.36; GCC 12 sudah cukup untuk build & test di lingkungan ini).

Preset tersedia di `CMakePresets.json`:

```bash
cmake --preset release && cmake --build --preset release
cmake --preset asan    && cmake --build --preset asan
cmake --preset ubsan   && cmake --build --preset ubsan
cmake --preset nonanbox && cmake --build --preset nonanbox   # mode nilai 16-byte
```

> Pada mesin dengan RAM kecil, build sanitizer + Debug perlu `-j 1`
> (`cmake --build --preset ubsan -j 1`); kalau tidak `cc1plus` bisa dibunuh OOM.

## Menjalankan

```bash
build/release/jawa run examples/halo.jw
build/release/jawa run -e 'tulis(1 + 2 * 3)'
build/release/jawa cek examples/halo.jw      # periksa sintaks saja
build/release/jawa token -e 'tulis(1)'     # daftar token
build/release/jawa ast -e 'gawe f() {}'    # cetak AST
build/release/jawa bytecode -e 'tulis(1)'  # cetak bytecode
build/release/jawa versi
build/release/jawa bantuan
```

Opsi runtime:

| Opsi | Arti |
|---|---|
| `--gc-stress` | memicu koleksi **setiap** alokasi (lambat, untuk menangkap bug akar) |
| `--log-gc` | cetak statistik tiap koleksi ke stderr |
| `--ketat-titik-koma` | wajib titik koma di akhir statement |
| `--ketat-krama` | larang campuran ejaan ngoko & krama |
| `--maks-langkah N` | batas jumlah instruksi (anti infinite loop) |
| `--maks-tumpukan N` | batas kedalaman frame (default 10000) |
| `--maks-memori MB` | batas memori heap; lewat = keluar dengan `KleruMemori` |

## Menguji

```bash
ctest --test-dir build/release --output-on-failure
```

Enam rangkaian uji:

| Uji | Yang diuji |
|---|---|
| `unit.test_lexer` | token, unicode, template, regex, escape, galat |
| `unit.test_parser` | AST semua bentuk statement & ekspresi |
| `unit.test_runtime` | NaN-boxing, format angka, model objek, GC, eksekusi VM |
| `golden.contoh` | 10 contoh acuan menghasilkan **keluaran persis sama** |
| `golden.contoh_gcstress` | idem, tapi tiap alokasi memicu GC |
| `golden.cek_contoh` | 11 contoh acuan ter-parse bersih oleh front-end |

Rangkaian yang sama dijalankan pada preset `release`, `asan`, `ubsan`, dan
`nonanbox` (mode nilai 16-byte).

Harness uji sendiri (`tests/harness.cpp`) supaya proyek tetap bebas dependensi.

Verifikasi tambahan:

```bash
python3 scripts/cek_golden.py            # uji emas (keluaran)
python3 scripts/cek_golden.py --gc-stress
python3 scripts/cek_contoh.py            # parse semua contoh acuan
python3 tools/check_sumber.py            # karakter terlarang pada sumber
```

## Contoh

| Berkas | Menunjukkan |
|---|---|
| `examples/halo.jw` | variabel `tetep` + template literal |
| `examples/fizzbuzz.jw` | `kanggo`/`yen`/`liyane`, operator modulo, `===` |
| `examples/closure.jw` | closure, `++` pada upvalue, argumen bertumpuk |
| `examples/golongan.jw` | kelas, privat `#`, getter `nampa`, `turunan`, `induk` |
| `examples/cocog.jw` | `cocog` dengan pola angka, dhaptar, objek + penjaga |
| `examples/generator.jw` | `gawe*` + `metokake` + spread `[...]` |
| `examples/pipeline.jw` | operator pipeline `\|>` |
| `examples/kleru.jw` | `coba`/`tangkep`/`intrigasan`, galat runtime |
| `examples/tipe.jw` | anotasi tipe bertahap, `KleruTipe` |
| `examples/angka_jawa.jw` | `StdAksara.angka_jawa` |
| `examples/asinkron.jw` | **belum didukung** (lihat `STATUS.md`) |

## Struktur

```
src/support/    Arena, Result, SourceMap, DiagnosticBag, messages.def
src/lex/        Lexer, tabel kata kunci (keywords.def)
src/parse/      Parser rekursif-desenden, AST, printer AST
src/compile/    AST -> bytecode, resolusi scope & upvalue
src/vm/         Chunk, VM, loop eksekusi, akses properti
src/rt/         Value (NaN-boxing), objek, shape, number
src/gc/         Heap mark-and-sweep, Handle/HandleScope
src/stdlib/     Pustaka standar (native functions)
src/cli/        CLI `jawa`
docs/           grammar, bytecode, object model, GC, stdlib
tests/          unit + golden
```

## Dokumentasi

| Berkas | Isi |
|---|---|
| [`docs/grammar.ebnf`](docs/grammar.ebnf) | tata bahasa EBNF lengkap |
| [`docs/bytecode.md`](docs/bytecode.md) | daftar opcode + semantik tumpukan |
| [`docs/object-model.md`](docs/object-model.md) | NaN-boxing, objek, shape, class |
| [`docs/gc.md`](docs/gc.md) | algoritma GC, akar, karantina, `--gc-stress` |
| [`docs/stdlib.md`](docs/stdlib.md) | pustaka standar yang tersedia |
| [`docs/async.md`](docs/async.md) | Janji, `enteni`, loop acara (tanpa fiber) |
| [`DECISIONS.md`](DECISIONS.md) | keputusan desain & penyimpangan dari spesifikasi |
| [`CHANGELOG.md`](CHANGELOG.md) | riwayat perubahan |
| [`STATUS.md`](STATUS.md) | status jujur: selesai / belum / cara verifikasi |

## Lisensi

MIT — lihat [`LICENSE`](LICENSE).
