# Generator: `gawe*` + `metokake`

Dokumen ini menjelaskan bagaimana Basa Jawa menjalankan generator **secara
lazy** — dan kenapa itu **tidak memakai fiber**.

## Ringkas

```jawa
gawe* cacah(n) {
  kanggo (ana i = 1; i <= n; i++) metokake i;
}

tulis([...cacah(5)]);            // [1, 2, 3, 4, 5]

// generator tak berhingga: tinggi call stack tidak bertambah
gawe* tak_henti() {
  ana i = 0;
  nalika (bener) { metokake i; i = i + 1; }
}
const g = tak_henti();
g.next();  // 0
g.next();  // 1
mandheg;   // berhenti kapan saja
```

## Tanpa fiber

Dokumen `async.md` menjelaskan bahwa `enteni` menunda rantai async dengan
**menyalin frame** ke continuation (`struct Lanjutan`). Generator memakai
mechanism yang **persis sama** (`src/vm/vm_gen.cpp`):

1. `metokake nilai` memanggil `VM::suspensi_generator`:
   menyalin frame `akar_agen`..teratas beserta nilai stack-nya ke `Lanjutan`,
   menutup upvalue yang menunjuk ke rentang itu, lalu memangkas `frames_` dan
   `stack_` sehingga pemanggil melanjutkan dari instruksi setelah `CALL`.
2. `.next()` memanggil `lanjutkan_generator`: menyalin balik, mendorong nilai
   baru ke puncak stack, memulihkan frame, lalu menjalankan loop bytecode lagi
   sampai `metokake` berikutnya atau body selesai.

Jadi **`FIBER_CREATE` / `FIBER_RESUME` tidak pernah dipakai.** Keputusan ini
menggantikan D-019 ("generator mode-eager"). Lihat D-028.

## Bedanya dengan async

| | `mengko` / `enteni` | `gawe*` / `metokake` |
|---|---|---|
| Pemanggilan | langsung mengembalikan Janji; body jalan sampai `enteni` pertama | mengembalikan objek Generator; body jalan sampai `metokake` pertama |
| Penundaan | `entani` Janji yang menunggu | setiap `metokake` |
| Pemulihan | antrean mikrotugas / timer | pemanggil memanggil `.next()` atau `for..of` |
| Yang menunggu | rantai Janji (bisa bercabang) | satu generator (berantai linier) |

Satu perbedaan teknis penting: untuk async, continuation **tidak boleh** ikut
frame pemanggil — pemanggil sudah selesai. Untuk generator, pemanggil **hidup**
dan melanjutkan eksekusi biasa di antara dua `.next()`. Itu yang memaksa
`lanjutkan_generator` menggeser indeks slot frame (§ di bawah).

## Melanjutkan di stack yang sudah bergeser

BUG yang ditemukan saat mengimplementasikan ini (dan cepat ketahuan karena
`--gc-stress`): continuation dipulihkan dengan `stack_.resize(lan->slot_base)`.

Itu salah. Selama generator tertunda, pemanggil sudah melanjutkan dan **sudah
memakan slot** yang tadinya menyimpan objek generator:

```jawa
tulis([...cacah(5)]);
//  ^ SPREAD_PUSH mem-pop generator dari stack sebelum menjalankan generator
```

`resize` ke `lan->slot_base` akan mengisi slot yang sudah dibuang pemanggil
dengan nilai sampah — dan nilai itulah yang muncul sebagai elemen pertama
Dhaptar (`[undefined, 1, 2, 3, 4, 5]`).

Perbaikannya: pulihkan di **atas stack saat ini** dan geser indeks slot semua
frame sebesar selisihnya:

```cpp
const std::ptrdiff_t geser = stack_.size() - lan->slot_base;
for (const Value& v : lan->stack) stack_.push_back(v);
for (Frame& fr : lan->frame) {
    fr.slot_base += geser;
    if (fr.target_balas != kTanpaTarget && fr.target_balas != kBuangHasil) {
        fr.target_balas += geser;
    }
}
```

Upvalue yang menunjuk ke rentang yang disalin sudah **ditutup** saat ditunda
(`Upvalue::close`), jadi tidak ada pointer yang perlu ikut bergeser.

## Ambang loop

`lanjutkan_generator` menjalankan `jalankan_loop(ambang)` dengan
`ambang = frames_.size()` **setelah** continuation dipulihkan. Loop berhenti
begitu frame generator hilang — baik karena `metokake` lagi (frame dipangkas)
maupun karena body selesai (frame di-pop).

Kalau ambangnya `0`, loop akan melanjutkan eksekusi **frame pemanggil** di
dalam loop bersarang — gejalanya bytecode pemanggil dieksekusi dua kali.

Untuk async, ambangnya `k` (indeks frame **akar** rantai), bukan
`frames_.size()`: saat modul ikut menjadi akar (top-level `enteni`), frame
modul ikut dipulihkan dan harus ikut berjalan sampai selesai.

## API generator

| Bentuk | Arti |
|---|---|
| `g.next()` | `{ nilai, selesai }` — satu `metokake` per panggilan |
| `g.next(x)` | mengirim `x` sebagai hasil `metokake` (belum dipakai bahasa) |
| `g.nilai` | nilai `metokake` terakhir |
| `g.bali` | nilai balik `bali` (setelah selesai) |
| `g.selesai` | `bener` kalau body sudah selesai |
| `g.jenis` | `"jalan"` / `"selesai"` / `"galat"` |
| `jenis(g)` | `"generator"` |
| `[...g]` | spread: menjalankan sampai habis |
| `for (const x saka g)` | iterasi; bisa dihentikan `mandheg` |

Saat selesai, `next()` melaporkan `nilai = mboh` (sifat JavaScript) dan nilai
`bali` tersedia lewat `g.bali`.

## Batas

- **Tidak ada argumen/hasil dua arah.** `g.next(x)` menerima nilai tapi
  `metokake` belum punya bentuk `=ekspresi` untuk mengirim nilai keluar.
- **`return()` belum ada.** Untuk berhenti lebih awal, pemanggil harus
  relinquish generator-nya (menjatuhkannya) — tidak ada cara memaksa body
  berhenti dari luar.
- **Spread generator tak berhingga** akan menghabiskan space tanpa akhir,
  persis seperti `[...takHenti()]` di JavaScript. Ada pengaman 2²⁴ nilai dengan
  `KleruWates`.
- **Galat di body** dilempar ke pemanggil `.next()`. Kalau tidak tertangkap,
  program berhenti. Ini sedikit berbeda dari JavaScript, di mana generator
  yang `Generator.next()` juga melempar — jadi perilakunya
  sebenarnya sama.
- **Tidak ada `FIBER_CREATE`/`FIBER_RESUME`.** Opcode itu sengaja tidak dipakai;
  lihat D-028.
