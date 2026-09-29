// Infrastruktur fuzzing bersama untuk Basa Jawa.
//
// Setiap target fuzz adalah berkas kecil yang memanggil
// `JAWAFUZZ_TARGET(label, fungsi)` di akhir. Makro itu mendefinisikan
// `LLVMFuzzerTestOneInput` bila dikompilasi dengan clang+libFuzzer, dan
// `main()` deterministik bila dikompilasi dengan GCC.
//
// Alasan ada driver mandiri: toolchain proyek ini hanya menyediakan GCC 12
// (lihat `STATUS.md`), sedangkan libFuzzer hanya ada di clang. Driver mandiri
// memakai PRNG deterministik (splitmix64) plus korpus seed dan mutasi, sehingga:
//
//   * campaign bisa diulang persis dari benih yang sama,
//   * bisa dijalankan di ctest tanpa dependensi,
//   * logika target tetap sama dengan yang dipakai libFuzzer.
//
// Catatan jujur: tanpa korpus crash yang tersimpan, "lolos" hanya berarti tidak
// ditemukan crash pada campaign ini, bukan bukti kebenaran. Lihat `STATUS.md`
// untuk posisi suite terhadap Definition of Done.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "compile/compiler.h"
#include "gc/heap.h"
#include "lex/lexer.h"
#include "parse/parser.h"
#include "support/arena.h"
#include "support/diagnostics.h"
#include "vm/vm.h"

namespace jawa::fuzz {

// ===========================================================================
// PRNG deterministik
// ===========================================================================

/// splitmix64: cepat, periodenya panjang, dan hanya butuh satu `uint64_t`
/// sebagai keadaan (xoshiro butuh empat, yang merepotkan saat menyimpan benih).
class Rng {
public:
    explicit Rng(std::uint64_t benih) noexcept : s_(benih) {}

