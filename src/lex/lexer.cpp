#include "lexer.h"

#include <algorithm>
#include <array>
#include <vector>
#include <cmath>
#include <cstring>
#include <limits>

#include "rt/number.h"

namespace jawa::lex {

// ---------------------------------------------------------------------------
// Tabel nama token
// ---------------------------------------------------------------------------
namespace {

const std::array<const char*, static_cast<std::size_t>(Tok::TokCount)>& tabel_nama() {
    static const std::array<const char*, static_cast<std::size_t>(Tok::TokCount)> nama = [] {
        std::array<const char*, static_cast<std::size_t>(Tok::TokCount)> a{};
        std::size_t k = 0;
        auto taruh = [&a, &k](const char* s) { a[k++] = s; };
        taruh("akhir berkas");
        taruh("token ora weruh");
        taruh("pengenal");
        taruh("#nama privat");
        taruh("angka");
        taruh("bigint");
        taruh("teks");
        taruh("bagian template");
        taruh("regex");
#define JAWA_KEYWORD(ngoko, krama, id_token, css_name) taruh(ngoko);
#include "keywords.def"
#undef JAWA_KEYWORD
        static const char* const tanda[] = {
            "(", ")", "{", "}", "[", "]", ",", ".", "...", ";", ":", "?", "?.", "@", "??", "?\?=",
            "+", "-", "*", "**", "/", "%", "++", "--",
            "=", "==", "===", "!", "!=", "!==", "<", ">", "<=", ">=",
            "&", "|", "^", "~", "<<", ">>", ">>>", "&=", "|=", "^=", "<<=", ">>=", ">>>=",
            "+=", "-=", "*=", "**=", "/=", "%=",
            "&&", "||", "|>", "&&=", "||=",
            "=>", "~>",
        };
        for (const char* t : tanda) taruh(t);
        return a;
    }();
    return nama;
}

// ---------------------------------------------------------------------------
// Tabel kata kunci: vektor terurut + binary search.
//
// Dipilih daripada hash table buatan sendiri karena: (a) deterministik &
// mudah diaudit, (b) 50-ish entri sehingga binary search hanya ~6 langkah,
// (c) tidak ada risiko probe takcja.
// ---------------------------------------------------------------------------
struct KeywordEntry {
    std::string_view ngoko;
    std::string_view krama;
    Tok token;
};

class KeywordTable {
public:
    KeywordTable() {
// Catatan: argumen keywords.def sudah berupa string literal, jadi TIDAK boleh
// di-stringify lagi dengan `#`.
#define JAWA_KEYWORD(ngoko, krama, id_token, css_name) \
    masukkan(std::string_view(ngoko), Tok::id_token, false); \
    if (std::string_view(krama).size() != 0) masukkan(std::string_view(krama), Tok::id_token, true);
#include "keywords.def"
#undef JAWA_KEYWORD
        std::sort(semua_.begin(), semua_.end(),
                  [](const Entri& a, const Entri& b) { return a.nama < b.nama; });
    }

    /// Cari nama (ngoko atau krama). `ketik_krama` diisi true bila via alias krama.
    Tok cari(std::string_view kata, bool& ketik_krama) const noexcept {
        ketik_krama = false;
        if (kata.empty()) return Tok::Eof;
        const Entri* e = find(kata);
        if (e == nullptr) return Tok::Eof;
        ketik_krama = e->krama;
        return e->token;
    }

    [[nodiscard]] const std::vector<KeywordEntry>& semua() const noexcept { return asli_; }

    /// Alias krama untuk token (string kosong bila tidak punya alias).
    [[nodiscard]] std::string_view alias_krama(Tok t) const noexcept {
        for (const KeywordEntry& e : asli_) {
            if (e.token == t && !e.krama.empty()) return e.krama;
        }
        return {};
    }

    /// Nama ngoko kanonik untuk token (baris pertama dengan token tsb).
    [[nodiscard]] std::string_view nama_ngoko(Tok t) const noexcept {
        for (const KeywordEntry& e : asli_) {
            if (e.token == t) return e.ngoko;
        }
        return {};
    }

    /// Semua nama (ngoko + krama) untuk sebuah token.
    [[nodiscard]] std::vector<std::string_view> nama_untuk(Tok t) const {
        std::vector<std::string_view> hasil;
        for (const KeywordEntry& e : asli_) {
            if (e.token == t) {
                hasil.push_back(e.ngoko);
                if (!e.krama.empty()) hasil.push_back(e.krama);
            }
        }
        return hasil;
    }

    [[nodiscard]] std::size_t jumlah() const noexcept { return semua_.size(); }

private:
    struct Entri {
        std::string_view nama;
        Tok token;
        bool krama;
    };

    void masukkan(std::string_view nama, Tok token, bool krama) {
        semua_.push_back(Entri{nama, token, krama});
        asli_.push_back(KeywordEntry{krama ? std::string_view{} : nama, krama ? nama : std::string_view{}, token});
    }

    [[nodiscard]] const Entri* find(std::string_view nama) const noexcept {
        std::size_t lo = 0;
        std::size_t hi = semua_.size();
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (semua_[mid].nama < nama) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo < semua_.size() && semua_[lo].nama == nama) return &semua_[lo];
        return nullptr;
    }

