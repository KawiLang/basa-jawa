# Fuzzing

Spesifikasi Definition of Done meminta **5 target fuzz**. Semuanya ada di
`tests/fuzz/`, dibangun oleh preset `fuzz`, dan dijalankan lewat
`scripts/fuzz_jalankan.py`.

Yang ditemukan fuzzing semuanya nyata dan sudah diperbaiki -- lihat bagian
"Temuan" di bawah.

---

## 5 target

| Target | Tahap | Yang diuji | Invarian |
|---|---|---|---|
| `fuzz_lexer` | teks → token | pemindaian, escape, template, regex | rentang token di dalam sumber; teks token tidak melebihi sumber |
| `fuzz_parser` | token → AST | recursive descent, cover grammar, pola, recovery | rentang program; `cetak_ast` menelusuri setiap node tanpa crash atau output tak terbatas |
| `fuzz_kompilasi` | AST → bytecode | patch lompatan, alokasi slot, konstanta | tujuan lompatan dalam kode; `jumlah_slot` cukup; `KONSTAN`/`GET_PROP` dalam pool; entri TDZ dalam kode |
| `fuzz_vm` | program → eksekusi | loop bytecode, closure, generator, async, Galat | berhenti dalam batas langkah/frame; keluaran dalam batas wajar; filesystem virtual (tidak menyentuh disk) |
| `fuzz_modul` | grafik impor | linker, impor siklik, re-export, impor namespace | sama seperti `fuzz_vm`, plus modul virtual dipecah dari satu masukan memakai penanda `===MODUL:nama===` |

Setiap target punya bentuk ganda:

- **GCC 12** (toolchain proyek): driver mandiri di `jawa_fuzz.h` --
  PRNG splitmix64 deterministik, korpus seed + mutasi, tanpa dependensi.
- **clang + libFuzzer**: `-DJAWA_FUZZ_LIBFUZZER` mendefinisikan
  `LLVMFuzzerTestOneInput`, jadi berkas yang sama langsung bisa dipakai
  `clang -fsanitize=fuzzer` lewat `-DJAWA_FUZZ_LIBFUZZER=ON`.

Alasan ada driver mandiri dicatat di `DECISIONS.md`: libFuzzer hanya ada di
clang, sementara lingkungan ini hanya menyediakan GCC 12.

---

## Menjalankan

```bash
export PATH=/opt/rh/gcc-toolset-12/root/usr/bin:$PATH
cmake --preset fuzz -DCMAKE_CXX_COMPILER=g++
cmake --build build/fuzz -j 1

# campaign: korpus seed dari examples/, plus cek determinisme
python3 scripts/fuzz_jalankan.py --batas 20000

# satu target saja
python3 scripts/fuzz_jalankan.py --target fuzz_vm --batas 50000

# reproducible dengan benih tetap
python3 scripts/fuzz_jalankan.py --batas 5000 --benih 12345

# mode ctest: cepat, tanpa korpus
python3 scripts/fuzz_jalankan.py --satuan --batas 400

# reproduksi satu kasus yang tersimpan
./build/fuzz/fuzz_lexer /tmp/jawa_fuzz_kasus.bin
```

`ctest --test-dir build/fuzz` menjalankan kelima target dengan batas 400 kasus.

### Reproduksibilitas

Setiap campaign menerima `--batas=N` dan `--benih=N`. Skrip runner menjalankan
campaign yang sama **dua kali** dengan benih sama dan menganggap statistik yang
berbeda sebagai kegagalan -- kalau hasilnya tidak reproducible, "lolos" tidak
berarti apa-apa.

Driver juga menulis input yang sedang diuji ke `/tmp/jawa_fuzz_kasus.bin`
(sebelum menjalankannya) supaya crash yang tidak bisa ditangkap -- misalnya
assertion glibc `malloc` -- tetap punya berkas yang bisa langsung dipakai ulang.
Lokasinya bisa ditimpa lewat `JAWA_FUZZ_KASUS`.

---

## Mutasi

Byte acak hampir selalu gagal di token pertama, jadi tidak pernah menjangkau
parser atau VM. Generator mutasi karena itu:

- **75%** mutasi dari seed, **15%** seed apa adanya, **10%** byte acak.
- Enam jenis mutasi: balik bit, ganti byte, sisip potongan, hapus rentang,
  duplikasi rentang, sisip program Jawa utuh.
- `kPotongan` berisi potongan sintaks (`=>`, `?.`, `...`, `` `${ ``, `\`u{}`,
  aksara Jawa U+A9BD, emoji di luar blok Jawa) yang membuat mutasi sering
  menghasilkan program *hampir valid*.
