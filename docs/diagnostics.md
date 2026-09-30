# Diagnostik & pesan galat

Semua pesan pengguna ada di `src/support/messages.def` (satu sumber
kebenaran, dwibahasa Jawa/Indonesia). Kode diagnostic diawali huruf jenis:

| Awalan | Jenis |
|---|---|
| `C` | CLI (`jawa`) |
| `L` | Lexer |
| `S` | Sintaks & scope |
| `T` | Tipe |
| `R` | Runtime |
| `I` | Internal |

Tiga tingkat: `Catatan`, `Peringatan`, `Galat`. Hanya `Galat` yang
menghentikan eksekusi; `Peringatan` dicetak lalu program jalan. Perintah
menampilkan peringatan lebih dulu, baru galat.

## Bentuk keluaran

```
KleruToken [L011] (contoh.jw:3:7)
  `baili` ora bisa ngasilakeake wong sabda ing kaping iki. Muga arep nulis `bali`.
    3 |     baili 1;
      |     ^~~~~~~
  Saran: Muga iki arep nulis `bali`?
```

Tanda `^~~~` menandai potongan sumber yang bermasalah, bukan hanya
kolom. Panjang underline dipotong 40 karakter supaya baris panjang tidak
menggeser seluruh keluaran.

## L011 & L012 -- "kode ini dari bahasa lain"

Dua aturan di lexer yang menangkap kode dari bahasa lain. Keduanya hanya
berlaku di **awal pernyataan**, yaitu sesudah `;`, `{`, `}`, atau `:`.
Batasan itu penting: di tengah ekspresi, `type`/`push`/`log` adalah nama
variabel yang sah dan tidak boleh dirapot.

| Kode | Tingkat | Pemicu |
|---|---|---|
| `L011` | Galat | pengenal di awal pernyataan diikuti pengenal, literal, atau `{` -- bentuk yang tidak mungkin jadi Basa Jawa |
| `L012` | Peringatan | pengenal di awal pernyataan yang persis kata kunci atau fungsi bawaan bahasa lain |

Contoh:

```jawa
gawe f() { baili 1; }     // L011 (galat)      -> `bali`
gawe f() { return 1; }   // L012 (peringatan) -> `bali`
function f() {}          // L012 (peringatan) -> `gawe`
let x = 1;               // L012 (peringatan) -> `ana`
tulis(push(1, 2));       // diam: `push` sah sebagai nama
```

Daftar kata yang dikenali ada di `src/lex/pinjaman.def`. Untuk menambah
entri baru, dua syarat: kata itu harus benar-benar bermakna di bahasa
asalnya, dan padanannya harus benar-benar ada di Basa Jawa. Kalau tidak
ada padanan yang jujur (`void`, `pass`, `package`), lebih baik tidak ada
saran daripada suggestion yang ngawur.

Kata `then` sengaja tidak ada di daftar itu: itu sudah nama method Basa
Jawa, jadi saran "`then` -> `bali`" hanya membingungkan.

### Saran (paddingan) hanya kalau dekat

Untuk L011, saran dihitung dengan jarak edit terhadap semua nama kata kunci
(ngoko dan krama). Diterima sampai jarak 2, dan jarak 2 wajib huruf
pertama sama. Tanpa syarat huruf pertama, `foo` mendapat saran `ana` -- dua
kata yang tidak mirip sama sekali. Saran yang salah lebih buruk daripada
tidak ada saran.

### Mengapa L012 bukan galat

`let x = 1;` kebetulan sudah "bekerja" di Basa Jawa: `let` dibaca sebagai
pengenal lalu `x = 1` menugaskan. Menjadikan L012 galat akan merusak
program yang tadinya jalan. Peringatan cukup -- tujuannya memberitahu, bukan
melarang. L011 boleh jadi galat karena bentuknya sudah pasti salah secara
tata bahasa, bukan karena maknanya.

## Menambah pesan baru

1. Tambah satu baris `JAWA_MSG(kode, jenis, jawa, indonesia, saran)` di
   `src/support/messages.def`.
2. Panggil `Lexer::diagnostik(kode, pos, ...)`.
3. Kalau pesannya butuh argumen, pesan penuh dikirim sebagai parameter
   `tambahan` -- begitu pesan ber-placeholder di katalog tetap berfungsi
   sebagai cadangan.
4. Tambahkan test di `tests/unit/test_lexer.cpp` (atau `test_parser.cpp`).
5. Kalau pesan itu bisa muncul pada kode repo sendiri, tambahkan syaratnya
   ke `scripts/cek_kode_asing.py`.

