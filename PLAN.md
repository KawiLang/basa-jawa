# PLAN.md — Basa Jawa (CLI `jawa`, ekstensi `.jw`)

> Interpreter bahasa pemrograman berkata kunci Jawa, sintaks mirip JavaScript,
> diimplementasikan dalam C++20 tanpa dependensi runtime pihak ketiga.

---

## 1. Tujuan & Batasan

| Batasan | Nilai |
|---|---|
| Bahasa implementasi | C++20 (fitur C++23 hanya bila ada polyfill) |
| Dependensi runtime | **NOL** (hanya standard library + lapisan tipis `platform/`) |
| Dependensi dev | hanya untuk test (`FetchContent`, opsional/offline) |
| Target | Linux, macOS, Windows (x64 + arm64) |
| Standar minimum kompilator | GCC 13, Clang 16, MSVC 19.36, AppleClang 15 |
| Kualitas | nol warning, nol temuan sanitizer, nol crash pada input apa pun |

**Batasan yang tidak diabaikan (dari Bagian 15):** tanpa `eval`/`Function`/`with`,
tanpa UB, tanpa `abort()` senyap, tanpa menandai fase selesai bila ada test merah.

---

## 2. Peta Fase

| Fase | Isi | Status |
|---|---|---|
| 0 | Fondasi: build, arena, `Result`, diagnostik, `keywords.def`, `Value`, harness test | **selesai** |
| 1 | Lexer: token, template, regex/div, ASI, UTF-8, alias ngoko/krama | **selesai** |
| 2 | Parser & AST: seluruh konstruk, recovery, `grammar.ebnf` | **selesai** (AST + parser + recovery 100%; `grammar.ebnf` menyusul) |
| 3 | Kompiler & VM inti: scope, emitter, VM stack-based, closure/upvalue | **selesai** |
| 4 | Object model: Shape, inline cache, objek, dhaptar, prototipe, golongan | **sebagian** (objek & golongan selesai; shape/inline cache belum) |
| 5 | GC: mark–sweep presisi, handle scope, intern lemah, `--gc-stress` | **selesai** (generasi incremental belum) |
| 6 | Galat & pengecualian: hirarki `Kleru`, unwinder, stack trace, batas | **sebagian** (hirarki & batas selesai; jejak stack sumber belum) |
| 7 | Fiber, generator, `Janji`, `mengko`/`enteni`, event loop, modul ES | **selesai tanpa fiber** (D-023/D-028) |
| 8 | Pustaka standar: `Teks`, `Angka`, `Matematika`, `JSON`, `Regex`, `Tanggal`, berkas, izin | sebagian — `Regex` & `Tanggal` selesai; berkas & izin belum |
| 9 | Fitur expert: `cocog`, pipeline, tipe bertahap, optimizer, superinstruction | belum |
| 10 | Tooling: REPL, `fmt`, `ubah`, `tes`, `bench`, embedding API, native C ABI, aksara/pasaran | belum |
| 11 | Pengerasan: GC inkremental + write barrier, tuning, fuzz penuh, dokumentasi | belum |

---

## 3. Urutan Kerja & Risiko

Risiko diurutkan dari yang paling mungkin menggagalkan proyek.

| # | Risiko | Mitigasi | Fase |
|---|---|---|---|
| R1 | Rekursi C++ saat pemanggilan fungsi JS → stack overflow native | Panggilan JS→JS **tidak** memakai rekursi C++; satu loop bytecode dengan frame eksplisit. Suspensi (`enteni`/`metokake`) menyalin frame ke continuation, **tanpa fiber** (D-023, D-028) | 3, 7 |
| R2 | GC presisi salah rooting → use-after-free | Root eksplisit: stack VM, frame, upvalue, handle native RAII, akar ber-scope (`ScopedRoot`), konstanta chunk, antrian async, dan store modul. `--gc-stress` menjalankan **seluruh** suite | 5 |
| R3 | Ambiguitas parser (arrow vs kurung, destructuring, regex vs div, `cocog`) | Pratt + cover grammar + backtracking terbatas, token `newline_before` untuk ASI, tokenizer "re-scan on demand" | 2 |
| R4 | `enteni`/`metokake` butuh keluar-masuk loop C++ | Sinyal `Suspend` yang keluar dari loop `execute()`; state fiber disimpan di frame VM; titik suspensi dijamin di luar builtin native (dijaga `RAII` assertion) | 7 |
| R5 | Stack nilai yang dapat tumbuh memindahkan pointer, membatalkan rujukan | Stack nilai berupa arena yang hanya tumbuh; setiap objek yang dirujuk frame di-*pin* lewat `HandleScope`; tidak ada realloc pada lintasan quenteh | 3, 5 |
| R6 | Intern table menahan objek mati | Tabel intern menyimpan **referensi lemah** (sweep entri mati), hash dihitung ulang bila perlu | 5 |
| R7 | Codepoint aksara Jawa / passersara salah | Tabel Unicode resmi dibangkitkan oleh skrip + test yang memeriksa nama karakter; siklus pasaran diverifikasi pada tanggal acuan | 10 |
| R8 | Toolchain tidak punya GCC 13/`std::expected` | Polyfill `jawa::support::Expected` + `jawa::support::print`; preset menggunakan toolchain yang tersedia | 0 |
| R9 | Kinerja di bawah target | Benchmark dulu, ukur, baru optimasi (IC, superinstruction, rope, constant folding) | 9, 11 |

