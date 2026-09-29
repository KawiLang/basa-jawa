# Pustaka standar Basa Jawa

Bagian ini mendokumentasikan apa yang **benar-benar ada** di `src/stdlib/` (`stdlib.cpp` + `stdlib_peta.cpp`).
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

## `Peta` & `Himpunan`

Keduanya diimplementasikan sebagai open addressing di `src/rt/object.cpp`
(`PetaObj`, `HimpunanObj`). `Himpunan` sebenarnya peta dengan kunci == nilai.

Constructor: `Peta()`, `Peta(peta_lain)`, `Peta({a: 1})` — dan
`Himpunan()`, `Himpunan([1, 2, 2])`, `Himpunan(peta)`. Kunci boleh nilai apa
saja; kunci tekstual dan numerik **tidak** tertukar (`peta[1]` bukan `peta["1"]`).

| Method `Peta` | Hasil |
|---|---|
| `p.dhawa` | jumlah pasangan aktif (properti, bukan method) |
| `p.get(k[, bawaan])` | nilai, atau `bawaan` kalau tidak ada |
| `p.set(k, v)` | **mengembalikan `p`** supaya bisa dirantai |
| `p.hapus(k)` | `bener` kalau ada yang dihapus |
| `p.ada(k)` | `bener`/`salah` |
| `p.kosong()` | `bener` kalau tidak ada pasangan |
| `p.bersih()` | kosongkan semua |
| `p.kunci()` / `p.nilai()` | dhaptar, urutan sisip |
| `p.entri()` | dhaptar pasangan `[kunci, nilai]` |
| `p.akeh("kunci"\|"nilai")` | dhaptar kunci atau nilai |

| Method `Himpunan` | Hasil |
|---|---|
| `h.dhapa` | jumlah anggota aktif (properti) |
| `h.tambah(x)` | **mengembalikan `h`** supaya bisa dirantai |
| `h.hapus(x)` / `h.ada(x)` | hapus / cek |
| `h.kosong()` / `h.bersih()` | kosong? / kosongkan |
| `h.ke_dhaptar()` | dhaptar anggota, urutan sisip |

Pembacaan langsung juga jalan: `peta["a"]` sama dengan `peta.get("a")`, dan
`peta["a"] = 1` sama dengan `peta.set("a", 1)`.

## `Janji` (method statis)

| Fungsi | Hasil |
|---|---|
| `Janji.all([...])` | Janji yang selesai setelah **semua** selesai; hasilnya dhaptar dengan nilai pada indeksnya. Satu yang ditolak langsung menolak gabungan. |
| `Janji.race([...])` | Janji yang selesai pada Janji **pertama** yang selesai. |
| `Janji.selesai(v)` | Janji yang sudah selesai dengan nilai `v` — berguna untuk menulis fungsi `mengko` tanpa `enteni`. |
| `Janji.tolak(e)` | Janji yang sudah ditolak. |

Nilai biasa (bukan Janji) di dalam iterable diperlakukan sebagai Janji yang
sudah selesai, sama seperti ECMAScript. Iterable kosong: `all` langsung selesai
dengan dhaptar kosong, `race` selesai dengan `mboh`.

Method instans: `j.then(f)`, `j.tangkep(f)`, `j.jenis` (`nunggu`/`slamet`/
`gagal`), `j.hasil`.

**Batas yang diketahui.** `Wektu.tundha` hanya mengurutkan timer, tidak
menunggu ms sungguhan (loop acara deterministik — lihat `docs/async.md`), jadi
`Janji.all` pada Janji dari timer selesai pada urutan timer pertama, bukan
setelah waktu sebenarnya berlalu.

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
| `==` | Setara longgar | `Angka` vs `Angka` longgar; tipe berbeda selalu tidak sama. |
| `mengko` / `enteni` | Async / await | Janji + loop acara deterministik; lihat [`async.md`](async.md). |

## Wektu (timer)

| Fungsi | Bentuk | Keterangan |
|---|---|---|
| `Wektu.tundha(ms)` | `Wektu.tundha(10)` | Janji yang **tuntas pada putaran timer**, bernilai `mboh`. `ms` hanya menentukan urutan relatif antar timer; tidak menunggu ms sungguhan. |
| `Wektu.teka()` | `Wektu.teka()` | Janji yang sudah selesai seketika (berguna untuk menyamakan bentuk kode sinkron & async). |