    std::uint64_t berikut() noexcept {
        s_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = s_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    /// Bilangan bulat seragam pada [0, n). `n == 0` menghasilkan 0 supaya
    /// pemanggil tidak perlu memeriksa.
    std::size_t indeks(std::size_t n) noexcept {
        if (n == 0) return 0;
        return static_cast<std::size_t>(berikut() % n);
    }

private:
    std::uint64_t s_;
};

// ===========================================================================
// Batas campaign
// ===========================================================================

struct Batas {
    std::size_t maks_kasus = 5000;      ///< jumlah input yang dicoba
    std::size_t maks_byte = 32 * 1024;  ///< sumber lebih besar dipotong
};

/// Invarian internal gagal: laporkan ke stderr lalu `abort()` supaya core dump
/// tersimpan dan kasusnya bisa direproduksi ulang dengan `./fuzz_* <kasus>`.
[[noreturn]] inline void fuzz_gagal(const char* apa, const char* kenapa) {
    std::fprintf(stderr, "INVARIAN GAGAL: %s -- %s\n", apa, kenapa);
    std::fflush(stderr);
    std::abort();
}

// ===========================================================================
// Pipeline bertahap
// ===========================================================================

/// Setiap tahap berhenti pada error yang DIHARAPKAN (galat sintaks, dll). Yang
/// dicari adalah crash, akses di luar jangkauan, dan invarian rusak -- bukan
/// ketiadaan error.
struct Hasil {
    support::Arena arena;
    support::DiagnosticBag bag{64};
    lex::TokenList token;
    ast::NodePtr program;
    compile::HasilKompilasi kompilasi;
    bool bisa_dikompilasi = false;
};

/// Tahap 1: lexer. `false` berarti ada galat diagnostik, bukan crash.
inline bool tahap_lex(Hasil& h, std::string_view sumber) {
    lex::Lexer lx(sumber, "<fuzz>", ".", lex::LexOptions{});
    lx.lex_semua(h.token);
    h.bag.gabung(lx.bag());
    return !h.bag.ada_galat();
}

/// Tahap 2: parser. `TokenList` dan `Arena` harus hidup selama `NodePtr` dipakai,
/// jadi keduanya disimpan di dalam `Hasil`.
inline bool tahap_parse(Hasil& h) {
    if (h.token.token.empty()) return false;
    parse::Parser parser(h.token, h.arena, h.bag, "<fuzz>");
    h.program = parser.parse_program();
    return !h.bag.ada_galat() && h.program != nullptr;
}

/// Tahap 3: kompilator. `heap` harus heap yang sama dengan yang dipakai VM,
/// karena konstanta string yang dibuat kompilator hidup di heap itu.
inline bool tahap_kompilasi(Hasil& h, gc::Heap& heap) {
    if (h.program == nullptr) return false;
    compile::Compiler kompiler(heap, h.bag, "<fuzz>");
    h.kompilasi = kompiler.compile(static_cast<const ast::Program*>(h.program));
    h.bisa_dikompilasi = !h.bag.ada_galat() && h.kompilasi.modul != nullptr;
    return h.bisa_dikompilasi;
}

// ===========================================================================
// Mutasi input
// ===========================================================================

/// Potongan yang membuat mutasi sering menghasilkan program yang *hampir valid*
/// (mutasi byte murni hampir selalu gaggal di token pertama, jadi tidak pernah
/// menjangkau parser atau VM yang dalam).
constexpr std::string_view kPotongan[] = {
    "gawe ", "bali ", "tulis(", ")", "{", "}", "[", "]", ";", ",", ".", ":", "?",
    "pilih", "kasus", "baku:", "coba", "tangkep", "nalika", "kanggo", "mengko",
    "enteni", "metokake", "impor", "ekspor", "saka", "golongan", "wiwit", "nampa",
    "tetep", "ana", "wonten", "cocog", "dhaptar", "obyek", "null", "bener",
    "salah", "iku", "iki", "0x", "1e309", "1e-309", "0.1", ".5",
    "`${", "}`", "`${`${", "}", "=>", "??", "?.", "&&", "||", "===", "!==",
    "++", "--", "+=", "/", "%", "~", "&", "|", "^", ">>>",
    "\xf0\x9f\x98\x80",  // emoji: di luar blok aksara Jawa
    "\xe1\xa9\xbd",      // aksara Jawa HA U+A9BD
    "\\u0000", "\\xff", "\\u{A9BD}", "\\u{110000}",
    "'", "\"", "//", "/*", "*/", "`",
};

/// Bentuk Jawa utuh yang menyasar jalur yang jarang terkena mutasi acak
/// (generator, async, modul, pola, zona mati-temporal).
constexpr std::string_view kBentukJawa[] = {
    "gawe* g() { metokake 1; } kanggo (const x saka g()) { tulis(x); }",
    "gawe mengko a() { enteni Wektu.tundha(1); bali 2; } enteni a();",
    "pilih ([1, 2, 3]) { kasus [a, ...sisa]: tulis(a); baku: tulis(sisa); }",
    "cocog (x) { kasus [1, 2]: bali 1; kasus {a}: bali a; baku: bali 0; }",
    "impor * saka \"./a.jw\"; ekspor { x }; tetep x = 1;",
    "coba { pilih (1) { kasus 1: tulis(q); } } tangkep (e) { tulis(e); }",
    "tulis(x); tetep x = 1;",
    "golongan X { #a; wiwit(a) { iki.#a = a; } nampa v() { bali iki.#a; } }",
    "gawe f(a, b = 2, ...sisa) { bali a + b + sisa.dawa; } f(1);",
    "const { x, ...sisa } = { x: 1, y: 2 }; tulis(x, sisa);",
};

inline void sisipkan(std::vector<std::uint8_t>& buf, std::size_t p, std::string_view s,
                     std::size_t maks_byte) {
    if (buf.size() + s.size() > maks_byte) return;
    buf.insert(buf.begin() + static_cast<std::ptrdiff_t>(p), s.begin(), s.end());
}

inline void mutasi(std::vector<std::uint8_t>& buf, Rng& rng, std::size_t maks_byte) {
    if (buf.empty()) {
        sisipkan(buf, 0,
                 kBentukJawa[rng.indeks(sizeof(kBentukJawa) / sizeof(kBentukJawa[0]))], maks_byte);
        return;
    }
    const std::size_t n_mutasi = 1 + rng.indeks(4);
    for (std::size_t i = 0; i < n_mutasi; ++i) {
        if (buf.empty()) break;
        switch (rng.indeks(6)) {
            case 0: {  // balik satu bit
                const std::size_t p = rng.indeks(buf.size());
                buf[p] = static_cast<std::uint8_t>(buf[p] ^ (1u << rng.indeks(8)));
                break;
            }
            case 1: {  // ganti byte acak
                buf[rng.indeks(buf.size())] = static_cast<std::uint8_t>(rng.berikut());
                break;
            }
            case 2: {  // sisip potongan
                sisipkan(buf, rng.indeks(buf.size() + 1),
                         kPotongan[rng.indeks(sizeof(kPotongan) / sizeof(kPotongan[0]))], maks_byte);
                break;
            }
            case 3: {  // hapus rentang
                const std::size_t p = rng.indeks(buf.size());
                const std::size_t len = 1 + rng.indeks(8);
                if (p + len > buf.size()) break;
                buf.erase(buf.begin() + static_cast<std::ptrdiff_t>(p),
                          buf.begin() + static_cast<std::ptrdiff_t>(p + len));
                break;
            }
            case 4: {  // duplikasi rentang (memperpanjang program)
                const std::size_t p = rng.indeks(buf.size());
                const std::size_t len = rng.indeks(buf.size() - p + 1);
                if (buf.size() + len > maks_byte) break;
                const std::vector<std::uint8_t> salinan(
                    buf.begin() + static_cast<std::ptrdiff_t>(p),
                    buf.begin() + static_cast<std::ptrdiff_t>(p + len));
                buf.insert(buf.begin() + static_cast<std::ptrdiff_t>(p), salinan.begin(),
                           salinan.end());
                break;
            }
            default: {  // sisip program Jawa utuh
                sisipkan(buf, rng.indeks(buf.size() + 1),
                         kBentukJawa[rng.indeks(sizeof(kBentukJawa) / sizeof(kBentukJawa[0]))],
                         maks_byte);
                break;
            }
        }
    }
    if (buf.size() > maks_byte) buf.resize(maks_byte);
}

inline void seed_byte_acak(std::vector<std::uint8_t>& buf, Rng& rng, std::size_t maks_byte) {
    const std::size_t n = rng.indeks(maks_byte / 2 + 1);
    buf.resize(n);
    for (std::uint8_t& b : buf) b = static_cast<std::uint8_t>(rng.berikut());
}

// ===========================================================================
// Pemeriksaan invarian bytecode
// ===========================================================================

inline bool adalah_lompatan(vm::Op op) {
    return op == vm::Op::JUMP || op == vm::Op::JUMP_IF_FALSE || op == vm::Op::JUMP_IF_TRUE ||
           op == vm::Op::JUMP_IF_NULLISH || op == vm::Op::JUMP_IF_NOT_NULLISH;
}

/// Setiap lompatan harus menunjuk instruksi yang ada. Label di akhir kode
/// (tujuan == jumlah instruksi) sah; melewati itu tidak.
inline void periksa_lompatan(const vm::Chunk& c) {
    for (const vm::Instruksi& ins : c.kode) {
        if (!adalah_lompatan(ins.op)) continue;
        if (static_cast<std::size_t>(ins.a) > c.kode.size()) {
            fuzz_gagal("lompatan melewati akhir kode", "tujuan lompatan di luar jangkauan");
        }
    }
}

/// `jumlah_slot` dihitung kompilator; kalau lebih kecil dari slot tertinggi yang
/// dipakai instruksi, VM akan menulis melewati frame saat berjalan.
inline void periksa_slot(const vm::Chunk& c) {
    std::size_t maks = 0;
    for (const vm::Instruksi& ins : c.kode) {
        switch (ins.op) {
            case vm::Op::DEF_LOCAL:
            case vm::Op::SET_LOCAL:
            case vm::Op::GET_LOCAL:
            case vm::Op::TDZ_CHECK:
                maks = std::max(maks, static_cast<std::size_t>(ins.a) + 1);
                break;
            default:
                break;
        }
    }
    if (maks > c.jumlah_slot) {
        fuzz_gagal("slot lokal melewati jumlah_slot", "kompilator menghitung slot terlalu kecil");
    }
    for (const vm::Instruksi& ins : c.kode) {
        if (ins.op == vm::Op::KONSTAN && ins.a >= c.konstanta.size()) {
            fuzz_gagal("indeks konstanta di luar jangkauan", "KONSTAN menunjuk konstanta yang tidak ada");
        }
        if (ins.op == vm::Op::GET_PROP && ins.a >= c.nama_properti.size()) {
            fuzz_gagal("indeks nama di luar jangkauan", "GET_PROP menunjuk nama yang tidak ada");
        }
    }
    for (const std::uint16_t ip : c.tdz_daftar) {
        if (static_cast<std::size_t>(ip) > c.kode.size()) {
            fuzz_gagal("entri TDZ di luar jangkauan", "ip deklarasi melewati akhir kode");
        }
    }
}

inline void periksa_semua_chunk(const std::vector<vm::ChunkPtr>& semua) {
    for (const vm::ChunkPtr& c : semua) {
        if (c == nullptr) continue;
        periksa_lompatan(*c);
        periksa_slot(*c);
    }
}

// ===========================================================================
// VM dengan langkah dan keluaran terbatas
// ===========================================================================

/// Batas keluaran yang masih waras. Program yang mencetak lebih dari ini
/// dianggap salah (loop tak berujung pada `tulis`, atau tebakan salah soal
/// spread).
inline constexpr std::size_t kMaksKeluaran = 4u * 1024u * 1024u;

inline void cek_batas_keluaran(const std::string& keluar) {
    if (keluar.size() > kMaksKeluaran) {
        fuzz_gagal("keluaran tanpa batas", "program mencetak melebihi batas wajar");
    }
}

/// `keluaran` VM diarahkan ke `std::string` milik pemanggil supaya program yang
/// mencetak tanpa batas tidak membanjiri log fuzzing, dan supaya keluarannya
/// bisa diperiksa.
class KonteksJalan {
public:
    /// `maks_langkah` mengikat berapa lama satu program boleh berjalan sebelum
    /// VM menyerah dengan `KleruRentang`. Tanpa batas ini, program tak berujung
    /// akan menggantung seluruh campaign.
    ///
    /// `baca_berkas` (bila diisi) menggantikan filesystem sungguhan dengan
    /// pembacaan virtual -- dipakai target linker modul supaya fuzzing tidak
    /// pernah menyentuh disk.
    explicit KonteksJalan(std::function<bool(const std::string&, std::string&)> baca = nullptr,
                          std::size_t maks_langkah = 200000) {
        vm::VMOptions o;
        o.maks_langkah = maks_langkah;
        o.maks_tumpukan = 256;
        o.maks_reentrancy = 8;
        o.keluaran = &keluaran_;
        o.baca_berkas = std::move(baca);
        vm_ = std::make_unique<vm::VM>(o);
    }
    KonteksJalan(const KonteksJalan&) = delete;
    KonteksJalan& operator=(const KonteksJalan&) = delete;

