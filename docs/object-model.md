# Model objek Basa Jawa

Ringkasan implementasi `src/rt/value.h`, `src/rt/object.h`, `src/rt/object.cpp`.

## Value: NaN-boxing 8 byte

Semua nilai bahasa menempati 8 byte (`static_assert(sizeof(Value) == 8)`),
sehingga `std::vector<Value>` setara `std::vector<double>`.

```
bit 63     : 1        penanda nilai boxed
bit 62..52 : 0x7FF    eksponen maksimum IEEE-754
bit 51..48 : TAG      4 bit -> 16 jenis
bit 47..0  : muatan   int32 / pointer 48-bit
```

Basis nilai boxed adalah `0xFFF0'0000'0000'0000`.

### Mengapa bit tanda, bukan bit 51

Desain awal memakai bit 51 sebagai penanda "quiet NaN" dan tag di bit 50..48.
Desain itu **tidak bisa dipakai**: suatu `double` biasa bisa persis memakai
pola itu, sehingga setiap nilai boxed akan terbaca sebagai `double`. Konsekuensinya
seluruh program salah klasifikasi (bug nyata yang ditemukan saat Fase 3).

Dengan bit 63 sebagai penanda, suatu `double` hanya dianggap boxed bila **bit
tandanya 1 DAN eksponennya 0x7FF** — persis pola "NaN negatif". Semua `double`
lain, termasuk `+Inf` dan NaN positif, aman.

### Normalisasi NaN

`Value::number(d)` menormalkan **setiap** NaN (apa pun bit tandanya) menjadi
nilai boxed bertag `NaNAngka`. Tanpa ini, `0.0/0.0` yang menghasilkan NaN
hardware akan menabrak pola boxed dan terbaca sebagai `mboh`.

Konsekuensi yang diketahui dan disepakati: `Value` round-trip
`number(NaN) -> as_number()` menghasilkan NaN, tetapi `number(NaN) != number(NaN)`
pada perbandingan bit. Kunci hash (`Value::key()`) menormalkan NaN sehingga
tabel hash tetap konsisten. Bandingkan NaN dengan `===` (tag `NaNAngka`
menghasilkan `salah`).

### Catatan LA57

Muatan hanya 48 bit, jadi alamat user-space harus < 2^48. Pada kernel dengan
LA57 aktif, jalankan dengan `-DJAWA_NO_NAN_BOX=ON`; mode itu memakai union
bertag 16 byte dengan API yang sama persis.

### Mode cadangan

`JAWA_NO_NAN_BOX` mengaktifkan `class Value` kedua (union 16 byte) dengan
metode yang identik. Proyek dikompilasi dengan satu mode saja; keduanya diuji
lewat `static_assert` ukuran.

## Tabel nilai

| Tag | Nilai | Keterangan |
|---:|---|---|
| 0 | `mboh` | undefined |
| 1 | `kosong` | null |
| 2 | `boole` | muatan 0/1 |
| 3 | `obyek` | pointer objek di heap |
| 4 | `bigint` | pointer objek BigInt |
| 5 | `simbol` | pointer objek Simbol |
| 6 | `NaNAngka` | NaN sebagai nilai angka |
| 7 | `angka_int32` | bilangan bulat 32-bit (optimasi) |
| 8..15 | — | cadangan |

## Objek

Semua objek turunan `rt::Obj` yang menyimpan `ObjHeader` (jenis + penanda GC).
Tiga bentuk armazenamento properti:

### Bentuk fast (shape)

```
ObyekObj { Shape* shape; Value* slot; size_t jumlah_slot; }
ArrayObj { Shape* shape; Value* elemen; size_t panjang, kapasitas; }
InstanceObj { ClassObj* kelas; vector<Value> slot; vector<string_view> nama_slot; }
```

Properti disimpan berurutan di `slot` sesuai urutan *shape*. `Shape` adalah
daftar properti immutable + peta transisi, jadi `shape->find(kunci)` berjalan
dalam O(jumlah properti) tanpa hash.

**Batasan Fase 3**: `Shape::transisi` mencari transisi dengan pemindaian linear
atas seluruh shape yang pernah dibuat, dan `ObyekObj::init` belum dipakai pada
jalur utama — objek yang dibuat `VM::buat_obyek()` langsung memakai mode
dictionary. Hidden class + inline cache yang sesungguhnya belum terpasang
(lihat `PLAN.md` Fase 4). Yang berfungsi sekarang adalah semantik dictionary
yang benar.

### Bentuk dictionary

`ObyekObj::dict` adalah `vector<pair<Value, Value>>`; kunci dibandingkan
dengan `rt::nilai_sama()` (teks dibandingkan **isi**, angka dibandingkan
numerik) — bukan perbandingan pointer.

### Bentuk class

`ClassObj` (constructor) memegang:
- `prototipe` — objek berisi method & accessor
- `induk` — class induk (untuk `turunan`)
- `konstruktor` — closure `wiwit`
- `inisial_field` — closure inisialisasi field instance (`y = <ekspresi>`);
  dijalankan di `NEW` sebelum `wiwit`, dari induk ke anak
- `nama_field` / `field_statis` — field instance
- `nama_statis` / `nilai_statis` — method & field `statis`

`InstanceObj` menunjuk `kelas`; pembacaan properti mencoba berurutan: slot
instance -> nilai statis class -> prototipe (rantai). Method turunan disalin ke
prototipe anak saat `CLASS` dieksekusi, begitu juga accessor dan konstruktor.

### Field privat

Field privat ditulis `#nama` dan disimpan sebagai slot biasa dengan nama
berawalan `#`. Privasi dijaga **kompilator** (nama `#x` tidak bisa ditulis di
luar kelas), bukan runtime.

## Rantai prototipe

`VM::ambil_properti` menelusuri:

```
obyek   : shape -> accessor (nampa) -> dict -> prototipe
dhaptar : indeks angka -> "dawa"/method bawaan -> prototipe
instans : nama_slot -> statis class -> prototipe class -> prototipe instans
golongan: statis class -> prototipe -> prototipe_dasar
teks    : indeks karakter -> method bawaan
fungsi  : "nama" / "dawa" -> prototipe
native  : "nama" / "dawa" -> prototipe
kleru   : "jeneng" / "pesan" / "sebab" / "jejak" / "baris" / "kolom" / "berkas"
```

Method bawaan Teks & Dhaptar dilayani `stdlib::cari_metode_builtin` (tabel
"jenis:nama"), bukan sebagai properti nyata pada prototipe — supaya
`Object.keys` dsb. tetap bersih.

## Kebenaran nilai

Perbandingan `==` longgar: `Angka` vs `Angka` dibandingkan numerik (NaN tidak
sama dengan NaN), tipe berbeda selalu tidak sama. Tidak ada koersi implisit
(D-007): `1 + "a"` adalah galat runtime, bukan `"1a"`.

`===` menambahkan syarat tipe: `1 === "1"` → `salah`.
