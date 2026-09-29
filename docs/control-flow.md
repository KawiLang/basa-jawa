# `pilih` (switch) dan Zona Mati-Temporal

Dokumen ini menjelaskan dua hal yang diperbaiki pada versi 0.7.0:

1. `pilih` — sebelumnya **hanya kasus pertama yang pernah diuji**, dan `baku`
   tidak pernah jalan.
2. Zona mati-temporal (TDZ) — sebelumnya membaca pengikat leksikal sebelum
   deklarasinya menghasilkan `undefined`, bukan galat.

---

## 1. `pilih`

### Bentuk dasar

```
pilih (subjek) {
  kasus <nilai>:
    <statement>*
  kasus <pola>:
    <statement>*
  baku:
    <statement>*
}
```

- Kasus **nilai** dibandingkan dengan `==` (yang di Basa Jawa tidak mengoersi,
  D-007: `1 == "1"` salah).
- Kasus **pola** aktif kalau kasusnya diawali `[` atau `{`. Pola dicocokkan
  dengan mesin yang **sama** dengan `cocog`, termasuk pola rest, wildcard,
  pola bersarang, dan pengikatan nama.
- `baku:` (default) jalan kalau tidak ada kasus lain yang cocok. Letaknya
  bebas; kalau `baku` ditulis sebelum kasus lain, kasus yang tertulis
  belakangan tetap salah mencapai kodenya.
- Tidak ada *implicit fall-through*: setiap kasus yang selesai melompat ke
  akhir `pilih`. Menulis `mandheg` untuk saling jatuh antar kasus memang
  didukung sebagai statement biasa, tapi tidak ada lompatan otomatis dari
  satu badan kasus ke berikutnya (keputusan D-006).

### Contoh

```
pilih (2) {
  kasus 1: tulis("satu");
  kasus 2: tulis("dua");     // <- yang ini yang jalan
  kasus 3: tulis("tiga");
}

pilih (9) {
  kasus 1: tulis("satu");
  baku:   tulis("lain");     // <- cadangan
}
```

```
pilih ([1, 2, 3]) {
  kasus [a, b, sisanya]: tulis(a, " ", b, " sisa ", sisanya);
  baku:                 tulis("ora cocok");
}
```

Pola `...sisa` mengikat **sisa** dhaptar, bukan seluruh subjek:

```
pilih ([1, 2, 3, 4]) {
  kasus [a, b, ...sisa]: tulis(a, b, sisa);   // a=1 b=2 sisa=[3, 4]
}
```

Panjang subjek dengan `...sisa` cukup **minimal** sepanjang bagian tetap; tanpa
`...sisa` panjangnya harus persis sama.

### Kode byte

```
ekspresi(subjek) ; SET_LOCAL s_subjek

L_kasus_i:
  [<test> ; EQ]  atau  [<pola>]      ; sisakan satu boolean
  JUMP_IF_FALSE L_gagal_i
  POP                              ; jalur cocok: buang boolean
  <body_i>
  JUMP L_akhir
L_gagal_i:
  POP                              ; jalur gagal: buang boolean juga
  ... (kasus berikutnya)

L_akhir:
```

Dua hal yang mudah salah dan sudah dikomentari di `Compiler::stmt_pilih`:

1. `Op::EQ` adalah perbandingan **biasa** (mendorong boolean ke stack), bukan
   lompatan bersyarat. Kodegen sebelumnya memperlakukannya sebagai lompatan,
   sehingga target lompatannya tidak pernah ditulis dan hanya kasus pertama
   yang terlihat benar.
2. `JUMP_IF_FALSE` hanya **memabat** nilai (`peek`), tidak mengambil. Karena itu
   kedua jalur harus membuang boolean-nya sendiri. Perhatikan bahwa titik gagal
   kasus ini adalah **awal kasus berikutnya**, bukan akhir `pilih`.

---

## 2. Zona mati-temporal (TDZ)

Semua pengikat leksikal (`ana`, `wonten`, `tetep`) punya zona mati-temporal:
nama **tidak bisa dibaca** sebelum deklarasinya dievaluasi.

