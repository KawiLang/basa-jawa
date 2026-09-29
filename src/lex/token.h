// Token Basa Jawa.
#pragma once

#include <cstdint>
#include <string_view>

namespace jawa::lex {

/// Jenis token. Nilai enum = indeks ke dalam tabel nama token.
enum class Tok : uint8_t {
    // --- pencanda (tidak pernah dikembalikan lexer) ---
    Eof,
    Unknown,

    // --- literal & identifier ---
    Ident,
    PrivateName,  // #nama
    Number,
    BigInt,
    Text,
    TemplateText,  // potongan di dalam `...` (bagian cooked)
    Regex,

    // --- kata kunci ---
#define JAWA_KEYWORD(ngoko, krama, id_token, css_name) id_token,
#include "keywords.def"
#undef JAWA_KEYWORD

    // --- operator & tanda baca ---
    LParen,     // (
    RParen,     // )
    LBrace,     // {
    RBrace,     // }
    LBracket,   // [
    RBracket,   // ]
    Comma,      // ,
    Dot,        // .
    Ellipsis,   // ...
    Semi,       // ;
    Colon,      // :
    Question,   // ?
    QuestionDot,// ?.
    At,         // @ (untuk anotasi & dekorator masa depan)
    QuestionQuestion, // ??
    QuestionQuestionEq, // ??=

    Plus, Minus, Star, StarStar, Slash, Percent,
    PlusPlus, MinusMinus,

    Eq,          // =
    EqEq,        // ==
    EqEqEq,      // ===
    Bang,        // !
    BangEq,      // !=
    BangEqEq,    // !==
    Lt, Gt, LtEq, GtEq,
    Amp, Pipe, Caret, Tilde,
    Shl, Shr, UShr,
    AmpEq, PipeEq, CaretEq,
    ShlEq, ShrEq, UShrEq,
    PlusEq, MinusEq, StarEq, StarStarEq, SlashEq, PercentEq,

    AmpAmp,      // &&
    PipePipe,    // ||
    PipeGreater, // |>

    AmpAmpEq,    // &&=
    PipePipeEq,  // ||=
    Arrow,       // =>
    TildeGreater,// ~>

    TokCount
};

/// Namanya kanonik (ngoko) untuk token kata kunci; string kosong untuk non-kata-kunci.
constexpr std::size_t kKeywordBegin = static_cast<std::size_t>(Tok::Ident) + 1;

/// Tabel nama token (dipakai disassembler, pesan galat, dan `jawa token`).
const char* token_name(Tok t) noexcept;

/// Apakah token boleh jadi nama variabel (identifier atau kata kunci kontekstual)?
bool is_identifier_like(Tok t) noexcept;

/// Apakah ini kata kunci "keras" (tidak boleh jadi identifier sama sekali)?
bool is_reserved_keyword(Tok t) noexcept;

/// Apakah ini kata kunci kontekstual (boleh jadi identifier bila konteks tak ambigu)?
bool is_contextual_keyword(Tok t) noexcept;

/// Slot bytecode length maksimum operand (untuk sementara: semua operand 1 u32).
inline constexpr int kMaxTokenNameLength = 24;

}  // namespace jawa::lex
