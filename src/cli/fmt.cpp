// `jawa fmt` — pemformat kode Basa Jawa.
//
// Bekerja di tingkat TOKEN, bukan AST. Alasannya: memformat dari AST akan
// membuang komentar, dan mengganti nama variabel yang sengaja direname adalah
// perubahan yang jauh lebih besar dari sekadar merapikan. Dengan token,
// struktur program tetap utuh dan setiap token bisa ditelusuri kembali ke
// sumbernya lewat `Token::range`.
//
// Yang DINORMALISASI:
//   - indentasi: empat spasi per tingkat kurung kurawal,
//   - jarak antar token yang pasti: spasi di sekitar `=>`, setelah koma dan
//     titik koma; tanpa spasi sebelum `)`/`]`/`,`/`.` dan setelah `(`/`[`/`.`,
//   - baris kosong: dibatasi satu, dan tidak ada di akhir berkas.
//
// Yang SENGAJA TIDAK diubah:
//   - pemenggalan baris. Penulis yang memecah satu statement menjadi lima baris
//     tetap seperti itu. Inilah yang membuat formatter ini bisa dipercaya: ia
//     tidak pernah mengubah bentuk program, hanya jarak.
//   - jarak di sekitar operator yang ambigu terhadap tipe. `<` bisa operator
//     perbandingan (`a < b`) atau generic (`dhaptar<angka>`); `+` bisa biner
//     atau unary; `:` bisa key literal objek atau ternary. Untuk kasus seperti
//     itu jarak dari sumber dipertahankan, karena dari token saja tidak bisa
//     dibedakan secara andal.
//
// Komentar tidak hilang: lexer membuangnya, jadi celah antar token di sumber
// dibaca ulang dan komentar disisipkan pada posisi yang sama relatif terhadap
// token. Komentar `//` selalu naik ke barisnya sendiri.
//
// Idempoten: memformat hasil format lagi tidak mengubah apa pun. Diuji di
// `tests/unit/test_fmt.cpp`.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cli/fmt.h"
#include "lex/lexer.h"
#include "parse/parser.h"
#include "support/diagnostics.h"
#include "support/source_map.h"

