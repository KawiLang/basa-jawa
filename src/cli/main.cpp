// CLI Basa Jawa: entry point `jawa`.
#include <cstdio>
#if !defined(_WIN32)
#include <unistd.h>
#endif
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cli/cli.h"
#include "lex/lexer.h"
#include "parse/ast.h"
#include "parse/parser.h"
#include "parse/ast_print.h"

namespace jawa::parse {
std::string cetak_ast(const ast::Program* p);
}  // namespace jawa::parse
#include "support/arena.h"
#include "support/diagnostics.h"
#include "rt/string.h"
#include "vm/vm.h"
#include "compile/compiler.h"

namespace jawa::cli {
namespace {

constexpr const char* kVersi = "1.0.0-dev";
constexpr const char* kNama = "jawa";

/// Kode keluar (Bagian 7).
enum ExitCode : int {
    Sukses = 0,
    GalatProgram = 1,
    SalahPakai = 2,
    BerkasTidakAda = 64,
    GalatInternal = 70,
};

void tulis_stderr(std::string_view s) {
    std::fwrite(s.data(), 1, s.size(), stderr);
    std::fputc('\n', stderr);
}

/// Baca seluruh isi berkas.
bool baca_berkas(const std::filesystem::path& p, std::string& keluar, int& kode) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        tulis_stderr("KleruCLI [C003] Berkas ora ketemu: " + p.string());
        kode = BerkasTidakAda;
        return false;
    }
    if (std::filesystem::is_directory(p, ec)) {
        tulis_stderr("KleruCLI [C002] Iku directori, ora berkas: " + p.string());
        kode = SalahPakai;
        return false;
    }
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        tulis_stderr("KleruCLI [C003] Gagal maca berkas: " + p.string());
        kode = BerkasTidakAda;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    keluar = ss.str();
    return true;
}

/// Cetak diagnostik dengan warna bila TTY.
void cetak_diagnostik(const support::DiagnosticBag& bag, bool warna) {
    for (const support::Diagnostic& d : bag.peringatan()) {
        std::string s = d.format();
        if (warna) s = "\x1b[33m" + s + "\x1b[0m";
        tulis_stderr(s);
    }
    for (const support::Diagnostic& d : bag.galat()) {
        std::string s = d.format();
        if (warna) s = "\x1b[31m" + s + "\x1b[0m";
        tulis_stderr(s);
    }
}

bool warna_aktif() {
    if (std::getenv("NO_COLOR") != nullptr) return false;
#if defined(_WIN32)
    return false;
#else
    return isatty(fileno(stderr)) != 0;
#endif
}

void cetak_bantuan() {
    std::printf(
        "%s %s — Basa Jawa (sintaks mirip JavaScript, kata kunci Jawa)\n"
        "\n"
        "Penggunaan:\n"
        "  jawa [opsi] berkas.jw [args...]   Jalankan program (alias: jawa run)\n"
        "  jawa run   berkas.jw              Jalankan program\n"
        "  jawa -e \"tulis(1+2)\"              Jalankan ekspresi\n"
        "  jawa cek   berkas.jw              Parse + analisis, tanpa eksekusi\n"
        "  jawa token berkas.jw              Cetak daftar token\n"
        "  jawa ast   berkas.jw              Cetak AST\n"
        "  jawa versi                        Cetak versi\n"
        "  jawa bantuan                      Cetak bantuan ini\n"
        "\n"
        "Opsi umum:\n"
        "  --jenis={mati,runtime,ketat}      Mode anotasi tipe bertahap\n"
        "  --ketat-titik-koma                Wajibkan titik koma\n"
        "  --ketat-krama                     Tolak campuran ngoko + krama\n"
        "  --tanpa-warna                     Matikan warna ANSI\n"
        "  --bahasa={jawa,id,dwi}            Bahasa pesan galat (bawaan: dwi)\n"
        "\n"
        "Tabel kata kunci (ngoko | krama):\n"
        "  ana | wonten        deklarasi variabel yang bisa diubah\n"
        "  tetep               deklarasi konstanta\n"
        "  gawe | damel        deklarasi fungsi\n"
        "  bali | wangsul      kembalikan nilai\n"
        "  yen | menawi        percabangan jika\n"
        "  liyane | sanes      cabang selain\n"
        "  nalika              loop selagi\n"
        "  lakoni | tindakaken loop minimal sekali\n"
        "  kanggo | kangge     loop (klasik, saka, ing, enteni)\n"
        "  pilih               percabangan banyak nilai\n"
        "  kasus | baku        nilai & cabang bawaan\n"
        "  mandheg | kendel    keluar dari loop\n"
        "  terusna | lajengaken lanjutkan loop\n"
        "  coba/tangkep/pungkasan  penanganan galat\n"
        "  uncal               lempar nilai\n"
        "  jinis               jenis nilai (typeof)\n"
        "  instansi_saka       uji instance\n"
        "  ing                 uji keberadaan kunci\n"
        "  busak               hapus properti\n"
        " >x                 operator pipeline (x |> f)\n"
        "  golongan | turunan | wiwit | anyar | statis | iki | induk\n"
        "  nampa | nyetel     accessor\n"
        "  mengko | enteni    async / await\n"
        "  metokake            yield (generator)\n"
        "  cocog               pencocokan pola (match)\n"
        "  ekspor/impor/saka/minangka   modul\n"
        "  bener/salah, kosong, mboh     nilai dasar\n"
        "  lan | utawa | ora   operator logika (&&, ||, !)\n",
        kNama, kVersi);
}

