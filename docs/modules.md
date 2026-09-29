# Modul ES (`impor` / `ekspor`)

Basa Jawa memakai sistem modul ES: `impor` mengikat nilai dari modul lain,
`ekspor` membuat nilai tersebut bisa dibaca modul lain. Modul adalah berkas
`.jw` yang dievaluasi persis sekali, dengan cakupan globalnya sendiri.

## Bentuk

```jawa
// impor dengan daftar nama
impor { tambah, kali } saka "./matematika.jw";

// impor dengan alias (nama lokal berbeda)
impor { tambah minangka jumlah, kali minangka product } saka "./matematika.jw";

// impor seluruh modul sebagai satu objek
impor * minangka Matematika saka "./matematika.jw";

// impor ekspor `baku` (default)
impor Utama saka "./bentuk.jw";

// impor tanpa pengikat -- hanya menjalankan efek samping modul
impor "./inisialisasi.jw";
```

Bentuk ekspor:

```jawa
ekspor tetep KONST = 42;                       // ekspor deklarasi
ekspor gawe kali(a, b) { bali a * b; }        // ekspor fungsi
ekspor golongan Persegi { ... }               // ekspor kelas
ekspor baku "nilai bawaan";                    // ekspor default (nama "default")

ekspor { x minangka y, z };                    // daftar nama, boleh ditulis
                                              // SEBELUM deklarasinya
ekspor { y minangka z } saka "./lain.jw";      // re-export
```

Kata kunci Javanese-nya: `impor`, `ekspor`, `saka` (*from*), `minangka` (*as*),
dan `baku` (*default*). `baku` dan `default` bisa dipakai bergantian untuk
ekspor default; nama bakunya di objek ekspor selalu `"default"`.

## How it works

Linker ada di `src/vm/linker.cpp`:

1. **Resolusi path.** `./a.jw` relatif terhadap direktori modul **pengimpor**,
   bukan direktori kerja. Path dinormalkan (`./` dibuang, `/` dirapatkan) dan
   dipakai sebagai kunci cache.
2. **Baca & kompilasi.** Sumber dibaca lewat `VMOptions::baca_berkas` (bisa
   diinjeksi embedder; default-nya `<fstream>`), lalu lex → parse → compile.
3. **Evaluasi.** Modul dijalankan di frame-nya sendiri dengan
   `VM::jalankan_loop(frame_awal + 1)`. Modul yang sedang diimpor dievaluasi di
   tengah loop bytecode yang sedang berjalan — bukan loop bersarang, bukan fiber.
4. **Cakupan global.** Tiap modul punya `ModuleRecord` sendiri dengan peta
   `global`-nya, ditumpuk di `VM::modul_tumpukan_`. `GET_GLOBAL` selalu melihat
   modul teratas.
5. **Cache.** Modul dievaluasi **satu kali**. Impor kedua mengembalikan objek
   ekspor yang sama.

Objek ekspor setiap modul adalah `ObyekObj` biasa. Yang diekspor adalah
*nilainya* (closure, objek kelas, angka), bukan salinannya — jadi mutasi pada
objek yang diekspor terlihat oleh semua importer.

## Dua aturan hoisting

Tanpa dua aturan ini, impor siklik dan penulisan ekspor mendahului deklarasi
tidak akan bekerja. Keduanya diterapkan di `Compiler::compile` (lihat
`src/compile/compiler.cpp`).

### 1. Fungsi & kelas yang diekspor di-hoist ke atas modul

`ekspor gawe f() {...}` dikompilasi **di awal body modul**, sebelum statement
`impor` mana pun. Jadi `f` sudah ada di objek ekspor ketika modul lain
mengimpornya.

Ini yang membuat siklik bekerja:

```jawa
// a.jw
impor { g } saka "./b.jw";
ekspor gawe f() { bali "f:" + g(); }

// b.jw
impor { f } saka "./a.jw";
ekspor gawe g() { bali "g"; }

// utama.jw
impor { f } saka "./a.jw";
tulis(f());   // f:g
```

### 2. Slot pengikat impor dialokasikan lebih dulu

Sebelum hoisting di atas, kompililer mengalokasikan slot untuk **semua**
pengikat impor (`tambah`, `Matematika`, dst.) tanpa mengemit bytecode. Gunanya:
fungsi yang di-hoist harus menangkap *slot*, bukan nama global.

Tanpa ini, `f` di `a.jw` di-hoist saat `g` belum diimpor — jadi `g` di dalam
badan `f` akan terpecat sebagai **global**, yang tidak pernah diisi, dan
panggilannya gagal.

### 3. `ekspor { x }` boleh mendahului deklarasi

Kalau slot-nya belum ada saat baris `ekspor { x }` dievaluasi, entri-nya
ditunda dan diekspor di akhir body modul. Jadi dua bentuk ini setara:

```jawa
tetep N = 8;
ekspor { N };
```
```jawa
ekspor { N };
tetep N = 8;
```

Yang **tidak** ikut di-hoist adalah `tetep`/`ana` (nilai baru, bukan closure).
Kalau dua modul saling mengimpor `const` yang saling memanggil, impor kedua
akan melihat `mboh`. Ini batas yang sama seperti ECMAScript: yang boleh
meng parto bootstrap modul adalah deklarasi fungsi.

## Galat

| Situasi | Perilaku |
|---|---|
| Berkas tidak ada / tidak terbaca | `KleruModul` — bisa ditangkap `coba`/`tangkep` |
| Gagal lex/parse/compile | diagnostik lengkap dengan nama berkas, lalu `KleruModul` |
| Nama tidak diekspor | `KleruModul` yang menyebut daftar nama yang diekspor |
| Galat saat modul dievaluasi | diteruskan sebagai galat biasa; bisa ditangkap pemanggil |
| `ekspor` di luar modul | `KleruKonteks` |

Galat dari `IMPORT` diteruskan lewat `unwind_galat`, bukan langsung
`galat_.ada`, supaya `coba` di modul pemanggil bisa menanganinya. Modul yang
gagal dihapus dari cache supaya importer berikutnya tidak memakai modul
separuh jadi.

## Batasan

- **Bukan live binding.** `ekspor tetep N = 1;` diimpor, lalu `N` diubah di
  modul asalnya, importer **tidak** melihat perubahan. Yang diedarkan adalah
  nilai saat statement `ekspor` dievaluasi. Kalau nilai yang berubah perlu
  dibagikan, ekspor *fungsi* yang membaca state modul — closure-nya tetap
  hidup dan melihat perubahan.
- Jalur modul tidak dinormalkan sepenuhnya: `..` tidak di-resolve, dan symlink
  tidak diikuti. Path relatif bertingkat (`../../a.jw`) bekerja sebagai
  penggabungan teks, yang cukup untuk struktur proyek biasa.
- Tidak ada modul bawaan yang terdaftar. `impor * saka "std:matematika"`
  mencari objek global bernama `matematika`; karena belum ada, hasilnya galat.
- Tanpa titik masuk (`main`) tunggal: program multi-berkas dijalankan dengan
  `jawa run path/ke/utama.jw`, dan berkas lain tidak dieksekusi sebagai program
  utama kecuali dipilih eksplisit.