namespace jawa::cli {

namespace {

using lex::Tok;
using lex::Token;

/// Alias lokal supaya badan fungsi di bawah tidak repetitif.
using Hasil = HasilFormat;

/// Token yang butuh spasi agar tidak menempel dengan token atom tetangganya.
/// Kata kunci termasuk di sini: `ana x` tidak boleh menjadi `anax`.
bool atom(Tok t) noexcept {
    switch (t) {
        case Tok::Ident:
        case Tok::PrivateName:
        case Tok::Number:
        case Tok::BigInt:
        case Tok::Text:
        case Tok::TemplateText:
        case Tok::Regex:
            return true;
        default:
            return lex::is_reserved_keyword(t) || lex::is_contextual_keyword(t);
    }
}

/// Operator yang PASTI biner: harus dikelilingi spasi. `+`/`-`/`<`/`>` tidak
/// ada di sini karena bisa jadi unary atau generic -- jarak untuk operator itu
/// diambil dari sumber (lihat `susun`).
bool operator_biner_pasti(Tok t) noexcept {
    switch (t) {
        case Tok::Eq:
        case Tok::EqEq:
        case Tok::EqEqEq:
        case Tok::BangEq:
        case Tok::BangEqEq:
        case Tok::LtEq:
        case Tok::GtEq:
        case Tok::AmpAmp:
        case Tok::PipePipe:
        case Tok::QuestionQuestion:
        case Tok::StarStar:
        case Tok::Slash:
        case Tok::Percent:
        case Tok::PlusEq:
        case Tok::MinusEq:
        case Tok::StarEq:
        case Tok::StarStarEq:
        case Tok::SlashEq:
        case Tok::PercentEq:
        case Tok::AmpEq:
        case Tok::PipeEq:
        case Tok::CaretEq:
        case Tok::ShlEq:
        case Tok::ShrEq:
        case Tok::UShrEq:
        case Tok::AmpAmpEq:
        case Tok::PipePipeEq:
        case Tok::QuestionQuestionEq:
        case Tok::Arrow:
        case Tok::PipeGreater:
        case Tok::TildeGreater:
            return true;
        default:
            return false;
    }
}

/// Token yang menempel pada token sebelumnya: tidak boleh ada spasi di kirinya.
bool tanpa_spasi(Tok sebelum, Tok t) noexcept {
    switch (t) {
        case Tok::RParen:
        case Tok::RBracket:
        case Tok::Comma:
        case Tok::Semi:
        case Tok::Dot:
        case Tok::QuestionDot:
            return true;
        default:
            break;
    }
    switch (sebelum) {
        case Tok::LParen:
        case Tok::LBracket:
        case Tok::Dot:
        case Tok::QuestionDot:
            return true;
        case Tok::Bang:      // `!bener`
        case Tok::Tilde:     // `~n`
        case Tok::PlusPlus:  // `i++`
        case Tok::MinusMinus:
            return true;
        default:
            return false;
    }
}

/// Apakah `{` setelah token `sebelum` membuka blok (bukan literal objek)?
/// `f() {` dan `wkw {` adalah blok; `= {`, `( {`, `, {`, `: {` adalah literal
/// objek, yang tidak boleh diberi spasi sebelum `{`.
bool brace_blok(Tok sebelum) noexcept {
    switch (sebelum) {
        case Tok::Eq:
        case Tok::Comma:
        case Tok::Colon:
        case Tok::LParen:
        case Tok::LBracket:
        case Tok::Arrow:
            return false;
        default:
            return true;
    }
}

/// Aturan mutlak: harus ada spasi di antara `sebelum` dan `t`.
bool dengan_spasi(Tok sebelum, Tok t) noexcept {
    if (tanpa_spasi(sebelum, t)) return false;
    // Spasi SESUDAH operator biner, dan SEBELUM-nya juga.
    if (operator_biner_pasti(sebelum) || operator_biner_pasti(t)) return true;
    if (t == Tok::LBrace) return brace_blok(sebelum);
    if (atom(t) && atom(sebelum)) return true;
    if (sebelum == Tok::Comma || sebelum == Tok::Semi) return t != Tok::RBrace;
    if (t == Tok::Ellipsis || t == Tok::At) return true;
    return false;
}

/// Potongan sumber untuk satu token. Dipakai supaya string, regex, dan
/// template literal ditulis ulang persis seperti aslinya -- `Token::teks` tidak
/// selalu berisi bentuk mentahnya.
std::string_view potongan(std::string_view sumber, const Token& t) {
    const std::size_t a = t.range.mulai.offset;
    const std::size_t b = t.range.selesai.offset;
    if (b <= a || b > sumber.size() || a > b) return t.teks;
    return sumber.substr(a, b - a);
}

/// Apakah celah sumber `[dari, sampai)` memuat whitespace?
bool celah_punya_spasi(std::string_view sumber, std::size_t dari, std::size_t sampai) {
    if (dari >= sampai || sampai > sumber.size()) return false;
    for (std::size_t i = dari; i < sampai; ++i) {
        const char c = sumber[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') return true;
    }
    return false;
}

/// Satu komentar yang ditemukan di celah sumber.
struct Komentar {
    std::string_view teks;
    bool baris = false;  ///< `//`
};

/// Hasil pemindaian satu celah sumber di antara dua token.
struct Celah {
    std::vector<Komentar> komentar;
    /// Jumlah baris baru di celah itu, TIDAK termasuk yang ada di dalam
    /// komentar. Penting: untuk
    ///
    ///     a.next();
    ///     // catatan
    ///     a.next();
    ///
    /// celah sebelum `a` kedua memuat dua baris baru, tapi hanya SATU yang
    /// memisahkan baris kode. Kalau baris baru di dalam komentar ikut dihitung,
    /// formatter menyisipkan baris kosong palsu setiap kali dipanggil dua kali
    /// -- jadi tidak idempoten.
    std::size_t baris_baru = 0;
};

/// Baca ulang celah `[dari, sampai)`: kumpulkan komentarnya dan hitung baris
/// baru yang benar-benar memisahkan baris kode.
///
/// Celah ini secara definisi tidak memuat string, regex, atau template literal
/// (lexer mengonsumsinya sebagai token), jadi `//` di sini selalu komentar.
/// Kalau ternyata ada isi lain, pemindaian berhenti agar teks mentah tidak
/// bocor ke keluaran.
Celah analisis_celah(std::string_view sumber, std::size_t dari, std::size_t sampai) {
    Celah hasil;
    std::size_t i = dari;
    while (i < sampai) {
        const char c = sumber[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        if (c == '\n') {
            if (++hasil.baris_baru == 2) break;  // cukup: satu baris kosong
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < sampai && sumber[i + 1] == '/') {
            std::size_t j = i + 2;
            while (j < sampai && sumber[j] != '\n') ++j;
            while (j > i && (sumber[j - 1] == '\r' || sumber[j - 1] == ' ' || sumber[j - 1] == '\t')) --j;
            hasil.komentar.push_back({sumber.substr(i, j - i), true});
            // Baris baru penutup `//` ikut dimiliki komentar, bukan
            // pemisah baris: `x; // c\ny;` berisi dua '\n' tapi hanya satu
            // yang memisahkan baris kode. Kalau tidak, formatter tidak
            // idempoten -- setiap pemanggilan menambah satu baris kosong.
            i = (j < sampai && sumber[j] == '\n') ? j + 1 : j;
            continue;
        }
        if (c == '/' && i + 1 < sampai && sumber[i + 1] == '*') {
            std::size_t j = i + 2;
            while (j + 1 < sampai && !(sumber[j] == '*' && sumber[j + 1] == '/')) ++j;
            const std::size_t akhir = std::min(j + 2, sampai);
            hasil.komentar.push_back({sumber.substr(i, akhir - i), false});
            i = akhir;
            continue;
        }
        // Sesuatu yang bukan whitespace dan bukan komentar. Teorinya tidak
        // mungkin ada di dalam celah token; kalau terjadi, berhenti agar teks
        // mentah tidak bocor ke keluaran.
        break;
    }
    return hasil;
}

/// Akhir satu template literal yang mulai di `mulai` (isinya backtick pembuka).
///
/// Ini harus dipindai langsung dari sumber, bukan dari `Token::range`: lexer
/// memberi offset 0 ke setiap token `TemplateText`, jadi rentangnya tidak bisa
/// dipakai. `mulai` selalu menunjuk tepat ke backtick pembuka, karena
/// `template_awal` terbit saat lexer sudah melewati backtick itu dan token
/// sebelumnya berakhir tepat sebelumnya.
///
/// Memindai juga berarti formatter aman terhadap template yang berisi string
/// dengan backtick, regex, `${}` bersarang, dan template di dalam `${}`.
std::size_t akhir_template(std::string_view s, std::size_t mulai, std::size_t& pembuka) {
    // `mulai` adalah ujung token sebelumnya, jadi whitespace di antaranya harus
    // dilewati dulu: `=>` diikuti spasi baru backtick, jadi titik mulainya bukan
    // ujung token sebelumnya.
    std::size_t i = mulai;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    pembuka = i;
    if (i >= s.size() || s[i] != '`') {
        // Bukan template setelah semua. Kembalikan rentang kosong supaya
        // formatter tidak menelan karakter yang tidak_dimilikinya.
        return i;
    }
    ++i;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\\') {
            i += 2;  // escape: dua byte dilewati apa pun isinya
            continue;
        }
        if (c == '`') return i + 1;
        if (c == '$' && i + 1 < s.size() && s[i + 1] == '{') {
            i += 2;
            std::size_t kurung = 1;
            while (i < s.size() && kurung > 0) {
                const char d = s[i];
                if (d == '\\') {
                    i += 2;
                    continue;
                }
                if (d == '"' || d == '\'' || d == '`') {
                    const char penutup = d;
                    ++i;
                    while (i < s.size() && s[i] != penutup) {
                        i += (s[i] == '\\') ? 2 : 1;
                    }
                    ++i;  // penutup (atau melewati akhir sumber)
                    continue;
                }
                if (d == '{') ++kurung;
                if (d == '}') --kurung;
                ++i;
            }
            continue;
        }
        ++i;
    }
    // Template tidak ketutup: ambil sisa sumber sebagai rentangnya.
    return s.size();
}

/// Bangun teks hasil format dari daftar token.
std::string susun(std::string_view sumber, const std::vector<Token>& token, std::size_t lebar_indent) {
    std::string keluar;
    std::string baris;
    std::size_t indentasi = 0;
    bool baris_terbuka = false;  ///< baris sekarang sudah dimulai (boleh kosong)
    Tok sebelumnya = Tok::Eof;

    // Celah tiap token dianalisis sekali di depan: `analisis_celah` menghitung
    // baris baru DI LUAR komentar sekaligus mencari komentar, jadi keduanya
    // selalu sepakat.
    std::vector<Celah> celah;
    celah.reserve(token.size());
    {
        std::size_t k = 0;
        for (const Token& t : token) {
            const std::size_t m = std::min<std::size_t>(t.range.mulai.offset, sumber.size());
            celah.push_back(analisis_celah(sumber, k, m));
            k = std::max(k, std::min<std::size_t>(t.range.selesai.offset, sumber.size()));
        }
    }

    const auto tulis_indent = [&]() { baris.append(indentasi * lebar_indent, ' '); };

    /// Tutup baris yang sedang dibangun. Baris yang kosong tetap menghasilkan
    /// satu baris kosong supaya pemformatan file yang memang ganjil tidak
    /// ikut dirapikan tanpa diminta.
    const auto tutup_baris = [&]() {
        while (!baris.empty() && baris.back() == ' ') baris.pop_back();
        keluar += baris;
        keluar += '\n';
        baris.clear();
        baris_terbuka = false;
    };

    /// Tutup baris (kalau ada) dan sisakan baris kosong sebanyak yang diminta
    /// penulis, dibatasi satu.
    const auto pindah_baris = [&](std::size_t jumlah) {
        if (baris_terbuka) tutup_baris();
        for (std::size_t k = 1; k < jumlah; ++k) keluar += '\n';
    };

    /// Tulis satu komentar. `//` selalu naik ke barisnya sendiri; `/* */`
    /// boleh menempel di baris yang sama seperti aslinya.
    const auto tulis_komentar = [&](const Komentar& k) {
        if (k.baris) {
            if (baris_terbuka) tutup_baris();
            keluar.append(indentasi * lebar_indent, ' ');
            keluar += k.teks;
            keluar += '\n';
        } else {
            if (!baris_terbuka) {
                tulis_indent();
                baris_terbuka = true;
            } else {
                baris += ' ';
            }
            baris += k.teks;
        }
    };

    std::size_t kursor = 0;  ///< offset tepat sesudah bagian yang sudah ditulis

    // Template literal diperlakukan sebagai satu token buram: rentangnya dibaca
    // dari sumber (lihat `akhir_template`) dan seluruh token `TemplateText` di
    // dalamnya dilewati. Tanpa ini isi template ikut di-normalisasi dan `${...}`
    // di dalamnya hancur.
    //
    // Kedalamannya harus dihitung: template di dalam `${}` template lain
    // (`` `a${`b`}c` ``) juga menerbitkan `template_awal` dan `template_akhir`
    // sendiri, jadi berhenti di `template_akhir` pertama akan membuat token
    // template di dalamnya ditulis ulang sebagai kode biasa.
    std::size_t dalam_template = 0;
    std::size_t akhir_template_berikut = 0;

    for (std::size_t i = 0; i < token.size(); ++i) {
        if (dalam_template > 0) {
            const Token& tl = token[i];
            if (tl.jenis == Tok::TemplateText) {
                if (tl.template_awal) {
                    ++dalam_template;
                } else if (tl.template_akhir) {
                    --dalam_template;
                    if (dalam_template == 0) kursor = std::max(kursor, akhir_template_berikut);
                }
            }
            continue;
        }

        Token t = token[i];
        if (t.jenis == Tok::Eof) break;

        if (t.jenis == Tok::TemplateText && t.template_awal) {
            std::size_t pembuka = kursor;
            const std::size_t tutup = akhir_template(sumber, kursor, pembuka);
            if (tutup > pembuka) {
                t.range.mulai.offset = static_cast<std::uint32_t>(pembuka);
                t.range.selesai.offset = static_cast<std::uint32_t>(tutup);
                t.teks = sumber.substr(pembuka, tutup - pembuka);
            }
            dalam_template = 1;
            akhir_template_berikut = tutup;
        }

        const std::size_t mulai = std::min<std::size_t>(t.range.mulai.offset, sumber.size());
        const std::size_t selesai = std::min<std::size_t>(t.range.selesai.offset, sumber.size());
        const std::size_t jeda_baris = celah[i].baris_baru;

        // --- 1. komentar di celah sebelum token ini ---
        // Celah dianalisis ulang di sini, bukan memakai `celah[i].komentar`:
        // untuk token template, `mulai` digeser ke backtick pembuka sehingga
        // ruang antara token sebelumnya dan backtick ikut terperiksa.
        if (mulai > kursor) {
            for (const Komentar& k : analisis_celah(sumber, kursor, mulai).komentar) {
                if (k.baris && baris_terbuka) tutup_baris();
                tulis_komentar(k);
            }
        }

        // --- 2. pemenggalan baris dari penulis ---
        if (jeda_baris > 0) pindah_baris(jeda_baris);

        // --- 3. kurung kurawal: atur indentasi ---
        // `}` di awal baris maju satu tingkat ke kiri. `{` menambah satu tingkat
        // kalau token BERIKUTNYA di baris baru -- itu satu-satunya cara tahu
        // apakah `{` membuka blok yang isi barisnya, atau literal objek satu
        // baris (`f({ a: 1 })`).
        if (t.jenis == Tok::RBrace && !baris_terbuka && indentasi > 0) --indentasi;
        if (t.jenis == Tok::LBrace && i + 1 < token.size() && celah[i + 1].baris_baru > 0) ++indentasi;

        // --- 4. pemisah antar token ---
        if (baris_terbuka && !baris.empty()) {
            bool spasi = false;
            if (!tanpa_spasi(sebelumnya, t.jenis)) {
                if (dengan_spasi(sebelumnya, t.jenis)) {
                    spasi = true;
                } else {
                    // Kasus ambigu terhadap tipe: ikut jarak di sumber.
                    spasi = celah_punya_spasi(sumber, kursor, mulai);
                }
            }
            if (spasi) baris += ' ';
        } else {
            if (!baris_terbuka) {
                if (jeda_baris > 0 && !baris.empty()) tutup_baris();
                tulis_indent();
                baris_terbuka = true;
            }
        }

        baris += potongan(sumber, t);
        baris_terbuka = true;
        sebelumnya = t.jenis;
        kursor = std::max(kursor, selesai);
    }

    // Komentar setelah token terakhir (biasanya di baris paling akhir) masih
    // harus ikut. Tanpa ini `x = 1; // catatan` kehilangan catatannya.
    for (const Komentar& k : analisis_celah(sumber, kursor, sumber.size()).komentar) {
        if (baris_terbuka) tutup_baris();
        tulis_komentar(k);
    }
    if (baris_terbuka) tutup_baris();

    while (keluar.size() >= 2 && keluar.compare(keluar.size() - 2, 2, "\n\n") == 0) {
        keluar.erase(keluar.size() - 2, 2);
    }
    return keluar;
}

/// Jumlah token bukan-`Eof`; untuk memeriksa format tidak mengubah program.
std::size_t jumlah_token(const std::vector<Token>& daftar) {
    std::size_t n = 0;
    for (const Token& t : daftar) {
        if (t.jenis != Tok::Eof) ++n;
    }
    return n;
}

}  // namespace

bool format_sumber(std::string_view sumber, std::string_view nama_berkas, const FormatOptions& opsi,
                   Hasil& hasil) {
    lex::LexOptions lo;
    lex::TokenList daftar;
    lex::Lexer lx(sumber, nama_berkas, ".", lo);
    lx.lex_semua(daftar);
    if (lx.bag().ada_galat()) {
        const auto& d = lx.bag().galat().front();
        hasil.galat = true;
        hasil.baris = d.pos.baris;
        hasil.pesan = std::string(d.code) + ": " + std::string(d.pesan);
        return false;
    }

    hasil.teks = susun(sumber, daftar.token, opsi.lebar_indent);
    hasil.berubah = hasil.teks != sumber;

    if (!opsi.cek_sintaks || hasil.teks.empty()) return true;

    // Parse ulang hasil format. Ini yang menangkap bug format yang
    // menghasilkan kode tidak valid -- dan bug yang lebih halus: hilangnya
    // token (string yang ikut di-normalisasi, komentar yang menelan baris).
    //
    // Cek leksikal saja tidak cukup: `ana x = ;` adalah token stream
    // yang sah tetapi program yang salah. Kalau pemformat menggeser satu
    // token, yang perlu terdeteksi adalah "program jadi tidak bisa di-parse",
    // bukan "token jadi tidak bisa di-lex".
    lex::TokenList ulang;
    lex::Lexer lx2(hasil.teks, nama_berkas, ".", lo);
    lx2.lex_semua(ulang);
    if (lx2.bag().ada_galat()) {
        const auto& d = lx2.bag().galat().front();
        hasil.galat = true;
        hasil.baris = d.pos.baris;
        hasil.pesan = "hasil format tidak bisa di-lex (" + std::string(d.code) + ": " +
                            std::string(d.pesan) + ") -- bug di `jawa fmt`.";
        return false;
    }
    support::Arena arena;
    support::DiagnosticBag bag{20};
    parse::Parser p(ulang, arena, bag, nama_berkas);
    (void)p.parse_program();
    if (bag.ada_galat()) {
        const auto& d = bag.galat().front();
        hasil.galat = true;
        hasil.baris = d.pos.baris;
        hasil.pesan = "hasil format tidak bisa di-parse (" + std::string(d.code) + ": " +
                      std::string(d.pesan) + ") -- bug di `jawa fmt`.";
        return false;
    }

    const std::size_t n_awal = jumlah_token(daftar.token);
    const std::size_t n_akhir = jumlah_token(ulang.token);
    if (n_awal != n_akhir) {
        hasil.galat = true;
        hasil.pesan = "jumlah token berubah (" + std::to_string(n_awal) + " -> " +
                            std::to_string(n_akhir) + ") -- bug di `jawa fmt`.";
        return false;
    }
    return true;
}

}  // namespace jawa::cli
