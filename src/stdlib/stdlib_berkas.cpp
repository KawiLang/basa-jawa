// Pustaka standar: I/O berkas.
//
// Semula Basa Jawa tidak punya cara apa pun menyentuh berkas -- program bisa
// mencetak ke stdout dan membaca baris dari terminal, tapi tidak bisa menyimpan
// hasil kerjanya. `impor` menutup sebagian kebutuhan itu, tapi tidak semuanya.
//
// Berkas ini sengaja terpisah dari `stdlib.cpp` yang sudah panjang; tidak ada
// header baru karena semua fungsi dipasang lewat `pasang_prototype_metode`/
// `daftarkan` yang menerima pointer fungsi biasa.
//
// ## Bentuk API
//
// Dua lapisan, supaya dua kebutuhan berbeda tidak saling memaksa:
//
//   baca_berkas("d.txt")            // seluruh isi sebagai teks, sekali panggil
//   tulis_berkas("d.txt", isi)      // tulis seluruh isi, truncate dulu
//
//   ana f = anyar Berkas("besar.txt", "baca");
//   nalika (f.baca_baris()) { tulis(f.baris); }
//   f.tutup();
//
// Yang pertama enak untuk berkas kecil -- konfigurasi, JSON, data tabular.
// Yang kedua untuk berkas yang tidak muat di memori.
//
// ## Tiga keputusan yang membatasi
//
// 1. **Teks saja.** Isi dibaca & ditulis sebagai byte UTF-8 apa adanya; tidak ada
//    konversi encoding dan tidak ada mode biner. `baca(n)` menghitung `n` dalam
//    *karakter* UTF-8, bukan byte -- pemanggil yang butuh byte harus menghitung
//    sendiri.
//
// 2. **Tidak ada `tutup()` otomatis.** Lihat catatan panjang di `BerkasObj`.
//    Ini satu-satunya sumber daya yang harus ditutup manual, dan sengaja
//    begitu: menutup gagang saat GC bisa membuat program yang masih memegang
//    `f` gagal di tengah jalan tanpa jejak.
//
// 3. **Path relatif terhadap direktori kerja proses**, persis seperti
//    `fopen()` di C atau `open()` di Python -- BUKAN relatif ke lokasi skrip.
//    Tidak ada mekanisme yang mengubah direktori kerja, jadi program yang
//    menulis relatif selalu menulis ke tempat yang sama untuk semua pemanggil.
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
// `mkdir`/`rmdir` di Windows punya nama berbeda.
#include <direct.h>
#else
// `stat`/`S_ISDIR` untuk membedakan direktori dari berkas biasa, `mkdir`/`rmdir`
// untuk membuat & menghapus direktori.
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "rt/object.h"
#include "rt/string.h"
#include "stdlib/stdlib.h"
#include "vm/vm.h"