## Janji (Promise)

Dialihkan dari fungsi `mengko` atau `Wektu.*`.

| Bentuk | Keterangan |
|---|---|
| `enteni nilai` | Nilai biasa: identitas (`enteni 5` -> `5`). |
| `enteni janji` | Janji selesai: memakai `hasil`. Janji ditolak: melemparkan galat. Janji menunggu: menunda rantai `async`. |
| `janji.then(f)` | Handler fulfilled; mengembalikan Janji turunan. Tetap asynchronous walau Janji sudah selesai. |
| `janji.tangkep(f)` | Handler rejected (`catch`). |
| `janji.jenis` | `"nunggu"`, `"slamet"`, atau `"gagal"`. |
| `janji.hasil` | Nilai hasil, atau alasan penolakan. |

```jawa
mengko gawe ambil(x) {
  enteni Wektu.tundha(10);
  bali x * 2;
}

mengko gawe utama() {
  tetep a = enteni ambil(21);
  tulis(a);
}

utama();
tulis("dhisik");
// dhisik
// 42
```

**Penyimpangan yang disengaja:** pemanggil `mengko` menunggu sampai fungsi itu
selesai, jadi `tulis(f())` mencetak Janji yang masih `nunggu`, bukan hasilnya.
Pakai `tulis(enteni f())` untuk nilai akhir. Penjelasan lengkap di
[`async.md`](async.md).

## Regex (method bawaan pada objek `/pola/flag`)

Dokumentasi lengkap ada di [`regex.md`](regex.md); ringkasannya:

| Method | Hasil |
|---|---|
| `cocog(teks)` | `bener` kalau ada kecocokan di mana saja |
| `kabeh(teks)` | `bener` kalau SELURUH teks cocok |
| `ganti(teks, pengganti)` | teks baru; semua kecocokan diganti (`$&`, `$0`..`$9`) |
| `pecah(teks)` | dhaptar; tiap elemen `[seluruh, grup1, ...]` |
| `nilai(teks)` | dhaptar kelompok dari kecocokan pertama |
| `grup(teks, kelompok)` | teks satu kelompok (nomor atau nama) |
| `pola()` / `flag()` | teks pola / flag |

Pola yang terlalu patologis (`(a+)+b`) melempar `KleruRegex` yang bisa
ditangkap `coba`/`tangkep` — bukan menggantung, dan bukan diam-diam "tidak
cocok".

## `Tanggal`

Dokumentasi lengkap ada di [`tanggal.md`](tanggal.md); ringkasannya:

```jawa
Tanggal()                       // sekarang (UTC)
Tanggal.dari(tahun, bulan, hari, jam, menit, detik)
Tanggal.ms(milidetik)           // dari milidetik sejak epoch
Tanggal("2026-09-29T14:03:07Z") // dari teks ISO-8601; `mboh` kalau tak dikenal
```

Method instans: `ke_teks`, `ke_tanggal`, `ke_waktu`, `tahun`, `bulan`, `hari`,
`jam`, `menit`, `detik`, `milidetik`, `hari_dalam_minggu`, `nama_hari`,
`nama_bulan`, `ms`, `tambah_ms`, `tambah_hari`, `selisih`, `sebelum`, `sesudah`,
`sama_dengan`.

**Tidak ada zona waktu.** Hanya UTC, dan alasannya tertulis di `docs/tanggal.md`.

## Yang belum ada

Sebutkan eksplisit agar tidak disalahpahami sebagai "hilang":

- Modul bawaan `Matematika`, `Object`, `Dhaptar`, `Teks` versi lengkap.
- Zona waktu lokal & daylight saving; kalender selain Gregorian (Rejrah/Saka,
  Hijriah); format tanggal bebas selain ISO-8601.
- Regex: lookahead/lookbehind, backreference, kuantifier possessif, `\p{...}`,
  mode `n`, dan pencocokan berbasis titik kode (`u` belum berarti apa-apa --
  mesinnya byte-oriented).
- Akses berkas, proses, jaringan. Semua I/O masih blocking dan sinkron.
- Janji: `Janji.anySelesai` & `Janji.bungkus` (yang `all`/`race`/`selesai`/
  `tolak` sudah ada).
- Jam nyata: `Wektu.tundha` hanya mengurutkan, tidak menunggu.
- `Intl`, `Buffer`.
- Inspector, `Proxy`, `Reflect`.