    vm::VM& mesin() noexcept { return *vm_; }
    std::string& keluaran() noexcept { return keluaran_; }

private:
    std::string keluaran_;
    std::unique_ptr<vm::VM> vm_;
};

// ===========================================================================
// Statistik & badan driver
// ===========================================================================

/// Dicetak `main` supaya hasilnya bisa dibandingkan antar revisi, dan supaya
/// "0 kasus" tidak terlihat sama dengan "lolos".
struct Statistik {
    std::size_t total = 0;
    std::size_t galat_lex = 0;
    std::size_t galat_parse = 0;
    std::size_t kompilasi_ok = 0;
    std::size_t jalan_ok = 0;
};

inline std::string_view lihat(const std::vector<std::uint8_t>& buf) {
    return std::string_view(reinterpret_cast<const char*>(buf.data()), buf.size());
}

/// Corps target paling dasar: lexer lalu parser. Dipakai target yang tidak
/// butuh kompilasi atau VM, dan jadi titik masuk bersama untuk target lain.
inline void jalankan_target_umum(const std::vector<std::uint8_t>& buf, Statistik& st,
                                 std::uint64_t /*benih*/) {
    ++st.total;
    Hasil hasil;
    if (!tahap_lex(hasil, lihat(buf))) {
        ++st.galat_lex;
        return;
    }
    if (!tahap_parse(hasil)) {
        ++st.galat_parse;
        return;
    }
    ++st.kompilasi_ok;
}

/// Lokasi berkas kasus. `JAWA_FUZZ_KASUS` mengizinkan penimpaan; default-nya
/// `/tmp/jawa_fuzz_kasus.bin`.
inline const char* jalur_kasus() {
    const char* p = std::getenv("JAWA_FUZZ_KASUS");
    return (p != nullptr && *p != '\0') ? p : "/tmp/jawa_fuzz_kasus.bin";
}

/// Tulis input ke berkas kasus.
inline void tulis_kasus(const char* path, const std::vector<std::uint8_t>& buf) {
    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return;
    if (!buf.empty()) std::fwrite(buf.data(), 1, buf.size(), f);
    std::fclose(f);
}

/// Simpan kasus lalu laporkan. Exception C++ dari pustaka standar (mis.
/// `string_view::substr` di luar jangkauan) adalah BUG, bukan hasil yang
/// diharapkan, jadi harus menghasilkan core dump plus berkas yang langsung bisa
/// dipakai ulang:
///
///     ./build/fuzz/fuzz_lexer /tmp/jawa_fuzz_kasus.bin
inline void simpan_kasus_gagal(const std::vector<std::uint8_t>& buf) {
    const char* path = jalur_kasus();
    tulis_kasus(path, buf);
    std::fprintf(stderr, "kasus disimpan ke %s (%zu byte)\n", path, buf.size());
}

inline std::vector<std::uint8_t> baca_berkas(const char* path) {
    FILE* f = std::fopen(path, "rb");
    std::vector<std::uint8_t> buf;
    if (f == nullptr) return buf;
    std::uint8_t tmp[4096];
    std::size_t n = 0;
    while ((n = std::fread(tmp, 1, sizeof(tmp), f)) > 0) buf.insert(buf.end(), tmp, tmp + n);
    std::fclose(f);
    return buf;
}

/// Jalankan satu kasus, mengubah exception C++ yang tidak terduga menjadi laporan
/// crash yang bisa direproduksi. Exception di sini hampir selalu bug (mis.
/// `std::string_view::substr` di luar jangkauan); tidak boleh lolos tanpa jejak.
template <typename F>
void panggil_target(const std::vector<std::uint8_t>& buf, Statistik& st, std::uint64_t benih,
                    F&& f) {
    try {
        f(buf, st, benih);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "EXCEPTION: %s\n", e.what());
        simpan_kasus_gagal(buf);
        std::fflush(stderr);
        std::abort();
    } catch (...) {
        std::fprintf(stderr, "EXCEPTION: jenis tak dikenal\n");
        simpan_kasus_gagal(buf);
        std::fflush(stderr);
        std::abort();
    }
}

}  // namespace jawa::fuzz