// ---------------------------------------------------------------------------
// `jawa token`
// ---------------------------------------------------------------------------
int perintah_token(const std::string& kode, bool warna) {
    support::DiagnosticBag lexbag{50};
    lex::Lexer lx(kode, "<stdin>", ".", lex::LexOptions{});
    lex::TokenList tl;
    lx.lex_semua(tl);

    std::size_t n = 0;
    for (const lex::Token& t : tl.token) {
        if (t.jenis == lex::Tok::Eof) break;
        std::printf("%4zu  %6u:%-4u %-22s", n++, t.range.mulai.baris, t.range.mulai.kolom, lex::token_name(t.jenis));
        if (!t.teks.empty()) {
            std::printf(" %s", warna ? "\x1b[36m" : "");
            std::printf("`%.*s`", static_cast<int>(t.teks.size() > 40 ? 40 : t.teks.size()), t.teks.data());
            if (warna) std::printf("\x1b[0m");
        } else if (t.jenis == lex::Tok::Text) {
            std::printf(" \"%.*s\"", static_cast<int>(t.nilai_teks.size() > 40 ? 40 : t.nilai_teks.size()), t.nilai_teks.data());
        } else if (t.jenis == lex::Tok::Number) {
            std::printf(" %g", t.angka);
        } else if (t.jenis == lex::Tok::Regex) {
            std::printf(" /%.*s/%.*s", static_cast<int>(t.regex_pola.size()), t.regex_pola.data(),
                        static_cast<int>(t.regex_flag.size()), t.regex_flag.data());
        }
        if (t.jenis == lex::Tok::TemplateText) {
            std::printf("  [awal=%d akhir=%d expr-ikut=%d]", t.template_awal ? 1 : 0, t.template_akhir ? 1 : 0,
                        t.template_expr_ikut ? 1 : 0);
        }
        if (t.jenis == lex::Tok::RBrace && t.template_expr_akhir) std::printf("  [expr-akhir]");
        if (t.baris_baru_sebelum) std::printf("  [baris-baru]");
        std::printf("\n");
    }
    std::printf("-- total %zu token\n", n);
    cetak_diagnostik(lx.bag(), warna);
    return lx.bag().ada_galat() ? GalatProgram : Sukses;
}

// ---------------------------------------------------------------------------
// `jawa cek` & `jawa ast`
// ---------------------------------------------------------------------------
struct HasilParse {
    support::Arena arena;
    lex::TokenList token;
    support::DiagnosticBag bag{50};
    ast::NodePtr program = nullptr;
};

void parse_kode(std::string_view kode, std::string_view nama, HasilParse& h) {
    lex::Lexer lx(kode, nama, ".", lex::LexOptions{});
    lx.lex_semua(h.token);
    h.bag.gabung(lx.bag());
    if (h.bag.penuh()) return;
    parse::Parser p(h.token, h.arena, h.bag, nama);
    h.program = p.parse_program();
}

int perintah_cek(const std::string& kode, bool warna) {
    HasilParse h;
    parse_kode(kode, "<stdin>", h);
    cetak_diagnostik(h.bag, warna);
    if (h.bag.ada_galat()) return GalatProgram;
    std::size_t n = 0;
    if (h.program != nullptr) n = static_cast<ast::Program*>(h.program)->body.size();
    std::printf("OK: %zu statement, tanpa galat.\n", n);
    return Sukses;
}

int perintah_ast(const std::string& kode, bool warna) {
    HasilParse h;
    parse_kode(kode, "<stdin>", h);
    if (h.program != nullptr) std::fputs(parse::cetak_ast(static_cast<const ast::Program*>(h.program)).c_str(), stdout);
    cetak_diagnostik(h.bag, warna);
    return h.bag.ada_galat() ? GalatProgram : Sukses;
}

