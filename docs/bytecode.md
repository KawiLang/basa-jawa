# Bytecode Basa Jawa

BytecodeBasa Jawa adalah **tumpukan** (stack machine) seperti Lox, bukan
mesin register. Setiap instruksi 1 byte opcode + 0/1/2 operand `uint16_t` +
`uint32_t` nomor baris sumber; total 6 atau 8 byte.

Daftar opcode berikut DIHASILKAN dari `src/vm/opcodes.def` (sumber tunggal).
Jalankan `jawa bytecode -e "<kode>"` untuk melihat hasil kompilasi nyata.

Konvensi tumpukan: `a b -> c` berarti pop dua, dorong satu. `v...` berarti
sejumlah nilai yang tidak diketahui kompilasi ini. Semua indeks lokal relatif
terhadap `Frame::slot_base`; slot 0 selalu `this`.

## Kontrol alur  (`kontrol`, 1 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 0 | `NOP` | 0 | `-` | Tidak ada operasi (dipakai sebagai jebakan lompatan). |

## Konstanta & literal  (`konstanta`, 12 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 1 | `KOSONG` | 0 | `- -> kosong` | Dorong `kosong` (null). |
| 2 | `MBOH` | 0 | `- -> mboh` | Dorong `mboh` (undefined). |
| 3 | `BENER` | 0 | `- -> bener` | Dorong boolean `bener`. |
| 4 | `SALAH` | 0 | `- -> salah` | Dorong boolean `salah`. |
| 5 | `KONSTAN` | 1 | `- -> v` | Dorong konstanta dari pool fungsi ini. |
| 6 | `NOMOR` | 1 | `- -> n` | Dorong nama (string) dari pool nama fungsi ini. |
| 7 | `TOSTRING` | 0 | `a - s` | Ubah nilai puncak menjadi teks (implementasi `toString`). |
| 8 | `CONCAT` | 0 | `a b - s` | Gabungkan dua nilai puncak menjadi teks (`+` untuk string). |
| 9 | `BIGINT` | 1 | `- -> n` | Operasi BigInt (belum diimplementasikan). |
| 10 | `TEKS` | 1 | `- -> t` | Dorong konstanta teks (alias lawas `KONSTAN`). |
| 11 | `NIL` | 0 | `- -> -` | Alias `MBOH` (dipakai importer modul). |
| 12 | `UNDEF` | 0 | `- -> -` | Alias `MBOH`. |

## Variabel, lokal, upvalue  (`variabel`, 13 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 13 | `GET_GLOBAL` | 1 | `- -> v` | Baca variabel global modul dengan nama `nama[a]`. |
| 14 | `SET_GLOBAL` | 1 | `v - >` | Tulis variabel global; menyisakan nilainya (penugasan = ekspresi). |
| 15 | `GET_MODULE` | 1 | `- -> v` | Baca ekspor modul. |
| 16 | `SET_MODULE` | 1 | `v - >` | Tulis ekspor modul. |
| 17 | `GET_LOCAL` | 1 | `- -> v` | Dorong slot lokal `a`. |
| 18 | `SET_LOCAL` | 1 | `v - >` | Simpan nilai puncak ke slot `a`, pop. |
| 19 | `DEF_LOCAL` | 1 | `v - >` | Sama seperti `SET_LOCAL`; dipakai deklarasi variabel. |
| 20 | `GET_UPVAL` | 1 | `- -> v` | Baca sel upvalue `a`. Sel `null` berarti nama(global) tersebut global. |
| 21 | `SET_UPVAL` | 1 | `v - >` | Tulis sel upvalue `a` (atau global bila sel null). |
| 22 | `GET_CELL` | 1 | `- -> v` | Ambil sel upvalue mentah (dipakai debugger/fiber). |
| 23 | `SET_CELL` | 1 | `v - >` | Tulis sel upvalue mentah. |
| 24 | `CLOSE_UPVAL` | 1 | `- -> -` | Tutup sel upvalue yang menunjuk ke frame yang berakhir. |
| 25 | `TDZ_CHECK` | 1 | `- -> -` | Cek zona mati-temporal (belum diimplementasikan; no-op). |

