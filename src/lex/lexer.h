// Lexer Basa Jawa.
//
// Token zero-copy (string_view ke buffer sumber), posisi presisi (baris/kolom
// dalam kolom Unicode), dan pemulihan dari galat yang tidak pernah crash.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "support/arena.h"
#include "support/diagnostics.h"
#include "support/source_map.h"
#include "token.h"

namespace jawa::lex {

using support::SourcePos;
using support::SourceRange;

/// Satu token hasil leksikal.
struct Token {
    Tok jenis = Tok::Eof;
    SourceRange range;
    /// Potongan teks mentah (untuk identifier/keyword/punctuator).
    std::string_view teks;
    /// Nilai yang sudah dihitung untuk Number/Text/TemplateText.
    double angka = 0.0;
    /// BigInt: digit mentah (tanpa sufiks `n`).
    std::string_view bigint_teks;
    /// Text: nilai string yang sudah di-decode escape, milik lexer.
    std::string_view nilai_teks;
    /// Regex: pola mentah (tanpa `/` dan flag), milik sumber.
    std::string_view regex_pola;
    /// Regex: flag mentah, mis. "gimsuy".
    std::string_view regex_flag;
    /// Ada baris baru sebelum token ini? (untuk ASI)
    bool baris_baru_sebelum = false;

    // --- penanda template literal ---
    /// Indeks ke `TokenList::template_part` (hanya untuk Tok::TemplateText).
    int template_indeks = -1;
    /// Bagian ini adalah backtick pembuka.
    bool template_awal = false;
    /// Bagian ini adalah backtick penutup.
    bool template_akhir = false;
    /// Setelah bagian ini Follow ekspresi `${...}`.
    bool template_expr_ikut = false;
    /// Token `}` ini menutup ekspresi template.
    bool template_expr_akhir = false;

    /// Nama token untuk pesan galat.
    [[nodiscard]] const char* nama() const noexcept { return token_name(jenis); }
    /// Teks yang ditampilkan user (identifier/keyword) atau nama token.
    [[nodiscard]] std::string_view tampilan() const noexcept { return teks.empty() ? std::string_view(nama()) : teks; }
};

/// Hasil lexing satu berkas: daftar token + daftar bagian template.
///
/// **Kepemilikan string:** `Token::nilai_teks` menunjuk ke string yang dimiliki
/// `TokenList::penyimpanan` (bukan ke buffer sumber, bukan ke Lexer). Dengan
/// begitu `TokenList` tetap valid setelah `Lexer` dihancurkan — penting untuk
/// CLI, REPL, dan embedding API. `std::deque` dipilih karena alamat elemennya
/// stabil terhadap `push_back` berikutnya.
struct TokenList {
    std::vector<Token> token;
    /// Template literal yang perlu diparse ulang (`${...}`).
    /// Indeks = nilai `Token::template_indeks`.
    std::vector<std::vector<Token>> template_expr;
    /// Pemilik semua string hasil decode (nilai teks, bagian template).
    std::deque<std::string> penyimpanan;

    /// Salin `s` ke penyimpanan & kembalikan view yang stabil.
    std::string_view salin(std::string_view s) {
        penyimpanan.emplace_back(s);
        return penyimpanan.back();
    }
};

/// Mode lexer.
struct LexOptions {
    /// Wajibkan titik koma (--ketat-titik-koma).
    bool ketat_titik_koma = false;
    /// Tolak pencampuran ngoko/krama (--ketat-krama).
    bool ketat_krama = false;
};

/// Lexer tanpa state (dapat dipakai ulang untuk re-scan pada titik tertentu).
class Lexer {
public:
    Lexer(std::string_view sumber, std::string_view nama_berkas, std::string_view dir_berkas, const LexOptions& opt = {});

    /// Lex seluruh berkas. Diagnostik dikumpulkan di `bag()`.
    void lex_semua(TokenList& keluar);

    /// Lex dari posisi tertentu sampai `sampai` (untuk parser sub-region).
    void lex_rentang(std::size_t dari, std::size_t sampai, TokenList& keluar);