---

## 4. Metrik Kualitas (target vs nyata)

| Metrik | Target | Cara ukur |
|---|---|---|
| Jumlah test | ≥ 600 | `ctest` |
| Cakupan baris | ≥ 90% | `gcovr` (preset `coverage`) |
| Contoh golden | ≥ 30 | `tests/golden` |
| Kasus konformansi | ≥ 500 | `tests/conformance` |
| Fuzz | ≥ 10 menit/target | `fuzz/` |
| Sanitizer | bersih | preset `asan`, `ubsan`, `tsan` |
| `--gc-stress` | seluruh suite hijau | `scripts/verify.sh` |
| Warning | 0 | `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror` |
| `fib(35)` | ≤ 2,0 s | `jawa bench` |
| startup `tulis("halo")` | ≤ 30 ms | `jawa bench` |

---

## 5. Checklis Fase

### Fase 0 — Fondasi ✅
- [x] `PLAN.md`, `DECISIONS.md`
- [x] `CMakeLists.txt` + `CMakePresets.json` (debug, release, relwithdebinfo, asan, ubsan, tsan, coverage, fuzz)
- [x] `.clang-format`, `.clang-tidy`
- [x] `src/support/`: `arena`, `result`, `string_intern`, `diagnostics`, `source_map`
- [x] `src/lex/keywords.def` (satu sumber kebenaran)
- [x] `src/rt/value.h` NaN-boxing + fallback tagged union
- [x] Harness test tanpa dependensi eksternal
- [x] `jawa versi` jalan

### Fase 1 — Lexer ✅
- [x] Seluruh token termasuk angka (separator/radix/BigInt), escape, template bersarang
- [x] Disambiguasi regex vs pembagian
- [x] Flag ASI `newline_before`
- [x] UTF-8 validasi + diagnosed, bukan crash
- [x] Alias ngoko/krama dari `keywords.def` (perfect hash)
- [x] Recovery, ≤ 50 diagnostik per berkas
- [x] `jawa token`

### Fase 2 — Parser & AST ✅
- [x] Semua konstruk Bagian 4
- [x] Pratt untuk ekspresi, cover grammar untuk arrow
- [x] `SourceRange` pada setiap node, arena
- [x] Error recovery via sinkronisasi
- [ ] `docs/grammar.ebnf` konsisten dengan parser (menyusul)
- [x] `jawa ast`

### Fase 3 — Kompiler & VM inti — **SELESAI (versi minimum)**
- [x] Scope/closure analysis (upvalue ala Lox; TDZ ditunda ke Fase 5)
- [x] Bytecode emitter + disassembler (`jawa bytecode`)
- [x] VM stack-based, tanpa rekursi C++ untuk JS→JS (lihat `DECISIONS.md` D-015)
- [x] Aritmetika, kontrol alur, fungsi, closure, upvalue
- [x] `tulis`, teks, angka (format terpendek round-trip)
- [x] Kelas, pola `cocog`, `coba`/`tangkep`, pipeline, generator (lazy, diimplementasikan di Fase 7)
- [x] GC mark-and-sweep presisi + karantina + akar sementara
- [x] 11 dari 11 contoh acuan menghasilkan keluaran persis

`async`/event loop (Fase 6) dan generator suspend (Fase 7) juga sudah selesai.
Belum: TDZ, `pilih` dengan pola, hidden class (inline cache). Lihat
`STATUS.md` § "Yang BELUM".