## Manipulasi tumpukan  (`stack`, 5 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 26 | `POP` | 0 | `v - >` | Buang puncak. |
| 27 | `DUP` | 0 | `v - v v` | Gandakan puncak. |
| 28 | `SWAP` | 0 | `a b - b a` | Tukar dua puncak. |
| 29 | `DUP2` | 0 | `a b - a b a b` | Gandakan dua puncak (urutan dipertahankan). |
| 30 | `ROT3` | 0 | `a b c - c a b` | Putar tiga puncak. |

## Aritmetika  (`aritmetika`, 9 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 31 | `ADD` | 0 | `a b - r` | Penjumlahan. Tanpa koersi implisit (D-007): `Angka + Teks` = galat. |
| 32 | `SUB` | 0 | `a b - r` | Pengurangan (angka saja). |
| 33 | `MUL` | 0 | `a b - r` | Perkalian. |
| 34 | `DIV` | 0 | `a b - r` | Pembagian. |
| 35 | `MOD` | 0 | `a b - r` | Sisa pembagian. |
| 36 | `POW` | 0 | `a b - r` | Perpangkatan. |
| 37 | `NEG` | 0 | `a - r` | Unary minus. |
| 38 | `INC` | 0 | `a - r` | Tambah 1 (++/-- dengan argumen implisit). |
| 39 | `DEC` | 0 | `a - r` | Kurang 1. |

## Operasi bit  (`bitwise`, 7 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 40 | `BIT_AND` | 0 | `a b - r` | `&`. |
| 41 | `BIT_OR` | 0 | `a b - r` | `|`. |
| 42 | `BIT_XOR` | 0 | `a b - r` | `^`. |
| 43 | `BIT_NOT` | 0 | `a - r` | `~`. |
| 44 | `SHL` | 0 | `a b - r` | `<<`. |
| 45 | `SHR` | 0 | `a b - r` | `>>` (arithmetik, preserves sign). |
| 46 | `USHR` | 0 | `a b - r` | `>>>` (logaritmik). |

## Perbandingan & kebenaran  (`perbandingan`, 10 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 47 | `EQ` | 0 | `a b - r` | `==` (longgar: menbandingkan nilai). |
| 48 | `NE` | 0 | `a b - r` | `!=`. |
| 49 | `SEQ` | 0 | `a b - r` | `===` (ketat: tipe harus sama). |
| 50 | `SNE` | 0 | `a b - r` | `!==`. |
| 51 | `LT` | 0 | `a b - r` | `<`. |
| 52 | `LE` | 0 | `a b - r` | `<=`. |
| 53 | `GT` | 0 | `a b - r` | `>`. |
| 54 | `GE` | 0 | `a b - r` | `>=`. |
| 55 | `SAME_VALUE` | 0 | `a b - r` |  |
| 56 | `NOT` | 0 | `a - r` | `!` (peek lalu pop). |

## Tipe  (`tipe`, 1 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 57 | `CEK_TIPE` | 1 | `v k - v` | Lempar `KleruTipe` bila nilai tidak cocok dengan tipe beranotasi. |

## Perbandingan & kebenaran  (`perbandingan`, 4 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 58 | `TYPEOF` | 0 | `a - t` | Nama jenis nilai puncak sebagai teks (`jenis(x)`). |
| 59 | `INSTANCEOF` | 0 | `a b - r` | Operator `saka` pada tipe. |
| 60 | `IN` | 0 | `a b - r` | Operator `ing`. |
| 61 | `TYPEOF_KIND` | 1 | `- -> t` | Nama tipe sebagai `jenis`. |