/// Cetak satu chunk rekursif (fungsi anak ikut ditampilkan).
void cetak_chunk(const vm::Chunk& c, int kedalaman, const std::vector<vm::ChunkPtr>* semua) {
    std::string indent(static_cast<std::size_t>(kedalaman) * 2, ' ');
    std::printf("%s== fungsi %.*s ==\n", indent.c_str(), static_cast<int>(c.nama.size()), c.nama.data());
    std::printf("%s   slot=%u param=%u upvalue=%u%s%s\n", indent.c_str(), static_cast<unsigned>(c.jumlah_slot),
                static_cast<unsigned>(c.jumlah_param), static_cast<unsigned>(c.jumlah_upvalue),
                c.variadic ? " variadic" : "", c.panah ? " panah" : "");
    if (c.generator) std::printf("%s   (generator)\n", indent.c_str());
    if (c.mengko) std::printf("%s   (async)\n", indent.c_str());
    for (std::size_t i = 0; i < c.konstanta.size(); ++i) {
        const std::string teks = rt::nilai_ke_teks_inspect_dummy(c.konstanta[i]);
        std::printf("%s   konst[%zu] = %s\n", indent.c_str(), i, teks.c_str());
    }
    for (std::size_t i = 0; i < c.nama_properti.size(); ++i) {
        const std::string teks = rt::nilai_ke_teks_inspect_dummy(c.nama_properti[i]);
        std::printf("%s   nama[%zu] = %s\n", indent.c_str(), i, teks.c_str());
    }
    for (std::size_t i = 0; i < c.upvalue.size(); ++i) {
        std::printf("%s   up[%zu] = %.*s%s\n", indent.c_str(), i, static_cast<int>(c.upvalue[i].size()),
                    c.upvalue[i].data(),
                    i < c.upvalue_sumber.size() && c.upvalue_sumber[i] < 0
                        ? (c.upvalue_sumber[i] == -0x40000000 ? "  (global)" : "  (dari upvalue)")
                        : "");
    }
    for (std::size_t i = 0; i < c.kode.size(); ++i) {
        const vm::Instruksi& in = c.kode[i];
        std::printf("%s   %4zu  %-16s a=%-5u b=%-5u  ; %s\n", indent.c_str(), i, vm::opcode_nama(in.op),
                    static_cast<unsigned>(in.a), static_cast<unsigned>(in.b), vm::opcode_tanda(in.op));
        if (in.op == vm::Op::CLOSURE && in.a < c.anak.size()) cetak_chunk(*c.anak[in.a], kedalaman + 1, semua);
    }
    (void)semua;
}

/// Cetak bytecode hasil kompilasi (untuk debug & dokumentasi `docs/bytecode.md`).
int perintah_bytecode(const std::string& kode, bool warna) {
    HasilParse h;
    parse_kode(kode, "<stdin>", h);
    if (h.program == nullptr || h.bag.ada_galat()) {
        cetak_diagnostik(h.bag, warna);
        return GalatProgram;
    }
    support::Arena arena;
    gc::Heap heap;
    compile::Compiler kompiler(heap, h.bag, "<stdin>");
    const compile::HasilKompilasi hasil = kompiler.compile(static_cast<const ast::Program*>(h.program));
    cetak_diagnostik(h.bag, warna);
    if (h.bag.ada_galat() || hasil.modul == nullptr) return GalatProgram;
    cetak_chunk(*hasil.modul, 0, &hasil.semua);
    return Sukses;
}

int perintah_berkas(const std::string& perintah, const std::string& path, bool warna) {
    std::string kode;
    int rc = Sukses;
    if (!baca_berkas(path, kode, rc)) return rc;
    if (perintah == "token") return perintah_token(kode, warna);
    if (perintah == "cek") return perintah_cek(kode, warna);
    if (perintah == "ast") return perintah_ast(kode, warna);
    if (perintah == "bytecode") return perintah_bytecode(kode, warna);
    return SalahPakai;
}

}  // namespace
}  // namespace

