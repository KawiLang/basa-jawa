# Async: Janji, `enteni`, dan loop acara

Dokumen ini menjelaskan cara Basa Jawa menjalankan `mengko` (async) dan
`enteni` (await) **tanpa fiber** dan **tanpa stack C++ terpisah**.

## Ringkas

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
```

Keluaran:

```
dhisik
42
```

`dhisik` tercetak **lebih dulu** karena `enteni` menunda rantai `async`;
kode sinkron di bawahnya berjalan sampai selesai sebelum Janji dituntaskan.

## Arsitektur: continuation, bukan fiber

Kebanyakan mesin JavaScript memakai **fiber** (stack terpisah peroutine) agar
`await` bisa menunda di tengah fungsi. Basa Jawa tidak memakai cara itu.
Alasannya:

1. Fiber berarti stack C++ tambahan per rantai aktif — mahal, dan sulit di-port
   ke platform tanpa `ucontext`/`setjmp` yang andal.
2. VM Basa Jawa sudah memakai **satu loop bytecode** dengan frame eksplisit
   (lihat D-003, D-015). Semua yang dibutuhkan penangguan sebenarnya sudah
   ada: nomor instruksi (`Frame::ip`) dan nilai stack.

Jadi `enteni` pada Janji yang masih menunggu hanya melakukan tiga hal
(`VM::suspensi_async`, `src/vm/vm_async.cpp`):

1. **Menyalin** frame `akar_async`..teratas beserta nilai stack-nya ke
   `struct Lanjutan` (`src/vm/vm.h`).
2. **Menutup** setiap upvalue yang menunjuk ke rentang stack yang disalin
   (`Upvalue::close`), supaya selnya menyimpan nilainya sendiri. Tanpa ini
   pointer upvalue akan menggantung setelah stack dipakai ulang.
3. **Memangkas** `frames_` dan `stack_`, sehingga frame pemanggil melanjutkan
   dari instruksi setelah `CALL` (nomor instruksinya sudah tersimpan).

Ketika Janji selesai, `VM::lanjutkan_async` melakukan kebalikannya: menyalin
kembali nilai stack, mendorong hasil `enteni` ke puncak, memulihkan frame, lalu
menjalankan loop bytecode lagi.

### Kenapa tidak perlu fiber

`Frame` sudah menyimpan segalanya yang dibutuhkan untuk melanjutkan: `ip`,
`slot_base`, `this_val`, `handlers` (blok `coba`), daftar upvalue, dan Janji
yang harus dituntaskan. Tidak ada state C++ di antara opcode yang harus bertahan
selama penangguan — semua nilai yang bertahan tinggal di stack VM.

## Loop acara

Setelah seluruh kode sinkron selesai, `VM::jalankan_loop_acara` berjalan
sampai tidak ada pekerjaan lagi. Urutannya meniru ECMAScript:

```
ulangi:
    jika ada mikrotugas: jalankan SEMUA mikrotugas; ulangi
    jika ada timer:     nyalakan SATU timer (tunda terkecil dulu); ulangi
    berhenti