namespace jawa::vm::stdlib {
namespace {

using jawa::vm::VM;
using rt::BerkasObj;
using rt::Obj;
using rt::OK;
using rt::Value;

/// Nama kleru untuk semua galat I/O berkas, supaya `tangkep (KleruBerkas)`
/// bisa menangkap semuanya (lihat `cocok_kleru` di `src/vm/vm.cpp`).
constexpr std::string_view kJenang = "KleruBerkas";

/// `BerkasObj` pada `v`, atau `nullptr`.
BerkasObj* berkas_dari(Value v) {
    if (!v.is_obyek() || v.pointer() == nullptr) return nullptr;
    auto* o = static_cast<Obj*>(v.mutable_pointer());
    return (o != nullptr && o->h.kind == OK::Berkas) ? static_cast<BerkasObj*>(o) : nullptr;
}

/// Argumen ke-`i`, atau `mboh`.
Value arg(std::vector<Value>& a, std::size_t i) { return i < a.size() ? a[i] : Value::mboh(); }

/// `true` kalau `v` benar-benar objek teks (bukan `mboh`/angka/null).
bool aku_teks(const Value& v) {
    if (!v.is_obyek() || v.pointer() == nullptr) return false;
    auto* o = static_cast<const Obj*>(v.pointer());
    return o != nullptr && o->h.kind == OK::Teks;
}

/// Isi teks `v`, atau string kosong kalau bukan teks.
std::string_view teks_dari(const Value& v) {
    if (!aku_teks(v)) return {};
    return static_cast<const rt::TeksObj*>(v.pointer())->str();
}

/// Lempar `KleruBerkas` dan kembalikan `mboh` supaya pemanggil bisa langsung
/// `return galat(...)`.
Value galat(VM& vm, std::string pesan) {
    vm.lempar_kleru(kJenang, std::move(pesan));
    return Value::mboh();
}

/// Kutip nama path di dalam pesan galat.
std::string kutip(std::string_view s) { return "\"" + std::string(s) + "\""; }

/// Penjelasan singkat untuk `errno` terakhir.
///
/// Disalin ke pesan sebagai "sistem: ..." karena `strerror` berbahasa Inggris,
/// sedangkan sisa pesan Galat Jawa. Menandai asalanya membuat dua bahasa itu
/// terbaca sebagai satu kalimat, bukan sebagai dua yang tidak sengaja
/// bercampur.
std::string sebab() { return std::string(std::strerror(errno)); }

/// `true` kalau `path` menunjuk ke DIREKTORI.
///
/// Pakai `stat` supaya tidak membuka handle. `struct stat` + `S_ISDIR` berasal
/// dari POSIX, bukan C++ standar, jadi dijaga makro-nya.
///
/// Nama fungsi ini menyebut apa yang dikembalikan, bukan kebalikannya. Versi
/// pertama bernama `Bukan_direktori` tapi isinya `S_ISDIR(...) != 0` -- jadi
/// `ada_berkas("/tmp")` membalas `bener`, persis kebalikan dari yang
/// diinginkan, dan uji manual tidak menangkapnya karena keduanya "terlihat
/// masuk akal".
bool adalah_direktori(const char* path) {
#if defined(_WIN32)
    return false;  // di Windows `fopen` direktori sudah ditolak, jadi cek ini tak perlu
#else
    struct stat st {};
    if (::stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode) != 0;
#endif
}

/// `mkdir` satu tingkat.
int dadi_satu(const char* path) {
#if defined(_WIN32)
    return ::_mkdir(path);  // <direct.h>
#else
    return ::mkdir(path, 0777);
#endif
}

/// `rmdir` satu tingkat.
int hapus_satu(const char* path) {
#if defined(_WIN32)
    return ::_rmdir(path);  // <direct.h>
#else
    return ::rmdir(path);
#endif
}

/// Panjang `s` setelah byte menggantung di ujung dibuang, supaya teksnya tetap
/// UTF-8 valid.
///
/// Dipakai `baca(n)` yang berhenti di batas byte -- kalau `n` jatuh di tengah
/// satu emoji, sisa byte-nya harus dibuang, bukan diteruskan sebagai teks.
std::size_t potong_utf8(std::string_view s) {
    if (s.empty()) return 0;
    std::size_t i = s.size();
    // Mundur melewati byte lanjutan (10xxxxxx) sampai byte awal karakter.
    while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) --i;
    if (i == 0) return 0;
    const unsigned char c = static_cast<unsigned char>(s[i - 1]);
    const std::size_t perlu =
        (c < 0x80) ? 1u : ((c & 0xE0) == 0xC0 ? 2u : ((c & 0xF0) == 0xE0 ? 3u : 4u));
    // `s.size() - (i - 1)` = byte yang tersedia untuk karakter terakhir.
    return (s.size() - (i - 1) >= perlu) ? s.size() : (i - 1);
}

/// Handle yang masih hidup, atau nullptr setelah melempar galat.
std::FILE* handle_aktif(VM& vm, BerkasObj* b, std::string_view apa) {
    if (b == nullptr) {
        galat(vm, "Ora bisa nanggil " + std::string(apa) + "(): argumentenya dudu objek Berkas.");
        return nullptr;
    }
    if (b->handle == nullptr || b->ditutup) {
        galat(vm, "Berkas " + kutip(b->nama) + " wis ditutup, ora bisa " + std::string(apa) + "() maneh.");
        return nullptr;
    }
    return b->handle;
}

/// Tolak operasi yang memang tidak mungkin untuk mode `b`.
bool cek_mode(VM& vm, BerkasObj* b, std::string_view apa) {
    if (b->bisa_baca()) return true;
    galat(vm, "Berkas " + kutip(b->nama) + " dibuka ing mode nulis, ora bisa " + std::string(apa) + "().");
    return false;
}

/// Tolak `tulis*()` pada berkas yang dibuka buat baca.
bool cek_mode_tulis(VM& vm, BerkasObj* b) {
    if (b->bisa_tulis()) return true;
    galat(vm, "Berkas " + kutip(b->nama) + " dibuka ing mode maca, ora bisa nulis. "
                      "Buka maneh nganggo mode \"tulis\" utawa \"tambah\".");
    return false;
}

/// Tolak argumen yang harus teks tapi bukan.
bool cek_arg_teks(VM& vm, const Value& v, std::size_t posisi, std::string_view apa) {
    if (aku_teks(v)) return true;
    galat(vm, std::string(apa) + " nanging bisa nampa teks ing kapindeks " + std::to_string(posisi) +
                      ", dudu " + std::string(rt::nama_jenis(v)) + ".");
    return false;
}

// ===========================================================================
// Constructor: `anyaar Berkas(path, mode)`
// ===========================================================================

Value berkas_baru(VM& vm, Value /*this*/, std::vector<Value>& args) {
    if (!cek_arg_teks(vm, arg(args, 0), 1, "Berkas()")) return Value::mboh();
    const std::string_view path = teks_dari(arg(args, 0));
    if (path.empty()) {
        return galat(vm, "Berkas() perlu path, contone: anyar Berkas(\"data.txt\", \"baca\").");
    }
    const Value mode_arg = arg(args, 1);
    const std::string_view mode = teks_dari(mode_arg);
    // Mode kosong berarti "baca": itu yang hampir selalu dibutuhkan, dan
    // `anyaar Berkas("x.txt")` yang diam-diam salah membaca jauh lebih mungkin
    // daripada yang harus menyebut mode secara eksplisit.
    BerkasObj::Mode m = BerkasObj::Mode::Baca;
    if (mode.empty() || mode == "baca") {
        m = BerkasObj::Mode::Baca;
    } else if (mode == "tulis") {
        m = BerkasObj::Mode::Tulis;
    } else if (mode == "tambah") {
        m = BerkasObj::Mode::Tambah;
    } else if (!aku_teks(mode_arg)) {
        // `aku_teks`, bukan "adalah objek": `"ngawur"` juga objek, dan selama
        // ini teks apa pun -- termasuk mode yang salah ketik -- ditolak dengan
        // pesan "mode kudu teks", yang menyesatkan: masalahnya mode-nya
        // tidak dikenal, bukan tipenya salah.
        return galat(vm, "Mode berkas kudu teks, dudu " + std::string(rt::nama_jenis(mode_arg)) + ".");
    } else {
        return galat(vm, "Mode berkas ora weruh: " + kutip(mode) +
                             ". Mode sing ana: \"baca\", \"tulis\", \"tambah\".");
    }

    const char* f = (m == BerkasObj::Mode::Baca) ? "rb" : (m == BerkasObj::Mode::Tambah ? "ab" : "wb");
    auto* b = static_cast<BerkasObj*>(vm.heap().alokasi<BerkasObj>());
    b->h.kind = OK::Berkas;
    b->nama = std::string(path);
    b->mode = m;
    b->handle = std::fopen(b->nama.c_str(), f);
    if (b->handle == nullptr) {
        // Objek TIDAK dikembalikan: `anyaar` gagal total, dan program yang
        // memakai hasilnya tidak akan bisa apa-apa tanpa `coba`/`tangkep`.
        // Mengembalikan objek tanpa handle hanya memindahkan galat ke
        // pemanggilan berikutnya yang jauh dari titik kesalahan.
        return galat(vm, "Ora bisa mbuka berkas " + kutip(b->nama) + " ing mode " + kutip(mode) + ": " +
                             sebab());
    }
    return Value::obyek(b);
}

// ===========================================================================
// Method objek
// ===========================================================================

/// `f.tutup()` -- tutup handle. Aman dipanggil berkali-kali.
Value berkas_tutup(VM& /*vm*/, Value this_val, std::vector<Value>& /*args*/) {
    BerkasObj* b = berkas_dari(this_val);
    if (b == nullptr) return Value::mboh();  // bukan objek kita: biar pemanggil yang menerima nilai kosong
    if (b->handle == nullptr) {
        b->ditutup = true;
        return Value::mboh();  // sudah ditutup: bukan galat
    }
    const int hasil = std::fclose(b->handle);
    b->handle = nullptr;
    b->ditutup = true;
    if (hasil != 0) return Value::boolean(false);
    return Value::mboh();
}

/// `f.baca()` / `f.baca(n)` -- baca sisa isi sebagai teks.
///
/// Tanpa `n`: seluruh sisa. Dengan `n`: paling banyak `n` karakter UTF-8,
/// dipotong di batas karakter supaya hasil baca tetap valid UTF-8.
Value berkas_baca(VM& vm, Value this_val, std::vector<Value>& args) {
    BerkasObj* b = berkas_dari(this_val);
    std::FILE* h = handle_aktif(vm, b, "baca");
    if (h == nullptr) return Value::mboh();
    if (!cek_mode(vm, b, "baca")) return Value::mboh();

    const bool dibatasi = !args.empty() && !args[0].is_mboh() && args[0].is_angka();
    std::string hasil;
    char buf[8192];
    if (dibatasi) {
        const double n = args[0].as_number();
        if (!(n > 0)) {
            b->akhir = true;
            return Value::obyek(rt::buat_teks(vm.heap(), std::string_view{}));
        }
        const std::size_t batas = static_cast<std::size_t>(n);
        while (hasil.size() < batas) {
            const std::size_t sisa = std::min(batas - hasil.size(), sizeof(buf));
            const std::size_t dapat = std::fread(buf, 1, sisa, h);
            if (dapat == 0) break;
            hasil.append(buf, dapat);
        }
        hasil.resize(potong_utf8(hasil));
    } else {
        std::size_t dapat = 0;
        while ((dapat = std::fread(buf, 1, sizeof(buf), h)) > 0) hasil.append(buf, dapat);
    }
    if (std::ferror(h) != 0) {
        return galat(vm, "Gagal maca berkas " + kutip(b->nama) + ": " + sebab());
    }
    b->akhir = std::feof(h) != 0;
    return Value::obyek(rt::buat_teks(vm.heap(), hasil));
}

/// `f.baca_baris()` -- baca satu baris ke `f.baris`.
///
/// Mengembalikan `bener` kalau ada baris yang terbaca (isi ada di `f.baris`),
/// `salah` kalau sudah habis. Baris terakhir tanpa newline ikut terbaca.
/// Baris kosong di tengah berkas tetap menghasilkan baris kosong; hanya
/// setelah baris terakhir barulah `salah`, supaya `nalika f.baca_baris()` tidak
/// memotong isi terakhir yang kosong.
Value berkas_baca_baris(VM& vm, Value this_val, std::vector<Value>& /*args*/) {
    BerkasObj* b = berkas_dari(this_val);
    std::FILE* h = handle_aktif(vm, b, "baca_baris");
    if (h == nullptr) return Value::mboh();
    if (!cek_mode(vm, b, "baca_baris")) return Value::mboh();

    std::string baris;
    int c = std::fgetc(h);
    if (c == EOF) {
        b->akhir = true;
        b->baris = Value::obyek(rt::buat_teks(vm.heap(), std::string_view{}));
        return Value::boolean(false);
    }
    while (c != EOF && c != '\n') {
        baris.push_back(static_cast<char>(c));
        c = std::fgetc(h);
    }
    // Buang `\r` di ujung baris: berkas yang ditulis di Windows lalu dibaca di
    // Linux akan punya satu `\r` di setiap baris kalau tidak dibuang di sini.
    if (!baris.empty() && baris.back() == '\r') baris.pop_back();
    b->baris = Value::obyek(rt::buat_teks(vm.heap(), baris));
    b->akhir = std::feof(h) != 0;
    return Value::boolean(true);
}

/// `f.tulis(teks)` -- tulis teks lalu newline.
Value berkas_tulis(VM& vm, Value this_val, std::vector<Value>& args) {
    if (!cek_arg_teks(vm, arg(args, 0), 1, "tulis()")) return Value::mboh();
    const std::string_view isi = teks_dari(arg(args, 0));
    BerkasObj* b = berkas_dari(this_val);
    std::FILE* h = handle_aktif(vm, b, "tulis");
    if (h == nullptr) return Value::mboh();
    if (!cek_mode_tulis(vm, b)) return Value::mboh();

    const std::size_t n = isi.empty() ? 0 : std::fwrite(isi.data(), 1, isi.size(), h);
    if (n != isi.size() || std::fputc('\n', h) == EOF) {
        return galat(vm, "Gagal nulis menyang berkas " + kutip(b->nama) + ": " + sebab());
    }
    return Value::mboh();
}

/// `f.tulis_nol(teks)` -- tulis teks tanpa newline di akhir.
Value berkas_tulis_nol(VM& vm, Value this_val, std::vector<Value>& args) {
    if (!cek_arg_teks(vm, arg(args, 0), 1, "tulis_nol()")) return Value::mboh();
    const std::string_view isi = teks_dari(arg(args, 0));
    BerkasObj* b = berkas_dari(this_val);
    std::FILE* h = handle_aktif(vm, b, "tulis_nol");
    if (h == nullptr) return Value::mboh();
    if (!cek_mode_tulis(vm, b)) return Value::mboh();

    if (!isi.empty() && std::fwrite(isi.data(), 1, isi.size(), h) != isi.size()) {
        return galat(vm, "Gagal nulis menyang berkas " + kutip(b->nama) + ": " + sebab());
    }
    return Value::mboh();
}

/// `f.flush()` -- pastikan isi yang sudah ditulis benar-benar sampai ke OS.
Value berkas_flush(VM& vm, Value this_val, std::vector<Value>& /*args*/) {
    BerkasObj* b = berkas_dari(this_val);
    std::FILE* h = handle_aktif(vm, b, "flush");
    if (h == nullptr) return Value::mboh();
    if (std::fflush(h) != 0) {
        return galat(vm, "Gagal flush berkas " + kutip(b->nama) + ": " + sebab());
    }
    return Value::mboh();
}

// ===========================================================================
// Fungsi global (lapisan "seluruh isi")
// ===========================================================================

/// `baca_berkas(path)` -- seluruh isi sebagai teks.
///
/// Melempar `KleruBerkas` kalau tidak bisa dibuka; pakai `ada_berkas()` dulu
/// kalau "tidak ada" itu jawaban yang wajar, bukan kondisi galat.
Value global_baca_berkas(VM& vm, Value /*this*/, std::vector<Value>& args) {
    if (!cek_arg_teks(vm, arg(args, 0), 1, "baca_berkas()")) return Value::mboh();
    const std::string_view path = teks_dari(arg(args, 0));
    if (path.empty()) return galat(vm, "baca_berkas() perlu path, contone: baca_berkas(\"data.txt\").");
    std::FILE* h = std::fopen(std::string(path).c_str(), "rb");
    if (h == nullptr) {
        return galat(vm, "Ora bisa maca berkas " + kutip(path) + ": " + sebab());
    }
    std::string hasil;
    char buf[8192];
    std::size_t dapat = 0;
    while ((dapat = std::fread(buf, 1, sizeof(buf), h)) > 0) hasil.append(buf, dapat);
    const bool gagal = std::ferror(h) != 0;
    std::fclose(h);
    if (gagal) return galat(vm, "Gagal maca berkas " + kutip(path) + ": " + sebab());
    return Value::obyek(rt::buat_teks(vm.heap(), hasil));
}

/// `tulis_berkas(path, isi)` -- tulis seluruh isi, truncate dulu.
///
/// Mengembalikan jumlah byte yang ditulis. Menulis ke path yang sama dua kali
/// menimpa, bukan menambah -- itu yang membuatnya aman untuk dibangun dari
/// nol: kalau program gagal di tengah, tidak ada sisa parsial dari jalan
/// sebelumnya yang tercampur. Kalau yang dituju memang menambahkan, pakai
/// `Berkas` mode `"tambah"`.
Value global_tulis_berkas(VM& vm, Value /*this*/, std::vector<Value>& args) {
    if (!cek_arg_teks(vm, arg(args, 0), 1, "tulis_berkas()")) return Value::mboh();
    if (!cek_arg_teks(vm, arg(args, 1), 2, "tulis_berkas()")) return Value::mboh();
    const std::string_view path = teks_dari(arg(args, 0));
    const std::string_view isi = teks_dari(arg(args, 1));
    if (path.empty()) {
        return galat(vm, "tulis_berkas() perlu path, contone: tulis_berkas(\"d.txt\", \"halo\").");
    }
    std::FILE* h = std::fopen(std::string(path).c_str(), "wb");
    if (h == nullptr) return galat(vm, "Ora bisa nulis berkas " + kutip(path) + ": " + sebab());
    const std::size_t n = isi.empty() ? 0 : std::fwrite(isi.data(), 1, isi.size(), h);
    const std::size_t belum = (n != isi.size()) ? 1 : 0;
    const bool gagal_fclose = std::fclose(h) != 0;
    if (belum != 0 || gagal_fclose) {
        return galat(vm, "Gagal nulis berkas " + kutip(path) + ": " + sebab());
    }
    return Value::number(static_cast<double>(n));
}

/// `ada_berkas(path)` -- apakah path itu ada dan bisa dibaca sebagai teks?
///
/// Dua pemeriksaan, karena keduanya salah tanpa yang lain:
///
///  - `fopen` (bukan `stat`), supaya path yang ada tapi tidak bisa dibuka
///    -- misalnya tanpa izin baca -- dibalas `salah`.
///  - Pemeriksaan direktori, karena di Linux `fopen("/tmp", "rb")`
///    BERHASIL: yang gagal adalah `fread`-nya (EISDIR). Tanpa pemeriksaan
///    kedua, `ada_berkas("/tmp")` bilang `bener` lalu `baca_berkas("/tmp")`
///    langsung galat -- dua jawaban yang kontradiksi untuk pertanyaan yang
///    sama.
Value global_ada_berkas(VM& /*vm*/, Value /*this*/, std::vector<Value>& args) {
    const std::string_view path = teks_dari(arg(args, 0));
    if (path.empty()) return Value::boolean(false);
    const std::string p{path};
    std::FILE* h = std::fopen(p.c_str(), "rb");
    if (h == nullptr) return Value::boolean(false);
    std::fclose(h);
    return Value::boolean(!adalah_direktori(p.c_str()));
}

/// `hapus_berkas(path)` -- hapus berkas. `bener` kalau benar-benar terhapus.
Value global_hapus_berkas(VM& /*vm*/, Value /*this*/, std::vector<Value>& args) {
    const std::string_view path = teks_dari(arg(args, 0));
    if (path.empty()) return Value::boolean(false);
    if (std::remove(std::string(path).c_str()) != 0) return Value::boolean(false);
    return Value::boolean(true);
}

/// `ukuran_berkas(path)` -- ukuran dalam byte, atau `-1` kalau tidak ada.
///
/// `-1` (bukan `0`) supaya "tidak ada" bisa dibedakan dari "ada tapi kosong" --
/// keduanya jawaban yang sangat berbeda untuk program yang menghitung.
Value global_ukuran_berkas(VM& /*vm*/, Value /*this*/, std::vector<Value>& args) {
    const std::string_view path = teks_dari(arg(args, 0));
    if (path.empty()) return Value::number(-1);
    std::FILE* h = std::fopen(std::string(path).c_str(), "rb");
    if (h == nullptr) return Value::number(-1);
    if (std::fseek(h, 0, SEEK_END) != 0) {
        std::fclose(h);
        return Value::number(-1);
    }
    const long n = std::ftell(h);
    std::fclose(h);
    if (n < 0) return Value::number(-1);
    return Value::number(static_cast<double>(n));
}

// ===========================================================================
// Direktori
//
// Tiga operasi direktori, bukan karena lengkap, tapi karena tanpa mereka
// `tulis_berkas` hanya berguna untuk program yang lebih dulu sudah punya
// direktori -- yaitu hampir tidak ada. `mkdir` satu tingkat dengan sifat
// idempoten cukup untuk kebanyakan kasus; membuat rantai `a/b/c` masih perlu
// pemanggil yang memanggilnya thrice atau sudah punya direktori `a`.
// ===========================================================================

/// `ada_direktori(path)` -- lawannya `ada_berkas`.
///
/// Satu pasang, bukan dua tebakan: `ada_berkas` menjawab `salah` untuk
/// direktori dan `ada_direktori` menjawab `salah` untuk berkas, jadi selalu ada
/// tepat satu yang `bener`.
Value global_ada_direktori(VM& /*vm*/, Value /*this*/, std::vector<Value>& args) {
    const std::string_view path = teks_dari(arg(args, 0));
    if (path.empty()) return Value::boolean(false);
    return Value::boolean(adalah_direktori(std::string(path).c_str()));
}

/// `dadi_direktori(path)` -- buat satu direktori.
///
/// `bener` kalau direktori ada SESUDAH pemanggilan, termasuk kalau sudah ada
/// sebelumnya -- sifat idempoten seperti `mkdir -p` satu tingkat, supaya
/// pemanggil tidak perlu memeriksa dulu dan tidak gagal di jalan kedua.
///
/// Induknya harus sudah ada. Ini batas yang disengaja: membuat rantai
/// `a/b/c/d` berarti memecah path di setiap `/`, yang di POSIX harus memperlambat
/// filesystem yang distant; program yang butuh itu biasanya lebih baik
/// menjalankan satu proses yang sudah tahu susunannya.
Value global_dadi_direktori(VM& vm, Value /*this*/, std::vector<Value>& args) {
    const std::string_view path = teks_dari(arg(args, 0));
    if (path.empty()) {
        return galat(vm, "dadi_direktori() perlu path, contone: dadi_direktori(\"out\").");
    }
    const std::string p{path};
    if (adalah_direktori(p.c_str())) return Value::boolean(true);
    if (dadi_satu(p.c_str()) != 0 && errno != EEXIST) {
        return galat(vm, "Ora bisa nggawe direktori " + kutip(path) + ": " + sebab());
    }
    return Value::boolean(adalah_direktori(p.c_str()));
}

/// `hapus_direktori(path)` -- hapus direktori kosong.
///
/// `salah` kalau tidak ada, bukan direktori, atau belum kosong. Direktori yang
/// masih berisi sengaja tidak dihapus: menghapus isi orang tanpa diminta adalah
/// jenis kerusakan yang tidak bisa dibatalkan.
Value global_hapus_direktori(VM& /*vm*/, Value /*this*/, std::vector<Value>& args) {
    const std::string_view path = teks_dari(arg(args, 0));
    if (path.empty()) return Value::boolean(false);
    if (hapus_satu(std::string(path).c_str()) != 0) return Value::boolean(false);
    return Value::boolean(true);
}

}  // namespace