    std::vector<Entri> semua_;   ///< terurut untuk pencarian
    std::vector<KeywordEntry> asli_;  ///< urutan deklarasi (untuk dokumentasi/ubah)
};

const KeywordTable& tabel_kata_kunci() {
    static const KeywordTable t;
    return t;
}

[[nodiscard]] int nilai_hex(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/// Decode satu code point; majukan `i`. Kembalikan 0xFFFD bila tidak valid.
char32_t decode_utf8(std::string_view s, std::size_t& i) noexcept {
    if (i >= s.size()) return 0;
    const auto b0 = static_cast<unsigned char>(s[i]);
    std::size_t len = 0;
    char32_t cp = 0;
    if (b0 < 0x80) {
        ++i;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0) {
        len = 2;
        cp = b0 & 0x1Fu;
    } else if ((b0 & 0xF0) == 0xE0) {
        len = 3;
        cp = b0 & 0x0Fu;
    } else if ((b0 & 0xF8) == 0xF0) {
        len = 4;
        cp = b0 & 0x07u;
    } else {
        ++i;
        return 0xFFFD;
    }
    if (i + len > s.size()) {
        ++i;
        return 0xFFFD;
    }
    for (std::size_t k = 1; k < len; ++k) {
        const auto bk = static_cast<unsigned char>(s[i + k]);
        if ((bk & 0xC0) != 0x80) {
            ++i;
            return 0xFFFD;
        }
        cp = (cp << 6) | (bk & 0x3Fu);
    }
    if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF ||
        (cp >= 0xD800 && cp <= 0xDFFF)) {
        ++i;
        return 0xFFFD;
    }
    i += len;
    return cp;
}

void encode_utf8(char32_t cp, std::string& keluar) {
    if (cp <= 0x7F) {
        keluar.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        keluar.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        keluar.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        keluar.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        keluar.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        keluar.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        keluar.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        keluar.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        keluar.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        keluar.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

[[nodiscard]] bool id_start(char32_t c) noexcept {
    if (c < 128) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
    if (c >= 0xC0 && c <= 0x24F) return true;      // Latin-1 supp + Latin Ext
    if (c >= 0x370 && c <= 0x3FF) return true;      // Greek
    if (c >= 0x400 && c <= 0x4FF) return true;      // Cyrillic
    if (c >= 0x1E00 && c <= 0x1FFF) return true;    // Latin Extended Additional
    if (c >= 0x2C60 && c <= 0x2C7F) return true;    // Latin Extended-C
    if (c >= 0xA720 && c <= 0xA7FF) return true;    // Latin Extended-D
    if (c >= 0xAB30 && c <= 0xAB6F) return true;    // Latin Extended-E
    if (c >= 0x3040 && c <= 0x30FF) return true;    // Hiragana + Katakana
    if (c >= 0x4E00 && c <= 0x9FFF) return true;    // CJK Unified
    if (c >= 0xAC00 && c <= 0xD7AF) return true;    // Hangul Syllables
    if (c >= 0x1100 && c <= 0x11FF) return true;    // Hangul Jamo
    if (c >= 0xA980 && c <= 0xA9DF) return true;    // Javanese Script (Hanacaraka)
    if (c >= 0x2D30 && c <= 0x2D7F) return true;    // Tifinagh
    if (c >= 0x0E00 && c <= 0x0E7F) return true;    // Thai
    if (c >= 0x0900 && c <= 0x0DFF) return true;    // Indic
    return false;
}

[[nodiscard]] bool id_continue(char32_t c) noexcept {
    if (c < 128) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
    }
    return true;
}

/// Apakah nilai `+`/`-` unary boleh muncul setelah token ini? (untuk regex disambiguation)
[[nodiscard]] bool izinkan_regex_setelah(Tok t) noexcept {
    switch (t) {
        case Tok::Number:
        case Tok::BigInt:
        case Tok::Text:
        case Tok::TemplateText:
        case Tok::Regex:
        case Tok::RParen:
        case Tok::RBracket:
        case Tok::RBrace:
        case Tok::PrivateName:
        case Tok::PlusPlus:
        case Tok::MinusMinus:
        case Tok::Ident:
        case Tok::KwThis:
        case Tok::KwTrue:
        case Tok::KwFalse:
        case Tok::KwNull:
        case Tok::KwUndefined:
        case Tok::KwNaNIdent:
        case Tok::KwInfinityIdent:
        case Tok::KwSuper:
            return false;
        default:
            return true;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Tabel publik
// ---------------------------------------------------------------------------

const char* token_name(Tok t) noexcept {
    const std::size_t i = static_cast<std::size_t>(t);
    if (i >= static_cast<std::size_t>(Tok::TokCount)) return "?";
    return tabel_nama()[i];
}

bool is_identifier_like(Tok t) noexcept { return t == Tok::Ident; }

bool is_reserved_keyword(Tok t) noexcept {
    const std::size_t i = static_cast<std::size_t>(t);
    return i > static_cast<std::size_t>(Tok::Ident) && i < static_cast<std::size_t>(Tok::LParen);
}

bool is_contextual_keyword(Tok t) noexcept {
    switch (t) {
        case Tok::KwGet:
        case Tok::KwSet:
        case Tok::KwStatic:
        case Tok::KwConstructor:
        case Tok::KwAs:
        case Tok::KwFrom:
        case Tok::KwDefault:
        case Tok::KwMatch:
        case Tok::KwAsync:
        case Tok::KwYield:
        case Tok::KwAwait:
        case Tok::KwTypeof:
        case Tok::KwInstanceof:
        case Tok::KwIn:
        case Tok::KwDelete:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------

Lexer::Lexer(std::string_view sumber, std::string_view nama_berkas, std::string_view dir_berkas, const LexOptions& opt)
    : src_(sumber), nama_(nama_berkas), dir_(dir_berkas), opt_(opt) {
    if (src_.size() >= 2 && src_[0] == '#' && src_[1] == '!' && (src_.size() < 3 || src_[2] != '[')) {
        const std::size_t eol = src_.find('\n');
        pos_ = (eol == std::string_view::npos) ? src_.size() : eol;
    }
    if (src_.size() >= 3 && static_cast<unsigned char>(src_[0]) == 0xEF && static_cast<unsigned char>(src_[1]) == 0xBB &&
        static_cast<unsigned char>(src_[2]) == 0xBF) {
        pos_ = 3;
    }
}

void Lexer::maju(std::size_t n) noexcept {
    for (std::size_t k = 0; k < n && pos_ < src_.size(); ++k) {
        const unsigned char c = static_cast<unsigned char>(src_[pos_]);
        if (c == '\n') {
            ++baris_;
            kolom_ = 1;
            baris_baru_pending_ = true;
        } else if ((c & 0xC0) != 0x80) {
            ++kolom_;
        }
        ++pos_;
    }
}

bool Lexer::cocok(std::string_view s) const noexcept {
    return pos_ + s.size() <= src_.size() && src_.compare(pos_, s.size(), s) == 0;
}

std::string_view Lexer::baris_sumber(std::size_t offset) const noexcept {
    if (offset >= src_.size()) return {};
    std::size_t awal = offset;
    while (awal > 0 && src_[awal - 1] != '\n') --awal;
    std::size_t akhir = offset;
    while (akhir < src_.size() && src_[akhir] != '\n') ++akhir;
    if (akhir > awal && src_[akhir - 1] == '\r') --akhir;
    return src_.substr(awal, akhir - awal);
}

bool Lexer::baris_baru_pada(std::size_t offset) const noexcept {
    std::size_t i = offset;
    while (i > 0) {
        --i;
        if (src_[i] == '\n') return true;
        if (src_[i] != ' ' && src_[i] != '\t' && src_[i] != '\r') return false;
    }
    return true;
}

bool Lexer::kata_kunci_di(std::size_t offset, Tok k) const noexcept {
    std::size_t i = offset;
    std::size_t mulai = offset;
    while (i < src_.size()) {
        const char c = src_[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$') {
            ++i;
        } else {
            break;
        }
    }
    if (i == mulai) return false;
    bool krama = false;
    return tabel_kata_kunci().cari(src_.substr(mulai, i - mulai), krama) == k;
}

void Lexer::diagnostik(const char* kode, SourcePos pos, std::string_view tambahan) {
    support::Diagnostic d;
    d.code = kode;
    d.level = support::DiagLevel::Galat;
    d.kind = support::DiagKind::Lexer;
    d.berkas = nama_;
    d.pos = pos;
    if (!tambahan.empty()) {
        d.pesan = std::string(tambahan);
    } else if (const support::CatalogMessage* m = support::find_message(kode)) {
        d.pesan = m->jawa;
    } else {
        d.pesan = "galat lexer";
    }
    support::isi_potongan(d, src_);
    bag_.add_galat(std::move(d));
}

void Lexer::lex_pengenal(Token& t) {
    const std::size_t mulai = pos_;
    std::size_t i = pos_;
    const char32_t cp0 = decode_utf8(src_, i);
    if (!id_start(cp0)) {
        const SourcePos p{static_cast<std::uint32_t>(mulai), baris_, kolom_};
        std::size_t j = pos_;
        decode_utf8(src_, j);
        diagnostik("L001", p, "Karakter ora sah sawise: " + std::string(src_.substr(pos_, j - pos_)));
        t.jenis = Tok::Unknown;
        t.teks = src_.substr(pos_, j - pos_);
        maju(j - pos_);
        return;
    }
    while (i < src_.size()) {
        std::size_t j = i;
        const char32_t c = decode_utf8(src_, j);
        if (!id_continue(c)) break;
        i = j;
    }
    const std::string_view kata = src_.substr(mulai, i - mulai);
    if (kata.size() > 255) {
        const SourcePos p{static_cast<std::uint32_t>(mulai), baris_, kolom_};
        diagnostik("L009", p, "Pengenal luwih saka 255 karakter.");
    }
    bool krama = false;
    const Tok k = tabel_kata_kunci().cari(kata, krama);
    t.jenis = (k == Tok::Eof) ? Tok::Ident : k;
    if (k != Tok::Eof) {
        if (krama) saw_krama_ = true; else saw_ngoko_ = true;
    }
    t.teks = kata;
    maju(i - mulai);
}

void Lexer::lex_private_name(Token& t) {
    const std::size_t mulai = pos_;
    std::size_t i = pos_ + 1;
    while (i < src_.size()) {
        std::size_t j = i;
        const char32_t c = decode_utf8(src_, j);
        if (!id_continue(c)) break;
        i = j;
    }
    if (i == mulai + 1) {
        const SourcePos p{static_cast<std::uint32_t>(mulai), baris_, kolom_};
        diagnostik("S018", p, "Ngarep-arep nama sawise # ing property privat.");
        t.jenis = Tok::Unknown;
        t.teks = src_.substr(mulai, 1);
        maju(1);
        return;
    }
    t.jenis = Tok::PrivateName;
    t.teks = src_.substr(mulai, i - mulai);
    maju(i - mulai);
}

void Lexer::lex_angka(Token& t) {
    const std::size_t mulai = pos_;
    std::size_t i = pos_;
    int radix = 10;

    if (src_[i] == '0' && i + 1 < src_.size()) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(src_[i + 1])));
        if (c == 'x') radix = 16;
        else if (c == 'o') radix = 8;
        else if (c == 'b') radix = 2;
    }

    if (radix != 10) {
        i += 2;
        const std::size_t digit_mulai = i;
        bool saw_digit = false;
        while (i < src_.size()) {
            const char c = src_[i];
            if (c == '_') { ++i; continue; }
            const int v = nilai_hex(c);
            if (v < 0 || v >= radix) break;
            saw_digit = true;
            ++i;
        }
        if (!saw_digit) {
            const SourcePos p{static_cast<std::uint32_t>(mulai), baris_, kolom_};
            diagnostik("L005", p, "Nomer radix tanpa digit: " + std::string(src_.substr(mulai, i - mulai)));
        }
        std::string bersih;
        for (std::size_t k = digit_mulai; k < i; ++k) {
            if (src_[k] != '_') bersih.push_back(src_[k]);
        }
        const std::size_t akhir_digit = i;
        if (i < src_.size() && src_[i] == 'n') {
            ++i;
            t.jenis = Tok::BigInt;
            t.bigint_teks = salin(bersih);
            t.teks = src_.substr(mulai, i - mulai);
            maju(i - mulai);
            return;
        }
        t.jenis = Tok::Number;
        t.angka = rt::parse_integer_radix(bersih, radix);
        t.teks = src_.substr(mulai, akhir_digit - mulai);
        if (i < src_.size() && (std::isalnum(static_cast<unsigned char>(src_[i])) != 0 || src_[i] == '_' || src_[i] == '$')) {
            const SourcePos p{static_cast<std::uint32_t>(i), baris_, kolom_};
            diagnostik("L005", p, "Nomer diikuti karakter ora sah: " + std::string(src_.substr(i, 1)));
        }
        maju(akhir_digit - mulai);
        return;
    }

    bool saw_digit = false;
    while (i < src_.size()) {
        const char c = src_[i];
        if (c >= '0' && c <= '9') { saw_digit = true; ++i; }
        else if (c == '_') ++i;
        else break;
    }
    bool ada_pecahan = false;
    if (i < src_.size() && src_[i] == '.') {
        ada_pecahan = true;
        ++i;
        while (i < src_.size()) {
            const char c = src_[i];
            if (c >= '0' && c <= '9') {
                saw_digit = true;
                ++i;
            } else if (c == '_') {
                ++i;
            } else {
                break;
            }
        }
    }
    bool ada_exponen = false;
    if (i < src_.size() && (src_[i] == 'e' || src_[i] == 'E')) {
        std::size_t j = i + 1;
        if (j < src_.size() && (src_[j] == '+' || src_[j] == '-')) ++j;
        if (j < src_.size() && src_[j] >= '0' && src_[j] <= '9') {
            while (j < src_.size() && ((src_[j] >= '0' && src_[j] <= '9') || src_[j] == '_')) ++j;
            i = j;
            ada_exponen = true;
        }
    }
    if (!saw_digit) {
        const SourcePos p{static_cast<std::uint32_t>(mulai), baris_, kolom_};
        diagnostik("L005", p, "Nomer ora valid: " + std::string(src_.substr(mulai, std::min<std::size_t>(4, i - mulai))));
    }
    const std::string_view mentah = src_.substr(mulai, i - mulai);
    if (i < src_.size() && src_[i] == 'n') {
        if (ada_pecahan || ada_exponen) {
            const SourcePos p{static_cast<std::uint32_t>(mulai), baris_, kolom_};
            diagnostik("L005", p, "BigInt ora bisa duwe titik pecahan utawa eksponen.");
        }
        std::string bersih;
        for (char c : mentah) {
            if (c != '_') bersih.push_back(c);
        }
        ++i;
        t.jenis = Tok::BigInt;
        t.bigint_teks = salin(bersih);
        t.teks = src_.substr(mulai, i - mulai);
        maju(i - mulai);
        return;
    }
    std::string bersih;
    bersih.reserve(mentah.size());
    for (char c : mentah) {
        if (c != '_') bersih.push_back(c);
    }
    t.jenis = Tok::Number;
    t.angka = rt::parse_decimal_strict(bersih);
    t.teks = mentah;
    if (i < src_.size() && (std::isalnum(static_cast<unsigned char>(src_[i])) != 0 || src_[i] == '_' || src_[i] == '$')) {
        const SourcePos p{static_cast<std::uint32_t>(i), baris_, kolom_};
        diagnostik("L005", p, "Nomer diikuti karakter ora sah: " + std::string(src_.substr(i, 1)));
    }
    maju(i - mulai);
}

/// Decode satu escape starting at `i` (which points at the backslash).
/// Memajukan `i` dan menambahkan hasil ke `keluar`. Mengembalikan false bila escape tidak dikenal.
static bool decode_escape(std::string_view src, std::size_t& i, std::string& keluar, std::string& pesan_galat) {
    if (i + 1 >= src.size()) {
        pesan_galat = "Escape kepotong.";
        return false;
    }
    const char e = src[i + 1];
    switch (e) {
        case 'n': keluar.push_back('\n'); i += 2; return true;
        case 't': keluar.push_back('\t'); i += 2; return true;
        case 'r': keluar.push_back('\r'); i += 2; return true;
        case 'b': keluar.push_back('\b'); i += 2; return true;
        case 'f': keluar.push_back('\f'); i += 2; return true;
        case 'v': keluar.push_back('\v'); i += 2; return true;
        case '0':
            if (i + 2 < src.size() && src[i + 2] >= '0' && src[i + 2] <= '9') {
                pesan_galat = "Oktal lawas ora diidinke ing mode ketat.";
                i += 2;
                return false;
            }
            keluar.push_back('\0');
            i += 2;
            return true;
        case 'x': {
            if (i + 3 < src.size() && nilai_hex(src[i + 2]) >= 0 && nilai_hex(src[i + 3]) >= 0) {
                keluar.push_back(static_cast<char>(nilai_hex(src[i + 2]) * 16 + nilai_hex(src[i + 3])));
                i += 4;
                return true;
            }
            pesan_galat = "\\x perlu followed dua digit heksadesimal.";
            i += 2;
            return false;
        }
        case 'u': {
            std::size_t j = i + 2;
            char32_t cp = 0;
            if (j < src.size() && src[j] == '{') {
                ++j;
                const std::size_t hs = j;
                while (j < src.size() && nilai_hex(src[j]) >= 0) ++j;
                if (j > hs && j < src.size() && src[j] == '}' && j - hs <= 6) {
                    for (std::size_t k = hs; k < j; ++k) cp = cp * 16 + static_cast<char32_t>(nilai_hex(src[k]));
                    if (cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
                        encode_utf8(cp, keluar);
                        i = j + 1;
                        return true;
                    }
                }
            } else if (j + 3 < src.size() && nilai_hex(src[j]) >= 0 && nilai_hex(src[j + 1]) >= 0 && nilai_hex(src[j + 2]) >= 0 &&
                       nilai_hex(src[j + 3]) >= 0) {
                for (int k = 0; k < 4; ++k) cp = cp * 16 + static_cast<char32_t>(nilai_hex(src[j + static_cast<std::size_t>(k)]));
                if (!(cp >= 0xD800 && cp <= 0xDFFF)) {
                    encode_utf8(cp, keluar);
                    i = j + 4;
                    return true;
                }
            }
            pesan_galat = "\\u perlu escape heksadesimal sing valid.";
            i += 2;
            return false;
        }
        case '\\': keluar.push_back('\\'); i += 2; return true;
        case '\'': keluar.push_back('\''); i += 2; return true;
        case '"': keluar.push_back('"'); i += 2; return true;
        case '`': keluar.push_back('`'); i += 2; return true;
        case '\n': i += 2; return true;  // line continuation
        case '\r':
            i += 2;
            if (i < src.size() && src[i - 1] == '\r' && src[i] == '\n') ++i;
            return true;
        default:
            pesan_galat = std::string("Escape ora weruh: \\") + e;
            keluar.push_back(e);
            i += 2;
            return false;
    }
}

void Lexer::lex_teks(Token& t) {
    const char kutip = peek();
    const std::size_t mulai = pos_;
    std::size_t i = pos_ + 1;
    std::string nilai;
    bool selesai = false;
    while (i < src_.size()) {
        const char c = src_[i];
        if (c == kutip) {
            selesai = true;
            ++i;
            break;
        }
        if (c == '\n') break;
        if (c == '\\') {
            const std::uint32_t baris_esc = baris_;
            const std::uint32_t kolom_esc = kolom_;
            std::string pesan;
            if (!decode_escape(src_, i, nilai, pesan)) {
                const SourcePos p{static_cast<std::uint32_t>(i - 2), baris_esc, kolom_esc};
                diagnostik("L004", p, pesan);
            }
            continue;
        }
        nilai.push_back(c);
        ++i;
    }
    if (!selesai) {
        const SourcePos p{static_cast<std::uint32_t>(mulai), baris_, kolom_};
        diagnostik("L002", p);
    }
    t.jenis = Tok::Text;
    t.teks = src_.substr(mulai, i - mulai);
    t.nilai_teks = salin(nilai);
    maju(i - mulai);
}

void Lexer::lex_regex(Token& t) {
    const std::size_t mulai = pos_;
    std::size_t i = pos_ + 1;
    int kurung_siku = 0;
    bool selesai = false;
    while (i < src_.size()) {
        const char c = src_[i];
        if (c == '\\') {
            i += 2;
            continue;
        }
        if (c == '\n') break;
        if (c == '[') ++kurung_siku;
        else if (c == ']') { if (kurung_siku > 0) --kurung_siku; }
        else if (c == '/' && kurung_siku == 0) {
            selesai = true;
            ++i;
            break;
        }
        ++i;
    }
    if (!selesai) {
        const SourcePos p{static_cast<std::uint32_t>(mulai), baris_, kolom_};
        diagnostik("L008", p, "Regex ora ketutup: teka baris anyar sadurunge penutup /.");
    }
    // Regex yang TIDAK ketutup bisa berhenti tepat di akhir sumber, jadi
    // `pola_akhir` bisa sama dengan `src_.size()`. Tanpa clamping, `substr` untuk
    // flag akan mulai pada `size() + 1` dan melempar `std::out_of_range`
    // (ditemukan fuzzing: masukan `/b` sepanjang 2 byte). Selain itu, regex
    // tanpa penutup tidak punya bagian flag sama sekali -- huruf setelahnya
    // milik token berikutnya, bukan flag.
    const std::size_t pola_akhir = std::min((selesai && i > mulai) ? i - 1 : i, src_.size());
    std::size_t fend = pola_akhir;
    if (selesai && fend < src_.size()) ++fend;
    while (fend < src_.size() && std::isalpha(static_cast<unsigned char>(src_[fend])) != 0) ++fend;
    t.jenis = Tok::Regex;
    t.teks = src_.substr(mulai, fend - mulai);
    t.regex_pola = src_.substr(mulai + 1, pola_akhir > mulai + 1 ? pola_akhir - mulai - 1 : 0);
    t.regex_flag = (selesai && pola_akhir < src_.size())
                       ? src_.substr(pola_akhir + 1, fend - pola_akhir - 1)
                       : std::string_view();
    for (char f : t.regex_flag) {
        if (f != 'g' && f != 'i' && f != 'm' && f != 's' && f != 'u' && f != 'y') {
            const SourcePos p{static_cast<std::uint32_t>(pola_akhir), baris_, kolom_};
            diagnostik("L008", p, std::string("Flag regex ora weruh: ") + f);
        }
    }
    if (!t.regex_pola.empty() && t.regex_pola.front() == '*') {
        const SourcePos p{static_cast<std::uint32_t>(mulai), baris_, kolom_};
        diagnostik("L008", p, "Regex ora bisa dimulai karo *.");
    }
    if (!t.regex_pola.empty() && t.regex_pola.back() == '\\') {
        const SourcePos p{static_cast<std::uint32_t>(pola_akhir > 0 ? pola_akhir - 1 : 0), baris_, kolom_};
        diagnostik("L008", p, "Regex ora bisa diakhiri backslash.");
    }
    maju(fend - mulai);
}

void Lexer::template_publish(TokenList& keluar, bool expr_ikut) {
    TemplateFrame& f = template_stack_.back();
    Token t;
    t.jenis = Tok::TemplateText;
    t.nilai_teks = salin(f.cooked);
    t.teks = t.nilai_teks;
    t.template_awal = !f.sudah_terbit;
    t.template_akhir = !expr_ikut;
    t.template_expr_ikut = expr_ikut;
    if (expr_ikut) {
        t.template_indeks = static_cast<int>(keluar.template_expr.size());
        keluar.template_expr.emplace_back();
    }
    keluar.token.push_back(t);
    f.cooked.clear();
    f.sudah_terbit = true;
}

void Lexer::token_sederhana(Token& t, Tok jenis, std::size_t panjang) {
    t.jenis = jenis;
    t.teks = src_.substr(pos_, panjang);
    maju(panjang);
}

void Lexer::token_punggel(Token& t) {
    // Max-munch: coba yang terpanjang dulu.
    struct Op {
        const char* teks;
        Tok token;
    };
    static constexpr Op ops3[] = {
        {">>>=", Tok::UShrEq}, {"===", Tok::EqEqEq}, {"!==", Tok::BangEqEq},
        {"**=", Tok::StarStarEq}, {"...", Tok::Ellipsis}, {"<<=", Tok::ShlEq},
        {">>=", Tok::ShrEq}, {"?\?=", Tok::QuestionQuestionEq}, {"&&=", Tok::AmpAmpEq}, {"||=", Tok::PipePipeEq},
    };
    static constexpr Op ops2[] = {
        {">>>", Tok::UShr}, {"**", Tok::StarStar}, {"=>", Tok::Arrow},
        {"?.", Tok::QuestionDot}, {"??", Tok::QuestionQuestion},
        {"++", Tok::PlusPlus}, {"--", Tok::MinusMinus}, {"+=", Tok::PlusEq},
        {"-=", Tok::MinusEq}, {"*=", Tok::StarEq}, {"/=", Tok::SlashEq},
        {"%=", Tok::PercentEq}, {"<=", Tok::LtEq}, {">=", Tok::GtEq},
        {"==", Tok::EqEq}, {"!=", Tok::BangEq}, {"<<", Tok::Shl},
        {">>", Tok::Shr}, {"&=", Tok::AmpEq}, {"|=", Tok::PipeEq},
        {"^=", Tok::CaretEq}, {"&&", Tok::AmpAmp}, {"||", Tok::PipePipe},
        {"|>", Tok::PipeGreater}, {"~>", Tok::TildeGreater},
    };
    for (const Op& o : ops3) {
        if (cocok(o.teks)) {
            token_sederhana(t, o.token, std::strlen(o.teks));
            return;
        }
    }
    for (const Op& o : ops2) {
        if (cocok(o.teks)) {
            token_sederhana(t, o.token, std::strlen(o.teks));
            return;
        }
    }
    const char c = peek();
    switch (c) {
        case '(': token_sederhana(t, Tok::LParen, 1); return;
        case ')': token_sederhana(t, Tok::RParen, 1); return;
        case '{': token_sederhana(t, Tok::LBrace, 1); return;
        case '}': token_sederhana(t, Tok::RBrace, 1); return;
        case '[': token_sederhana(t, Tok::LBracket, 1); return;
        case ']': token_sederhana(t, Tok::RBracket, 1); return;
        case ',': token_sederhana(t, Tok::Comma, 1); return;
        case '.': token_sederhana(t, Tok::Dot, 1); return;
        case ';': token_sederhana(t, Tok::Semi, 1); return;
        case ':': token_sederhana(t, Tok::Colon, 1); return;
        case '?': token_sederhana(t, Tok::Question, 1); return;
        case '@': token_sederhana(t, Tok::At, 1); return;
        case '+': token_sederhana(t, Tok::Plus, 1); return;
        case '-': token_sederhana(t, Tok::Minus, 1); return;
        case '*': token_sederhana(t, Tok::Star, 1); return;
        case '/': token_sederhana(t, Tok::Slash, 1); return;
        case '%': token_sederhana(t, Tok::Percent, 1); return;
        case '=': token_sederhana(t, Tok::Eq, 1); return;
        case '!': token_sederhana(t, Tok::Bang, 1); return;
        case '<': token_sederhana(t, Tok::Lt, 1); return;
        case '>': token_sederhana(t, Tok::Gt, 1); return;
        case '&': token_sederhana(t, Tok::Amp, 1); return;
        case '|': token_sederhana(t, Tok::Pipe, 1); return;
        case '^': token_sederhana(t, Tok::Caret, 1); return;
        case '~': token_sederhana(t, Tok::Tilde, 1); return;
        default: {
            const SourcePos p{static_cast<std::uint32_t>(pos_), baris_, kolom_};
            std::size_t j = pos_;
            decode_utf8(src_, j);
            if (j == pos_) j = pos_ + 1;
            const unsigned char c0 = static_cast<unsigned char>(src_[pos_]);
            if (c0 < 0x80) {
                diagnostik("L001", p, "Karakter ora ngalapis: " + std::string(1, c0));
            } else {
                diagnostik("L007", p, "Urutan byte UTF-8 ora valid.");
            }
            t.jenis = Tok::Unknown;
            t.teks = src_.substr(pos_, j - pos_);
            maju(j - pos_);
            return;
        }
    }
}

void Lexer::lex_semua(TokenList& keluar) { lex_rentang(pos_, src_.size(), keluar); }

void Lexer::lex_rentang(std::size_t dari, std::size_t sampai, TokenList& keluar) {
    list_ = &keluar;
    pos_ = dari;
    baris_ = 1;
    kolom_ = 1;
    for (std::size_t i = 0; i < dari && i < src_.size(); ++i) {
        if (src_[i] == '\n') { ++baris_; kolom_ = 1; }
        else if ((static_cast<unsigned char>(src_[i]) & 0xC0) != 0x80) ++kolom_;
    }
    baris_baru_pending_ = false;
    template_stack_.clear();
    bool regex_boleh = true;

    while (pos_ < sampai && pos_ < src_.size()) {
        // --- lewati spasi & komentar ---
        // Penting: di dalam template literal, spasi adalah bagian teks dan TIDAK
        // boleh dilewati. Tanpa penjagaan ini, `${x} ngeluarke swara` kehilangan
        // spasi setelah `}`.
        const bool mode_template = !template_stack_.empty() && template_stack_.back().aktif &&
                                   template_stack_.back().kedalaman_kurung == 0;
        bool baris_baru = false;
        for (;;) {
            if (mode_template) break;
            const char c = peek();
            if (c == '\n') { baris_baru = true; maju(1); }
            else if (c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f') maju(1);
            else if (c == '/' && peek(1) == '/') {
                while (pos_ < src_.size() && src_[pos_] != '\n') maju(1);
            } else if (c == '/' && peek(1) == '*') {
                const std::size_t mulai = pos_;
                const std::uint32_t b0 = baris_;
                const std::uint32_t k0 = kolom_;
                maju(2);
                bool ditutup = false;
                while (pos_ < src_.size()) {
                    if (src_[pos_] == '*' && peek(1) == '/') { maju(2); ditutup = true; break; }
                    if (src_[pos_] == '\n') baris_baru = true;
                    maju(1);
                }
                if (!ditutup) {
                    diagnostik("L006", SourcePos{static_cast<std::uint32_t>(mulai), b0, k0});
                }
            } else {
                break;
            }
        }
        if (pos_ >= sampai || pos_ >= src_.size()) break;
        if (baris_baru) baris_baru_pending_ = true;

        Token t;
        t.baris_baru_sebelum = baris_baru_pending_;
        baris_baru_pending_ = false;
        const SourcePos mulai_pos{static_cast<std::uint32_t>(pos_), baris_, kolom_};

        // --- mode template: cooked chunk ---
        if (!template_stack_.empty() && template_stack_.back().aktif && template_stack_.back().kedalaman_kurung == 0) {
            const char c = peek();
            if (c == '`') {
                template_publish(keluar, false);
                template_stack_.pop_back();
                maju(1);
                regex_boleh = true;
                t = keluar.token.back();
                t.range.mulai = mulai_pos;
                t.range.selesai = SourcePos{static_cast<std::uint32_t>(pos_), baris_, kolom_};
                t.baris_baru_sebelum = false;
                continue;
            }
            if (c == '$' && peek(1) == '{') {
                // Token `TemplateText` dengan `template_expr_ikut` sudah
                // menandakan bahwa ekspresi menyusul, jadi `${` tidak
                // menghasilkan token tersendiri (lihat DECISIONS.md D-004).
                template_publish(keluar, true);
                template_stack_.back().aktif = false;
                template_stack_.back().kedalaman_kurung = 1;
                baris_baru_pending_ = false;
                maju(2);
                regex_boleh = true;
                continue;
            }
            // kumpulkan cooked
            TemplateFrame& f = template_stack_.back();
            if (c == '\\') {
                std::string pesan;
                if (!decode_escape(src_, pos_, f.cooked, pesan)) {
                    diagnostik("L004", SourcePos{static_cast<std::uint32_t>(pos_), baris_, kolom_}, pesan);
                }
            } else {
                f.cooked.push_back(c);
                maju(1);
            }
            continue;
        }

        const char c = peek();
        if (c == '`') {
            template_stack_.emplace_back();
            template_stack_.back().aktif = true;
            template_stack_.back().kedalaman_kurung = 0;
            maju(1);
            // bagian cooked pertama akan diterbitkan saat ${ atau `
            continue;
        }

        if (c == '#' && peek(1) != '#' && peek(1) != '!') {
            lex_private_name(t);
        } else if (c == '"' || c == '\'') {
            lex_teks(t);
            regex_boleh = false;
        } else if ((c >= '0' && c <= '9') || (c == '.' && peek(1) >= '0' && peek(1) <= '9')) {
            lex_angka(t);
            regex_boleh = false;
        } else if (c == '/' && regex_boleh) {
            lex_regex(t);
            regex_boleh = false;
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' ||
                   static_cast<unsigned char>(c) >= 0x80) {
            lex_pengenal(t);
            // Setelah pengenal (atau `this`/`bener`/dll) `/` berarti pembagian;
            // setelah `)`/`]`/`}` regex baru boleh.
            regex_boleh = izinkan_regex_setelah(t.jenis);
        } else {
            // di dalam ekspresi template: `}` menutup ekspresi
            if (c == '}' && !template_stack_.empty() && template_stack_.back().kedalaman_kurung == 1) {
                t.jenis = Tok::RBrace;
                t.teks = src_.substr(pos_, 1);
                t.template_expr_akhir = true;
                template_stack_.back().kedalaman_kurung = 0;
                template_stack_.back().aktif = true;
                regex_boleh = true;
                maju(1);
            } else {
                token_punggel(t);
                if (t.jenis == Tok::LBrace && !template_stack_.empty()) ++template_stack_.back().kedalaman_kurung;
                regex_boleh = izinkan_regex_setelah(t.jenis);
            }
        }

        t.range.mulai = mulai_pos;
        t.range.selesai = SourcePos{static_cast<std::uint32_t>(pos_), baris_, kolom_};
        keluar.token.push_back(t);
        if (bag_.penuh()) break;
    }

    // template yang tidak ketutup
    while (!template_stack_.empty()) {
        if (template_stack_.back().kedalaman_kurung > 0) {
            diagnostik("L003", SourcePos{static_cast<std::uint32_t>(pos_), baris_, kolom_}, "Ekspresi template ora ketutup.");
        } else if (template_stack_.back().aktif) {
            diagnostik("L003", SourcePos{static_cast<std::uint32_t>(pos_), baris_, kolom_});
        }
        template_stack_.pop_back();
    }

    Token eof_tok;
    eof_tok.jenis = Tok::Eof;
    eof_tok.baris_baru_sebelum = baris_baru_pending_;
    eof_tok.range.mulai = SourcePos{static_cast<std::uint32_t>(pos_), baris_, kolom_};
    eof_tok.range.selesai = eof_tok.range.mulai;
    keluar.token.push_back(eof_tok);

    if (opt_.ketat_krama && saw_krama_ && saw_ngoko_) {
        support::Diagnostic d;
        d.code = "R036";
        d.kind = support::DiagKind::Lexer;
        d.berkas = nama_;
        d.pos = SourcePos{0, 1, 1};
        const support::CatalogMessage* m = support::find_message("R036");
        d.pesan = m != nullptr ? m->jawa : "Mencampur ngoko lan krama ora diidinke ing mode ketat.";
        d.saran = m != nullptr ? std::string(m->saran) : std::string();
        support::isi_potongan(d, src_);
        bag_.add_galat(std::move(d));
    }
}

}  // namespace jawa::lex