```
tulis(x);        // galat: zona mati-temporal
tetep x = 5;

tetep x = 5;
tulis(x);        // 5
```

Berlaku juga di dalam fungsi, di dalam blok, dan bisa ditangkap `coba`:

```
coba {
  tulis(z);                 // galat TDZ
} tangkep (galat) {
  tulis("ditangkep");
}
tetep z = 1;                 // deklarasi setelahnya
```

### Cara kerjanya

Tidak ada state per-frame. `Chunk::tdz_daftar` menyimpan, untuk tiap pengikat
leksikal, **nomor ip instruksi yang menginisialisasi slotnya**. `Op::TDZ_CHECK a`
melempar galat kalau `f.ip` belum melewati `tdz_daftar[a]`.

Karena posisinya diturunkan dari `Frame::ip` yang memang sudah disimpan,
continuation (`mengko`/`enteni` dan generator) ikut benar tanpa field tambahan.

### Kompilator

Slot harus dialokasikan **sebelum** statement apa pun dikompilasi — kalau
slot-nya baru dibuat saat deklarasi dikompilasi, pembacaan yang lebih awal
tidak tahu apa-apa dan tidak menghasilkan cek. Itu tugas
`Compiler::pradaftar_tdz`, yang menelusuri badan fungsi dan modul (tidak
masuk ke badan fungsi anak) lalu memesan slot untuk tiap `ana`/`tetep`.

Setelah statement deklarasi selesai dikompilasi, entri `tdz_menunggu` dibuang,
sehingga pembacaan berikutnya tidak memerlukan cek sama sekali — biaya `pilih`
yang tidak dibaca sebelum deklarasi adalah nol opcode.

### Interaksi

- `ekspor { x }` juga ikut menunda kalau slot `x` masih di zona mati-temporal,
  supaya tidak mengekspor nilai yang belum diinisialisasi.
- Target `kanggo (ana i = 0; ...)` memakai slot yang sama, jadi tidak bentrok
  dengan pra-walk.
- Destructuring deklaratif (`const { a } = ...`) menutup TDZ di akhir statement,
  jadi slotnya terbaca sebagai sudah siap setelah seluruh pengikatan selesai.

## Pengikatan per-iterasi `kanggo`

`kanggo (ana i = 0; i < 3; i = i + 1)` mengikat `i` **per-iterasi**, sama
seperti ECMAScript: closure yang dibuat di dalam body setiap iterasi melihat
nilai `i` pada iterasinya sendiri, bukan nilai iterasi terakhir.

```
ana t = [];
kanggo (ana i = 0; i < 3; i = i + 1) t.tambah(() => i);
kanggo (ana f saka t) tulis(f());   // 0, 1, 2
```

Caranya: slot pengikat loop berisi `SelObj` (`SEL_BUAT`), dan tiap akhir
iterasi `SEL_SALIN` menggantinya dengan sel baru berisi nilai yang sama
(`CreatePerIterationEnvironment`). Pembacaan & penulisan `i` memakai
`GET_CELL`/`SET_CELL`, jadi `cari_atau_buat_upvalue` mengikat closure ke sel
yang sedang aktif -- bukan ke slot stack. Lihat D-037.

`terusna` melompat ke titik **sebelum** `SEL_SALIN`, bukan langsung ke bagian
pembaruan: kalau tidak, pembaruan menulis ke sel yang sudah ditangkap closure
iterasi itu.

Setelah loop, `i` berisi nilai iterasi terakhir (sel terakhir tidak disalin
lagi karena kondisi sudah gagal) -- sama seperti JavaScript.

### Batasan yang disengaja

- Slot bersifat **fungsi-wide**, bukan blok-wide, jadi kompilator ini tidak
  mengimplementasikan skop blok. `pradaftar_tdz` karena itu menelusuri seluruh
  badan fungsi. Konsekuensinya: dua pengikat dengan nama sama di loop berbeda
  dalam satu fungsi saling berebut slot, dan loop kedua yang memakai
  `kanggo (x saka ...)` tanpa `ana` bisa salah membaca pengikat loop lain.
  Pengikatan per-iterasi sendiri sudah dimodelkan (di atas).
- `catch (e)` tidak punya TDZ.