```

- **Mikrotugas** — penyelesaian Janji dan handler `.then`/`.tangkep`.
  Mikrotugas **selalu** mendahului timer, sehingga `.then` pada Janji yang
  baru selesai berjalan di giliran yang sama.
- **Timer** — hanya `Wektu.tundha`. Dinyalakan satu per satu, diurutkan
  `tunda_ms` terkecil; ties dipecah menurut urutan pemanggilan.

### `Wektu.tundha` tidak benar-benar menunggu

`Wektu.tundha(ms)` **tidak** memblokir thread maupun benar-benar sleeps
`ms`. Janjinya diselesaikan pada putaran timer pertama setelah seluruh
mikrotugas habis, dengan nilai `mboh` (setara `setTimeout` yang memanggil
callback tanpa argumen). Yang dijamin adalah **urutan relatif** antar timer
sesuai `ms`, bukan durasi absolutnya.

Konsekuensinya: program Basa Jawa yang hanya memakai `Wektu.tundha` bersifat
deterministik dan bisa diuji dengan golden file. Program yang butuh jam dunia
sesuai perlu `Tanggal` (Fase 8) dan loop acara berbasis I/O.

## Model Janji

`JanjiObj` (`src/rt/object.h`) punya tiga status: `Menunggu`, `Slamet`,
`Gagal`. Setiap Janji menyimpan `hasil` (nilai atau alasan penolakan).

| Bentuk | Perilaku |
|---|---|
| `enteni nilai` biasa | identitas: `enteni 5` -> `5` |
| `enteni Janji` selesai | memakai `hasil` langsung |
| `enteni Janji` ditolak | melempar `hasil` sebagai galat (bisa ditangkap `coba`) |
| `enteni Janji` menunggu | menunda rantai `async` |
| `Janji.then(f)` | handler fulfilled; mengembalikan Janji turunan |
| `Janji.tangkep(f)` | handler rejected; mengembalikan Janji turunan |
| `Janji.jenis` | `"nunggu"` / `"slamet"` / `"gagal"` |
| `Janji.hasil` | nilai hasil (atau alasan penolakan) |

`.then(f)` dan `.tangkep(f)` pada Janji yang **sudah selesai** tetap
menjadwalkan handler sebagai mikrotugas, bukan memanggilnya langsung. Ini
meniru `Promise.then` pada promise yang sudah settled: handler selalu
asynchronous.

## Galat di dalam `mengko`

Galat yang tidak tertangkap di dalam fungsi `mengko` menjadi **penolakan Janji**,
bukan galat program. `VM::unwind_galat` menerapkan hal ini: saat chain
unwind mencapai frame yang punya `janji_async`, Janji itu ditolak dan
frame-nya dilepas — frame pemanggil di bawahnya tetap utuh dan melanjutkan dari
instruksi setelah `CALL`.

Jadi program berikut tidak berhenti diam-diam:

```jawa
mengko gawe gagal() {
  enteni Wektu.tundha(1);
  uncal "walah";
}

gagal().tangkep(e => tulis("ditangkep: ", e));
tulis("dhisik");
```

Keluaran:

```
dhisik
ditangkep:  walah
```

Penolakan yang tidak pernah ditangani `.tangkep` maupun `enteni` akhirnya
menjadi galat program, sama seperti unhandled rejection di JavaScript.

## Penyimpangan dari JavaScript (penting)

**Pemanggil fungsi `mengko` menunggu sampai fungsi itu selesai.**

Di JavaScript, `f()` yang async langsung mengembalikan Janji dan pemanggil
langsung lanjut. Di Basa Jawa, frame `f` didorong ke `frames_` seperti
pemanggilan biasa, jadi bytecode pemanggil baru berjalan setelah frame `f`
pop — termasuk setelah `f` ditunda dan dilanjutkan.

Akibatnya `tulis(f())` mencetak Janji yang masih `nunggu`, bukan hasil akhirnya:

```jawa
mengko gawe f() { enteni Wektu.tundha(1); bali 42; }
tulis(f());        // "Janji { <pending> }"
tulis(enteni f());  // "42"
```

Ini disengaja: pipeline bytecode yang sama bisa dipakai untuk `f` sinkron
maupun async_, dan tidak ada jalur eksekusi terpisah yang harus dijaga. Yang
membedakan keduanya hanyalah `Frame::janji_async` dan `dasar_async_`.
Alat-alat (REPL, debugger, `fmt`) harus remember hal ini: untuk melihat nilai
akhir sebuah fungsi async, pakai `enteni`.

## Top-level await

Frame modul menjadi akar rantai `async` **hanya** bila modul tersebut benar-benar
memakai `enteni` di tingkat modul. Penandanya disimpan di
`Chunk::await_tingkat_modul`, diisi kompiler saat encounter `AWAIT` di luar
fungsi.

Tanpa penanda itu, setiap panggilan `mengko` di tingkat modul akan ikut menunda
modul — dan `dhisik` pada contoh di atas akan tercetak setelah `42`. Kompiler
memberi tahu agar perilaku default tetap "sinkron sampai bawah".

```jawa
tulis("A");
const v = enteni Wektu.tundha(1);   // top-level await
tulis(jenis(v));                      // "mboh"
```

## Yang belum ada

- `Janji.all`, `Janji.race`, `Janji.anySelesai` (agregator).
- Penjadwalan berbasis jam nyata; `Wektu.tundha` hanya mengurutkan.
- I/O async (berkas, jaringan). Semua I/O masih blocking.
- `FIBER_CREATE`/`FIBER_RESUME` masih opcode kosong: `metokake` (generator)
  masih mode-eager (D-019).