## Objek & properti  (`objek`, 21 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 62 | `GET_PROP` | 2 | `o k - v` | Baca properti `nama[a]`. Getter `nampa` dijalankan otomatis. |
| 63 | `SET_PROP` | 2 | `o k v - v` | Tulis properti `nama[a]`; menyisakan nilainya. |
| 64 | `GET_INDEX` | 0 | `o k - v` | Baca `obj[kunci]`. |
| 65 | `SET_INDEX` | 0 | `o k v - v` | Tulis `obj[kunci]`. |
| 66 | `GET_SUPER` | 1 | `o k - v` | Cari method `nama[a]` pada prototipe class INDUK (untuk `induk.f()`). |
| 67 | `SET_SUPER` | 1 | `o v k - v` | Panggil setter pada prototipe class induk. |
| 68 | `DEFINE` | 2 | `o k v - v` | Tambah properti `nama[a]` pada objek; objek tetap di stack (literal objek bernilai objek). |
| 69 | `DEFINE_METHOD` | 2 | `o k f - >` | Tambah method `nama[a]`; `b=1` berarti statis. Method bernama `wiwit` menjadi konstruktor. |
| 70 | `DEFINE_ACCESSOR` | 2 | `o k f - >` | Tambah accessor `nama[a]`; `b=1` berarti getter (`nampa`). |
| 71 | `DEFINE_FIELD` | 1 | `v - >` | Daftarkan nama field instance `nama[a]`; `b=1` berarti privat. |
| 71b | `DEFINE_FIELD_INIT` | 0 | `c f - c` | Simpan closure inisialisasi field instance (`y = <ekspresi>`) ke kelas. Dijalankan di `NEW` sebelum `wiwit`, induk-ke-anak. |
| 71c | `DEFINE_STATIC` | 1 | `c v - c` | Simpan field statis bernilai `nama[a]` ke kelas (dievaluasi sekali saat definisi). |
| 72 | `DELETE` | 0 | `o k - r` | Hapus properti; dorong boolean berhasil/tidak. |
| 73 | `GET_PROTO` | 0 | `o - p` | Dorong prototipe objek. |
| 74 | `SET_PROTO` | 0 | `o p - o` | Ganti prototipe objek. |
| 75 | `MAKE_OBJECT` | 0 | `- > o` | Alokasikan objek kosong (prototipe = `prototipe_dasar`). |
| 76 | `MAKE_ARRAY` | 1 | `n v... - a` | Alokasikan dhaptar berisi `a` nilai teratas (urutan kiri-ke-kanan). |
| 77 | `MAKE_ARRAY_SPREAD` | 0 | `v... - a` | Sama seperti `MAKE_ARRAY`, jumlah dihitung dari indeks yang dicatat `SPREAD_PUSH`. |
| 78 | `SPREAD_PUSH` | 0 | `i - i...` | Bentangkan iterable menjadi nilai-nilai terpisah; mencatat indeks awal untuk `MAKE_ARRAY_SPREAD`. |
| 79 | `SPREAD` | 0 | `dst src - >` | Bentangkan argumen (dipakai pemanggilan fungsi). |
| 80 | `ARRAY_PUSH` | 1 | `a v - >` | Dorong nilai ke dhaptar (dipakai internal). |
| 81 | `ITER_INIT` | 0 | `o i - i` |  |
| 82 | `ITER_NEXT` | 0 | `i - v >` |  |

## Fungsi & kelas  (`fungsi`, 11 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 83 | `CLOSURE` | 1 | `- > c` | Buat objek fungsi untuk `anak[a]` dari chunk ini, lalu tangkap upvalue. |
| 84 | `CALL` | 0 | `f a... - r` | Panggil: stack `[this, callee, arg0..argN-1]`. Argumen dievaluasi kiri-ke-kanan sebelum callee dijalankan. |
| 85 | `CALL_METHOD` | 1 | `o f a... - r` | Alias `CALL` (this sudah di stack). |
| 86 | `CALL_SPREAD` | 0 | `f a... - r` | Panggil dengan argumen dari iterable. |
| 87 | `TAIL_CALL` | 0 | `f a... - r` | Panggilan ekor (optimasi; diperlakukan sebagai `CALL`). |
| 88 | `RETURN` | 0 | `v - >` | Kembalikan nilai puncak dari frame saat ini. |
| 89 | `RETURN_UNDEF` | 0 | `- > ` | Kembalikan `mboh`. |
| 90 | `NEW` | 0 | `c a... - o` | Alokasikan instans dari class; konstruktor dipanggil dengan `this` = instans. |
| 91 | `FUNCTION` | 1 | `- > f` | Alias lawas `CLOSURE` (tidak dipakai). |
| 92 | `CLASS` | 0 | `p n i - c` |  |
| 93 | `INHERIT` | 0 | `c p - c` |  |

