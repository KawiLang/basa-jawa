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

## D-019. Generator mode-eager (penyimpangan sementara)

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
berawalan `#`. Privasi dijaga kompilator (nama `#x` tidak bisa ditulis di luar
kelas).

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

## TODO-VERIFIKASI

| # | Item | Rencana verifikasi |
|---|---|---|
| V-1 | Nama hari Jawa (Senen, Slasa, Rebo, Kemis, Jumuwah, Setu, Ahad) | Cocokkan dengan kamus/kalender Jawa; test snapshot |
| V-2 | Siklus pasaran (Legi, Pahing, Pon, Wage, Kliwon) | Verifikasi pada tanggal acuan (mis. 1 Januari 2000) dan turunannya |
| V-3 | Kodepoint Hanacaraka U+A980–A9DF | Test yang memeriksa nama karakter Unicode untuk tiap entri tabel |
| V-4 | Angka Jawa (siji..sanga, ronguluh, atus, sewu) | Tabel uji 0–99 lengkap + catatan sumber di sini |
| V-5 | ~~Aksara Jawa 0–9 (U+A9D0..U+A9D9)~~ **SELESAI** | Nama Unicode diverifikasi: `JAVANESE DIGIT ZERO` .. `JAVANESE DIGIT NINE`. Dipakai `StdAksara.angka_jawa` |
| V-6 | Tanda negatif `StdAksara.angka_jawa` | Dipakai U+A9CA (`JAVANESE PADA ADEG`, tanda baca) sebagai tanda minus. **Perlu konfirmasi** apakah lambang Jawa yang tepat adalah U+A9CA atau bentuk lain |
