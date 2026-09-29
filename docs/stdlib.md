# Pustaka standar Basa Jawa

Bagian ini mendokumentasikan apa yang **benar-benar ada** di `src/stdlib/stdlib.cpp`.
Yang tidak terdaftar di sini **belum ada** — lihat `STATUS.md`.

Semua nama API memakai kata kunci Bahasa Jawa; argumen & nilai balik mengikuti
semantik Bagian 3 (tanpa koersi implisit, D-007).

## Keluaran

| Fungsi | Bentuk | Keterangan |
|---|---|---|
| `tulis(...)` | `tulis(nilai, ...)` | Tulis satu baris ke stdout, argumen dipisah satu spasi. `undefined` dicetak sebagai `undefined`. |
| `tulis_nol(...)` | `tulis_nol(nilai, ...)` | Sama seperti `tulis` tanpa baris baru. |
| `tulis_kleru(...)` | `tulis_kleru(nilai, ...)` | Sama seperti `tulis`, ke **stderr**. |
| `konsol.cathet/log/informasi` | `konsol.log(...)` | Alias `tulis`. |
| `konsol.peringatan/kleru` | `konsol.kleru(...)` | Alias `tulis_kleru`. |
| `konsol.debug` | `konsol.debug(...)` | Alias `tulis`. |
| `metu(kode)` | `metu([kode])` | Keluar dari proses dengan status `kode` (default 0). |

## Konversi & pemeriksaan

| Fungsi | Bentuk | Keterangan |
|---|---|---|
| `Teks(x)` | `Teks(nilai)` | Mengubah nilai apa pun menjadi teks (toa `nilai_ke_teks`). |
| `Angka(x)` | `Angka(nilai)` | Ke angka; `NaN` bila tidak bisa diurai. |
| `Boole(x)` | `Boole(nilai)` | Ke boolean memakai aturan kebenaran Bagian 3.5. |
| `jenis(x)` | `jenis(nilai)` | Nama jenis: `mboh`, `kosong`, `boole`, `angka`, `teks`, `dhaptar`, `peta`, `fungsi`, `obyek`, ... |
| `pratela(a, b)` | `pratela(a, b)` | Perbandingan kebenaran (`a == b` secara longgar). |
| `Object.keys(o)` | | Belum diimplementasikan. |

## Konstanta global

| Nama | Nilai |
|---|---|
| `DuduAngka` | `NaN` |
| `Tak_Wates` | `Infinity` |
| `globalIki` | Objek global yang bisa ditambahkan properti |
| `Mathematika` | Alias `Matematika` (lihat di bawah) |

## `Matematika` (alias `Math`)

| Fungsi | Hasil |
|---|---|
| `Matematika.mutlak(x)` | `abs(x)` |
| `Matematika.lantai(x)` | `floor(x)` |
| `Matematika.bunder(x)` | `ceil(x)` |
| `Matematika.akar(x)` | `sqrt(x)` |
| `Matematika.pangkat(a, b)` | `a ** b` |
| `Matematika.paling_kecil(a, b)` | `min(a, b)` |
| `Matematika.paling_besar(a, b)` | `max(a, b)` |
| `Matematika.sin/cos/tan(x)` | trigonometri (radian) |
| `Matematika.log(x)`, `log2`, `log10`, `exp` | logaritma & eksponen |
| `Matematika.tanda(x)` | `-1` atau `1` |
| `Matematika.trunc(x)` | potong bagian desimal |
| `Matematika.faktorial(n)` | `n!` untuk bilangan bulat non-negatif, `NaN` bila bukan |
| `Matematika.PI`, `E`, `LN2`, `LN10`, `SQRT2` | konstanta standar |

## `StdAksara`

| Fungsi | Bentuk | Keterangan |
|---|---|---|
| `StdAksara.angka_jawa(n)` | → teks | Angka ke aksara Jawa. Digit U+A9D0..U+A9D9 (`JAVANESE DIGIT ZERO`..`NINE`). `21` → `꧒꧑`; angka negatif didahului U+A9CA (`JAVANESE PADA ADEG`, tanda baca). |
| `StdAksara.angka_arab(t)` | → angka | Kebalikan dari `angka_jawa`; `NaN` bila ada karakter bukan digit aksara. |

