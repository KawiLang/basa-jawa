# `jawa repl`

Lingkup baca-evaluasi-cetak.

```
jawa repl                  # interaktif, dengan prompt
jawa repl < skrip.jw       # non-interaktif: baca sampai habis, tanpa prompt
```

```
jawa> ana x = 10
jawa> x * 2
20
jawa> gawe kali(a, b) { bali a * b }
jawa> kali(6, 7)
42
jawa> [1, 2, 3].kebut []
[1, 2, 3]
jawa> :q
```

## Yang bisa dilakukan

- **Nilai ekspresi langsung dicetak.** `1 + 1` mencetak `2`, tanpa perlu
  `tulis(...)`. Panggilan `tulis(...)` sendiri dikecualikan supaya tidak
  tercetak dua kali.
- **Pengikut bertahan antar baris.** `ana x = 10` lalu `x * 2` di baris
  berikutnya benar-benar membaca `x` yang sama. `gawe`, `golongan`, dan `tetik` juga
  bertahan.
- **Masukan beberapa baris.** Kurung kurawal, kurung siku, atau kurung kurung
  yang belum tertutup berarti baris berikutnya adalah kelanjutannya:

  ```
  jawa> gawe f() {
    ...>   bali 5
    ...> }
  jawa> f()
  5
  ```

  String, template literal, dan komentar blok yang belum ditutup juga
  mengaktifkan kelanjutan. Penentuannya memakai lexer, jadi `{` di dalam teks
  atau komentar tidak ikut dihitung.
- **Galat tidak menghentikan sesi.** Galat sintaks maupun runtime dicetak dan
  baris berikutnya tetap dievaluasi. Kode keluar proses menjadi bukan nol kalau
  ada evaluasi yang gagal.
- **Perintah**, diawali `:` supaya tidak bentrok dengan sintaks bahasa:

  | Perintah | Guna |
  |---|---|
  | `:q`, `:keluar` | keluar |
  | `:bantuan`, `:?` | tampilkan bantuan |
  | `:nilai <nama>` | cetak nilai global bernama itu |
  | `:sampah` | statistik pengumpulan sampah |
  | `:reset` | kosongkan seluruh state; semua nama hilang |

## Yang perlu tahu

- **Setiap baris adalah modul baru.** Bentuk programnya dikompilasi ulang, jadi
  pengikut frame modul disimpan sebagai global (yang milik VM) alih-alih slot
  yang ikut hilang bersama frame-nya. Body **fungsi** tidak terpengaruh: di
  sana pengikut tetap slot seperti biasa, dan zona mati-temporal tetap berlaku.
- **Tidak ada input multiline via `\` atau kurung kurawal pembungkus.** Yang
  recognising multiline hanya kurung yang belum tertutup. Ekspresi yang sangat
  panjang lebih enak ditulis di berkas lalu `impor` dari REPL.
- **Riwayat tidak disimpan.** `jawa repl` tidak membaca input dari terminal
  dengan riwayat (butuh `readline`, yang tidak selalu ada). Untuk sesi panjang,
  lebih enak pakai berkas `.jw` dan `jawa run`.
- **`Wektu.tundha` tidak menunggu ms sungguhan** (loop acara deterministik —
  lihat `docs/async.md`), jadi `enteni` di REPL akan mengembalikan Janji yang
  masih `nunggu`.
- **Tidak ada debugger.** Galat runtime tidak membawa jejak stack sumber, hanya
  nama fungsi.

## Cara kerjanya

| Bagian | Lokasi |
|---|---|
| Prompt, kelanjutan, perintah | `src/cli/repl.cpp` |
| Mode REPL di kompilator | `Compiler::set_repl` |
| Menjalankan modul yang sudah dikompilasi | `VM::jalankan_modul` |
| Uji | `scripts/cek_repl.py` (12 kasus) |

Pencetakan nilai ekspresi dilakukan **di kompilator**, bukan dengan membungkus
sumber menjadi `tulis(<ekspresi>)`. Pembungkusan seperti itu butuh daftar kata
kunci untuk membedakan statement dari ekspresi, dan daftar itu selalu tertinggal
satu kata -- `anyaar` sempat terlewat, lalu `anyaar Kucing("Budi").speak()` ikut
dibungkus dan gagal diurai.
