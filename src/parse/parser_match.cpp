// Parser Basa Jawa bagian 5: template literal, `cocog` (match), dan pola.
#include <cstring>

#include "parser.h"

namespace jawa::parse {

using ast::NK;
using ast::NodePtr;
using lex::Tok;

// ===========================================================================
// TEMPLATE LITERAL
// ===========================================================================

NodePtr Parser::parse_template() {
    const std::size_t m = idx_;
    auto* tpl = buat<ast::TemplateLit>(rentang_dari(m));
    if (!cek(Tok::TemplateText)) {
        diagnosa_di("S001", "Ngarep-arep template literal (backtick).");
        return tpl;
    }

    for (;;) {
        if (!cek(Tok::TemplateText)) {
            diagnosa_di("S003", "Bagian template sabanjure ekspresi ora ketemu.",
                        "Yen template wis kudu nutup, tambahi backtick penutup.");
            break;
        }
        // Bagian cooked: satu token TemplateText dengan penanda awal/akhir.
        const lex::Token& t = saat();
        ast::TemplateBagian bagian;
        bagian.teks = std::string(t.nilai_teks);
        bagian.range = t.range;
        bagian.ekspresi = false;
        tpl->bagian.push_back(std::move(bagian));

        const bool expr_ikut = t.template_expr_ikut;
        const bool akhir = t.template_akhir;
        ++idx_;
        if (!expr_ikut) break;  // bagian penutup: template selesai

        // Ekspresi berada di aliran token utama dan diakhiri `}` bertanda
        // `template_expr_akhir`.
        ast::TemplateBagian b2;
        b2.ekspresi = true;
        b2.range = saat().range;
        b2.ekspresi_node = parse_assignment();
        b2.range.selesai = peek(-1).range.selesai;
        if (cek(Tok::RBrace) && saat().template_expr_akhir) {
            ++idx_;
        } else {
            diagnosa_di("S011", "Ekspresi template ora ketutup: ngarep-arep `}`.", "Tutup kurung kurawal `${...}`.");
            sinkronisasi_statement();
            if (cek(Tok::RBrace)) ++idx_;
        }
        tpl->bagian.push_back(std::move(b2));
        if (akhir) break;
    }
    return tpl;
}

// ===========================================================================
// COCOG (match)
// ===========================================================================

NodePtr Parser::parse_cocog() {
    const std::size_t m = idx_;
    ++idx_;  // cocog
    auto* c = buat<ast::CocogExpr>(rentang_dari(m));
    lewati_asi();
    c->subjek = parse_ekspresi();
    lewati_asi();
    aspek_ke_close(Tok::LBrace, "S016", "\"{\" sawise subjek cocog");
    lewati_asi();
    while (!cek(Tok::RBrace) && !cek(Tok::Eof) && !bag_.penuh()) {
        const std::size_t km = idx_;
        // Makan `kasus` (atau `baku` untuk kasus bawaan).
        if (cek(Tok::KwCase) || cek(Tok::KwDefault)) {
            ++idx_;
        } else {
            diagnosa_di("S002", "Ing jero cocog, ngarep-arep kasus utawa baku.");
            sinkronisasi_statement();
            continue;
        }
        auto* k = buat<ast::KasusKocog>(rentang_dari(km));
        k->pola = parse_pola();
        lewati_asi();
        if (makan(Tok::KwIf)) {
            lewati_asi();
            // Penjaga boleh ditulis `yen (kondisi)` maupun `yen kondisi`.
            const bool berkurung = makan(Tok::LParen);
            lewati_asi();
            NodePtr penjaga = parse_ekspresi();
            if (berkurung) {
                lewati_asi();
                aspek_ke_close(Tok::RParen, "S002", "\")\" sawise penjaga pola");
            }
            if (k->pola != nullptr) {
                auto* p = static_cast<ast::Pola*>(k->pola);
                p->penjaga = penjaga;
            }
        }
        lewati_asi();
        if (!makan(Tok::Arrow) && !makan(Tok::Colon)) {
            diagnosa_di("S001", "Nemu \"" + std::string(saat().tampilan()) +
                                    "\" sawise pola; ngarep-arep `=>` utawa `:`.");
            sinkronisasi_statement();
            continue;
        }
        lewati_asi();
        k->nilai = parse_assignment();
        k->range.selesai = peek(-1).range.selesai;
        c->kasus.push_back(k);
        lewati_asi();
        if (!makan(Tok::Comma)) lewati_asi();
    }
    aspek_ke_close(Tok::RBrace, "S011", "\"}\" penutup cocog");
    return c;
}

// ===========================================================================
// POLA
// ===========================================================================

/// Parse literal untuk dipakai sebagai pola (tanpa efek samping parsing lain).
NodePtr Parser::parse_literal_primer() {
    const std::size_t m = idx_;
    if (makan(Tok::Minus)) {
        auto* n = buat<ast::NomorLit>(rentang_dari(m));
        n->nilai = -(saat().angka);
        n->teks_mentah = "-";
        ++idx_;
        return n;
    }
    if (cek(Tok::Number)) {
        auto* n = buat<ast::NomorLit>(rentang_dari(m));
        n->nilai = saat().angka;
        n->teks_mentah = saat().teks;
        ++idx_;
        return n;
    }
    if (cek(Tok::Text)) {
        auto* t = buat<ast::TeksLit>(rentang_dari(m));
        t->nilai = std::string(saat().nilai_teks);
        ++idx_;
        return t;
    }
    return parse_literal();
}

NodePtr Parser::parse_pola() {
    const std::size_t m = idx_;
    NodePtr pertama = parse_pola_alternatif();
    if (pertama == nullptr) return nullptr;
    if (!cek(Tok::Pipe)) return pertama;
    auto* alt = buat<ast::Pola>(rentang_dari(m));
    alt->jenis = ast::Pola::Jenis::Alternatif;
    alt->alternatif.push_back(pertama);
    while (makan(Tok::Pipe)) {
        lewati_asi();
        alt->alternatif.push_back(parse_pola_alternatif());
    }
    return alt;
}

NodePtr Parser::parse_pola_alternatif() {
    const std::size_t m = idx_;

    // dhaptar: `[a, b, ...sisa]`
    if (cek(Tok::LBracket)) {
        auto* p = buat<ast::Pola>(rentang_dari(m));
        p->jenis = ast::Pola::Jenis::Dhaptar;
        ++idx_;
        lewati_asi();
        while (!cek(Tok::RBracket) && !cek(Tok::Eof) && !bag_.penuh()) {
            if (makan(Tok::Ellipsis)) {
                p->sisanya = parse_pola_alternatif();
                lewati_asi();
                break;
            }
            p->elemen.push_back(parse_pola_alternatif());
            lewati_asi();
            if (!makan(Tok::Comma)) break;
            lewati_asi();
        }
        aspek_ke_close(Tok::RBracket, "S011", "\"]\" sawise pola dhaptar");
        return p;
    }

    // objek: `{a, b: pola, ...sisa}`
    if (cek(Tok::LBrace)) {
        auto* p = buat<ast::Pola>(rentang_dari(m));
        p->jenis = ast::Pola::Jenis::Objek;
        ++idx_;
        lewati_asi();
        while (!cek(Tok::RBrace) && !cek(Tok::Eof) && !bag_.penuh()) {
            const std::size_t pm = idx_;
            auto* pp = buat<ast::PropertiPola>(rentang_dari(pm));
            if (makan(Tok::Ellipsis)) {
                pp->rest = true;
                if (cek_nama()) pp->nama = ambil_nama();
            } else {
                if (makan(Tok::LBracket)) {
                    lewati_asi();
                    pp->kunci = parse_assignment();
                    lewati_asi();
                    aspek_ke_close(Tok::RBracket, "S011", "\"]\" sawise kunci pola objek");
                } else if (cek(Tok::Text)) {
                    auto* t = buat<ast::TeksLit>(rentang_dari(pm));
                    t->nilai = std::string(saat().nilai_teks);
                    pp->kunci = t;
                    ++idx_;
                } else if (cek_nama()) {
                    pp->nama = ambil_nama();
                } else {
                    diagnosa_di("S001", "Ngarep-arep jeneng properti ing pola objek.");
                    sinkronisasi_statement();
                    break;
                }
                if (makan(Tok::Colon)) {
                    lewati_asi();
                    pp->pola = parse_pola_alternatif();
                } else if (!pp->kunci) {
                    // shorthand: pola = nama baru dengan nama itu
                    auto* nm = buat<ast::Pola>(rentang_dari(pm));
                    nm->jenis = ast::Pola::Jenis::Nama;
                    nm->nama = pp->nama;
                    nm->range = rentang_dari(pm);
                    pp->pola = nm;
                }
            }
            pp->range.selesai = peek(-1).range.selesai;
            p->properti.push_back(pp);
            lewati_asi();
            if (!makan(Tok::Comma)) break;
            lewati_asi();
        }
        aspek_ke_close(Tok::RBrace, "S011", "\"}\" sawise pola objek");
        return p;
    }

    // literal angka/teks/boolean
    if (cek(Tok::Number) || cek(Tok::Text)) {
        auto* p = buat<ast::Pola>(rentang_dari(m));
        p->jenis = ast::Pola::Jenis::Literal;
        p->nilai = parse_literal_primer();
        return p;
    }
    if (cek(Tok::Minus) && cek(1, Tok::Number)) {
        auto* p = buat<ast::Pola>(rentang_dari(m));
        p->jenis = ast::Pola::Jenis::Literal;
        ++idx_;
        p->nilai = parse_literal_primer();
        return p;
    }
    if (cek(Tok::KwTrue) || cek(Tok::KwFalse) || cek(Tok::KwNull) || cek(Tok::KwUndefined) || cek(Tok::KwTrue) ||
        cek(Tok::KwFalse) || cek(Tok::KwNull)) {
        auto* p = buat<ast::Pola>(rentang_dari(m));
        p->jenis = ast::Pola::Jenis::Literal;
        p->nilai = parse_literal_primer();
        return p;
    }

    // wildcard `_`
    if (cek(Tok::Ident) && saat().teks == "_") {
        auto* p = buat<ast::Pola>(rentang_dari(m));
        p->jenis = ast::Pola::Jenis::Wildcard;
        ++idx_;
        return p;
    }

    // `nama: Tipe` (pola bertipe) atau `nama` (binding)
    if (cek_nama()) {
        const std::string_view nama = saat().teks;
        const std::size_t pos_simpan = idx_;
        ++idx_;
        if (makan(Tok::Colon)) {
            lewati_asi();
            // pola bertipe: nama dibinding, lalu tipe
            // `Teks` / `jenis[1]` / `{| x: x }` -> `jenis` sebagai tipe
            auto* p = buat<ast::Pola>(rentang_dari(m));
            p->jenis = ast::Pola::Jenis::Tipe;
            p->nama = nama;
            p->ada_nama = true;
            if (cek(Tok::LBrace)) {
                auto* t = buat<ast::TipeAnotasi>(rentang_dari(m));
                t->nama = "objek";
                p->tipe = t;
            } else {
                p->tipe = parse_tipe();
            }
            return p;
        }
        if (makan(Tok::Pipe)) {
            idx_ = pos_simpan;  // kembali: ini adalah alternatif
        }
        auto* p = buat<ast::Pola>(rentang_dari(m));
        p->jenis = ast::Pola::Jenis::Nama;
        p->nama = nama;
        return p;
    }

    // pola seeded: nilai > 5
    if (cek(Tok::Minus) || cek(Tok::Number)) {
        auto* p = buat<ast::Pola>(rentang_dari(m));
        p->jenis = ast::Pola::Jenis::Ekspresi;
        p->nilai = parse_pangkat();
        return p;
    }

    diagnosa_di("S011", "Pola ora valid: nemu \"" + std::string(saat().tampilan()) + "\".",
                "Pola bisa: literal, nama, `_`, [a, b], {a, b}, utawa tipe.");
    return nullptr;
}

}  // namespace jawa::parse
