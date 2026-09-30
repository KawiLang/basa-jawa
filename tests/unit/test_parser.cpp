// Test parser: setiap konstruk harus ter-parse tanpa galat, dan AST harus
// punya `kind` yang benar (regresi untuk bug `kind` tidak ter-set).
#include "harness.h"
#include "lex/lexer.h"
#include "parse/ast.h"
#include "parse/ast_print.h"
#include "parse/parser.h"
#include "support/arena.h"
#include "support/diagnostics.h"

#include <string>
#include <vector>

using namespace jawa;

namespace {

struct Hasil {
    support::Arena arena;
    lex::TokenList token;
    support::DiagnosticBag bag{50};
    ast::NodePtr program = nullptr;
};

Hasil parse_kode(std::string_view src) {
    Hasil h;
    lex::Lexer lx(src, "uji.jw", ".", lex::LexOptions{});
    lx.lex_semua(h.token);
    h.bag.gabung(lx.bag());
    if (h.bag.penuh()) return h;
    parse::Parser p(h.token, h.arena, h.bag, "uji.jw");
    h.program = p.parse_program();
    return h;
}

std::size_t jumlah_statement(const Hasil& h) {
    if (h.program == nullptr) return 0;
    return static_cast<ast::Program*>(h.program)->body.size();
}



}  // namespace

TEST_CASE("parser: halo") {
    const Hasil h = parse_kode("tetep jeneng = \"Budi\";\ntulis(`Halo, ${jeneng}!`);");
    CHECK(!h.bag.ada_galat());
    CHECK(jumlah_statement(h) == 2);
}

TEST_CASE("parser: semua statement dasar") {
    const Hasil h = parse_kode(R"(
ana a = 1;
tetep b = 2;
tulis(a, b);
yen (a > 0) { tulis("pos"); } liyane { tulis("neg"); }
nalika (a < 3) { a++; }
lakoni { a--; } nalika (a > 0);
kanggo (ana i = 0; i < 3; i++) { tulis(i); }
kanggo (ana x saka [1,2,3]) { tulis(x); }
kanggo (ana k ing {a: 1}) { tulis(k); }
pilih (a) { kasus 1: mandheg; baku: mandheg; }
coba { uncal anyar Kleru("x"); } tangkep (e) { tulis(e); }
pungkasan { tulis("rampung"); }
luar: kanggo (ana i = 0; i < 3; i++) { yen (i == 1) mandheg luar; }
)");
    CHECK(!h.bag.ada_galat());
    CHECK(jumlah_statement(h) == 12);  // `coba`/`tangkep`/`pungkasan` = satu statement
}

