# Regex (`/pola/flag`)

Dokumen ini adalah acuan untuk nilai `/pola/flag` di Basa Jawa: apa yang
didukung, apa yang sengaja tidak, dan batas-batasnya.

Regex dulu hanya token lexer — nilainya `mboh`, tidak ada yang bisa dilakukan.
Sejak 0.11.0 pola dikompilasi jadi program dan dicocokkan dengan mesin
backtracking.

## Bentuk dasar

```jawa
tulis(/abc/.cocog("xxabcyy"));            // true
tulis(/a.c/.cocog("abc"));               // true
tulis(/^abc$/.cocog("xabc"));             // false
tulis(/^\d{4}-\d{2}$/.kabeh("2026-09"));  // true
```

Aturan lexer: `/` setelah identifier SELALU dibaca sebagai awal regex (aturan
ECMAScript), bukan pembagian. Kalau maksudnya pembagian, tulis angka sebagai
operan kiri yang eksplisit: `a / b`.

## Method

| Method | Hasil |
|---|---|
| `cocog(teks)` | `bener` kalau ada kecocokan di mana saja |
| `kabeh(teks)` | `bener` kalau SELURUH teks cocok (anchor di kedua ujung) |
| `ganti(teks, pengganti)` | teks baru; mengganti semua kecocokan |
| `pecah(teks)` | dhaptar; tiap elemen `[seluruh, grup1, grup2, ...]` |
| `nilai(teks)` | dhaptar kelompok tangkap dari kecocokan **pertama** |
| `grup(teks, kelompok)` | teks satu kelompok (nomor **atau** nama) |
| `pola()` | teks pola |
| `flag()` | teks flag |

```jawa
tulis(/[0-9]+/g.ganti("a1b22c333", "#"));        // "a#b#c#"
tulis(/(\w+)@(\w+)/.ganti("budi@sari", "$2 $1")); // "sari budi"
tulis(/(\d+)-(\d+)/.nilai("10-20"));              // ["10", "20"]
tulis(/(\d+)-(\d+)/.grup("10-20", 2));            // "20"
tulis(/(?<tahun>\d{4})/.grup("2026", "tahun"));   // "2026"
tulis(/[0-9]+/.pecah("a1b22c333").panjang);       // 3
```

`$0` dan `$&` berarti seluruh kecocokan; `$1`..`$9` berarti kelompok tangkap.
Kelompok yang tidak ikut cocok menghasilkan teks kosong. `nilai` dan `pecah`
mengembalikan dhaptar **kosong** kalau tidak ada kecocokan — bukan dhaptar
berisi `mboh`, supaya `panjang`-nya langsung berguna.

Regex adalah nilai biasa: bisa masuk dhaptar, jadi properti, jadi argumen.

## Sintaks yang didukung

| Kategori | Bentuk |
|---|---|
| Kuantifier | `*` `+` `?` `{n}` `{n,}` `{n,m}`, dan `?` susul jadi lazy (`*?`) |
| Alternasi | `\|` |
| Grup | `(...)` penangkap, `(?:...)` non-penangkap, `(?<nama>...)` bernama |
| Kelas | `[abc]` `[a-z]` `[^abc]` `[\d\w\s]` |
| Shorthand | `\d \D \w \W \s \S` |
| Jangkar | `^` `$` (anchor, bisa di mana saja dalam pola) |
| Batas kata | `\b` `\B` |
| Escape | `\n \t \r \f \v \0 \xHH \uHHHH \cX` dan `\<karakter>` |
| Titik | `.` (selalu satu byte, bukan satu titik kode) |

Flag: `g` `i` `m` `s` `y` `u`.

Perhatikan dua hal yang sering mengejutkan orang yang datang dari bahasa lain:

* **`|` berikatan longgar.** `/^kucing|anjing$/` berarti `^kucing` **atau**
  `anjing$` — bukan "kucing di awal atau anjing di akhir". Kalau itu yang
  dimaksud, kurung harus eksplisit: `/^(kucing|anjing)$/`.
