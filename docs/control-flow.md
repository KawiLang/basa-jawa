# Alur kendali: `pilih`, `coba`, zona mati-temporal

Dokumen ini menjelaskan tiga hal:

1. `pilih` (switch) — sebelumnya **hanya kasus pertama yang pernah diuji**, dan
   `baku` tidak pernah jalan.
2. Zona mati-temporal (TDZ) — sebelumnya membaca pengikat leksikal sebelum
   deklarasinya menghasilkan `undefined`, bukan galat.
3. `coba`/`tangkep`/`pungkasan` — sebelumnya `pungkasan` dilewati di jalur
   normal, hanya klausula `tangkep` pertama yang dipakai, dan galat
   aritmetika tidak bisa ditangkap sama sekali.

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
- Kasus **tipe** aktif kalau kasusnya `kasus <Kelas>:` — pengenal yang
  diawali huruf kapital dan langsung diikuti `:`. Cocok kalau subjek adalah
  instans class itu **atau salah satu induknya**, jadi `kasus Kucing:` juga
  menjerat `KucingPriba`. Opcode `INSTAN_DARI` menelusuri rantai `induk`.
  Pengenal huruf kecil (`kasus warna:`) tetap perbandingan nilai biasa — syarat
  kapital itu yang membedakan keduanya, bukan posisinya.
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

## 2. `coba` / `tangkep` / `pungkasan`

### Klausula `tangkep` bertipe

Satu `coba` boleh punya banyak klausula `tangkep`, masing-masing memilih
handler berdasarkan tipe galat:

```
coba {
  salah_koersi();          // melempar KleruJenis
} tangkep (KleruModul) {   // tidak cocok -> dilewati
  tulis("modul");
} tangkep (Kleru) {        // cocok: objek kleru apa pun
  tulis("kleru: ", galat.jeneng);
} tangkep (lain) {         // tanpa tipe = tangkap apa saja (harus terakhir)
  tulis("lain: ", lain);
}
```

Dua bentuk penulisan tipe:

- `tangkep (KleruJenis) { ... }` — tanpa binding.
- `tangkep (err: KleruJenis) { ... }` — dengan binding.

Aturan pencocokan (`cocok_kleru` di `src/vm/vm.cpp`):

| `tipe` | cocok dengan |
|---|---|
| kosong (tanpa anotasi) | apa saja |
| `Kleru` | objek kleru apa pun |
| `KleruJenis`, `KleruModul`, ... | `KleruObj::jeneng` yang sama persis |

Nilai yang **bukan** objek kleru (mis. `uncal "teks"`) hanya cocok dengan
klausula tanpa tipe. Klausula pertama yang cocok menang. Kalau tidak ada
klausula yang cocok, `coba` itu tidak menanganinya dan galat naik ke `coba` di
luarnya — persis seperti ECMAScript.

Klausula tanpa tipe **harus berada di urutan terakhir**; kalau tidak, klausula
setelahnya tidak akan pernah dijalankan (unwinder berhenti di klausula
pertama yang cocok).

### `pungkasan` selalu jalan

Badan `pungkasan` dijalankan di keempat jalur:

1. Blok selesai normal.
2. Setelah klausula `tangkep` yang cocok selesai.
3. Tidak ada klausula yang cocok — `pungkasan` jalan, lalu galat diteruskan
   dengan `THROW` ke handler di luar.
4. `bali` di dalam blok — badan `pungkasan` disalin **tepat sebelum** `RETURN`,
   lalu nilai `bali` dipop dari slot temporer dan dikembalikan (ECMAScript).

Jalur (4) diimplementasikan dengan menyalin badan `pungkasan` ke dalam setiap
`bali`, bukan dengan lompatan ke satu salinan bersama. Alasannya: setiap `bali`
punya slot temporer sendiri, jadi epilogusnya (`GET_LOCAL <slot> ; RETURN`)
berbeda-beda. Jumlah `bali` dalam satu `coba` kecil, jadi penalinannya
mempermurah.

`bali` di dalam badan `pungkasan` sendiri **tidak** menyalin `pungkasan` lagi —
`pungkasan` yang sedang berjalan sudah cukup, dan menyalinnya akan tak
berujung. `bali` di dalam `pungkasan` juga memang menggantikan nilai
kembalian sebelumnya, jadi hasilnya langsung dikembalikan.

Jalur (3) dikompilasi sebagai `L_tolak`: badan `pungkasan` diemit ulang,
diakhiri `THROW` (nilai galat masih di puncak stack karena unwinder
meninggalkannya di sana). Karena itu badan `pungkasan` bisa muncul beberapa
kali dalam bytecode — itu disengaja, bukan duplikasi tak sengaja.

### Cara kerjanya

`TRY_BEGIN` mendaftarkan satu `Frame::Handler` (tinggi stack saat itu +
ip jalur tolak). Tiap klausula `tangkep` menambah entri lewat `TRY_KLAUSUL`:
sebuah `std::string_view` nama tipe + ip awal klausul. Handler dipecah
dari bentuk "satu ip" menjadi "daftar klausul" justru supaya seleksi tipe
bisa dilakukan **di sisi unwinder**. Kalau seleksinya dilakukan di bytecode,
handler tetap harus menangkap galat yang salah lalu dilempar ulang, dan
`pungkasan`-nya ikut jalan dua kali.

`VM::unwind_galat` mencoba tiap handler dari yang terdekat: kalau tidak ada
klausula yang cocok, handler itu **dicabut** dan pencarian lanjut ke handler
berikutnya atau ke frame pemanggil.

### Galat yang bisa ditangkap

Galat level bahasa harus dibentuk sebagai `KleruObj` lalu dilempar lewat
`unwind_galat` supaya bisa ditangkap. Opcode yang menulis `galat_.ada` secara
langsung membuat galat **tidak bisa ditangkap** sama sekali — itulah bug lama
yang membuat `1 + "a"` di dalam `coba` tetap mematikan program
(`lempar_dan_tangkap` di `src/vm/vm_loop.cpp`).

Galat internal VM (instruksi rusak, `ITER_NEXT` tanpa iterator, langkah
maksimum terlampaui) sengaja **tidak** bisa ditangkap: itu kondisi mesin, bukan
kesalahan program.

---

## 3. Zona mati-temporal (TDZ)

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

---

## 4. Pengikatan per-iterasi `kanggo`

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
