## Apa yang diubah

<!-- Ringkas 1-3 kalimat. Rujuk issue kalau ada: "Closes #12" atau "Refs #12". -->

## Mengapa

<!-- Masalah yang diselesaikan, atau alasan perubahan. -->

Closes #

## Verifikasi

Centang yang benar-benar dijalankan di lingkungan Anda:

- [ ] `cmake --preset release && cmake --build --preset release` — build bersih, **nol warning**
- [ ] `ctest --test-dir build/release --output-on-failure` — semua hijau
- [ ] `python3 scripts/cek_golden.py` — keluaran contoh persis sama
- [ ] `python3 scripts/cek_contoh.py` — semua contoh acuan ter-parse bersih
- [ ] `python3 tools/check_sumber.py` — tidak ada karakter terlarang pada sumber
- [ ] Preset `asan` masih hijau
- [ ] Preset `ubsan` masih hijau
- [ ] Preset `nonanbox` (mode nilai 16-byte) masih hijau

> Batasan proyek: **nol warning, nol temuan sanitizer, nol crash pada input apa pun.**
> Kalau ada yang tidak dijalankan, jelaskan alasannya di bagian "Catatan" - jangan dicentang diam-diam.

## Yang tidak diubah

<!-- Area yang sengaja belum disentuh supaya tidak terlihat terlewat. -->

## Catatan

<!-- Tambahkan konteks lain di sini. -->
