# `jawa fmt`

Pemformat kode Basa Jawa.

```
jawa fmt berkas.jw              # cetak hasil ke stdout
jawa fmt --cek berkas.jw ...    # laporkan saja; kode keluar 1 bila perlu diformat
jawa fmt --tulis berkas.jw ...  # timpa berkasnya
jawa fmt --lebar 2 berkas.jw    # spasi per tingkat indent
jawa fmt --tanpa-cek-sintaks f  # jangan parse ulang hasilnya
```

## Prinsip: hanya mengubah jarak

Pemformat ini dijamin **tidak pernah mengubah bentuk program** — hanya jarak
antara token. Pemenggalan baris milik penulis: kalau satu statement ditulis
melintasi lima baris, hasilnya tetap lima baris. Formatter tidak menyisipkan
atau menggabungkan baris.

Alasan sesungguhnya: pemformat yang mengubah bentuk program adalah pemformat
yang perlu dipercaya buta. Yang hanya merapikan jarak bisa ditinjau dengan aman,
dan hasilnya tetap terlihat seperti tulisan orang yang sedang dibaca.

## Yang Dinormalisasi

| | Contoh |
|---|---|
| Indentasi | empat spasi per tingkat kurung kurawal |
| Kurung kurawal penutup | selalu di awal baris |
| Koma dan titik koma | `f(1,2,3)` -> `f(1, 2, 3)` |
| Penugasan | `ana x=1` -> `ana x = 1` |
| Operator biner pasti | `a<b` tidak disentuh, tapi `a==b` -> `a == b` |
| Tanda baca | `f( x )` -> `f(x)`, `peta . get` -> `peta.get` |
| Baris kosong | dibatasi satu; tidak ada di akhir berkas |

## Yang Sengaja Tidak Diubah

**Operator yang ambigu terhadap tipe.** `<` bisa operator perbandingan
(`a < b`) atau generic; `+`/`-` bisa biner atau unary; `&`/`|`/`*` juga bisa
unary. Dari token saja bentuk mana yang dimaksud tidak bisa dibedakan secara
andal, jadi jarak dari sumber dipertahankan. Menormalkan paksa di sini berisiko
mengubah program, bukan cuma merapikannya.

Kalau Anda menulis `a<b` dan formatter tidak mengubahnya, itu disengaja.
Tulis `a < b` kalau Anda memang mau spasi.

**Nama, string, regex, dan template literal.** Isi literal ditulis ulang persis
seperti aslinya. Template literal diperlakukan sebagai satu token buram: `${...}`
di dalamnya, string di dalam `${...}`, dan template bersarang tidak pernah
disentuh. Rentangnya dipindai langsung dari sumber, karena lexer memberi offset
0 ke setiap token `TemplateText`.

**Komentar.** Lexer membuangnya, jadi formatter membaca ulang celah antara token
dan menyisipkan komentar pada posisi yang sama relatif terhadap token.
Komentar `//` selalu naik ke barisnya sendiri; `/* */` boleh menempel di baris
yang sama seperti aslinya.

## Keamanan: dua jaring

`format_sumber` memverifikasi hasilnya sebelum mengembalikannya:

1. **Jumlah token sama.** Kalau satu token hilang atau terpecah — string yang ikut
   di-normalisasi, komentar yang menelan baris — format ditolak.
2. **Hasilnya di-parse ulang.** Cek leksikal saja tidak cukup: `ana x = ;` adalah
   token stream yang sah tetapi program yang salah. Yang perlu terdeteksi kalau
   formatter menggeser satu token adalah "program jadi tidak bisa di-parse".

Keduanya bisa dimatikan dengan `--tanpa-cek-sintaks`, tapi untuk produksi tidak
perlu.

## `--cek` untuk CI

`--cek` tidak menulis apa pun; keluar dengan kode bukan nol kalau ada berkas
yang perlu diformat. `scripts/cek_fmt.py` membungkusnya jadi uji yang lebih
ketat: untuk setiap berkas di `examples/` dan `tests/tes/`, ia memeriksa bahwa
format-nya **idempoten** dan bahwa `jawa run` keluarannya sama persis sebelum
dan sesudah format.

Idempoten adalah syarat `jawa fmt --tulis` berguna. Kalau tidak, setiap commit
menghasilkan diff baru dan git blame jadi tidak berguna.

## Batasan yang diketahui

- **Pemenggalan baris tidak pernah berubah.** `f(a,\n b)` tetap seperti itu.
  Untuk itu ada `jawa ubah`, yang belum ada.
- **Baris yang melebihi batas panjang** tidak dibungkus otomatis.
- **Posisi tanda koma di ujung baris** (`a,` lalu `b`) dipertahankan; pemformat
  lengkap akan memindahkannya ke depan.
- **Perataan tanda `=`** di deklarasi berurutan belum dilakukan.
- **Blok kosong** `{\n}` tidak diringkas jadi `{}`.
- **Urutan impor** tidak diurutkan.
- Indentasi berbasis kurung kurawal, bukan kurung siku: argumen multi-baris tidak
  diberi indentasi tambahan.
