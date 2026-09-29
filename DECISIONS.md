# DECISIONS.md — Catatan Keputusan Desain Basa Jawa

Setiap keputusan penting, alasannya, alternatif yang dipertimbangkan, dan
deviasi dari spesifikasi dicatat di sini.

---

## D-001. Toolchain: GCC 12.2.1 (bukan GCC 13)

**Keputusan.** Proyek targeting C++20 dan dibangun dengan GCC 12.2.1 pada mesin
build ini.

**Alasan.** Lingkungan build hanya menyediakan GCC 11.5 bawaan; `gcc-toolset-12`
(2.2.1) diinstal via paket distro. GCC 13 tidak tersedia di repositori
AlmaLinux 9.

**Alternatif.**
- (a) Menyalin toolchain sumber — terlalu lama & melanggar "tanpa dependensi".
- (b) Menulis ulang dengan C++17 — menurunkan kualitas kode.
- (c) Memerlukan GCC 13 dan berhenti berkode — tidak delivering apa pun.

**Dampak.** Semua fitur C++23 (`std::expected`, `std::print`) memakai polyfill
C++20 (`support::Result<T,E>`, `rt::print`). Kode tetap kompilabel di GCC 13+
karena tidak memakai apa pun yang dihapus.

---

## D-002. NaN-boxing dengan tag di bit 50..48

**Keputusan.** `Value` berukuran 8 byte. Pola boxed =
`0x7FF8_0000_0000_0000 | (tag << 48) | payload48`.

**Alasan.**
- Bit 63..52 = 0x7FF (eksponen maksimum) dan bit 51 = 1 menandai quiet NaN.
- Bit 50..48 = tag membedakan 8 jenis nilai boxed.
- Bit 47..0 = muatan: int32 bertanda atau pointer 48-bit.
- Pola `0x7FF8000000000000` (NaN bawaan hardware, muatan 0) **tidak pernah**
  dipakai sebagai nilai boxed, sehingga `0.0/0.0` terbaca sebagai `double`.

**Alternatif.** Tagged union 16 byte (selalu aman, tapi 2x footprint dan lambat).

**Invarian wajib.** Kode Basa Jawa tidak boleh membuat NaN bermuatan non-nol.
Semua jalur pembuatan NaN (pembagian nol, `Matematika.sqrt(-1)`, parse gagal)
memakai muatan 0. Bila asumsi ini dilanggar, mode fallback
`JAWA_NO_NAN_BOX` tersedia dannilainya identik secara semantik.

**Catatan 48-bit.** Pointer 48-bit hanya aman bila platform tidak memakai
alamat virtual 5-level paging dengan bit canonical higher-order.	x86-64 Linux
dengan 4-level paging memakai 48 bit; 5-level paging (LA57) memakai 57 bit dan
**akan salah baca**. Mitigasi: di Linux, `JAWA_NAN_BOX` dinonaktifkan otomatis
jika deteksi LA57; selain itu build 16-byte. Lihat `platform/detect.cpp`.

---

## D-003. Panggilan JS→JS tanpa rekursi C++

**Keputusan.** Semua eksekusi bytecode berjalan di dalam **satu** loop
`VM::execute()`. Panggilan fungsi JS tidak menambah frame stack C++.

**Alasan.** Rekurusi C++ pada pemanggilan JS akan menyebabkan stack overflow
native (crash) alih-alih `KleruRangat`. Dengan satu loop, batas kedalaman dikontrol
VM (bawaan 10.000 frame) dan dapat dilaporkan sebagai galat biasa.

**Konsekuensi.**
- Reentrancy native→JS (mis. `Array.prototype.sort` memanggil callback) dibuat
  dengan memanggil ulang `execute()` pada fiber/frame baru; kedalaman reentrancy
  dibatasi dan diawasi RAII.
- `metokake`/`enteni` memakai sinyal `Suspend` yang keluar dari loop; state fiber
  disimpan di frame VM. Titik suspensi dijamin tidak berada di dalam builtin
  native (dijaga `NativeFrameGuard`).

---

## D-004. Template literal: mode stack di lexer

**Keputusan.** Lexer memakai *mode stack*. Setiap bagian cooked template
menghasilkan satu token `TemplateText` dengan penanda
`template_awal` / `template_akhir` / `template_expr_ikut`. Token `}` yang
menutup `${...}` ditandai `template_expr_akhir`.

