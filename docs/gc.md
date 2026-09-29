# Kolektor sampah (GC)

Implementasi: `src/gc/heap.h`, `src/gc/heap.cpp`. Algoritma: **mark & sweep
presisi, non-moving, stop-the-world**.

## Prinsip

Presisi berarti **semua** objek yang bisa dijangkau dari root ditandai. Mark hanya
mengikuti pointer yang benar-benar ada, bukan tebakan konservatif. Ini mungkin
lebih lambat dari mark-konservatif, tetapi membuat bug akar yang lolos
deteksi `--gc-stress` jauh lebih jarang.

Non-moving berarti alamat objek tidak pernah berubah. Konsekuensi: sel upvalue
(yang menunjuk langsung ke stack) tidak perlu diperbarui saat GC berjalan.
Konsekuensi kedua: fragmentasi ditangani oleh allocator sistem, bukan oleh
kolektor.

## Root

Kolektor tidak tahu apa itu "program yang sedang berjalan"; `VM` mendaftarkan
callback (`gc::RootVisitor`) yang dipanggil tiap koleksi:

| Sumber root | Di mana |
|---|---|
| Stack nilai VM | `for (v : stack_) rv.rooted(v)` |
| `this` setiap frame | per frame |
| Sel upvalue yang dirujuk closure berjalan | per frame |
| Upvalue terbuka & sel yang sudah ditutup | `open_upvalues_`, `sel_tutup_` |
| Prototype dasar | `proto_dasar_`, `proto_dhaptar_` |
| Variabel global modul aktif | `modul_aktif->global` |
| Tabel global & method bawaan stdlib | visitor di `pasang_semua` |
| Handle native | `gc::HandleScope` (lihat bawah) |
| Akar sementara kompilasi | `Heap::akar_sementara` |
| Pool konstanta & nama fungsi | ditandai lewat `FungsiObj` |

## Handle & HandleScope

Kode native tidak boleh menyimpan `Value` mentah selama bisa ada alokasi
(alokasi bisa memicu koleksi pada mode stress). Pola yang dipakai:

```cpp
gc::HandleScope scope(heap);              // daftar nilai terlindungi
Value* slot = scope.slot(Value::mboh());  // alamat stabil
// ... alokasi ...
*slot = buat_teks(heap, "baru");          // nilai lama tetap hidup
```

Slot `HandleScope` disimpan di `std::deque` sehingga alamatnya stabil saat
`push_back`. Semua handle dibersihkan otomatis saat `scope` keluar.

## Akar sementara saat kompilasi

Kompiler membuat objek `TeksObj` untuk setiap konstanta dan nama properti
**sebelum** ada frame VM. Tanpa akar tambahan, `--gc-stress` akan membebaskan
konstanta itu (koleksi terjadi pada setiap alokasi). Karena itu
`Compiler::tambah_konstanta` dan `tambah_nama` mendaftarkan nilai ke
`Heap::akar_semetery`, yang dilepas `bersihkan_akar_sementara()` setelah
objek `FungsiObj` modul menjadi root (pool konstantanya sudah dijaga `FungsiObj`).

## Karantina alokasi

Ada jeda takNrăg antara "objek dialokasikan" dan "pemanggil mengisinya" (atau
menjadikannya root). Contoh: `heap.alokasi_baru<TeksObj>()` langsung memicu
koleksi pada mode stress, dan objek yang baru dibuat itu — belum punya anak,
belum di-root — akan tersapu **sebelum** pemanggil sempat mengisinya.

Solusinya: `Heap::kKarantina` (64 alokasi terakhir) tidak pernah disapu.
Konsekuensi yang jujur: jejak objek bisa tertinggal sampai 64 alokasi berikutnya
pada program yang melingkar terus-menerus. Batas konservatif ini sengaja
dipilih daripada use-after-free.

## Ambang koleksi

Koleksi normal (bukan stress) dipicu saat `bytes_aktif >= ambang_bytes()`. Ambang
diatur ulang setelah tiap koleksi: `1.5 × bytes_aktif`, dibatasi 256 KB .. 32 MB.
Koleksi yang pertama pasti terjadi saat ambang awal 256 KB terlampaui, sehingga
program kecil tidak pernah dikenai koleksi sama sekali.

## `--gc-stress`

Mengaktifkan `Heap(stress = true)`: **setiap** alokasi memicu `koleksi_full()`.
Tujuannya bukan performa melainkanKebocoran akar: setiap objek harus tetap hidup
selama masih dirujuk. Skrip `scripts/cek_golden.py --gc-stress` menjalankan
seluruh contoh acuan dengan mode ini.

`--log-gc` mencetak `[gc] koleksi #N: M objek, T ms` ke stderr.

## Batas memori

`--maks-memori MB` menentukan batas keras: ketika `bytes_aktif` lewat, proses
mencetak `KleruMemori [R010]` lalu keluar dengan status 1. Ini lebih mudah
didiagnosis daripada `std::bad_alloc` yang lolos tanpa pesan.

## Batas langkah & tumpukan

 Bukan GC, tapi keduanya
berinteraksi erat: koleksi yang sering memperlambat program yang memang sudah
berhenti. `--maks-langkah N` menghentikan
program yang melakukan loop tak berhingga; `--maks-tumpukan N` (default 10000)
mengubah rekursi tak berhingga dari stack overflow native menjadi
`KleruRentang [R003]` yang rapi.

## Yang belum

- **Generational GC**: koleksi penuh pada tiap ambang; untuk program alokasi
  berat ini mahal. Rencana: pisahkan buffer generasi muda & tua (lihat
  `PLAN.md` Fase 10).
- **Concurrent/incremental marking**: saat ini stop-the-world penuh.
- **Interior pointers**: pointer di tengah objek akan salah menandai; dalam
  implementasi ini hanya pointer ke awal objek yang pernah disimpan.
- **Finalizer / weak reference**: belum ada (Fase 8).