// ===========================================================================
// Makro target
// ===========================================================================

/// `JAWAFUZZ_TARGET(label, fungsi)` membangun driver untuk satu target.
///
/// Bentuk clang: `-DJAWAFUZZ_LIBFUZZER` mendefinisikan
/// `LLVMFuzzerTestOneInput`, jadi berkas ini langsung bisa dipakai libFuzzer.
///
/// Bentuk GCC: `main()` menjalankan campaign deterministik dan mencetak
/// statistiknya. Seed corpus dibaca dari argumen yang bukan option; `--batas=N`
/// mengatur jumlah kasus dan `--benih=N` mengatur benih PRNG. Tanpa argumen,
/// campaign memakai seed yang tertanam (`kBentukJawa`).
///
/// `fungsi` bertanda tangan
/// `void(const std::vector<std::uint8_t>&, Statistik&, std::uint64_t)`.
#if defined(JAWAFUZZ_LIBFUZZER)
#    define JAWAFUZZ_TARGET(LABEL, FUNGSI)                                 \
        extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,   \
                                              std::size_t size) {          \
            std::vector<std::uint8_t> buf(data, data + size);              \
            ::jawa::fuzz::Statistik st;                                    \
            ::jawa::fuzz::panggil_target(buf, st, 0, FUNGSI);              \
            return 0;                                                       \
        }