**Alasan.** Pendekatan "token dalam token" (sub-token list per bagian) menyulitkan
parser dan managing lokasi. Mode stack menghasilkan **satu aliran token datar**,
sehingga parser tetap recursive-descent biasa.

---

## D-005. Arrow function: cover grammar, bukan backtracking penuh

**Keputusan.** `coba_arrow()` memindai token ke depan untuk mencari `)` yang
cocok, lalu memeriksa apakah token berikutnya `=>`. Bila ya, kurung itu
ditempatkan sebagai daftar parameter.

**Alasan.** Backtracking penuh (coba parse ekspresi lalu undo) boros dan rapuh
terhadap galat. Scan satu-pass untuk penutup kurung cukup untuk membedakan
`(a, b)` dari `(ekspresi)`, dan tidak pernah gagal diam-diam.

**Keterbatasan.** Pola berkurung di dalam daftar parameter, seperti
`((a), b) => x`, tidak dikenali dan ditolak dengan diagnostik yang jelas. Ini
trade-off yang disengaja; lihat `docs/02-sintaks.md` bagian batasan.

---

## D-006. `pilih` tidak fall-through

**Keputusan.** `pilih` tidak melakukan fall-through implisit. Kasus-kasus
berturut tanpa statement diperlakukan sebagai satu kelompok (meniru `case 1: case 2:`),
`mandheg` di akhir kasus bersifat opsional.

**Alasan.** Ini adalah salah satu "perbaikan atas kelemahan JavaScript" yang
diminta di Bagian 3.6: fall-through yang tak disengaja adalah sumber bug klasik.

**Deviasi dari spesifikasi.** Spesifikasi menyebut "fall-through tak sengaja
ditandai sebagai kesalahan sintaks". Implementasi memilih **melarang** fall-
through sepenuhnya (dengan pengelompokan kasus) alih-alih melaporkannya sebagai
lint, karena lebih aman dan lebih mudah ditegakkan.

---

## D-007. Semantik `+` tanpa koersi

**Keputusan.** `Angka + Teks` menghasilkan `KleruJinis`, bukan teks gabungan.

**Alasan.** Diminta eksplisit di Bagian 3.6. Konsekuensinya kode yang biasa
bertahan di JavaScript harus memakai template literal atau `Teks(x)`.

**Catatan kompatibilitas.** Ini adalah Biggest sumber perbedaan dari JS; akan
disebutkan tegas di README dan `docs/02-sintaks.md`.

---

## D-008. `Teks.dawa` = jumlah code point

**Keputusan.** `.dawa` menghitung **code point Unicode**, bukan unit UTF-16
seperti JavaScript. `.dawa_bita` memberi panjang byte UTF-8.

**Alasan.** Basa Jawa menyimpan string sebagai UTF-8 tervalidasi. Menghitung
code point adalah operasi yang benar secara linguistik dan menghindari kasus
"emoji terpotong". Dokumen ini menyatakan perbedaannya dengan JS secara eksplisit.

---

## D-009. `sort` stabil & komparator sesuai jenis

**Keputusan.** `Dhaptar.urut()` memakai stable sort. Tanpa komparator, elemen
diurutkan: angka secara numerik, teks secara leksikografis Unicode; tipe campuran
menurut urutan eksplisit (angka < teks < boole).

**Alasan.** Diperminta di Bagian 3.6. Urutan eksplisit untuk tipe campuran
membuat hasil deterministik dan terdokumentasi.

---

## D-010. Regex: mesin backtracking buatan sendiri

**Keputusan.** Mesin regex ditulis sendiri (bukan `std::regex`) dengan batas
langkah backtracking.

**Alasan.** `std::regex` punya perilaku tidak terdefinisi dan lambat; mesin
kustom memberi kontrol atas batas langkah (anti-ReDoS) dan dukungan
lookahead/lookbehind.

---

## D-011. Harness test sendiri, bukan Catch2/GTest

**Keputusan.** `tests/harness.cpp` menyediakan `TEST_CASE`, `SECTION`, `CHECK*`,
`REQUIRE*`, `CHECK_THROWS*`.