- `kBentukJawa` berisi program utuh yang menyasar jalur yang jarang terkena
  mutasi acak: generator, `enteni`/`mengko`, pola `pilih`, `cocog`, impor
  siklik, `coba`/`tangkep`, pelanggaran TDZ, field privat, parameter rest.

---

## Temuan (semua sudah diperbaiki)

| # | Target | Masukan minimal | Gejala | Akar |
|---|---|---|---|---|
| 1 | `fuzz_lexer` | `/b` (2 byte) | `std::out_of_range` dari `string_view::substr` | `lex_regex` menghitung flag dari `pola_akhir + 1`. Regex yang tidak ketutup dan berhenti tepat di akhir sumber punya `pola_akhir == src_.size()`, jadi `substr` mulai pada `size() + 1` |
| 2 | `fuzz_parser` | `gawe* { }` | stack overflow (rekursi tak berujung) | `parse_deklarasi_fungsi` untuk `gawe` diikuti `[`/`{` memundurkan `idx_` ke `gawe`, lalu memanggil `parse_statement()` yang memanggil dirinya lagi |
| 3 | `fuzz_parser` | `((((...1...))))` × 5000 | stack overflow | tidak ada batas kedalaman rekursi parser |
| 4 | regression | `gawe f(a, b = 2) { bali b; }` | `undefined`, bukan `2` | `ParamDeklarasi::nilai_default` di-parse tapi **tidak pernah dikompilasi** -- parameter default tidak pernah berfungsi sejak awal |
| 5 | regression | `gawe f(a, ...sisa) { bali jenis(sisa); }` | `angka`, bukan `dhaptar` | `Chunk::n_argumen_tetap` tidak pernah diisi, jadi VM menyalin argumen ke slot parameter rest dan mendahulukan dhaptar sisa satu slot |
| 6 | regression | `kanggo (const x saka g())` | galat sintaks | `parse_kanggo` hanya menerima `ana` sebagai pengikat loop, bukan `tetep` |

Temuan 1--3 ditemukan langsung oleh fuzzer. Temuan 4--6 ditemukan oleh test
regresi yang ditulis **karena** fuzzer -- keduanya dibungkus di
`tests/unit/test_runtime.cpp` dengan masukan yang sama persis.

Bug 4 dan 5 lebih lama umurnya daripada fuzzing: `nilai_default` dan
`n_argumen_tetap` sudah ada di kode sejak Fase 3 tanpa pernah dipakai. Ini bukti
bahwa campaign pendek saja sudah menemukan bug yang tidak terlihat dari review
dan test yang ditulis manuals.

### Perbaikan yang menyertainya

- **Batas kedalaman parser** (`Parser::kKedalamanMaks = 160`) lewat RAII
  (`RakKedalaman`) di `parse_statement` dan `parse_assignment`, sehingga rekursi
  mutual di masa depan tidak bisa meledakkan stack tanpa penjaga. Lebih dari itu
  menjadi galat `S002` yang bisa dibaca, bukan crash.
- **Prolog parameter default** memakai `JUMP_IF_NOT_NULLISH`. Konsekuensinya:
  argumen yang sengaja dilewatkan sebagai `mboh` juga memakai nilai default.
  Membedakan keduanya butuh opcode baru yang membaca `Frame::n_argumen`;
  untuk sekarang selisihnya dianggap tidak sepadat dengan complexity-nya
  (dicatat di `STATUS.md`).

---

## Batasan (jujur)

- **Tidak ada korpus crash yang tersimpan.** "Lolos" berarti tidak ditemukan
  crash pada campaign ini, bukan bukti kebenaran. Status campaign terakhir yang
  dijalankan ada di `STATUS.md`.
- **`fuzz_parser` tidak memeriksa rentang setiap node anak.** Untuk itu perlu
  penelusur anak AST per-jenis-node yang belum ada di `ast.h`; yang diperiksa
  baru rentang program. `cetak_ast` tetap menelusuri seluruh pohon, jadi crash
  dari AST yang bentuknya rusak akan ditemukan.
- **Regex sebagai nilai runtime belum ada** (Fase 8), jadi `fuzz_vm` tidak
  menguji eksekusi regex -- hanya tokenisasinya.
- **Campaign tidak memakai kamus (dictionary).** Tidak ada grammar-aware fuzzer (AFL/libFuzzer
  dengan dictionary); seluruh optimisation datang dari `kPotongan` dan
  `kBentukJawa`.
- **Tidak ada check-in `tests/fuzz/corpus/`.** Seed diambil dari `examples/`
  supaya tidak bisa basi.