namespace jawa::cli {

int jalankan(int argc, char** argv) {
    using namespace jawa;
    using namespace jawa::cli;

    bool warna = warna_aktif();
    std::string perintah;
    std::vector<std::string> posisial;
    std::string kode_e;
    bool ada_e = false;
    // Opsi runtime (lihat `vm::VMOptions`).
    jawa::vm::VMOptions opsi;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--tanpa-warna") {
            warna = false;
        } else if (a == "--bantuan" || a == "-h" || a == "--help") {
            perintah = "bantuan";
        } else if (a == "--versi" || a == "-v" || a == "--version") {
            perintah = "versi";
        } else if (a == "-e") {
            ada_e = true;
            if (i + 1 < argc) kode_e = argv[++i];
        } else if (a.rfind("-e", 0) == 0 && a.size() > 2) {
            ada_e = true;
            kode_e = a.substr(2);
        } else if (a == "--gc-stress") {
            opsi.gc_stress = true;
        } else if (a == "--log-gc") {
            opsi.log_gc = true;
        } else if (a == "--ketat-titik-koma") {
            opsi.strict_titik_koma = true;
        } else if (a == "--ketat-krama") {
            opsi.strict_krama = true;
        } else if (a == "--maks-langkah") {
            if (i + 1 < argc) opsi.maks_langkah = std::strtoull(argv[++i], nullptr, 10);
        } else if (a == "--maks-tumpukan") {
            if (i + 1 < argc) opsi.maks_tumpukan = std::strtoull(argv[++i], nullptr, 10);
        } else if (a == "--maks-memori") {
            if (i + 1 < argc) opsi.maks_memori_mb = std::strtoull(argv[++i], nullptr, 10);
        } else if (!a.empty() && a[0] == '-') {
            // opsi lain: diterima & diabaikan untuk sekarang
            if ((a == "--jenis" || a == "--bahasa" || a == "--izin") && i + 1 < argc) ++i;
        } else if (perintah.empty()) {
            perintah = "run";
            posisial.push_back(a);
        } else {
            posisial.push_back(a);
        }
    }

    // Perintah pertama: bila posisial pertama adalah perintah yang dikenal,
    // perlakukan sebagai perintah; selain itu `run`.
    if (perintah == "run" && !posisial.empty()) {
        static const char* kPerintah[] = {"run",   "cek",   "token",  "ast",  "versi",
                                          "bantuan", "help", "repl",   "tes",  "fmt",
                                          "ubah",  "bench", "bytecode", "lsp"};
        for (const char* p : kPerintah) {
            if (posisial[0] == p) {
                perintah = p;
                posisial.erase(posisial.begin());
                break;
            }
        }
    }
    if (perintah.empty()) perintah = "run";

    if (perintah == "versi") {
        std::printf("%s %s\n", kNama, kVersi);
        std::printf("Bahasa: Basa Jawa (ekstensi .jw)\n");
        std::printf("Build: C++%ld, mode nilai: %s\n", static_cast<long>(__cplusplus / 100L) - 2000L,
#ifdef JAWA_NO_NAN_BOX
                    "tagged union 16-byte"
#else
                    "NaN-boxing 8-byte"
#endif
        );
        return Sukses;
    }
    if (perintah == "bantuan" || perintah == "help") {
        cetak_bantuan();
        return Sukses;
    }
    if (perintah == "token" || perintah == "cek" || perintah == "ast" || perintah == "bytecode") {
        if (posisial.empty() && ada_e) {
            if (perintah == "token") return perintah_token(kode_e, warna);
            if (perintah == "cek") return perintah_cek(kode_e, warna);
            if (perintah == "bytecode") return perintah_bytecode(kode_e, warna);
            return perintah_ast(kode_e, warna);
        }
        if (posisial.empty()) {
            // baca dari stdin
            std::ostringstream ss;
            ss << std::cin.rdbuf();
            const std::string kode = ss.str();
            if (perintah == "token") return perintah_token(kode, warna);
            if (perintah == "cek") return perintah_cek(kode, warna);
            if (perintah == "bytecode") return perintah_bytecode(kode, warna);
            return perintah_ast(kode, warna);
        }
        return perintah_berkas(perintah, posisial[0], warna);
    }
    if (ada_e && perintah != "run") {
        if (perintah == "bytecode") return perintah_bytecode(kode_e, warna);
        return perintah_cek(kode_e, warna);
    }
    if (perintah == "run") {
        if (ada_e) {
            vm::VM mesin(opsi);
            vm::Status s = mesin.jalankan_sumber(kode_e, "<baris-kode>");
            std::fflush(stdout);
            return s == vm::Status::Selesai ? Sukses : GalatProgram;
        }
        if (posisial.empty()) {
            tulis_stderr("KleruCLI [C002] Ngarep-arep nama berkas .jw.");
            std::fputs("Coba `jawa bantuan` kanggo ndeleng dhaptar perintah.\n", stderr);
            return SalahPakai;
        }
        std::string kode;
        int rc = Sukses;
        if (!baca_berkas(posisial[0], kode, rc)) return rc;
        std::string dir = std::filesystem::path(posisial[0]).parent_path().string();
        if (dir.empty()) dir = ".";
        vm::VM mesin(opsi);
        vm::Status s = mesin.jalankan_sumber(kode, posisial[0], dir);
        std::fflush(stdout);
        return s == vm::Status::Selesai ? Sukses : GalatProgram;
    }

    tulis_stderr("KleruCLI [C005] Perintah ora weruh: " + perintah);
    return SalahPakai;
}

}  // namespace jawa::cli