**Alasan.** Spesifikasi mengizinkan Catch2/GTest sebagai dependensi *dev only*,
tetapi lingkungan ini tidak punya paket C++ test dan `FetchContent` memerlukan
sumber daya tambahan. Harness sendiri menambah ±300 baris dan membuat proyek
benar-benar bebas dependensi.

**Keterbatasan.** Tidak mendukung SECTION multi-run (fungsi dijalankan sekali per
test). Tabel benchmark & golden test menutupi kebutuhan tersebut.

---

## D-012. Emoji/aksara: verifikasi lewat tabel, bukan tebakan

**Keputusan.** Kode akasara Jawa (`aksara_jawa`) dan nama hari/pasaran dibangun
dari tabel yang diverifikasi terhadap daftar nama Unicode resmi. Setiap entri
kode point diberi test yang memeriksa nilainya kembali terhadap tabel.

**Alasan.** Bagian 15 melarang mengarang kaidah bahasa Jawa. Codepoint aksara
Jawa (U+A980–A9DF) dan nama hari Jawa akan diverifikasi sebelum dipakai.

**Status.** Lihat tabel `TODO-VERIFIKASI` di bawah. V-5 sudah diverifikasi lewat
nama Unicode resmi; sisanya masih menunggu sumber Jawa yang otoritatif.

---

---

## D-014. NaN-boxing: bit tanda sebagai penanda, bukan bit 51

**Keputusan.** Nilai boxed dikenali dari `bit 63 == 1` DAN `eksponen == 0x7FF`
(pola "NaN negatif"). Tag occupying 4 bit (51..48), muatan 48 bit (47..0).
`Value::number(NaN)` menormalkan setiap NaN ke tag `NaNAngka`.

**Alasan.** Desain awal memakai bit 51 sebagai penanda "quiet NaN" dengan tag di
bit 50..48. Pola itu **tabrakan** dengan `double` biasa: setiap nilai boxed
(bit 51=1, eksponen 0x7FF) terbaca sebagai double, sehingga hanya `mboh` yang
terbaca benar. Bug ditemukan saat Fase 3 dan dampaknya menyeluruh.

Dengan bit tanda sebagai penanda, satu-satunya `double` yang bisa tertukar adalah
NaN negatif; `number()` menormalkan NaN apa pun, jadi pola itu tidak pernah muncul
dari kode runtime.

**Konsekuensi.** Membutuhkan satu tag tambahan untuk NaN (dikurangi dari medial kosong
`Kucing`). Ke orthodontic LA57 tetap: muatan 48 bit hanya aman bila alamat
user-space < 2^48; `JAWA_NO_NAN_BOX` menyediakan mode 16-byte.

---

## D-015. Panggilan JS→JS tanpa rekursi C++ (batas tumpukan, bukan stack native)

**Keputusan.** `panggil_objek` untuk closure hanya mendorong frame baru ke
`VM::frames_`; instruksi berikutnya dieksekusi oleh loop bytecode yang SAMA.
Reentransi C++ (`jalankan_loop` bersarang) hanya terjadi untuk pemanggilan
native → JS, dibatasi `VMOptions::maks_reentrancy`.

**Alasan.** Versi awal memanggil `jalankan_loop()` dari dalam `CALL`. Akibatnya
(1) pemanggilan JS→JS memakai stack C++ sehingga rekursi tak berhingga menjadi
segfault native, bukan galat bahasa; (2) loop bersarang melanjutkan eksekusi
frame INDUK setelah frame anak selesai, sehingga argumen setelah panggilan salah
dibaca.

Sekarang rekursi tak berhingga menghasilkan `KleruRentang [R003]`, dan setiap
frame punya batas `maks_tumpukan` (default 10000).

**Sisa pekerjaan.** Reentranci native→JS masih memakai loop bersarang dengan
penjaga kedalaman (`kedalaman_awal`). Fiber (Fase 7) akan menghilangkannya.

---

## D-016. `std::deque` untuk stack nilai VM

**Keputusan.** `VM::stack_` adalah `std::deque<Value>`, bukan `std::vector`.

**Alasan.** Sel upvalue menyimpan POINTER ke slot stack. `std::vector::push_back`
merealokasi memori sehingga pointer itu menggantung — closures membaca nilai
sampah dan kadang merusak heap. `std::deque` menjamin alamat elemen existing
tidak berubah saat menambah di kedua ujung.

