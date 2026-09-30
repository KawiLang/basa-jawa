// Test lexer: token, escape, template, regex/div, ASI, UTF-8, alias krama.
#include "harness.h"
#include "lex/lexer.h"

#include <string>
#include <vector>

using namespace jawa;

namespace {

struct Hasil {
    lex::TokenList token;
    support::DiagnosticBag bag{50};
};

Hasil lex_semua(std::string_view src, lex::LexOptions opt = {}) {
    Hasil h;
    lex::Lexer lx(src, "test.jw", ".", opt);
    lx.lex_semua(h.token);
    h.bag = lx.bag();
    return h;
}

std::vector<lex::Tok> jenis(const lex::TokenList& t) {
    std::vector<lex::Tok> v;
    for (const lex::Token& tok : t.token) {
        if (tok.jenis != lex::Tok::Eof) v.push_back(tok.jenis);
    }
    return v;
}

bool ada(const lex::TokenList& t, lex::Tok k) {
    for (const lex::Token& tok : t.token) {
        if (tok.jenis == k) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("lexer: token dasar") {
    const Hasil h = lex_semua("42 + x - y;");
    CHECK(!h.bag.ada_galat());
    const auto j = jenis(h.token);
    REQUIRE(j.size() == 6);
    CHECK(j[0] == lex::Tok::Number);
    CHECK(j[1] == lex::Tok::Plus);
    CHECK(j[2] == lex::Tok::Ident);
    CHECK(j[3] == lex::Tok::Minus);
    CHECK(j[4] == lex::Tok::Ident);
    CHECK(j[5] == lex::Tok::Semi);
}

TEST_CASE("lexer: nama token lengkap") {
    CHECK(std::string(lex::token_name(lex::Tok::Number)) == "angka");
    CHECK(std::string(lex::token_name(lex::Tok::Arrow)) == "=>");
    CHECK(std::string(lex::token_name(lex::Tok::PipeGreater)) == "|>");
}

TEST_CASE("lexer: angka desimal & separator") {
    const Hasil a = lex_semua("1_000_000");
    REQUIRE(a.token.token.size() >= 2);
    CHECK(a.token.token[0].angka == 1000000.0);
    CHECK(!a.bag.ada_galat());

    const Hasil b = lex_semua("3.14");
    CHECK(b.token.token[0].angka == 3.14);
    const Hasil c = lex_semua("2.5e-3");
    CHECK(c.token.token[0].angka == 0.0025);
    const Hasil d = lex_semua(".5");
    CHECK(d.token.token[0].angka == 0.5);
    const Hasil e = lex_semua("0xFF");
    CHECK(e.token.token[0].angka == 255.0);
    const Hasil f = lex_semua("0b1010");
    CHECK(f.token.token[0].angka == 10.0);
    const Hasil g = lex_semua("0o17");
    CHECK(g.token.token[0].angka == 15.0);
}

TEST_CASE("lexer: bigint") {
    const Hasil h = lex_semua("123n");
    CHECK(h.token.token[0].jenis == lex::Tok::BigInt);
    CHECK(h.token.token[0].bigint_teks == "123");
    const Hasil h2 = lex_semua("0xFFn");
    CHECK(h2.token.token[0].jenis == lex::Tok::BigInt);
    CHECK(h2.token.token[0].bigint_teks == "FF");
}

TEST_CASE("lexer: teks & escape") {
    const Hasil h = lex_semua("\"halo\"");
    CHECK(!h.bag.ada_galat());
    CHECK(h.token.token[0].nilai_teks == "halo");

    const Hasil e = lex_semua("\"a\\nb\\tc\\u00e9\\x41\\u{1F600}\"");
    CHECK(!e.bag.ada_galat());
    CHECK(e.token.token[0].nilai_teks == "a\nb\tc\xc3\xa9" "A\xf0\x9f\x98\x80");

    const Hasil s = lex_semua("'\\''");
    CHECK(s.token.token[0].nilai_teks == "'");
}

TEST_CASE("lexer: escape tidak valid") {
    const Hasil h = lex_semua("\"\\q\"");
    CHECK(h.bag.ada_galat());
    CHECK(h.bag.galat()[0].code == "L004");
}

TEST_CASE("lexer: string tidak ditutup tidak crash") {
    const Hasil h = lex_semua("\"abc");
    CHECK(h.bag.ada_galat());
    CHECK(h.bag.galat()[0].code == "L002");
}

TEST_CASE("lexer: identifier unicode termasuk aksara jawa") {
    const Hasil h = lex_semua("nama = ꦲꦤꦕꦫꦏ;");
    CHECK(!h.bag.ada_galat());
    CHECK(ada(h.token, lex::Tok::Ident));
    CHECK(ada(h.token, lex::Tok::Eq));
}

TEST_CASE("lexer: kata kunci ngoko & krama") {
    const Hasil a = lex_semua("ana x = 1;");
    CHECK(ada(a.token, lex::Tok::KwAna));
    const Hasil b = lex_semua("wonten x = 1;");
    CHECK(ada(b.token, lex::Tok::KwAna));
    const Hasil c = lex_semua("menawi (x) { }");
    CHECK(ada(c.token, lex::Tok::KwIf));
    const Hasil d = lex_semua("yen (x) { }");
    CHECK(ada(d.token, lex::Tok::KwIf));
    CHECK(!d.bag.ada_galat());
    const Hasil e = lex_semua("kangge (x saka y) { }");
    CHECK(ada(e.token, lex::Tok::KwFor));
    CHECK(ada(e.token, lex::Tok::KwFrom));
    const Hasil f = lex_semua("bali bener; wangsul salah;");
    CHECK(ada(f.token, lex::Tok::KwReturn));
    CHECK(ada(f.token, lex::Tok::KwTrue));
    CHECK(ada(f.token, lex::Tok::KwFalse));
    const Hasil g = lex_semua("tut wong; apa_wae x;");
    CHECK(ada(g.token, lex::Tok::Ident));  // "tut" bukan kata kunci
    CHECK(ada(g.token, lex::Tok::KwAny));
}

TEST_CASE("lexer: --ketat-krama menolak campuran") {
    lex::LexOptions opt;
    opt.ketat_krama = true;
    const Hasil h = lex_semua("ana a = 1; wonten b = 2;", opt);
    CHECK(h.bag.ada_galat());
    CHECK(h.bag.galat()[0].code == "R036");
}

TEST_CASE("lexer: template literal sederhana") {
    const Hasil h = lex_semua("`Halo, ${jeneng}!`");
    CHECK(!h.bag.ada_galat());
    // Minimal: ada token TemplateText, LBrace, Ident, RBrace(template), TemplateText
    bool saw_awal = false;
    bool saw_akhir = false;
    int expr_ikut = 0;
    for (const lex::Token& t : h.token.token) {
        if (t.jenis == lex::Tok::TemplateText) {
            if (t.template_awal) saw_awal = true;
            if (t.template_akhir) saw_akhir = true;
            if (t.template_expr_ikut) ++expr_ikut;
        }
    }
    CHECK(saw_awal);
    CHECK(saw_akhir);
    CHECK(expr_ikut == 1);
}

TEST_CASE("lexer: template multibaris & escape") {
    const Hasil h = lex_semua("`baris1\nbaris2 \\` x ${1+2} akhir`");
    CHECK(!h.bag.ada_galat());
}

TEST_CASE("lexer: template tidak ditutup") {
    const Hasil h = lex_semua("`abc");
    CHECK(h.bag.ada_galat());
    CHECK(h.bag.galat()[0].code == "L003");
}

TEST_CASE("lexer: regex vs pembagian") {
    const Hasil a = lex_semua("a / b / c");
    CHECK(ada(a.token, lex::Tok::Slash));
    CHECK(!ada(a.token, lex::Tok::Regex));

    const Hasil b = lex_semua("x = /abc+/gi;");
    CHECK(ada(b.token, lex::Tok::Regex));
    bool rx = false;
    for (const lex::Token& t : b.token.token) {
        if (t.jenis == lex::Tok::Regex) {
            CHECK(t.regex_pola == "abc+");
            CHECK(t.regex_flag == "gi");
            rx = true;
        }
    }
    CHECK(rx);

    const Hasil c = lex_semua("tulis(/halo/.uji(\"halo\"))");
    CHECK(ada(c.token, lex::Tok::Regex));

    const Hasil d = lex_semua("n = a / b; n /= 2;");
    CHECK(ada(d.token, lex::Tok::SlashEq));
    CHECK(!ada(d.token, lex::Tok::Regex));
}

TEST_CASE("lexer: ASI flag baris baru") {
    const Hasil h = lex_semua("a\nb\nc");
    const std::vector<lex::Token>& t = h.token.token;
    REQUIRE(t.size() >= 4);
    CHECK(t[0].baris_baru_sebelum == false);
    CHECK(t[1].baris_baru_sebelum == true);
    CHECK(t[2].baris_baru_sebelum == true);
}

TEST_CASE("lexer: jenis komentar") {
    const Hasil h = lex_semua("// baris\n/* blok */ /** dok */ a");
    CHECK(!h.bag.ada_galat());
    CHECK(h.token.token.size() == 2);  // ident + eof
    const Hasil b = lex_semua("/* tak ditutup");
    CHECK(b.bag.ada_galat());
    CHECK(b.bag.galat()[0].code == "L006");
}

TEST_CASE("lexer: shebang & BOM diabaikan") {
    const Hasil h = lex_semua("#!/usr/bin/env jawa\ntulis(1);");
    CHECK(!h.bag.ada_galat());
    const Hasil b = lex_semua("\xEF\xBB\xBFtulis(1);");
    CHECK(!b.bag.ada_galat());
}

TEST_CASE("lexer: operator majemuk") {
    const Hasil h = lex_semua("a ?\?= b ?? c; d **= e; f >>>= g; h |> i |> j; k => l;");
    CHECK(!h.bag.ada_galat());
    CHECK(ada(h.token, lex::Tok::QuestionQuestionEq));
    CHECK(ada(h.token, lex::Tok::StarStarEq));
    CHECK(ada(h.token, lex::Tok::UShrEq));
    CHECK(ada(h.token, lex::Tok::PipeGreater));
    CHECK(ada(h.token, lex::Tok::Arrow));
}

TEST_CASE("lexer: private name") {
    const Hasil h = lex_semua("iki.#rahasia = 1;");
    CHECK(ada(h.token, lex::Tok::PrivateName));
    bool ketemu = false;
    for (const lex::Token& t : h.token.token) {
        if (t.jenis == lex::Tok::PrivateName) ketemu = (t.teks == "#rahasia");
    }
    CHECK(ketemu);
}

TEST_CASE("lexer: utf8 rusak tidak crash") {
    std::string rusak = "a = \"";
    rusak.push_back(static_cast<char>(0xC3));  // awal 2-byte tanpa lanjutan
    rusak += "b\";";
    const Hasil h = lex_semua(rusak);
    // Tidak harus crash; boleh ada diagnostik.
    CHECK(h.token.token.size() >= 2);
}

TEST_CASE("lexer: publik token_name stabil") {
    for (int i = 0; i < static_cast<int>(lex::Tok::TokCount); ++i) {
        CHECK(lex::token_name(static_cast<lex::Tok>(i)) != nullptr);
    }
}

// --- diagnosa "kode ini dari bahasa lain" (L011/L012) -----------------------
//
// Dua aturan ini bisa salah menembak pada program yang benar, jadi yang diuji
// bukan hanya kasus positif tapi juga semua bentuk yang WAJIB diam.

namespace {
std::size_t n_peringatan(const Hasil& h) { return h.bag.jumlah_peringatan(); }
std::size_t n_galat(const Hasil& h) { return h.bag.galat().size(); }

bool ada_kode(const Hasil& h, std::string_view kode) {
    for (const support::Diagnostic& d : h.bag.peringatan()) {
        if (d.code == kode) return true;
    }
    for (const support::Diagnostic& d : h.bag.galat()) {
        if (d.code == kode) return true;
    }
    return false;
}
}  // namespace

TEST_CASE("lexer: L012 kata dari bahasa lain, dengan padanan") {
    struct Kasus {
        const char* kode;
        const char* padanan;
    };
    const Kasus kasus[] = {
        {"return 1;", "bali"},     {"function f() {}", "gawe"}, {"let x = 1;", "ana"},
        {"class F {}", "golongan"}, {"while (a) {}", "nalika"}, {"if (a) {}", "yen"},
        {"try {} tangkep (e) {}", "coba"}, {"true;", "bener"},    {"false;", "salah"},
        {"null;", "kosong"},       {"for (;;) {}", "kanggo"},   {"def f() {}", "gawe"},
        {"import x saka \"y\";", "impor"},
    };
    for (const Kasus& k : kasus) {
        const Hasil h = lex_semua(k.kode);
        CHECK_MSG(ada_kode(h, "L012"), k.kode);
        CHECK_MSG(n_peringatan(h) == 1, k.kode);
        // Pesan harus menyebut padanannya, bukan hanya kode galat.
        bool ada_padanan = false;
        for (const support::Diagnostic& d : h.bag.peringatan()) {
            if (d.pesan.find(k.padanan) != std::string::npos) ada_padanan = true;
        }
        CHECK_MSG(ada_padanan, k.kode);
    }
}

TEST_CASE("lexer: L012 adalah peringatan, bukan galat") {
    // `return 1;` TIDAK menghasilkan galat sintaks apa pun -- itu sebabnya
    // program bisa berjalan dengan hasil yang salah tanpa suara. Kalau L012 jadi
    // galat, `jawa tes` akan gagal pada program yang tadinya "hanya" salah.
    const Hasil h = lex_semua("gawe f() { return 1; }");
    CHECK(n_galat(h) == 0);
    CHECK(n_peringatan(h) == 1);
    CHECK(ada_kode(h, "L012"));
}

TEST_CASE("lexer: L011 pengenal di awal pernyataan diikuti pengenal lain") {
    // `baili` = salah ketik `bali`. Bentuk `pengenal pengenal` tidak mungkin
    // jadi Basa Jawa, jadi ini galat (bukan peringatan): bentuknya sudah salah
    // sebelum maknanya kartu.
    const Hasil h = lex_semua("gawe f() { baili 1; }");
    CHECK(ada_kode(h, "L011"));
    CHECK(n_galat(h) == 1);
    bool ada_saran = false;
    for (const support::Diagnostic& d : h.bag.galat()) {
        if (d.saran.find("bali") != std::string::npos) ada_saran = true;
    }
    CHECK(ada_saran);
}

TEST_CASE("lexer: L011 membidik juga pengenal diikuti literal & kurung kurawal") {
    for (const char* src : {"foo 1;", "foo \"teks\";", "foo { }"}) {
        const Hasil h = lex_semua(src);
        CHECK_MSG(ada_kode(h, "L011"), src);
    }
}

TEST_CASE("lexer: diagnosa kata asing diam pada Basa Jawa yang benar") {
    // Semua bentuk ini pernah memicu L011/L012 kalau aturannya terlalu longgar.
    // Ini yang menjaga agar diagnosa tidak merusak program yang jalan.
    const char* bersih[] = {
        "tulis(1);",                                        // panggilan
        "tulis(1)\ntulis(2);",                              // dua pernyataan
        "x\ny;",                                            // pengenal, baris baru
        "gawe o = { a: 1, b: 2 };",                         // objek: `a` lalu `:`
        "gawe f(a: angka): angka { bali a; }",              // anotasi tipe
        "pilih x { kasus 1: tulis(1); }",                   // label kasus
        "ana a = [1, 2, 3];",                                // daftar
        "tulis(`halo ${nama} dunia`);",                     // template
        "var x = 1;",                                       // `var` itu kata kunci
        "tulis(jenis(1));",                                 // `jenis` itu fungsi
        "jinis x;",
        "tulis(1); // return 1;",                            // komentar
        "tulis(1); /* return */",                           // komentar blok
        "gawe f() { bali (1 + 2); }",                       // `bali` + kurung
        "gawe f() { bali; }",                               // `bali` polos
        "gawe f() { bali x; }",                             // `bali` + pengenal
        "nama = 1;",                                        // penugasan
        "iki.x = 1;",
        "tulis(1) |> |> tulis(2);",
    };
    for (const char* src : bersih) {
        const Hasil h = lex_semua(src);
        CHECK_MSG(n_galat(h) == 0, src);
        CHECK_MSG(n_peringatan(h) == 0, src);
    }
}

TEST_CASE("lexer: saran L011 tidak menebak kata yang jauh") {
    // `foo` cuma 3 huruf: jarak edit ke `ana` tepat 3. Menyarankan `ana` untuk
    // `foo` lebih membingungkan daripada diam, jadi batasnya 2 (dan jarak 2
    // wajib huruf pertama sama).
    const Hasil h = lex_semua("foo 1;");
    CHECK(ada_kode(h, "L011"));
    for (const support::Diagnostic& d : h.bag.galat()) {
        CHECK(d.saran.empty());
    }
    // `gaweke` -> `gawe`: jarak 2, huruf pertama sama, jadi boleh disarankan.
    const Hasil h2 = lex_semua("gaweke f() {}");
    CHECK(ada_kode(h2, "L011"));
    bool ada = false;
    for (const support::Diagnostic& d : h2.bag.galat()) {
        if (d.saran.find("gawe") != std::string::npos) ada = true;
    }
    CHECK(ada);
}

TEST_CASE("lexer: L011 mengabaikan pengenal di tengah ekspresi") {
    // `push`/`type`/`log` sah sebagai nama variabel, dan `t.push(1)` adalah
    // penggunaan yang benar. Aturan ini hanya boleh jalan di awal pernyataan.
    const char* src[] = {
        "ana type = 1;",
        "t.push(1);",
        "t.log = 1;",
        "gawe f() { bali push(1, 2); }",
    };
    for (const char* s : src) {
        const Hasil h = lex_semua(s);
        CHECK_MSG(n_peringatan(h) == 0, s);
        CHECK_MSG(n_galat(h) == 0, s);
    }
}
