# Kontribusi

Terima kasih sudah ingin membantu Basa Jawa.

## Sebelum mulai

1. Baca [`README.md`](../README.md), [`PLAN.md`](../PLAN.md) — khususnya **Bagian 1
   (Batasan)** — dan [`STATUS.md`](../STATUS.md).
2. Cek daftar issue terbuka. Kalau sedang mengerjakan sesuatu, tulis di issue
   tersebut supaya tidak ada kerja ganda.
3. Kalau usulannya besar (bisa memakan beberapa fase), buka issue dulu sebelum
   menulis kode. Roadmap ada di `PLAN.md` Bagian 2; deviasi dari roadmap perlu
   dijelaskan alasannya.

## Batasan yang tidak bisa ditawar

| Batasan | Artinya |
|---|---|
| Nol dependensi runtime | Hanya standard library + lapisan tipis `src/platform/` |
| Nol warning | Build harus bersih di `-Wall -Wextra -Werror` |
| Nol temuan sanitizer | `asan` dan `ubsan` harus hijau |
| Nol crash | Input apa pun tidak boleh menyebabkan `abort()` atau crash |
| Tanpa `eval`/`Function`/`with` | Lihat Bagian 15 `PLAN.md` |
| Fase tidak ditandai selesai bila test merah | Test merah = fase belum selesai, sesederhana itu |

## Membangun

```bash
export PATH=/opt/rh/gcc-toolset-12/root/usr/bin:$PATH   # GCC 12.2.1
cmake --preset release && cmake --build --preset release
```

Butuh **CMake >= 3.20** dan compiler **C++20** (disarankan GCC 13 / Clang 16 /
MSVC 19.36). Di mesin RAM kecil, build sanitizer + Debug perlu `-j 1`.

## Wajib dijalankan sebelum membuka PR

```bash
ctest --test-dir build/release --output-on-failure
python3 scripts/cek_golden.py
python3 scripts/cek_contoh.py
python3 tools/check_sumber.py
python3 tools/cek_tabel.py
```

Lalu ulangi dengan `asan`, `ubsan`, dan `nonanbox`:

```bash
cmake --preset asan   && cmake --build --preset asan   && ctest --test-dir build/asan   --output-on-failure
cmake --preset ubsan  && cmake --build --preset ubsan  && ctest --test-dir build/ubsan  --output-on-failure
cmake --preset nonanbox && cmake --build --preset nonanbox && ctest --test-dir build/nonanbox --output-on-failure
```

## Gaya kode

- Ikuti `.clang-format` dan `.clang-tidy` yang sudah ada. Jangan menambahkan
  gaya baru.
- Pesan galat, nama API pustaka, dan dokumentasi ditulis dalam **Bahasa Jawa**.
- Komentar dan identifier ditulis dalam Bahasa Jawa/Inggris, seperti di
  sekelilingnya.
- Kalau sebuah keputusan desain penting, catat alasannya di `DECISIONS.md` —
  jangan hanya tertulis di kode.

## Menambahkan fitur bahasa

Fitur baru hampir selalu menyentuh lebih dari satu lapis. Urutannya:

1. Tambah sintaksnya di `docs/grammar.ebnf`.
2. Tulis dulu **test yang gagal** di `tests/tes/`.
3. Implementasikan di lexer/parser/AST.
4. Kompilasi ke bytecode, lalu jalankan di VM.
5. Tambah contoh yang bisa dijalankan di `examples/` **dan** keluaran emasnya di
   `tests/golden/`.
6. Perbarui `STATUS.md`, lalu `CHANGELOG.md`.

Kalau sebuah contoh memang belum didukung, tambahkan ke `SKIP` di
`scripts/cek_golden.py` **beserta alasannya**. Jangan dihapus diam-diam.

## Pull request

- Satu PR = satu perubahan yang bisa direview. Jangan campur refactor dengan
  fitur.
- Isi bagian template di `.github/pull_request_template.md`. Kalau satu
  pemeriksaan tidak dijalankan, tulis alasannya - jangan dicentang palsu.
- PR besar lebih baik dipecah jadi beberapa PR kecil yang masing-masing hijau.
- Tutup issue secara otomatis dengan `Closes #N` di deskripsi PR.

## Laporan galat

Lihat template `Laporan galat` di `.github/ISSUE_TEMPLATE/`. Sertakan program
reproduksi sekecil mungkin, versi `jawa versi`, dan preset build yang dipakai.

## Pertanyaan

Buka discussion. Jangan membuka issue untuk pertanyaan pemakaian.