**Konsekuensi.** `&stack_[0]` dipakai untuk menghitung indeks relatif pada
`tutup_upvalue_frame`; bukan `stack_.data()`.

---

## D-017. Karantina alokasi pada GC

**Keputusan.** `Heap::kKarantina` = 64; 64 objek terakhir yang dialokasikan tidak
pernah disapu sampai 64 alokasi berikutnya terjadi.

**Alasan.** Ada jeda antara "objek dibuat" dan "pemanggil mengisinya/menjadikannya
root". `heap.alokasi_baru<TeksObj>()` pada mode `--gc-stress` memicu koleksi
SEGERA; objek yang baru dibuat belum punya anak dan belum di-root, sehingga
tersapu sebelum `data_.assign(...)` — use-after-free. Ditemukan oleh ASan.

**Konsekuensi.**-jejak objek bisa tertinggal sampai 64 alokasi berikutnya pada
program yang melingkar terus-menerus. Batas konservatif yang disengaja; jauh lebih
baik daripada use-after-free.

---

## D-018. Akar sementara untuk objek yang dibuat kompilator

**Keputusan.** `Heap::akar_sementara(Value)` menyimpan nilai yang dibuat DI LUAR
frame VM (terutama konstanta `TeksObj` hasil kompilasi). `Compiler::tambah_konstanta`
dan `tambah_nama` mendaftarkan setiap nilai. `VM::jalankan_sumber` memanggil
`bersihkan_akar_sementara()` setelah objek `FungsiObj` modul menjadi root
(pool konstantanya sudah dijaga `FungsiObj`).

**Alasan.** Tanpa ini, `--gc-stress` membebaskan setiap konstanta string selama
kompilasi (koleksi terjadi tiap alokasi) dan program salah tanpa error.

**Catatan.** `RootVisitor` yang dibuat `Heap::tandai_roots` WAJIB memasang
`visit`; kalau `visit` null, `rooted(v)` diam-diam tidak menandai apa pun dan
seluruh objek hidup tampak hilang (bug yang sempat muncul).

---

## D-019. ~~Generator mode-eager~~ — DICABUT oleh D-028

**Keputusan.** `gawe* f() { ... metokake x; ... }` dijalankan sampai selesai;
semua hasil `metokake` dikumpulkan menjadi `ArrayObj` yang dikembalikan. Ada
pengaman 2^20 hasil.

**Alasan.** Suspensi frame yang sesungguhnya membutuhkan fiber (Fase 7), sama
seperti `async`. Versi eager menghasilkan keluaran yang benar untuk generator
berhingga (termasuk contoh acuan `[...cacah(5)]`).

**Penyimpangan.** Salah untuk generator tak berhingga dan untuk semantik laziness
umum (mis. `gawe* tabel(){ ... }` yang tidak pernah dipanggil). Dicatat di
`STATUS.md` § 2. Catatan: `STATUS.md` § 2 akan menggantinya.

---

## D-020. Field privat dijaga kompilator, bukan runtime

**Keputusan.** Field privat `#nama` disimpan sebagai slot biasa dengan nama
berawalan `#`. Privasi dijaga kompilator: nama `#x` tidak bisa ditulis di luar
kelas, jadi tidak bocor lewat refleksi.

**Alasan.** Slot instance berbasis `vector<Value>` + `vector<string_view>` lebih
sederhana dan cukup untuk semantik yang dibutuhkan; `#Brand` + slot privat
sebenarnya menambah mekanisme penanda yang belum diperlukan Fase 3.

**Penyimpangan.** Refleksi (`Object.getOwnPropertyNames`) bisa melihat nama
`#x`; tidak ada verifikasi runtime bahwa penulisan `#x` terjadi di dalam kelas.

---

## D-021. `coba`: handler pertama saja

**Keputusan.** Hanya klausa `tangkep` PERTAMA yang dipakai sebagai handler.
Klausa tambahan memicu peringatan kompilator S504.

**Alasan.** Seleksi handler berdasarkan tipe kleru belum ada. Mendiamkan
klausa yang tidak terpakai akan lebih buruk daripada memperingatkan.

**Rencana.** Fase 8 menambah tabel tipe handler per frame.