## Kontrol alur  (`kontrol`, 7 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 94 | `JUMP` | 1 | `- > ` | Lompat tak bersyarat ke `a`. |
| 95 | `JUMP_IF_FALSE` | 1 | `c - >` | Lompat ke `a` bila puncak FALSY. MEMBATAS (tidak pop). |
| 96 | `JUMP_IF_TRUE` | 1 | `c - >` | Lompat ke `a` bila puncak TRUTHY. MEMBATAS. |
| 97 | `JUMP_IF_NOT_NULLISH` | 0 | `v - >` | Lompat ke `a` bila puncak BUKAN nullish. MEMBATAS (dipakai `??`). |
| 98 | `JUMP_IF_NULLISH` | 1 | `c - >` | Lompat ke `a` bila puncak nullish. MEMBATAS. |
| 99 | `LOOP` | 1 | `- > ` | Alias `JUMP`. |
| 100 | `TEST_TRUTHY` | 0 | `c - >` | Dorong kebenaran puncak. |
| 100b | `PARAM_HADAH` | 1 | `- > b` | Dorong true bila argumen indeks `a` benar-benar diberikan pemanggil (`Frame::n_argumen > a`). Dipakai prolog parameter default supaya `f(mboh)` tidak tertukar dengan `f()` (D-036). |
| 100c | `INSTAN_DARI` | 1 | `o - b` | Dorong true kalau nilai puncak adalah instans class bernama `nama[a]` **atau** salah satu induknya. Dipakai `kasus <Kelas>:` pada `pilih` (D-039) dan pola bertipe `cocog`. |
| 100d | `COCOK_TIPE` | 1 | `v - > b` | Pop nilainya, dorong true kalau `rt::nama_jenis`-nya sama dengan `nama[a]`. Berbeda dari `CEK_TIPE` (yang melempar galat), ini untuk pola `cocog` yang harus bisa gagal. |

## Galat & handler  (`galat`, 4 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 101 | `THROW` | 0 | `v - > ` | Lempar nilai puncak. Unwinder mencari handler `coba` terdekat. |
| 102 | `TRY_BEGIN` | 2 | `- t i > ` | Daftarkan handler `coba`; `a` = ip `tangkep`, `b` = ip `intrigasan`. |
| 103 | `TRY_END` | 0 | `- > ` | Lepas handler `coba` yang paling dalam. |
| 104 | `FINALLY_END` | 0 | `- > ` | Tanda akhir blok `pungkasan`. |
| 104b | `TRY_KLAUSUL` | 2 | `- > ` | Daftarkan klausa `tangkep` pada handler `TRY_BEGIN`: `a` = indeks nama tipe kleru **+1** (`0` = tanpa tipe / tangkap semua), `b` = ip awal klausa. Unwinder memilih klausula pertama yang cocok; kalau tidak ada, `pungkasan` (jalur tolak) lalu galat naik ke luar (D-038). |

## Objek & properti  (`objek`, 2 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 105 | `IS_OBJECT` | 0 | `v - b` | Dorong true bila nilai adalah objek/kelas/peta. |
| 106 | `IS_ARRAY` | 0 | `v - b` | Dorong true bila nilai adalah dhaptar. |

## Pola (cocog)  (`cocog`, 2 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 107 | `MATCH_TEST` | 1 | `v p - >` | Uji pola (dipakai ekspansi `cocog`). |
| 108 | `MATCH_BIND` | 1 | `v p - >` | Binding pola. |

## Modul  (`modul`, 2 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 109 | `IMPORT` | 1 | `- > m` | Muat & evaluasi modul, dorong objek ekspornya. |
| 110 | `EXPORT` | 1 | `v - > ` | Simpan nilai ke objek ekspor modul aktif. |
| 111 | `GET_EXPORT` | 1 | `m - v` | Baca nama dari objek ekspor. Nama yang tidak ada = galat (bukan `mboh`). |