### Fase 4 — Object model — **BELUM**
(Fase 3 memakai mode dictionary yang benar; hidden class & inline cache belum.)
- [ ] Shape + transisi + inline cache (mono/poli/mega)
- [ ] Objek, dhaptar, prototipe, class privat/statis/accessor
- [ ] Destructuring & spread
- [ ] `cocog` (Fase 9, diaktifkan di sini agar contoh lolos)

### Fase 5 — GC
- [ ] Mark–sweep presisi non-moving
- [ ] `HandleScope` RAII
- [ ] Tabel intern lemah
- [ ] `--max-memori`, `--gc-stress`, `--gc-log`

### Fase 6 — Galat
- [ ] Hirarki `Kleru` lengkap
- [ ] `coba`/`tangkep`/`pungkasan`/`uncal` + unwinder
- [ ] Stack trace presisi via source map
- [ ] Katalog pesan `messages.def` + snapshot test
- [ ] Batas langkah/tumpukan/memori

### Fase 7 — Async, generator, modul — **SELESAI tanpa fiber**
- [x] `Janji` + event loop deterministik (mikrotugas mendahului timer)
- [x] `mengko`/`enteni`, termasuk top-level `enteni`
- [x] Generator lazy `gawe*` + `metokake` (continuation, tanpa fiber — D-028)
- [x] `Wektu` (timer) + method Janji (`then`, `tangkep`)
- [x] Modul ES: cache, impor siklik, alias, `baku`, re-export (D-025 s/d D-027)
- [x] Live binding (ekspor variabel dibagi lewat sel; lihat `docs/modules.md`)
- [ ] Async generator (`gawe mengko`), `kanggo enteni`
- [ ] `Janji.all` / `race` / `anySelesai`

### Fase 8 — Pustaka standar
- [x] Regex runtime: `/pola/flag`, backtracking, kelompok tangkap & bernama,
      anggaran langkah anti-ReDoS (`docs/regex.md`)
- [x] `Tanggal`: kalender proleptis Gregorian UTC, ISO-8601, aritmetika
      (`docs/tanggal.md`) — tanpa zona waktu, dengan alasannya tertulis
- [ ] `Teks`, `Angka`, `Matematika`, `JSON`, `Peta`, `Himpunan` (versi minimum
      sudah ada, belum komprehensif)
- [ ] `konsol`, `kleru`, `proses`, `std:berkas`, `std:path`, `std:tes`, ...
- [ ] Model izin

### Fase 9 — Fitur expert
- [ ] `cocog`, `|>`, anotasi tipe bertahap 3 mode + pemeriksa statis
- [ ] Optimizer: folding, propagasi, DCE, peephole, TCO
- [ ] Superinstruction

### Fase 10 — Tooling
- [ ] REPL penuh + perintah meta
- [ ] `fmt` idempoten, `ubah` krama/ngoko round-trip
- [x] `tes` (`docs/testing.md`)
- [ ] `bench`
- [ ] Embedding API `include/jawa/jawa.h` + contoh
- [ ] Native C ABI `include/jawa/jawa_ngapi.h` + contoh + test
- [ ] `aksara_jawa`, `angka_jawa`, `pasaran`, `dina_jawa`

### Fase 11 — Pengerasan
- [ ] GC inkremental + write barrier
- [ ] Tuning, benchmark terdokumentasi
- [x] Fuzz penuh — 6 target (`fuzz_lexer`, `fuzz_parser`, `fuzz_kompilasi`,
      `fuzz_vm`, `fuzz_modul`, `fuzz_regex`) + `docs/fuzzing.md`
- [ ] Dokumentasi final + `CHANGELOG.md` + `v1.0.0`

---

## Catatan Fase 6 & 7

Kedua fase itu selesai **tanpa fiber**, padahal rencana awal menyebut fiber
sebagai syarat. Alasannya tercatat di `DECISIONS.md`:

- **D-023** — `entani` menunda rantai async dengan menyalin frame ke
  continuation (`struct Lanjutan`), bukan dengan stack C++ terpisah.
- **D-028** — generator `metokake` memakai mechanism yang **sama persis**.
  D-019 ("generator mode-eager") dicabut.
- **D-029** — konstanta setiap chunk harus di-root selama program berjalan
  (bug GC yang hanya muncul dengan `--gc-stress`).
- **D-030** — nilai yang dipop dari stack lalu dipakai selama banyak alokasi
  perlu akar ber-scope (`gc::ScopedRoot`).

Konsekuensinya `FIBER_CREATE` / `FIBER_RESUME` adalah opcode mati: ruang nama
merek itu dijaga, tapi tidak pernah dipakai.