---

## D-022. `anyaar` memakai rantai akses tanpa pemanggilan

**Keputusan.** Parser memakai `Parser::parse_konstruktor()` (rantai `.x`/`[i]`
TANPA `(`) untuk konstruktor `anyaar`, lalu memisahkan daftar argumen, baru
terakhir applies `lanjut_member` untuk sisa rantai (`.nilai` pada
`anyaar Foo(1).nilai`).

**Alasan.** Bila `parse_call_member()` (yang menerima `(`) dipakai langsung,
`anyaar Foo(1).nilai` ter-parse sebagai `anyaar (Foo(1).nilai)` — konstruktor
menjadi hasil panggilan, dan program salah tanpa error kompilasi.

---

## D-023. `async`/`await` memakai continuation, bukan fiber

**Konteks.** Fase 6 mensyaratkan event loop, Janji (Promise), dan `await` yang
benar-benar menunda. Pendekatan umum (dan yang disebut pada catatan awal) adalah
**fiber**: satu stack C++ terpisah per rantai async, lalu `FIBER_CREATE`/
`FIBER_RESUME` untuk menukar konteks. Itu berat dan tidak portabel.

**Keputusan.** Rantai `async` disuspensi dengan **menyalin frame** (`Frame` +
nilai stack) ke `struct Lanjutan`, lalu memangkas `frames_`/`stack_`. Pemulihan
menyalin balik dan menjalankan loop bytecode lagi. Tidak ada stack C++ tambahan,
tidak ada fiber, tidak ada rekursi.

**Alasan.**

- `Frame` sudah menyimpan seluruh state eksekusi: `ip`, `slot_base`,
  `this_val`, `handlers` (blok `coba`), upvalue, dan Janji yang harus dituntaskan.
  Tidak ada state C++ di antara opcode yang perlu bertahan selama penangguan.
- Menghormati D-003 dan D-015: tidak ada jalur eksekusi kedua yang bisa
  menyimpang dari loop utama.
- Deterministik. `--gc-stress` dan golden file bisa memverifikasinya.
- Upvalue yang menunjuk ke stack yang disalin harus **ditutup** lebih dulu
  (`Upvalue::close`); kalau tidak, selnya akan menggantung setelah stack dipakai
  ulang kode lain.

**Konsekuensi (disengaja).** Pemanggil fungsi `mengko` menunggu sampai fungsi itu
selesai — termasuk setelah ditunda lalu dilanjutkan. Jadi `tulis(f())` mencetak
Janji yang masih `nunggu`; `tulis(enteni f())` yang menghasilkan nilai akhir.
Ini penyimpangan dari JavaScript, dicatat di `docs/async.md` dan `STATUS.md`.

**Konsekuensi kedua.** `Wektu.tundha(ms)` tidak benar-benar menunggu: Janji
dituntaskan pada putaran timer pertama, dengan nilai `mboh`. Yang dijamin
adalah urutan relatif antar timer. Ini membuat golden test async stabil.

**Catatan.** `Chunk::await_tingkat_modul` menandai modul yang memakai `enteni`
di tingkat modul. Tanpa penanda itu, setiap panggilan `mengko` di tingkat modul
ikut menunda modul dan urutan keluaran berubah (contoh acuan `asinkron.jw`
mencetak `42` sebelum `dhisik`).

---

## D-024. Kata kunci boleh menjadi nama properti setelah `.`

**Konteks.** Banyak nama method bawaan Basa Jawa bentrok dengan kata kunci:
`tangkep` (`.catch`), `nampa`/`.get`, `nyetel`/`.set`, `bali`/`.return`, `jenis`,
`saka`, `tulis`, `jangka`, `kanggo`.

**Keputusan.** Setelah `.` (dan `?.`), parser menerima `pengenal` **serta** kata
kunci apa pun sebagai nama properti (`Parser::boleh_adi_properti`).

**Alasan.** Menolaknya membuat pustaka standar mustahil ditulis dengan API yang
wajar. Token kata kunci sudah membawa teksnya di `Token::teks`, jadi tidak ada
kerugian informasi. Tidak ada ambiguitas: `a.b` di posisi ekspresi adalah akses
properti; setelah `.` yang diharapkan adalah nama, bukan operator.

