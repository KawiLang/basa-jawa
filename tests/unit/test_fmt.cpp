// Uji untuk `jawa fmt`.
//
// Yang diuji, urut dari yang paling penting:
//
//  1. **Program tidak berubah.** Format hanya boleh mengubah jarak, tidak
//     pernah isi. `format_sumber` sendiri menolak kalau jumlah token sebelum
//     dan sesudah berbeda, jadi setiap kasus di bawah sekaligus membuktikan
//     bahwa programnya utuh.
//  2. **Idempoten.** format(format(x)) == format(x). Kalau tidak, `jawa fmt
//     --tulis` akan mengubah berkas setiap kali dijalankan.
//  3. **Indentasi benar** untuk blok, literal objek, dan loop bersarang.
//  4. **Komentar tidak hilang** dan tetap di posisi yang masuk akal.
//  5. **Kasus ambigu** (generic vs perbandingan, unary vs biner) tidak dirusak
//     -- jarak dari sumber dipertahankan.
#include <string>

#include "cli/fmt.h"
#include "harness.h"

using jawa::cli::FormatOptions;
using jawa::cli::HasilFormat;

namespace {

/// Format `s`; kalau ada masalah, laporkan sebagai kegagalan dan kembalikan
/// masukan apa adanya supaya kegagalan lain tidak menumpuk.
std::string fmt(const std::string& s) {
    FormatOptions o;
    HasilFormat h;
    if (!jawa::cli::format_sumber(s, "<uji>", o, h)) {
        ::testing::Context::instance().Report(false, "format_sumber", h.pesan, __FILE__, __LINE__);
        return s;
    }
    return h.teks;
}

/// Format `s` berulang kali; gagal kalau hasil suatu saat berubah.
void cek_idempoten(const std::string& s) {
    const std::string sekali = fmt(s);
    CHECK_EQ(sekali, fmt(s));
    CHECK_EQ(sekali, fmt(sekali));
    CHECK_EQ(sekali, fmt(fmt(sekali)));
}

}  // namespace

TEST_CASE("fmt_indentasi_blok") {
    CHECK_EQ(fmt("gawe f() {\ntulis(1);\n}\n"), std::string("gawe f() {\n    tulis(1);\n}\n"));
    // Pemenggalan baris milik penulis: `gawe f(){tulis(1);}` tetap satu baris.
    // Formatter tidak menyisipkan baris baru, hanya merapikan.
    CHECK_EQ(fmt("gawe f(){tulis(1);}\n"), std::string("gawe f() {tulis(1);}\n"));
}

TEST_CASE("fmt_indentasi_nasal_salah_diperbaiki") {
    CHECK_EQ(fmt("gawe f() {\n      tulis(1);\n        tulis(2);\n}\n"),
             std::string("gawe f() {\n    tulis(1);\n    tulis(2);\n}\n"));
}

TEST_CASE("fmt_loop_bersarang") {
    const std::string masuk = "kanggo (a saka [1,2]) {\nkanggo (b saka [3,4]) {\ntulis(a,b);\n}\n}\n";
    CHECK_EQ(fmt(masuk),
             std::string("kanggo (a saka [1, 2]) {\n    kanggo (b saka [3, 4]) {\n        tulis(a, b);\n"
                         "    }\n}\n"));
}

TEST_CASE("fmt_literal_objek_bukan_blok") {
    // `{` di sini literal objek: token berikutnya bukan baris baru, jadi
    // indentasi tidak bertambah dan `}` tidak mendedent.
    CHECK_EQ(fmt("ana o = { a: 1, b: 2 };\n"), std::string("ana o = { a: 1, b: 2 };\n"));
    CHECK_EQ(fmt("ana o = {\na: 1,\nb: 2,\n};\n"),
             std::string("ana o = {\n    a: 1,\n    b: 2,\n};\n"));
}

TEST_CASE("fmt_pemenggalan_baris_penulis_dihormati") {
    // Satu statement yang dipecah tiga baris tetap begitu.
    CHECK_EQ(fmt("tulis(\n1,\n2);\n"), std::string("tulis(\n1,\n2);\n"));
}

TEST_CASE("fmt_jarak_koma_dan_titik_koma") {
    CHECK_EQ(fmt("tulis(1,2,3);\n"), std::string("tulis(1, 2, 3);\n"));
    CHECK_EQ(fmt("tulis(1 , 2 , 3) ;\n"), std::string("tulis(1, 2, 3);\n"));
}

TEST_CASE("fmt_jarak_penugasan") {
    CHECK_EQ(fmt("ana x=1;\n"), std::string("ana x = 1;\n"));
    CHECK_EQ(fmt("ana   x   =   1;\n"), std::string("ana x = 1;\n"));
    CHECK_EQ(fmt("x+=1;\n"), std::string("x += 1;\n"));
}

TEST_CASE("fmt_operator_unary_tidak_dipisah") {
    CHECK_EQ(fmt("tulis(!bener);\n"), std::string("tulis(!bener);\n"));
    CHECK_EQ(fmt("ana y=-1;\n"), std::string("ana y = -1;\n"));
    // `+` ambigu (biner atau unary) jadi jarak ikut sumber; `<` juga ambigu
    // (perbandingan atau generic) dan sama sekali tidak disentuh.
    CHECK_EQ(fmt("ana i=0;i<3;i=i+1\n"), std::string("ana i = 0; i<3; i = i+1\n"));
}