## Dhaptar (method bawaan)

| Method | Bentuk | Keterangan |
|---|---|---|
| `.tambah(v, ...)` | `d.tambah(a, b)` | Dorong beberapa nilai, kembalikan panjang baru. |
| `.copot()` | `d.copot()` | Buang & kembalikan elemen terakhir, atau `undefined`. |
| `.gabung(pemisah)` | `d.gabung(", ")` | Gabung dengan teks. |
| `.peta(f)` | `d.peta(x => x * 2)` | `map` |
| `.saring(f)` | `d.saring(x => x > 2)` | `filter` |
| `.saben(f)` | `d.saben(x => x > 0)` | `every` |
| `.kurangi(f)` | `d.kurangi((a, b) => a + b)` | `reduce` tanpa nilai awal. |
| `.nilai()` | `d.nilai()` | Elemen terakhir. |
| `.dawa` / `.panjang` | `d.dawa` | Panjang (properti, bukan method). |

Belum: `sort`, `saring_index` (`findIndex`), `ndak`/`ngisor`, `gabung` array, `ndak_dene` (`some`).

## Teks (method bawaan)

| Method | Bentuk | Keterangan |
|---|---|---|
| `.dawa` | `t.dawa` | Panjang dalam **code point** (bukan byte/UTF-16). |
| `.huruf_gedhe()` | `t.huruf_gedhe()` | Huruf besar (ASCII saja). |
| `.huruf_kecil()` | `t.huruf_kecil()` | Huruf kecil (ASCII saja). |
| `.pangkas(a, b?)` | `t.pangkas(1, 4)` | Potong. |
| `.ganti(cari, ganti)` | `t.ganti("a", "b")` | Ganti semua kemunculan. |
| `.pecah(pisah)` | `t.pecah(",")` | `split`; `""` memecah per karakter. |
| `.termasuk(x)` | `t.termasuk("a")` | `includes` (substring). |
| `.mulainya_dengan(x)` | `t.mulainya_dengan("Ha")` | `startsWith` |

Indeks numerik pada teks mengembalikan satu karakter (code point):
`"abc".dawa` → 3, `"abc"[1]` → `"b"`.

## `JSON`

| Fungsi | Bentuk | Keterangan |
|---|---|---|
| `JSON.gawe_teks(v)` | `JSON.gawe_teks(nilai)` | Stringify. **Saat ini** hanya memanggil `nilai_ke_teks` — belum serializer JSON sungguhan. |

## Operator & bahasa

Bukan pustaka, tapi perlu dicatat karena sering disalahpahami:

| Bentuk | Arti | Catatan |
|---|---|---|
| `a + b` | Penjumlahan / gabungan | Tanpa koersi: `Angka + Teks` **galat** (D-007). |
| `a - b`, `a * b`, `a / b`, `a % b` | Aritmetika | Angka saja. |
| `a ** b` | Pangkat | Angka saja. |
| `a ?? b` | Nullish coalescing | Mengembalikan `b` hanya bila `a` nullish. |
| `a |> f` | Pipeline | Setara `f(a)`. |
| `a saka` | `instanceof` | Hanya untuk tipe class. |
| `a ing b` | `in` | Kunci dhaptar/peta atau `jenis(b) == a`. |
| `a === b` | Setara ketat | Tipe harus sama. |
| `==` | Setara longgar | `Angka` vs `Angka` longgar; tipe berbeda selalu tidak sama.; tipe berbeda selalu tidak sama. |

## Yang belum ada

Sebutkan eksplisit agar tidak disalahpahami sebagai "hilang":

- Modul bawaan `Matematika`, `Object`, `Dhaptar`, `Teks` versi lengkap.
- `Date` / `Tanggal`, `RegExp` runtime, `Map`/`Peta` komprehensif, `Set`/`Himpunan`.
- Akses berkas, proses, jaringan.
- `Intl`, `Buffer`.
- Inspector, `Proxy`, `Reflect`.