// ===========================================================================
// Pemasangan
// ===========================================================================

void pasang_berkas(VM& vm) {
    daftarkan(vm, "Berkas", 0, false, berkas_baru);
    daftarkan(vm, "baca_berkas", 1, false, global_baca_berkas);
    daftarkan(vm, "tulis_berkas", 2, false, global_tulis_berkas);
    daftarkan(vm, "ada_berkas", 1, false, global_ada_berkas);
    daftarkan(vm, "hapus_berkas", 1, false, global_hapus_berkas);
    daftarkan(vm, "ukuran_berkas", 1, false, global_ukuran_berkas);
    daftarkan(vm, "ada_direktori", 1, false, global_ada_direktori);
    daftarkan(vm, "dadi_direktori", 1, false, global_dadi_direktori);
    daftarkan(vm, "hapus_direktori", 1, false, global_hapus_direktori);

    const auto B = static_cast<std::uint8_t>(OK::Berkas);
    pasang_prototype_metode(vm, B, "tutup", 0, berkas_tutup);
    pasang_prototype_metode(vm, B, "baca", 0, berkas_baca);
    pasang_prototype_metode(vm, B, "baca_baris", 0, berkas_baca_baris);
    pasang_prototype_metode(vm, B, "tulis", 1, berkas_tulis);
    pasang_prototype_metode(vm, B, "tulis_nol", 1, berkas_tulis_nol);
    pasang_prototype_metode(vm, B, "flush", 0, berkas_flush);
}

}  // namespace jawa::vm::stdlib