TEST_CASE("fmt_panah") {
    CHECK_EQ(fmt("ana f=(a,b)=>a+b;\n"), std::string("ana f = (a, b) => a+b;\n"));
    CHECK_EQ(fmt("ana f=(a,b)=>a + b;\n"), std::string("ana f = (a, b) => a + b;\n"));
    cek_idempoten("ana f=(a,b)=>a+b;\n");
}

TEST_CASE("fmt_operator_ambigu_dipertahankan") {
    // `<`, `>`, `+`, `-`, `*`, dan `&` bisa berarti operator atau bagian dari
    // bentuk lain (anotasi tipe, unary). Dari token saja tidak bisa dibedakan
    // secara andal, jadi jarak dari sumber dipertahankan. Menormalkan paksa
    // di sini berisiko mengubah program, bukan cuma merapikannya.
    CHECK_EQ(fmt("ana a=1;\nana b=2;\ntulis(a<b);\n"),
             std::string("ana a = 1;\nana b = 2;\ntulis(a<b);\n"));
    CHECK_EQ(fmt("ana a=1;\nana b=2;\ntulis(a < b);\n"),
             std::string("ana a = 1;\nana b = 2;\ntulis(a < b);\n"));
    // Anotasi tipe array memakai `[]`, bukan `<>`: bentuk ini tidak boleh
    // berubah jadi perbandingan.
    cek_idempoten("ana x: angka[] = [1];\n");
    cek_idempoten("tulis(a<b);\n");
}

TEST_CASE("fmt_komentar_tetap_ada") {
    const std::string keluar = fmt("// heads up\nana x = 1; // sawise\n");
    CHECK(keluar.find("// heads up") != std::string::npos);
    CHECK(keluar.find("// sawise") != std::string::npos);
}

TEST_CASE("fmt_komentar_blok_bersama_kode") {
    const std::string keluar = fmt("ana /* tengah */ x = 1;\n");
    CHECK(keluar.find("/* tengah */") != std::string::npos);
    CHECK(keluar.find("x = 1") != std::string::npos);
}

TEST_CASE("fmt_template_literal_opaque") {
    // Isi template tidak boleh disentuh: `${...}`, string, dan backtick di
    // dalamnya harus utuh.
    CHECK_EQ(fmt("tulis(`Halo, ${nama}!`);\n"), std::string("tulis(`Halo, ${nama}!`);\n"));
    CHECK_EQ(fmt("tulis(`a${`b${c}d`}e`);\n"), std::string("tulis(`a${`b${c}d`}e`);\n"));
    cek_idempoten("tulis(`Halo, ${nama}!`);\n");
    cek_idempoten("tulis(`a${`b${c}d`}e`);\n");
}

TEST_CASE("fmt_string_dan_regex_opaque") {
    CHECK_EQ(fmt("ana s=\"  a  ,  b  \";\n"), std::string("ana s = \"  a  ,  b  \";\n"));
    CHECK_EQ(fmt("ana r=/a , b/;\n"), std::string("ana r = /a , b/;\n"));
}

TEST_CASE("fmt_baris_kosong_dibatasi_satu") {
    CHECK_EQ(fmt("ana a = 1;\n\n\n\nana b = 2;\n"), std::string("ana a = 1;\n\nana b = 2;\n"));
}

TEST_CASE("fmt_tanpa_baris_baru_di_akhir") {
    CHECK_EQ(fmt("ana a = 1;\n\n\n"), std::string("ana a = 1;\n"));
}

TEST_CASE("fmt_idempoten_umum") {
    cek_idempoten("gawe f(a, b) {\n  yen (a > b) {\n    bali a;\n  }\n  bali b;\n}\n");
    cek_idempoten("kanggo (i saka [1, 2, 3]) {\n  tulis(i);\n}\n");
    cek_idempoten("pilih (x) {\n  kasus 1: tulis(\"satu\");\n  baku: tulis(\"liyane\");\n}\n");
    cek_idempoten("ana o = { a: 1, b: { c: 2 } };\n");
    cek_idempoten("coba { uncal 1; } tangkep (e) { tulis(e); }\n");
}

TEST_CASE("fmt_galat_sintaks_ditolak") {
    FormatOptions o;
    HasilFormat h;
    // `ana x = ;` leksikalnya sah tapi programnya salah: pemformat harus
    // menolak, bukan menebak. Ini yang membuktikan nilai `cek_sintaks` --
    // hanya mengecek leksikal akan lolos.
    CHECK(!jawa::cli::format_sumber("ana x = ;\n", "<uji>", o, h));
    // Token yang benar-benar rusak juga ditolak.
    HasilFormat h2;
    CHECK(!jawa::cli::format_sumber("ana x = \"belum ditutup\n", "<uji>", o, h2));
    CHECK(h.galat);
    CHECK(!h.pesan.empty());
}