    [[nodiscard]] std::string_view sumber() const noexcept { return src_; }
    [[nodiscard]] std::string_view nama_berkas() const noexcept { return nama_; }
    [[nodiscard]] std::string_view dir_berkas() const noexcept { return dir_; }
    [[nodiscard]] const support::DiagnosticBag& bag() const noexcept { return bag_; }
    support::DiagnosticBag& bag() noexcept { return bag_; }

    /// Apakah posisi `offset` memulai baris baru (heuristik untuk ASI & diagnostik).
    [[nodiscard]] bool baris_baru_pada(std::size_t offset) const noexcept;

    /// Peeking: apakah kata kunciNljack (tanpaidents) cocok pada offset (untuk parser).
    [[nodiscard]] bool kata_kunci_di(std::size_t offset, Tok k) const noexcept;

    /// Ambil baris sumber lengkap untuk diagnostik.
    [[nodiscard]] std::string_view baris_sumber(std::size_t offset) const noexcept;

    /// Salin `s` ke `TokenList` yang sedang dibangun (lihat `TokenList::salin`).
    [[nodiscard]] std::string_view salin(std::string_view s) {
        return list_->salin(s);
    }

    [[nodiscard]] const LexOptions& opsi() const noexcept { return opt_; }
    [[nodiscard]] bool ketat_titik_koma() const noexcept { return opt_.ketat_titik_koma; }
    [[nodiscard]] bool ketat_krama() const noexcept { return opt_.ketat_krama; }

private:
    // --- posisi & karakter ---
    [[nodiscard]] bool eof() const noexcept { return pos_ >= src_.size(); }
    [[nodiscard]] char peek(std::size_t lookahead = 0) const noexcept {
        return pos_ + lookahead < src_.size() ? src_[pos_ + lookahead] : '\0';
    }
    [[nodiscard]] char peek_prev() const noexcept { return pos_ > 0 ? src_[pos_ - 1] : '\0'; }
    void maju(std::size_t n = 1) noexcept;
    void maju_ke(std::size_t target) noexcept;

    // --- primitif ---
    void lewati_spasi(bool& baris_baru);
    [[nodiscard]] bool cocok(std::string_view s) const noexcept;
    void diagnostik(const char* kode, SourcePos pos, std::string_view tambahan = {});

    // --- pengenal & kata kunci ---
    void lex_pengenal(Token& t);
    void lex_kata_kunci(Token& t, std::string_view kata);
    Tok cari_kata_kunci(std::string_view kata) const noexcept;
    void catat_kata_kunci_krama(bool ngoko) noexcept;

    // --- literal ---
    void lex_angka(Token& t);
    void lex_teks(Token& t);
    void lex_regex(Token& t);
    void lex_private_name(Token& t);

    // --- token umum ---
    void token_sederhana(Token& t, Tok jenis, std::size_t panjang);
    void token_punggel(Token& t);

    // --- template literal (mode stack) ---
    struct TemplateFrame {
        std::string cooked;         ///< teks terakumulasi sebelum `${` atau `` ` ``
        int kedalaman_kurung = 0;   ///< kedalaman `{` di dalam ekspresi
        bool aktif = false;         ///< sedang mengumpulkan cooked (bukan ekspresi)
        bool sudah_terbit = false;  ///< sudah menerbitkan minimal satu bagian
    };
    /// Terbitkan bagian cooked yang terkumpul; `expr_ikut` = diikuti `${`.
    void template_publish(TokenList& keluar, bool expr_ikut);

    std::string_view src_;
    std::string_view nama_;
    std::string_view dir_;
    LexOptions opt_;
    std::size_t pos_ = 0;
    std::uint32_t baris_ = 1;
    std::uint32_t kolom_ = 1;
    bool baris_baru_pending_ = false;
    bool saw_krama_ = false;
    bool saw_ngoko_ = false;
    support::DiagnosticBag bag_;
    /// TokenList tujuan; semua string hasil decode disimpan di sana.
    TokenList* list_ = nullptr;
    std::vector<TemplateFrame> template_stack_;
};

}  // namespace jawa::lex