#else
#    define JAWAFUZZ_TARGET(LABEL, FUNGSI)                                                  \
        int main(int argc, char** argv) {                                                  \
            using ::jawa::fuzz::Statistik;                                                 \
            const ::jawa::fuzz::Batas batas;                                               \
            std::vector<std::vector<std::uint8_t>> korpus;                                \
            std::size_t maks_kasus = batas.maks_kasus;                                     \
            std::uint64_t benih = 0xB4A5A4A5EEULL;                                      \
            for (int i = 1; i < argc; ++i) {                                               \
                const bool batas_nilai = std::strncmp(argv[i], "--batas=", 8) == 0;          \
                const bool batas_spasi = std::strcmp(argv[i], "--batas") == 0;               \
                if (batas_nilai) {                                                         \
                    maks_kasus =                                                           \
                        static_cast<std::size_t>(std::strtoull(argv[i] + 8, nullptr, 10));\
                    continue;                                                              \
                }                                                                         \
                if (batas_spasi) {                                                         \
                    if (i + 1 < argc)                                                      \
                        maks_kasus = static_cast<std::size_t>(                             \
                            std::strtoull(argv[i + 1], nullptr, 10));                      \
                    ++i;                                                                   \
                    continue;                                                              \
                }                                                                         \
                const bool benih_nilai = std::strncmp(argv[i], "--benih=", 8) == 0;          \
                const bool benih_spasi = std::strcmp(argv[i], "--benih") == 0;               \
                if (benih_nilai) {                                                         \
                    benih = std::strtoull(argv[i] + 8, nullptr, 10);                        \
                    continue;                                                              \
                }                                                                         \
                if (benih_spasi) {                                                         \
                    if (i + 1 < argc) benih = std::strtoull(argv[i + 1], nullptr, 10);       \
                    ++i;                                                                   \
                    continue;                                                              \
                }                                                                         \
                std::vector<std::uint8_t> b = ::jawa::fuzz::baca_berkas(argv[i]);          \
                if (!b.empty()) korpus.push_back(std::move(b));                            \
            }                                                                             \
            if (korpus.empty()) {                                                          \
                for (std::string_view s : ::jawa::fuzz::kBentukJawa)                      \
                    korpus.emplace_back(s.begin(), s.end());                                \
            }                                                                             \
            ::jawa::fuzz::Rng rng(benih);                                                  \
            Statistik st;                                                                  \
            for (std::size_t n = 0; n < maks_kasus; ++n) {                                 \
                std::vector<std::uint8_t> buf;                                            \
                const std::size_t putar = rng.indeks(100);                                \
                const std::size_t dasar = rng.indeks(korpus.size());                       \
                if (putar < 75) {                                                         \
                    buf = korpus[dasar];                                                   \
                    ::jawa::fuzz::mutasi(buf, rng, batas.maks_byte);                       \
                } else if (putar < 90) {                                                  \
                    buf = korpus[dasar];                                                   \
                } else {                                                                  \
                    ::jawa::fuzz::seed_byte_acak(buf, rng, batas.maks_byte);               \
                }                                                                         \
                /* Tulis kasus SEBELUM menjalankan: crash dari glibc          */        \
                /* (assertion malloc) tidak bisa ditangkap, jadi berkas harus   */        \
                /* sudah berisi input yang sedang diuji.                         */        \
                ::jawa::fuzz::tulis_kasus(::jawa::fuzz::jalur_kasus(), buf);                \
                ::jawa::fuzz::panggil_target(buf, st, benih ^ n, FUNGSI);                \
            }                                                                             \
            std::printf("%-13s %7zu kasus | lex %6zu gagal | parse %6zu gagal | "           \
                        "kompilasi %6zu | jalan %6zu | korpus %3zu | benih %llu\n",       \
                        LABEL, st.total, st.galat_lex, st.galat_parse, st.kompilasi_ok,    \
                        st.jalan_ok, korpus.size(),                                        \
                        static_cast<unsigned long long>(benih));                           \
            return 0;                                                                      \
        }
#endif