---

## D-025. Modul dievaluasi sinkron di tengah loop bytecode

**Konteks.** Modul A yang mengimpor B harus menjalankan body B sampai selesai
sebelum A bisa memakai ekspornya. Satu jalan alternatif adalah menjalankan B di
loop bytecode bersarang (reentrancy) — tapi itu berarti jalur eksekusi kedua
yang bisa menyimpang dari loop utama (melanggar D-003/D-015).

**Keputusan.** Linker (`src/vm/linker.cpp`) mendorong frame modul ke
`VM::frames_` yang SAMA dan menjalankan `jalankan_loop(frame_awal + 1)`. Loop
berhenti begitu jumlah frame turun kembali, jadi modul A melanjutkan tepat dari
instruksi setelah `IMPORT`.

**Alasan.**

- Tidak ada loop bersarang: semua operasi bytecodec tetap di satu loop, dengan
  satu bentuk lompatan dan satu bentuk galat.
- Modul punya `ModuleRecord` sendiri dengan peta `global` sendiri, ditumpuk di
  `VM::modul_tumpukan_`. `GET_GLOBAL` melihat modul teratas, jadi dua modul
  dengan nama variabel yang sama tidak bertabrakan.
- Deep import (a→b→c→d) tidak menambah kedalaman stack C++, cuma menambah frame
  bytecode.

**Catatan implementasi.** Ambang loop dihitung dari JUMLAH FRAME, bukan indeks
stack. Memakai indeks stack membuat loop langsung keluar (kondisi `2 < 3` sudah
terpenuhi) sehingga body modul tak pernah berjalan — gejalanya `ekspor` melihat
`modul_aktif == nullptr`.

---

## D-026. Tiga aturan hoisting supaya impor siklik bisa bekerja

**Konteks.** `a.jw` mengimpor `b.jw` dan `b.jw` mengimpor `a.jw`. Keduanya
saling memanggil. Tanpa penanganan khusus, salah satunya melihat objek ekspor
yang masih kosong.

**Keputusan.** Tiga aturan, semuanya di `Compiler::compile`:

1. **Fungsi & kelas yang diekspor di-hoist ke atas modul.** `ekspor gawe f()`
   dikompilasi sebelum statement `impor` mana pun, sehingga `f` sudah ada di
   objek ekspor saat modul lain membacanya.
2. **Slot pengikat impor dialokasikan lebih dulu** (tanpa emits) sebelum
   hoisting. Tanpa ini, fungsi yang di-hoist meng-capture `g` sebagai **nama
   global** (karena `g` belum jadi slot lokal saat hoisting berjalan), dan
   pemanggilannya gagal.
3. **`ekspor { x }` boleh mendahului deklarasi.** Entri yang slot-nya belum ada
   ditunda ke akhir body modul.

**Alasan.** Aturan 1 & 2 harus bersama. Aturan 1 saja tidak cukup — tanpa
pre-alokasi slot, capture upvalue-nya salah. Aturan 3 murah dan membuat
`ekspor` tidakzys order-dependent, yang sulit dibaca.

**Batas yang diterima.** Hanya deklarasi fungsi/kelas yang bisaBootstrap lewat
siklus. `tetep`/`ana` yang saling diimpor akan melihat `mboh` — sama seperti
ECMAScript, di mana yang boleh mengarahkan bootstrap modul adalah deklarasi
fungsi.

---

## D-027. Nama yang tidak diekspor adalah galat, bukan `mboh`

**Konteks.** `GET_PROP` mengembalikan `mboh` untuk nama yang tidak ada — itu
benar untuk objek biasa. Tapi untuk impor modul, `impor { hngal } saka "./a.jw"`
adalah kesalahan ketik program, dan diam-diam menghasilkan `undefined` adalah
sumber bug yang mahal dicari.

**Keputusan.** Opcode baru `GET_EXPORT` (grup `modul`) yang melempar
`KleruModul` bila nama tidak ada, dengan pesan yang **mencantumkan nama yang
diekspor modul**. Opcode `GET_PROP` tidak berubah.

**Alasan.**

- Galat ada di tempat yang salah ketik terjadi, bukan Diameter frame pemanggil.
- Pesan galat menyebutkan kandidat yang benar — kesalahan yang paling sering
  adalah beda ejaan atau lupa `ekspor`.
