# `Tanggal`

Nilai waktu untuk Basa Jawa — ada sejak 0.11.0.

## Keputusan yang mendasari: UTC saja

`Tanggal` **tidak** menyimpan zona waktu. Nilainya adalah milidetik sejak epoch
UTC (`1970-01-01T00:00:00.000Z`).

Alasannya jujur: tanpa basis data zona waktu yang andal (basis data IANA, besar dan
dengan aturan yang berubah setiap beberapa tahun), "jam berapa di sini" lebih
sering salah daripada tidak dijawab. Jalan yang lebih stabil adalah
menyimpan *titik waktu* dan membiarkan pemanggil memutuskan bagaimana
menampilkannya.

Kalau nanti zona waktu ditambahkan, itu penambahan, bukan perubahan diam-diam:
nilai yang sekarang sudah ada tidak akan bergeser.

## Membuat nilai

```jawa
ana t = Tanggal();                        // sekarang (UTC)
ana u = Tanggal.dari(2026, 9, 29, 14, 3, 7);
ana v = Tanggal("2026-09-29T14:03:07Z");  // dari teks ISO-8601
ana w = Tanggal.ms(1759147387000);        // dari milidetik sejak epoch
tulis(Tanggal.sekarang().ke_tanggal());
```

* `Tanggal()` — waktu sekarang.
* `Tanggal.dari(tahun, bulan, hari, jam, menit, detik)` — dari komponen kalender.
  Komponen yang tidak diberi dianggap 0. **Rollover**: hari di luar rentang bulan
  digeser, seperti `Date` ECMAScript — `Tanggal.dari(2026, 2, 30).bulan()` = 3,
  `Tanggal.dari(2026, 13, 1).tahun()` = 2027.
* `Tanggal(angka)` — dari milidetik sejak epoch.
* `Tanggal("...")` — dari teks ISO-8601 (lihat di bawah). Teks yang tidak bisa
  diparse menghasilkan `mboh`, sama seperti `Teks("x")` menghasilkan `NaN`.

## Format teks

| Method | Bentuk | Contoh |
|---|---|---|
| `ke_teks()` | ISO-8601 lengkap | `2026-09-29T14:03:07.250Z` |
| `ke_tanggal()` | tanggal saja | `2026-09-29` |
| `ke_waktu()` | waktu saja | `14:03:07.250Z` |

Teks yang diterima `Tanggal("...")`:

```
YYYY-MM-DD
YYYY-MM-DDTHH:MM
YYYY-MM-DDTHH:MM:SS
YYYY-MM-DDTHH:MM:SS.sss
```

`T` boleh diganti spasi. Detik boleh pecahan (`7.5` = 7 detik 500 ms). Akhiran
`Z` diterima dan opsional; `+HH:MM` juga diterima. Bulan di luar 1..12, hari di
luar 1..31, jam di luar 0..23, menit/detik di luar 0..59, atau teks yang tidak
cocok bentuk di atas menghasilkan `mboh`.

Perhatikan batasannya: teks yang salah bulan **ditolak**, sementara
`Tanggal.dari(2026, 13, 1)` **di-rollover** ke 2027. Itu disengaja — dalam
pemanggilan konstruktor pengguna berarti "hitung sendiri", sedangkan teks
ISO-8601 yang salah bulan hampir selalu berarti input yang rusak.

## Komponen

Semua mengembalikan angka.

```jawa
tulis(t.tahun(), t.bulan(), t.hari());
tulis(t.jam(), t.menit(), t.detik(), t.milidetik());
tulis(t.hari_dalam_minggu());   // 0 = Minggu ... 6 = Sabtu
tulis(t.nama_hari());           // "Selasa"
tulis(t.nama_bulan());          // "September"
tulis(t.ms());                  // milidetik sejak epoch
```

`hari_dalam_minggu()` memakai 0 = Minggu, mengikuti `Date` ECMAScript
(`getDay()`), bukan kalender Jawa Tradisional (yang memakai Rejrah). Kalender
Rejrah/Saka **tidak** ada di sini; kalau dibutuhkan, itu penambahan tersendiri,
dan lebih tepat sebagai pustaka pihak ketiga.

## Aritmetika & perbandingan

```jawa
ana a = Tanggal.dari(2026, 1, 1);
ana b = Tanggal.dari(2026, 1, 2);

tulis(a.selisih(b));            // 86400000  (ms, lain - ini)
tulis(a.tambah_ms(1000).detik());
tulis(a.tambah_hari(1).hari());  // 2
tulis(a.sebelum(b));             // true
tulis(b.sesudah(a));             // true
tulis(a.sama_dengan(a));         // true
```

`tambah_ms` mengembalikan nilai `Tanggal` baru — nilai lama tidak berubah.
`tambah_hari` menambah jumlah hari kalender; karena zona waktu tidak dipakai,
sama dengan menambah 86.400.000 ms (tidak ada "hari 23 atau 25 jam" seperti di
zona waktu).

## Nilai sebagai nilai biasa

`Tanggal` adalah objek biasa: bisa jadi elemen dhaptar, nilai properti, kunci
peta, dan dibandingkan dengan `sama_dengan` (bukan `==` — `==` pada objek
mengecek identitas, sama seperti di bahasa lain).

`jenis(t)` menghasilkan `"tanggal"`. `tulis(t)` memakai `ke_teks()`.

## Kalender proleptis Gregorian

Hitungan memakai algoritma `days_from_civil` — kalender Gregorian yang
diperpanjang ke belakang tanpa batas (sehingga tahun negatif dan tahun sebelum
masehi bekerja, seperti yang dilakukan ECMAScript). Aturan kabisat yang berlaku:
tahun habis dibagi 4, **kecuali** yang habis dibagi 100, **kecuali lagi** yang
juga habis dibagi 400.

```jawa
tulis(Tanggal.dari(2000, 2, 29).hari());  // 29 (2000 habis dibagi 400)
tulis(Tanggal.dari(1900, 2, 28).hari());  // 28 (1900 habis dibagi 100)
tulis(Tanggal.dari(2024, 2, 29).hari());  // 29
tulis(Tanggal.ms(-1).ke_teks());          // "1969-12-31T23:59:59.999Z"
```

## Batasan yang jujur

* **Tidak ada zona waktu, tidak ada daylight saving.** Konversi ke/from waktu
  lokal tidak tersedia sama sekali.
* **Tidak ada parsing format bebas.** Hanya ISO-8601 di atas; `02/09/2026`
  (yang ambigu antar negara) ditolak, bukan ditebak.
* **Tidak ada kalender selain Gregorian.** Tidak ada Rejrah/Jawa, tidak ada
  bulan Hijriah, tidak ada ISO-8601 week date (`2026-W40-1`).
* **`milidetik()` mengembalikan milidetik dalam detik** (0..999), mengikuti
  `Date.prototype.getMilliseconds()`. Untuk milidetik sejak epoch, pakai `ms()`.
  Nama keduanya mirip, dan itu memang jebakan — tapi mengikuti `Date` lebih
  berguna daripada mengarang konvensi sendiri.
* Batas milidetik: di luar ±8.64e15 ms (tahun ±275760) hasilnya tidak
  dijamin benar, karena presisi `double` tidak cukup.

## Lihat juga

* `docs/stdlib.md` — pustaka standar lain.
* `docs/regex.md` — pola `/pola/flag`.
* `tests/tes/tanggal.tes.jw` — 63 assertion.