## Async & generator  (`fiber`, 2 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 112 | `YIELD` | 0 | `v - r` | `metokake`. Menunda generator: frame generator disalin ke continuation lalu pemanggil melanjutkan (lihat `docs/generator.md`). |
| 113 | `AWAIT` | 0 | `v - r` | `enteni`. Janji yang sudah selesai langsung dipakai; yang masih menunggu menunda seluruh rantai `async` (lihat `docs/async.md`). |

## Lain-lain  (`lain`, 3 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 114 | `DEBUGGER` | 0 | `- > ` | Titik henti debugger. |
| 115 | `NOP_LINE` | 1 | `- > ` | Alias `NOP`. |
| 116 | `UNDEF_LINE` | 1 | `- > ` | Alias `MBOH`. |

## Sel & live binding  (`variabel` / `modul`)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 158 | `GET_IMPORT` | 2 | `- > s` | Buat `SelObj` untuk pengikatan impor pada modul & indeks lalu; opcode `0` Remix yang dijalankan. Sel ini nanti diarahkan ke sel modul asal oleh `SEL_ALIAS`. |
| 159 | `SEL_ALIAS` | 1 | `s - > ` | Arahkan sel di puncak ke sel yang sudah ada, jadi pembacaan & penulisan diteruskan. Rantai alias ditelusuri sampai ke akar. |
| 160 | `GET_CELL` | 1 | `- > v` | Baca isi sel upvalue (bukan upvalue-nya). |
| 161 | `SET_CELL` | 1 | `v - > v` | Tulis isi sel upvalue; **harus** lewat `SelObj::tulis()` supaya alias ikut — menulis `nilai` langsung membuat live binding hanya satu arah. |
| 162 | `SEL_BUAT` | 0 | `v - > s` | Bungkus nilai menjadi `SelObj` baru (variabel modul yang diekspor, atau pengikat per-iterasi `kanggo`). |
| 163 | `SEL_SALIN` | 1 | `- > -` | Ganti sel pada slot `a` dengan sel BARU berisi nilai yang sama. Dipanggil tiap akhir iterasi `kanggo` supaya closure tiap iterasi menangkap sel berbeda (D-037). |

## Nilai runtime  (`objek`)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 168 | `MAKE_REGEX` | 1 | `- > r` | Bentuk objek `RegexObj` dari konstanta `pola` & `flag`. Pola rusak → `KleruRegex` (bisa ditangkap `coba`/`tangkep`). Lihat `docs/regex.md`. |
| 169 | `MAKE_TANGGAL` | 1 | `- > d` | Bentuk objek `Tanggal` dari konstanta ISO-8601. Teks tak dikenal → `mboh`. Lihat `docs/tanggal.md`. |

## Variabel, lokal, upvalue  (`variabel`, 1 opcode)

| # | Opcode | Operand | Tumpukan | Keterangan |
|---:|---|---:|---|---|
| 175 | `GET_GLOBAL_FUNC` | 1 | `- > f` | Alias lawas `GET_GLOBAL`. |

## Opcode belum diimplementasikan

Opcode berikut ada di `.def` agar ruang nama stabil, tapi memicu
`KleruInternal [I001]` bila bytecode memakainya:

- `BIGINT` (BigInt runtime, Fase 8)

## BatasanISA sementara

Batas 3 byte per instruksi membuat lompatan jangkauan 16-bit. Untuk program
dengan lebih dari 65535 instruksi per fungsi, byte `uint16_t` tidak cukup.
Program Basa Jawa realistis belum mencapai batas itu; Nevertheless ini
disebutkan sebagai penyimpangan dari spesifikasi (lihat `DECISIONS.md`).

## Contoh

```
$ jawa bytecode -e 'gawe f(a) { bali a * 2; } f(21)'
```

```
== fungsi <modul> ==
   slot=2 param=0 upvalue=0
   konst[0] = 21.000000
   nama[0] = f
      0  CLOSURE          a=0
  == fungsi f ==
     slot=2 param=1 upvalue=0
        0  GET_LOCAL        a=1
        1  KONSTAN          a=0
        2  MUL
        3  RETURN
        4  MBOH
        5  RETURN_UNDEF
      1  DEF_LOCAL        a=1
      2  MBOH
      3  GET_LOCAL        a=1
      4  KONSTAN          a=0
      5  CALL             a=1
      6  POP
```