* **`^` dan `$` adalah anchor**, bukan jangkar literal. `^` hanya cocok di
  awal baris, `$` hanya di akhir. Dengan flag `m`, keduanya juga cocok di setiap
  akhir baris.

## Yang tidak didukung, dan kenapa

| Tidak ada | Alasan |
|---|---|
| Lookahead `(?=...)` / lookbehind `(?<=...)` | Kombinasi keduanya membuat mesin tak sederhana; ditolak eksplisit, bukan diam-diam. |
| Backreference `\1` | Butuh pencocokan rekursif; dampaknya ke anggaran langkah besar sekali. |
| Kuantifier possessif `a*+` | Diam-diam berubahnya semantik lebih berbahaya daripada tidak ada. |
| `[a-\d]` (rentang campur harf & shorthand) | Ditolak: hasilnya tidak intuitif. |
| Nama kelompok yang sama dua kali | Sama seperti ECMAScript, ditolak. |


Bagian yang "tidak ada" ini menghasilkan galat, **bukan** perilaku diam-diam.
Galat dibelah dua, dan itu disengaja:

* Yang bisa dilihat lexer (regex tak ditutup, flag tak dikenal) jadi `KleruToken`
  waktu kompilasi — lebih dekat ke sumber, dan program tidak mungkin jalan
  dengan pola yang tak bisa dikompilasi.
* Yang baru ketahuan setelah parsing pola (kurung tak seimbang, rentang
  terbalik, kuantifier tanpa operand) jadi `KleruRegex` waktu runtime, sehingga
  bisa ditangkap `coba`/`tangkep`.

## Anggaran langkah (ReDoS)

Mesin ini backtracking murni, dan backtracking bersifat eksponensial dalam
**waktu**: pola `(a+)+b` terhadap 40 huruf `a` punya 2^40 cabang. Kedalaman
rekursinya hanya ~80, jadi penjaga kedalaman tidak berguna — yang dipakai adalah
**anggaran langkah**: setiap pemanggilan `match` menambah penghitung, dan begitu
mencapai 200.000 langkah pencarian dihentikan.

Yang penting: anggaran habis dilaporkan sebagai **galat**, bukan diam-diam
dijawab "tidak cocok". Jawaban yang salah lebih buruk daripada menggantung —
pemanggil akan menyimpulkan tidak ada kecocokan padahal mesinnya menyerah.

```jawa
coba {
    /(a+)+b/.cocog(teks_dari_pengguna);
} tangkep (galat) {
    tulis("pola terlalu rumit: ", galat.pesan);
}
```

Pola yang benar-benar linear (`^a+b$`, `[0-9]+`, `(?:ab)+c`) tidak pernah
menyentuh anggaran ini, berapa pun panjang teksnya.

Anggaran bisa diubah per program lewat `RegexProgram::set_anggaran_langkah`
(kalau `-e` dengan API, bukan lewat bahasa), tapi mengubahnya tidak membuat pola
ReDoS jadi cepat — hanya menunda(xsed) batasnya.

## Batasan yang jujur

* `.` dan semua kelas karakter bersifat **byte**, bukan titik kode. `/^.$/`
  tidak cocok `"é"`; itu dua byte UTF-8. Flag `u` diterima dan disimpan, tapi
  belum mengubah apa pun. Ini batasan yang diketahui, bukan tersembunyi.
* Tidak ada mode `n` (literal), dan tidak ada penulisan `\p{...}` (kategori
  Unicode).
* Dengan flag `i`, rentang kelas seperti `[a-c]` diperluas dengan huruf
  besar-kecil yang setara, jadi sifatnya sedikit berbeda dari ECMAScript pada
  beberapa kasus batas non-ASCII.
* Tidak ada pencarian balik (reverse search).

## Lihat juga

* `docs/stdlib.md` — pustaka standar lain.
* `docs/bytecode.md` — opcode `MAKE_REGEX`.
* `tests/tes/regex.tes.jw` — 90+ assertion, termasuk kasus ReDoS dan regresi
  bug `]` (lihat komentar di berkasnya).
