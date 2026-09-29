// Parser Basa Jawa bagian 3: ekspresi (precedence climbing).
//
// Presedensi mengikuti Bagian 3.4 spesifikasi, dari `parse_ekspresi` (terendah)
// hingga `parse_unary` (tertinggi). Ambiguitas arrow-vs-kurung diselesaikan
// dengan *cover grammar*: `coba_arrow()` memindai token untuk melihat apakah
// kurung yang dimulai `(` ditutup oleh `=>`; bila ya, kurung itu diparse
// sebagai daftar parameter.
#include <cstring>

#include "parser.h"

namespace jawa::parse {

using ast::NK;
using ast::NodePtr;
using lex::Tok;

// ===========================================================================
// BANTUAN
// ===========================================================================

// ===========================================================================
// PANAH: cover grammar
// ===========================================================================

bool Parser::coba_arrow(NodePtr& keluar) {
    // Hanya dipanggil bila token sekarang `(`.
    // ScanHh: cari `)` yang cocok, lalu cek apakah token berikutnya `=>`.
    const std::size_t simpan = idx_;
    int kedalaman = 0;
    std::size_t i = idx_;
    for (; i < token_.token.size(); ++i) {
        const Tok t = token_.token[i].jenis;
        if (t == Tok::LParen || t == Tok::LBracket || t == Tok::LBrace) ++kedalaman;
        else if (t == Tok::RParen || t == Tok::RBracket || t == Tok::RBrace) {
            --kedalaman;
            if (kedalaman == 0) break;
        } else if (t == Tok::Eof) {
            break;
        }
    }
    bool bisa = false;
    if (i + 1 < token_.token.size() && kedalaman == 0) {
        bisa = token_.token[i + 1].jenis == Tok::Arrow ||
               token_.token[i + 1].jenis == Tok::Colon;  // `(): Tipe =>` (lambat tapi sah)
    }
    if (!bisa) {
        idx_ = simpan;
        return false;
    }

    // Parse daftar parameter dari kurung tersebut.
    const std::size_t m = idx_;
    ++idx_;  // '('
    auto* fn = buat<ast::FungsiDeklarasi>(rentang_dari(m));
    fn->panah = true;
    lewati_asi();
    while (!cek(Tok::RParen) && !cek(Tok::Eof) && !bag_.penuh()) {
        auto* par = buat<ast::ParamDeklarasi>(saat().range);
        if (makan(Tok::Ellipsis)) par->rest = true;
        if (cek(Tok::Ident)) {
            par->nama = saat().teks;
            ++idx_;
        } else if (cek(Tok::LBrace) || cek(Tok::LBracket)) {
            par->destructuring = true;
            par->pola = cek(Tok::LBrace) ? parse_objek_literal() : parse_array_literal();
        } else {
            diagnosa_di("S001", "Ngarep-arep jeneng parametru fungsi panah.");
            sinkronisasi_statement();
            break;
        }
        tipe_annotation(par->tipe);
        par->range.selesai = peek(-1).range.selesai;
        if (makan(Tok::Eq)) par->nilai_default = parse_assignment();
        fn->param.push_back(par);
        lewati_asi();
        if (!makan(Tok::Comma)) break;
        lewati_asi();
    }
    aspek_ke_close(Tok::RParen, "S002", "\")\" sawise daftar parametru panah");
    lewati_asi();
    if (makan(Tok::Colon)) {
        fn->tipe_bali = parse_tipe();
        lewati_asi();
    }
    if (!makan(Tok::Arrow)) {
        diagnosa_di("S001", "Ngarep-arep `=>` sawise daftar parametru fungsi panah.");
        sinkronisasi_statement();
        return false;
    }
    lewati_asi();
    // badan
    if (makan(Tok::LBrace)) {
        fn->awak = parse_blok();
    } else {
        fn->ekspresi_badan = true;
        fn->badan_ekspresi = parse_assignment();
    }
    keluar = fn;
    return true;
}

// ===========================================================================
// EKSPRESI
// ===========================================================================

NodePtr Parser::parse_ekspresi() {
    const std::size_t m = idx_;
    NodePtr kiri = parse_assignment();
    if (!cek(Tok::Comma)) return kiri;
    std::vector<NodePtr> semua{kiri};
    while (makan(Tok::Comma)) {
        lewati_asi();
        semua.push_back(parse_assignment());
    }
    NodePtr hasil = semua.front();
    for (std::size_t i = 1; i < semua.size(); ++i) {
        auto* b = buat<ast::BinerExpr>(rentang_dari(m));
        b->op = ast::BinOp::Koma;
        b->kiri = hasil;
        b->kanan = semua[i];
        hasil = b;
    }
    return hasil;
}

NodePtr Parser::parse_assignment() {
    const std::size_t m = idx_;

    // `cocog (nilai) { ... }`
    if (cek(Tok::KwMatch)) return parse_cocog();

    // Fungsi panah: `mengko (a) => ...` / `mengko x => ...`
    if (cek(Tok::KwAsync) || cek(Tok::KwAsync)) {
        const std::size_t simpan = idx_;
        ++idx_;
        lewati_asi();
        bool berhasil = false;
        if (cek(Tok::Ident) && cek(1, Tok::Arrow)) {
            auto* fn = buat<ast::FungsiDeklarasi>(rentang_dari(m));
            fn->panah = true;
            fn->mengko = true;
            auto* par = buat<ast::ParamDeklarasi>(rentang_dari(m));
            par->nama = saat().teks;
            fn->param.push_back(par);
            idx_ += 2;
            if (makan(Tok::LBrace)) {
                fn->awak = parse_blok();
            } else {
                fn->ekspresi_badan = true;
                fn->badan_ekspresi = parse_assignment();
            }
            return fn;
        }
        if (cek(Tok::LParen)) {
            NodePtr arrow = nullptr;
            if (coba_arrow(arrow)) {
                static_cast<ast::FungsiDeklarasi*>(arrow)->mengko = true;
                static_cast<ast::FungsiDeklarasi*>(arrow)->range = rentang_dari(m);
                return arrow;
            }
        }
        if (cek(Tok::Star)) {  // `mengko * f`
            ++idx_;
            if (cek(Tok::Ident)) {
                auto* fn = buat<ast::FungsiDeklarasi>(rentang_dari(m));
                fn->panah = true;
                fn->mengko = true;
                fn->generator = true;
                auto* par = buat<ast::ParamDeklarasi>(rentang_dari(m));
                par->nama = saat().teks;
                fn->param.push_back(par);
                idx_ += 2;
                if (makan(Tok::LBrace)) {
                    fn->awak = parse_blok();
                } else {
                    fn->ekspresi_badan = true;
                    fn->badan_ekspresi = parse_assignment();
                }
                return fn;
            }
        }
        if (cek(Tok::KwFunction)) {
            NodePtr f = parse_deklarasi_fungsi();
            if (f != nullptr) static_cast<ast::FungsiDeklarasi*>(f)->mengko = true;
            return f;
        }
        idx_ = simpan;
        (void)berhasil;
    }

    // `x => ...`
    if (cek(Tok::Ident) && cek(1, Tok::Arrow)) {
        auto* fn = buat<ast::FungsiDeklarasi>(rentang_dari(m));
        fn->panah = true;
        auto* par = buat<ast::ParamDeklarasi>(rentang_dari(m));
        par->nama = saat().teks;
        par->range = saat().range;
        fn->param.push_back(par);
        idx_ += 2;
        lewati_asi();
        if (makan(Tok::LBrace)) {
            fn->awak = parse_blok();
        } else {
            fn->ekspresi_badan = true;
            fn->badan_ekspresi = parse_assignment();
        }
        return fn;
    }

    // `(a, b) => ...`
    if (cek(Tok::LParen)) {
        NodePtr arrow = nullptr;
        if (coba_arrow(arrow)) {
            static_cast<ast::FungsiDeklarasi*>(arrow)->range = rentang_dari(m);
            return arrow;
        }
    }

    // `async (x) { ... }` gaya fungsi biasa tanpa `gawe`
    NodePtr kiri = parse_kondisional();

    // Penugasan
    ast::AssignOp aop = ast::AssignOp::Set;
    bool ada = false;
    switch (jenis_sekarang()) {
        case Tok::Eq: aop = ast::AssignOp::Set; ada = true; break;
        case Tok::PlusEq: aop = ast::AssignOp::Tambah; ada = true; break;
        case Tok::MinusEq: aop = ast::AssignOp::Kurang; ada = true; break;
        case Tok::StarEq: aop = ast::AssignOp::Kali; ada = true; break;
        case Tok::SlashEq: aop = ast::AssignOp::Bagi; ada = true; break;
        case Tok::PercentEq: aop = ast::AssignOp::Modulo; ada = true; break;
        case Tok::StarStarEq: aop = ast::AssignOp::Pangkat; ada = true; break;
        case Tok::ShlEq: aop = ast::AssignOp::GeserKiri; ada = true; break;
        case Tok::ShrEq: aop = ast::AssignOp::GeserKanan; ada = true; break;
        case Tok::UShrEq: aop = ast::AssignOp::GeserKananTanpaTanda; ada = true; break;
        case Tok::AmpEq: aop = ast::AssignOp::BitDan; ada = true; break;
        case Tok::PipeEq: aop = ast::AssignOp::BitOr; ada = true; break;
        case Tok::CaretEq: aop = ast::AssignOp::BitXor; ada = true; break;
        case Tok::AmpAmpEq: aop = ast::AssignOp::Lan; ada = true; break;
        case Tok::PipePipeEq: aop = ast::AssignOp::Utawa; ada = true; break;
        case Tok::QuestionQuestionEq: aop = ast::AssignOp::Nullish; ada = true; break;
        default: break;
    }
    if (ada) {
        ++idx_;
        lewati_asi();
        auto* pen = buat<ast::PenugasanExpr>(rentang_dari(m));
        pen->op = aop;
        pen->target = kiri;
        pen->nilai = parse_assignment();
        return pen;
    }
    return kiri;
}

NodePtr Parser::parse_kondisional() {
    const std::size_t m = idx_;
    NodePtr kondisi = parse_pipeline();
    if (!makan(Tok::Question)) return kondisi;
    lewati_asi();
    auto* k = buat<ast::KondisionalExpr>(rentang_dari(m));
    k->kondisi = kondisi;
    k->bila_benar = parse_assignment();
    lewati_asi();
    aspek_ke_close(Tok::Colon, "S002", "\":\" sawise cabang bener");
    lewati_asi();
    k->bila_salah = parse_assignment();
    return k;
}

NodePtr Parser::parse_pipeline() {
    const std::size_t m = idx_;
    NodePtr kiri = parse_logika_or();
    while (makan(Tok::PipeGreater)) {
        lewati_asi();
        auto* p = buat<ast::RangkaianExpr>(rentang_dari(m));
        p->kiri = kiri;
        p->kanan = parse_logika_or();
        kiri = p;
    }
    return kiri;
}

NodePtr Parser::parse_logika_or() {
    const std::size_t m = idx_;
    NodePtr kiri = parse_logika_and();
    for (;;) {
        ast::LogOp op;
        if (cek(Tok::PipePipe) || cek(Tok::KwOr) || cek(Tok::KwOr)) op = ast::LogOp::Utawa;
        else if (cek(Tok::QuestionQuestion)) op = ast::LogOp::Nullish;
        else break;
        ++idx_;
        lewati_asi();
        auto* l = buat<ast::LogikaExpr>(rentang_dari(m));
        l->op = op;
        l->kiri = kiri;
        l->kanan = parse_logika_and();
        kiri = l;
    }
    return kiri;
}

NodePtr Parser::parse_logika_and() {
    const std::size_t m = idx_;
    NodePtr kiri = parse_bit_or();
    while (cek(Tok::AmpAmp) || cek(Tok::KwAnd) || cek(Tok::KwAnd)) {
        ++idx_;
        lewati_asi();
        auto* l = buat<ast::LogikaExpr>(rentang_dari(m));
        l->op = ast::LogOp::Lan;
        l->kiri = kiri;
        l->kanan = parse_bit_or();
        kiri = l;
    }
    return kiri;
}

NodePtr Parser::parse_bit_or() {
    const std::size_t m = idx_;
    NodePtr kiri = parse_bit_xor();
    while (cek(Tok::Pipe)) {
        ++idx_;
        lewati_asi();
        auto* b = buat<ast::BinerExpr>(rentang_dari(m));
        b->op = ast::BinOp::BitOr;
        b->kiri = kiri;
        b->kanan = parse_bit_xor();
        kiri = b;
    }
    return kiri;
}

NodePtr Parser::parse_bit_xor() {
    const std::size_t m = idx_;
    NodePtr kiri = parse_bit_and();
    while (cek(Tok::Caret)) {
        ++idx_;
        lewati_asi();
        auto* b = buat<ast::BinerExpr>(rentang_dari(m));
        b->op = ast::BinOp::BitXor;
        b->kiri = kiri;
        b->kanan = parse_bit_and();
        kiri = b;
    }
    return kiri;
}

NodePtr Parser::parse_bit_and() {
    const std::size_t m = idx_;
    NodePtr kiri = parse_equality();
    while (cek(Tok::Amp)) {
        ++idx_;
        lewati_asi();
        auto* b = buat<ast::BinerExpr>(rentang_dari(m));
        b->op = ast::BinOp::BitDan;
        b->kiri = kiri;
        b->kanan = parse_equality();
        kiri = b;
    }
    return kiri;
}

NodePtr Parser::parse_equality() {
    const std::size_t m = idx_;
    NodePtr kiri = parse_relasional();
    for (;;) {
        ast::BinOp op;
        if (cek(Tok::EqEqEq)) op = ast::BinOp::EqKetat;
        else if (cek(Tok::BangEqEq)) op = ast::BinOp::NeKetat;
        else if (cek(Tok::EqEq)) op = ast::BinOp::Eq;
        else if (cek(Tok::BangEq)) op = ast::BinOp::Ne;
        else break;
        ++idx_;
        lewati_asi();
        auto* b = buat<ast::BinerExpr>(rentang_dari(m));
        b->op = op;
        b->kiri = kiri;
        b->kanan = parse_relasional();
        kiri = b;
    }
    return kiri;
}

NodePtr Parser::parse_relasional() {
    const std::size_t m = idx_;
    NodePtr kiri = parse_shift();
    for (;;) {
        ast::BinOp op;
        switch (jenis_sekarang()) {
            case Tok::Lt: op = ast::BinOp::Lt; break;
            case Tok::LtEq: op = ast::BinOp::Le; break;
            case Tok::Gt: op = ast::BinOp::Gt; break;
            case Tok::GtEq: op = ast::BinOp::Ge; break;
            case Tok::KwInstanceof: op = ast::BinOp::instanceSaka; break;
            case Tok::KwIn: op = ast::BinOp::ing; break;
            default: return kiri;
        }
        ++idx_;
        lewati_asi();
        auto* b = buat<ast::BinerExpr>(rentang_dari(m));
        b->op = op;
        b->kiri = kiri;
        b->kanan = parse_shift();
        kiri = b;
    }
}

NodePtr Parser::parse_shift() {
    const std::size_t m = idx_;
    NodePtr kiri = parse_additif();
    for (;;) {
        ast::BinOp op;
        switch (jenis_sekarang()) {
            case Tok::Shl: op = ast::BinOp::GeserKiri; break;
            case Tok::Shr: op = ast::BinOp::GeserKanan; break;
            case Tok::UShr: op = ast::BinOp::GeserKananTanpaTanda; break;
            default: return kiri;
        }
        ++idx_;
        lewati_asi();
        auto* b = buat<ast::BinerExpr>(rentang_dari(m));
        b->op = op;
        b->kiri = kiri;
        b->kanan = parse_additif();
        kiri = b;
    }
}

NodePtr Parser::parse_additif() {
    const std::size_t m = idx_;
    NodePtr kiri = parse_multiplikatif();
    for (;;) {
        ast::BinOp op;
        if (cek(Tok::Plus)) op = ast::BinOp::Tambah;
        else if (cek(Tok::Minus)) op = ast::BinOp::Kurang;
        else break;
        ++idx_;
        lewati_asi();
        auto* b = buat<ast::BinerExpr>(rentang_dari(m));
        b->op = op;
        b->kiri = kiri;
        b->kanan = parse_multiplikatif();
        kiri = b;
    }
    return kiri;
}

NodePtr Parser::parse_multiplikatif() {
    const std::size_t m = idx_;
    NodePtr kiri = parse_pangkat();
    for (;;) {
        ast::BinOp op;
        if (cek(Tok::Star)) op = ast::BinOp::Kali;
        else if (cek(Tok::Slash)) op = ast::BinOp::Bagi;
        else if (cek(Tok::Percent)) op = ast::BinOp::Modulo;
        else break;
        ++idx_;
        lewati_asi();
        auto* b = buat<ast::BinerExpr>(rentang_dari(m));
        b->op = op;
        b->kiri = kiri;
        b->kanan = parse_pangkat();
        kiri = b;
    }
    return kiri;
}

NodePtr Parser::parse_pangkat() {
    const std::size_t m = idx_;
    NodePtr kiri = parse_unary();
    if (makan(Tok::StarStar)) {
        lewati_asi();
        auto* b = buat<ast::BinerExpr>(rentang_dari(m));
        b->op = ast::BinOp::Pangkat;
        b->kiri = kiri;
        b->kanan = parse_pangkat();  // asosiatif kanan
        return b;
    }
    return kiri;
}

NodePtr Parser::parse_unary() {
    const std::size_t m = idx_;
    ast::UnOp op = ast::UnOp::Neg;
    bool ada = false;
    switch (jenis_sekarang()) {
        case Tok::Minus: op = ast::UnOp::Neg; ada = true; break;
        case Tok::Plus: op = ast::UnOp::Pos; ada = true; break;
        case Tok::Bang: case Tok::KwNot: op = ast::UnOp::Ora; ada = true; break;
        case Tok::Tilde: op = ast::UnOp::BitNot; ada = true; break;
        case Tok::KwTypeof: op = ast::UnOp::Jinis; ada = true; break;
        case Tok::KwDelete: op = ast::UnOp::Busak; ada = true; break;
        case Tok::PlusPlus: op = ast::UnOp::PlusPlus; ada = true; break;
        case Tok::MinusMinus: op = ast::UnOp::MinusMinus; ada = true; break;
        case Tok::KwAwait: op = ast::UnOp::Entani; ada = true; break;
        case Tok::KwYield: op = ast::UnOp::Metokake; ada = true; break;
        default: break;
    }
    if (!ada) return parse_postfix();

    ++idx_;
    lewati_asi();
    if (op == ast::UnOp::PlusPlus || op == ast::UnOp::MinusMinus) {
        auto* p = buat<ast::PembaruanExpr>(rentang_dari(m));
        p->op = op;
        p->target = parse_unary();
        p->prefiks = true;
        return p;
    }
    auto* u = buat<ast::UnaryExpr>(rentang_dari(m));
    u->op = op;
    u->operand = parse_unary();
    return u;
}

NodePtr Parser::parse_postfix() {
    const std::size_t m = idx_;
    NodePtr e = parse_call_member();
    if (e != nullptr && (cek(Tok::PlusPlus) || cek(Tok::MinusMinus)) && !saat().baris_baru_sebelum) {
        auto* p = buat<ast::PembaruanExpr>(rentang_dari(m));
        p->op = cek(Tok::PlusPlus) ? ast::UnOp::PlusPlus : ast::UnOp::MinusMinus;
        ++idx_;
        p->target = e;
        p->prefiks = false;
        return p;
    }
    return e;
}

// ===========================================================================
// CALL / MEMBER
// ===========================================================================

/// Parse daftar argumen setelah `(` sudah dimakan. Tutup dengan `)`.
void Parser::parse_argumen(std::vector<NodePtr>& keluar, const std::size_t m) {
    lewati_asi();
    while (!cek(Tok::RParen) && !cek(Tok::Eof) && !bag_.penuh()) {
        if (makan(Tok::Ellipsis)) {
            auto* el = buat<ast::ElemenArr>(rentang_dari(m));
            el->spread = true;
            el->nilai = parse_assignment();
            keluar.push_back(el);
        } else {
            keluar.push_back(parse_assignment());
        }
        lewati_asi();
        if (!makan(Tok::Comma)) break;
        lewati_asi();
    }
    aspek_ke_close(Tok::RParen, "S002", "\")\" sawise daftar argumen");
}

NodePtr Parser::parse_call_member() {
    const std::size_t m = idx_;
    return lanjut_member(parse_primary(), m);
}

NodePtr Parser::parse_konstruktor() {
    const std::size_t m = idx_;
    NodePtr e = parse_primary();
    if (e == nullptr) return nullptr;
    for (;;) {
        if (makan(Tok::Dot)) {
            lewati_asi();
            auto* a = buat<ast::AksesProperti>(rentang_dari(m));
            a->objek = e;
            if (cek(Tok::PrivateName)) {
                a->privat = true;
                a->nama = saat().teks;
                ++idx_;
            } else if (boleh_adi_properti(saat().jenis)) {
                a->nama = saat().teks;
                ++idx_;
            } else {
                diagnosa_di("S001", "Ngarep-arep jeneng properti sawise titik.");
                sinkronisasi_statement();
                return a;
            }
            e = a;
            continue;
        }
        if (makan(Tok::LBracket)) {
            lewati_asi();
            auto* a = buat<ast::AksesProperti>(rentang_dari(m));
            a->objek = e;
            a->komputat = parse_ekspresi();
            lewati_asi();
            aspek_ke_close(Tok::RBracket, "S002", "\"]\" sawise indeks");
            e = a;
            continue;
        }
        break;
    }
    return e;
}

NodePtr Parser::lanjut_member(NodePtr e, const std::size_t m) {
    if (e == nullptr) return nullptr;
    for (;;) {
        if (makan(Tok::Dot)) {
            lewati_asi();
            auto* a = buat<ast::AksesProperti>(rentang_dari(m));
            a->objek = e;
            if (cek(Tok::PrivateName)) {
                a->privat = true;
                a->nama = saat().teks;
                ++idx_;
            } else if (boleh_adi_properti(saat().jenis)) {
                a->nama = saat().teks;
                ++idx_;
            } else {
                diagnosa_di("S001", "Ngarep-arep jeneng properti sawise titik.");
                sinkronisasi_statement();
                return a;
            }
            e = a;
            continue;
        }

        if (makan(Tok::QuestionDot)) {
            lewati_asi();
            if (makan(Tok::LParen)) {
                auto* holder = buat<ast::AksesProperti>(rentang_dari(m));
                holder->objek = e;
                holder->nama = {};
                auto* p = buat<ast::Panggilan>(rentang_dari(m));
                p->callee = holder;
                p->opsional = true;
                parse_argumen(p->argumen, m);
                e = p;
                continue;
            }
            auto* a = buat<ast::AksesProperti>(rentang_dari(m));
            a->objek = e;
            a->opsional = true;
            if (makan(Tok::LBracket)) {
                lewati_asi();
                a->komputat = parse_ekspresi();
                lewati_asi();
                aspek_ke_close(Tok::RBracket, "S002", "\"]\" sawise indeks");
            } else if (cek(Tok::PrivateName)) {
                a->privat = true;
                a->nama = saat().teks;
                ++idx_;
            } else if (boleh_adi_properti(saat().jenis)) {
                a->nama = saat().teks;
                ++idx_;
            } else {
                diagnosa_di("S001", "Ngarep-arep jeneng properti sawise ?..");
            }
            e = a;
            continue;
        }

        if (makan(Tok::LBracket)) {
            lewati_asi();
            auto* a = buat<ast::AksesProperti>(rentang_dari(m));
            a->objek = e;
            a->komputat = parse_ekspresi();
            lewati_asi();
            aspek_ke_close(Tok::RBracket, "S002", "\"]\" sawise indeks");
            e = a;
            continue;
        }

        if (makan(Tok::LParen)) {
            auto* p = buat<ast::Panggilan>(rentang_dari(m));
            p->callee = e;
            parse_argumen(p->argumen, m);
            e = p;
            continue;
        }

        // template bertag: tag`...`
        if (cek(Tok::TemplateText) && !saat().baris_baru_sebelum) {
            NodePtr tpl = parse_template();
            if (tpl != nullptr) {
                auto* t = static_cast<ast::TemplateLit*>(tpl);
                t->ada_tag = true;
                t->tag = e;
                e = t;
            }
            continue;
        }
        break;
    }
    return e;
}

}  // namespace jawa::parse