TEST_CASE("parser: fungsi & kelas") {
    const Hasil h = parse_kode(R"(
gawe tambah(a, b = 0, ...sisa) { bali a + b; }
tetep kali = (x) => x * 2;
tetep sapa = ({jeneng, umur = 0}) => `${jeneng} (${umur})`;
gawe* gen(n) { kanggo (ana i = 1; i <= n; i++) metokake i; }
mengko gawe ambil(x) { enteni x; bali x * 2; }
golongan Kewan {
  #jeneng;
  swara = "...";
  statis cacah = 0;
  statis { Kewan.cacah = 0; }
  wiwit(jeneng) { iki.#jeneng = jeneng; Kewan.cacah++; }
  nampa jeneng() { bali iki.#jeneng; }
  nyetel jeneng(v) { iki.#jeneng = v; }
  #rahasia() { bali 42; }
  statis gawe_anyar(j) { bali anyar Kewan(j); }
}
golongan Kucing turunan Kewan {
  swara = "meong";
  wiwit(jeneng) { induk(jeneng); }
  ngomong() { induk.swara(); }
}
)");
    CHECK(!h.bag.ada_galat());
}

TEST_CASE("parser: objek & dhaptar & destructuring") {
    const Hasil h = parse_kode(R"(
ana [a, b = 5, ...sisa] = [1, 2, 3, 4];
ana {jeneng: j, umur = 17, ...liyane} = wong;
[a, b] = [b, a];
ana o = {a: 1, "b-c": 2, [kunci]: 3, cendhak, nampa x() { bali 1; }, nyetel y(v) { iki.v = v; }, m() {}};
ana d = [1, 2, ...liyane];
tulis(o?.a, o?.[0], o?.());
busak o.a;
)");
    CHECK(!h.bag.ada_galat());
}

TEST_CASE("parser: operator & presedensi") {
    const Hasil h = parse_kode(R"(
tulis(1 + 2 * 3 ** 4 % 5);
tulis(a ?? b, a || b && c, a | b ^ c & d);
tulis(a < b <= c, a == b, a != b);
tulis(a << 2 >> 3 >>> 4, ~a, !a, -a, +a);
tulis(5 |> f |> g);
tulis(a ? b : c);
tulis(a instansi_saka K, "k" ing o, jinis a);
x += 1; x -= 1; x *= 2; x /= 2; x %= 2; x **= 2;
x <<= 1; x >>= 1; x >>>= 1; x &= 1; x |= 1; x ^= 1;
x ??= 1; x ||= 1; x &&= 1;
++x; x++; --x; x--;
)");
    CHECK(!h.bag.ada_galat());
}

TEST_CASE("parser: cocog & pola") {
    const Hasil h = parse_kode(R"(
tetep hasil = cocog (nilai) {
  kasus 0                          => "nol",
  kasus 1 | 2 | 3                  => "cilik",
  kasus [a, b]                     => `pasangan ${a},${b}`,
  kasus [kepala, ...buntut]        => `dhaptar ${kepala}`,
  kasus {jeneng, umur} yen umur >= 17 => `${jeneng} dewasa`,
  kasus {jinis: "lingkaran", r}    => 3.14 * r ** 2,
  kasus teks_x: Teks               => `teks ${teks_x}`,
  kasus _                          => "liyane"
};
)");
    CHECK(!h.bag.ada_galat());
}

TEST_CASE("parser: anotasi tipe") {
    const Hasil h = parse_kode(R"(
ana umur: angka = 20;
ana nama: teks | kosong = kosong;
gawe tambah(a: angka, b: angka = 0): angka { bali a + b; }
gawe petakan<T, U>(xs: dhaptar<T>, f: (T) => U): dhaptar<U> { bali xs; }
golongan Titik { x: angka; y: angka; wiwit(x: angka, y: angka) { iki.x = x; } }
)");
    CHECK(!h.bag.ada_galat());
}

// Alias tipe (`jenis Id = angka | teks`) BELUM ada di parser: node
// `ast::AliasTipe` sudah ada di src/parse/ast.h tapi tidak pernah dibangun.
// Test lama punya baris `jenis Id = ...` dan tetap lulus karena parser
// memperlakukannya sebagai dua `EkspresiStmt` terpisah (ASI menyisipkan
// titik koma) -- artinya tidak ada yang benar-benar diuji. Test ini mengunci
// keadaan sekarang: `jenis` itu function global, bukan pengenal tipe.
TEST_CASE("parser: `jenis` itu fungsi global, bukan alias tipe") {
    const Hasil h = parse_kode(R"(
tulis(jenis(1));
)");
    CHECK(!h.bag.ada_galat());
    CHECK(h.bag.jumlah_peringatan() == 0);
}

TEST_CASE("lexer: `jenis Id = ...` ditolak (bukan sintaks Basa Jawa)") {
    const Hasil h = parse_kode("jenis Id = angka | teks;");
    CHECK(h.bag.ada_galat());
    CHECK(h.bag.jumlah_peringatan() == 0);
}

TEST_CASE("parser: modul") {
    const Hasil h = parse_kode(R"(
impor Titik, {kuadrat, PI2 minangka DUA_PI} saka "./matematika_ku.jw";
impor * minangka M saka "std:matematika";
tetep m = enteni impor("./lazy.jw");
ekspor {kuadrat minangka kuadrat_ku} saka "./x.jw";
ekspor gawe f() { bali 1; }
ekspor baku 42;
)");
    CHECK(!h.bag.ada_galat());
}

TEST_CASE("parser: semua literal") {
    const Hasil h = parse_kode(R"(
tulis(42, 1_000_000, 0xFF, 0b1010, 0o17, 3.14, 2.5e-3, .5);
tulis(123n, 0xFFn);
tulis("halo", 'halo');
tulis(`a ${1 + 2} b ${`nested ${3}`} c`);
tulis(/abc+/gi);
tulis(bener, salah, kosong, mboh, DuduAngka, Tak_Wates);
tulis(-0.0, Infinity);
)");
    CHECK(!h.bag.ada_galat());
}

TEST_CASE("parser: galat sintaks terdeteksi") {
    CHECK(parse_kode("gawe (").bag.ada_galat());
    CHECK(parse_kode("yen (x { }").bag.ada_galat());
    CHECK(parse_kode("}").bag.ada_galat());
    CHECK(parse_kode("golongan { }").bag.ada_galat());
    CHECK(parse_kode("1 +").bag.ada_galat());
    CHECK(parse_kode("[1, 2").bag.ada_galat());
}

TEST_CASE("parser: error recovery terdeteksi") {
    CHECK(parse_kode("tulis('halo);").bag.ada_galat());
    CHECK(parse_kode("`halo").bag.ada_galat());
    CHECK(parse_kode("/* tak ditutup").bag.ada_galat());
}

TEST_CASE("parser: kind node selalu ter-set") {
    // Regresi: `buat<T>()` harus mengisi `kind`. Jika tidak, printer/compiler
    // akan salah menebak jenis node.
    const Hasil h = parse_kode("ana x = 1; tulis(x + 1);");
    CHECK(!h.bag.ada_galat());
    const ast::Program* prog = static_cast<ast::Program*>(h.program);
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->body.empty());
    CHECK(prog->body[0]->kind == ast::NK::DeklarasiVar);
    CHECK(prog->body[1]->kind == ast::NK::EkspresiStmt);
    const auto* dvl = static_cast<ast::DeklarasiVarStmt*>(prog->body[0]);
    CHECK(dvl->nilai != nullptr);
    CHECK(dvl->nilai->kind == ast::NK::Nomor);
    const auto* eks = static_cast<ast::EkspresiStmt*>(prog->body[1]);
    CHECK(eks->ekspresi != nullptr);
    CHECK(eks->ekspresi->kind == ast::NK::Panggilan);
    const auto* pgl = static_cast<ast::Panggilan*>(eks->ekspresi);
    CHECK(pgl->callee != nullptr);
    CHECK(pgl->callee->kind == ast::NK::RefIdent);
    const auto* bin = static_cast<ast::BinerExpr*>(pgl->argumen[0]);
    CHECK(bin->op == ast::BinOp::Tambah);
}

TEST_CASE("parser: cetak_ast tidak crash") {
    const Hasil h = parse_kode("tetep a = 1; gawe f(x) { bali x * 2; } f(a);");
    CHECK(!h.bag.ada_galat());
    const std::string s = parse::cetak_ast(static_cast<ast::Program*>(h.program));
    CHECK(s.find("Program") != std::string::npos);
    CHECK(s.find("DeklarasiVar") != std::string::npos);
}
