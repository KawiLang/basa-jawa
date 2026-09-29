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