- Galat diteruskan lewat `unwind_galat`, jadi `coba`/`tangkep` di modul
  pemanggil bisa menanganinya (bukan `galat_.ada` langsung).

---

## D-028. Generator lazy memakai continuation yang sama dengan async

**Konteks.** D-019 menetapkan generator "mode-eager": `gawe* g() {...}` dijalankan
sampai selesai dan semua hasil `metokake` dikumpulkan jadi Dhaptar, dengan
pengaman 2^20 hasil. Itu benar untuk generator berhingga dan **salah** untuk
generator tak berhingga. D-019 juga menyebut fiber sebagai jalan ke lazy
generator.

Fase 6 (D-023) membuktikan fiber tidak diperlukan untuk async: `enteni` cukup
menyalin frame ke continuation. Pertanyaannya, apakah mechanism yang sama bisa
dipakai untuk generator.

**Keputusan.** Ya. `metokake` memakai `struct Lanjutan` yang persis sama dengan
rantai async (`src/vm/vm_gen.cpp`):

- `VM::suspensi_generator` menyalin frame `akar_agen`..teratas + nilai stack,
  menutup upvalue yang menunjuk ke rentang itu, lalu memangkas `frames_`/`stack_`.
- `VM::lanjutkan_generator` menyalin balik dan menjalankan loop bytecode lagi.
- `FIBER_CREATE` / `FIBER_RESUME` **tidak pernah dipakai**.

D-019 dicabut.

**Alasan.**

- Tinggi call stack tidak bertambah, jadi generator tak berhingga bisa dipakai.
- Satu mechanism, satu bentuk lompatan, satu bentuk galat. Tidak ada jalur
  eksekusi kedua (sejalan dengan D-003/D-015/D-023).
- `VMOptions::maks_tumpukan` tetap berlaku: generator yang bersarang banyak
  (`for (const x saka g1) for (const y saka g2)`) tetap menghasilkan
  `KleruRentang`, bukan crash.
- Pengaman 2^20 hasil tidak perlu lagi: pemanggilan tidak lagi berjalan sampai
  selesai.

**Konsekuensi (disengaja).** Pemanggil `mengko`/generator harus **meminta
langkah berikutnya**; tidak ada penjadwalan otomatis. `g.next()` mengembalikan
`{nilai, selesai}` dengan sifat JavaScript; spread `[...g]` dan
`for..of` keduanya memintanya satu langkah pada satu waktu.

**Dua bug nyata yang muncul selama implementasi (keduanya ditemukan lewat
pengujian):**

1. **Pemulihan continuation harus menggeser indeks slot, bukan memaksa ukuran
   stack.** Selama generator tertunda, pemanggil sudah melanjutkan dan bisa
   saja memakan slot yang tadinya menyimpan objek generator (`SPREAD_PUSH` mem-pop
   nilai itu). `stack_.resize(lan->slot_base)` mengisi slot yang sudah dibuang
   dengan nilai sampah. Gejalanya `[undefined, 1, 2, 3]` untuk generator 3 hasil.
   Perbaikan: dorong nilai tersimpan di atas stack saat ini, lalu geser
   `slot_base` dan `target_balas` setiap frame sebesar selisihnya. Upvalue ke
   rentang itu sudah ditutup saat ditunda, jadi tidak ada pointer yang ikut
   bergeser.
2. **Ambang loop resume tidak boleh `0`.** Dengan `0`, loop resuming melanjutkan
   eksekusi frame pemanggil di dalam loop bersarang. Untuk generator ambangnya
   `frames_.size()` setelah pemulihan; untuk async ambangnya indeks frame **akar**
   rantai (`k`), karena saat modul ikut menjadi akar (top-level `enteni`) frame
   modul ikut dipulihkan dan harus ikut berjalan sampai selesai.

---

## D-029. Konstanta chunk harus di-root selama program berjalan

**Konteks.** `Heap::bersihkan_akar_sementara()` (D-018) mengosongkan daftar akar
sementara kompilator setelah `FungsiObj` modul menjadi root. Tujuannya: konstanta
string modul tidak tertahan selamanya.

