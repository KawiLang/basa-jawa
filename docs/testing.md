# Menguji program Basa Jawa

Ada dua alat: `jawa tes` untuk program, dan `jawa fmt --cek` untuk basis kode
(diterangkan di [`fmt.md`](fmt.md)).

`jawa tes` menjalankan berkas uji `.jw` dan melaporkan assertion yang gagal.
Tidak perlu framework dari luar: assertion adalah fungsi bawaan yang dipasang
`jawa tes` ke dalam program uji.

```bash
jawa tes tests/tes/                 # seluruh berkas uji di direktori
jawa tes tests/tes/bahasa.tes.jw    # satu berkas
jawa tes --gc-stress tests/tes/     # koleksi GC tiap alokasi
```

Kode keluar `0` bila semua assertion lulus, `1` bila ada yang gagal, `2` bila
dipakai dengan salah (mis. tanpa path).

---

## 1. Menulis berkas uji

Setiap berkas uji adalah program Basa Jawa biasa. Tulis perbandingan langsung
di tubuh program:

```jawa
// tests/tes/contoh.tes.jw
pratelas(1 + 1, 2);
pratelas("Halo".dawa(), 4);
wajib_bener([1, 2, 3].saben((x) => x > 0));
```

Tidak ada fungsi registrasi. Berkas dijalankan sampai habis; assertion yang
gagal **tidak** menghentikan program, jadi seluruh kegagalan di satu berkas
dilaporkan sekaligus.

### Fungsi bawaan

| Fungsi | Arti |
|---|---|
| `pratelas(nilai, harapan, pesan?)` | kedua nilai harus sama **secara mendalam** |
| `wajib_bener(nilai, pesan?)` | nilai harus bernilai `bener` |
| `wajib_salah(nilai, pesan?)` | nilai harus bernilai `salah` |
| `wajib_lempar(fungsi, pesan?)` | fungsi harus melempar nilai apa pun |

Argumen `pesan` bersifat opsional; kalau diisi, ia disisipkan di depan laporan
kegagalan.

**Kenapa bukan `bener()` dan `salah()`?** Keduanya adalah kata kunci
(`JAWA_KEYWORD` di `src/lex/keywords.def`), jadi tidak bisa dipakai sebagai
nama fungsi. Karena itu assertion yang menuntut nilai boolean memakai awalan
`wajib_`.

### Perbandingan mendalam

`pratelas` tidak memakai `rt::nilai_sama()` secara langsung. Fungsi itu
membandingkan **bit** untuk objek non-teks, sehingga dua `TeksObj` dengan isi
sama tapi berbeda alamat dianggap berbeda. `pratelas` membandingkan:

- angka: banding nilainya (`DuduAngka` sama dengan `DuduAngka`)
- boolean / `kosong` / `mboh`: banding nilainya
- teks: banding **isinya**
- dhaptar: panjang sama, lalu setiap elemen dibandingkan recursively
- obyek biasa: himpunan properti sama, lalu setiap nilainya dibandingkan
- galat (`Kleru`): nama dan pesannya sama
- lainnya: banding bit

Objek yang muncul dua kali (siklus) di bandingkan hanya sampai kedalaman 32.

---

## 2. Keluaran

Berkas yang lulus:

```
$ jawa tes tests/tes/
uji tests/tes/bahasa.tes.jw
  v 69 assertion lulus
uji tests/tes/panggilan.tes.jw
  v 16 assertion lulus

85 assertion ing 2 berkas — semua lulus
```

Berkas yang gagal:

```
$ jawa tes tests/tes/contoh.tes.jw
uji tests/tes/contoh.tes.jw
  x (12:1) pratelas gagal
      entuk: "Halo"
    harapan: "Hallo"
  x (20:1) nilai kudu `bener`: ngae nilainya salah
      entuk: 0

1 assertion ing 1 berkas — 2 assertion GAGAL
```

Nomor baris diambil dari `pos_sumber_` VM, yang diisi opcode `NOP_LINE` setiap
kali kompilator melewati pergantian baris. Tanpa itu semua laporan akan
menunjuk baris 1.

Berkas yang gagal dikompilasi atau gagal saat dijalankan dilaporkan terpisah:

```
uji tests/tes/ rusak.jw
  x berkas gagal dijalankan
  <jejak stack ke stderr>
```

---

## 3. Penemuan berkas

`jawa tes <path>`:

- `<path>` adalah berkas `.jw` → berkas itu dijalankan, apa pun namanya.
- `<path>` adalah direktori → dipindai **rekursif**, terurut, dan semua
  `.tes.jw` diambil. Berkas `.jw` biasa hanya diambil bila berada **langsung**
  di direktori yang disebut, supaya `jawa tes tests/` tidak ikut menjalankan
  berkas pendukung yang kebetulan ada di sana.

Suffiks `.tes.jw` sebaiknya dipakai untuk berkas uji supaya intention-nya jelas
dan berkas tersebut ikut terbawa saat direktori di-scan dari atas.

---

## 4. Batasan

- **Satu VM per berkas uji.** Global yang dideklarasikan program uji tidak
  bocor ke berkas lain. Konsekuensinya, tabel global pustaka standar harus
  dimiliki VM (lihat `stdlib::State`), bukan `static` global.
- **`tulis` dialihkan ke buffer.** Keluaran program uji tidak tercampur dengan
  laporan assertion. Program uji tetap boleh memakainya untuk debug, tapi
  hasilnya tidak akan terlihat di terminal.
- **Assertion yang memanggil balik ke bytecode paling baik di berkas sendiri.**
  `wajib_lempar` adalah native function yang memanggil fungsi Basa Jawa; itu
  reentrancy native -> JS yang masuk ke loop bytecode yang sama. Berkas
  `panggilan.tes.jw` menguji jalur itu secara terpisah karena setiap pemanggilan
  balik memberi kesempatan bagi frame nyasar muncul.
- **Tidak ada isolasi galat.** Galat yang tidak tertangani menghentikan berkas
  pada titik itu; assertion setelahnya tidak dievaluasi.
- **Tidak ada time-out per berkas.** Loop tak berhingga akan menggantung
  `jawa tes`. Batas langkah bawaan (`--maks-langkah`) juga berlaku, jadi
  `jawa tes --maks-langkah 100000 tests/tes/` aman untuk berkas uji yang
  tidak sengaja menjalankan loop abadi.

---

## 5. Hubungan dengan suite lain

| Suite | Yang diuji | Runner |
|---|---|---|
| `tests/unit/*.cpp` | lexer, parser, VM, GC, stdlib (level C++) | `ctest` |
| `tests/tes/*.tes.jw` | **level bahasa** — sintaks & semantik | `jawa tes` |
| `tests/golden/*.out` | keluaran persis program contoh | `scripts/cek_golden.py` |
| `tests/fuzz/*` | ketahanan terhadap input acak | `scripts/fuzz_jalankan.py` |

`tests/tes/` memeriksa bahasa apa yang dilihat pengguna: apakah `kanggo ... saka`
benar-benar mengiterasi, apakah closure membaca variabel yang benar, apakah
`mandheg` keluar dari loop dengan stack yang seimbang. Semua itu sulit ditulis
sebagai unit test C++ karena harus melewati kompilator.

`ctest` menjalankan `tests/tes/` dua kali: biasa, dan dengan `--gc-stress`.