**Bug yang ditemukan.** Konstan untuk **fungsi anak** ikut hilang dari daftar itu
padahal `FungsiObj`-nya belum ada. `FungsiObj` sebuah fungsi anak baru dibuat
runtime, saat opcode `CLOSURE` dieksekusi — bisa jauh setelah modul di-root. lalu
`bersihkan_akar_sementara()` dipanggil, konstanta string anak tidak punya akar
sama sekali. Program yang baru rusak setelah beberapa alokasi, bukan seketika.

Gejalanya sangat menyesatkan: pada program kecil (yang langsung memanggil
fungsi tersebut) tidak ada koleksi di antara waktu itu, jadi tidak terlihat.
Pada program yang lebih panjang, konstanta `"pamungkas"` tersapu dan memorinya
dipakai ulang `TeksObj` lain; program membaca string yang salah — dan hanya
dalam mode `--gc-stress`.

**Keputusan.** `VM::chunk_akar_` menyimpan **semua** chunk program (modul utama +
fungsi anak + modul terimpor), dan root visitor menandai `konstanta` serta
`nama_properti` setiap chunk. Daftar itu dibersihkan bersama VM.

**Alasan.** Benar secara lokal dan sederhana: setiap konstan chunk benar-benar
dipakai sepanjang program, jadi menahannya bukan kebocoran.
Akar yang "lebih ketat" -- misalnya melepas konstanta yang tidak lagi dirujuk --
butuh analisis yang belum ada dan tidak dibutuhkan sekarang.

**Efek samping yang bagus.** Konstanta `nama_properti` ikut tertahan, termasuk
nama yang hanya dipakai untuk `GET_GLOBAL`/`GET_PROP` di chunk anak yang belum
dibuat closure-nya.

---

## D-030. Nilai yang dipop dari stack perlu akar ber-scope

**Konteks.** Beberapa opcode mempop nilai dari stack lalu memakainya selama
*banyak* alokasi. Contoh: `SPREAD_PUSH` mem-pop objek yang akan di-spread, lalu
menjalankan generator sampai habis (yang bisa memicu ribuan alokasi).

**Bug.** Selama nilai itu tidak ada di stack, ia juga tidak ada di root GC.
Dengan `--gc-stress` (koleksi tiap alokasi) objeknya bisa tersapu di tengah,
bersama nilai yang dipegang frame yang sedang disuspensi.

**Keputusan.** `gc::ScopedRoot` — RAII satu nilai yang didorong ke
`Heap::akar_scope_` (deque, push/pop LIFO) dan ditandai sebagai root.
Dipakai di `SPREAD_PUSH`.

**Catatan.** `gc::Handle` yang sudah ada **bukan** root GC — dia hanya menjaga
alamat salinan nilai lokal (dirancang untuk `HandleScope` yang mendaftarkan
slot-nya ke heap). Pemakaian `Handle` sendirian di opcode akan terlihat benar
tetapi tidak efectuar apa pun.


---

## TODO-VERIFIKASI

| # | Item | Rencana verifikasi |
|---|---|---|
| V-1 | Nama hari Jawa (Senen, Slasa, Rebo, Kemis, Jumuwah, Setu, Ahad) | Cocokkan dengan kamus/kalender Jawa; test snapshot |
| V-2 | Siklus pasaran (Legi, Pahing, Pon, Wage, Kliwon) | Verifikasi pada tanggal acuan (mis. 1 Januari 2000) dan turunannya |
| V-3 | Kodepoint Hanacaraka U+A980–A9DF | Test yang memeriksa nama karakter Unicode untuk tiap entri tabel |
| V-4 | Angka Jawa (siji..sanga, ronguluh, atus, sewu) | Tabel uji 0–99 lengkap + catatan sumber di sini |
| V-5 | ~~Aksara Jawa 0–9 (U+A9D0..U+A9D9)~~ **SELESAI** | Nama Unicode diverifikasi: `JAVANESE DIGIT ZERO` .. `JAVANESE DIGIT NINE`. Dipakai `StdAksara.angka_jawa` |
| V-6 | Tanda negatif `StdAksara.angka_jawa` | Dipakai U+A9CA (`JAVANESE PADA ADEG`, tanda baca) sebagai tanda minus. **Perlu konfirmasi** apakah lambang Jawa yang tepat adalah U+A9CA atau bentuk lain |
