// Parser Basa Jawa bagian 4: primary, literal, objek, dhaptar, template, cocog.
#include <cstring>

#include "parser.h"

namespace jawa::parse {

using ast::NK;
using ast::NodePtr;
using lex::Tok;

// ===========================================================================
// PRIMARY
// ===========================================================================

NodePtr Parser::parse_primary() {
    const std::size_t m = idx_;
    switch (jenis_sekarang()) {
        case Tok::Number: {
            auto* n = buat<ast::NomorLit>(rentang_dari(m));
            n->nilai = saat().angka;
            n->teks_mentah = saat().teks;
            ++idx_;
            return n;
        }
        case Tok::BigInt: {
            auto* n = buat<ast::BigIntLit>(rentang_dari(m));
            n->digit = saat().bigint_teks;
            n->radix = 10;
            const std::string_view d = n->digit;
            if (d.size() > 1 && d[0] == '0') {
                if (d[1] == 'x' || d[1] == 'X') n->radix = 16;
                else if (d[1] == 'o' || d[1] == 'O') n->radix = 8;
                else if (d[1] == 'b' || d[1] == 'B') n->radix = 2;
            }
            ++idx_;
            return n;
        }
        case Tok::Text: {
            auto* t = buat<ast::TeksLit>(rentang_dari(m));
            t->nilai = std::string(saat().nilai_teks);
            ++idx_;
            return t;
        }
        case Tok::TemplateText: return parse_template();
        case Tok::Regex: {
            auto* r = buat<ast::RegexLit>(rentang_dari(m));
            r->pola = std::string(saat().regex_pola);
            r->flag = std::string(saat().regex_flag);
            ++idx_;
            return r;
        }
        case Tok::LParen: {
            ++idx_;
            lewati_asi();
            NodePtr e = parse_ekspresi();
            lewati_asi();
            aspek_ke_close(Tok::RParen, "S002", "\")\" sawise kurung");
            return e;
        }
        case Tok::LBracket: return parse_array_literal();
        case Tok::LBrace: return parse_objek_literal();
        case Tok::KwThis: {
            auto* t = buat<ast::ThisExpr>(rentang_dari(m));
            ++idx_;
            return t;
        }
        case Tok::KwSuper: {
            auto* t = buat<ast::SuperExpr>(rentang_dari(m));
            ++idx_;
            return t;
        }
        case Tok::KwNew: {
            ++idx_;
            lewati_asi();
            auto* n = buat<ast::AnyarExpr>(rentang_dari(m));
            n->konstruktor = parse_konstruktor();
            // `anyaar Foo.bar` juga boleh, lalu argumen mengikuti.
            if (makan(Tok::LParen)) {
                n->ada_argumen = true;
                parse_argumen(n->argumen, m);
            }
            // `anyaar Foo(1, 2)` bisa ter-parse sebagai `anyaar` + `Panggilan`.
            // Buka pemanggilan tersebut agar argumen jadi milik `anyaar`, bukan panggilan.
            if (n->argumen.empty() && n->konstruktor != nullptr && n->konstruktor->kind == NK::Panggilan) {
                auto* panggil = static_cast<ast::Panggilan*>(n->konstruktor);
                n->konstruktor = panggil->callee;
                n->argumen = panggil->argumen;
                n->ada_argumen = true;
            }
            // Rantai member setelah `anyaar`: `anyaar Foo(1).bar.baz()`.
            return lanjut_member(n, m);
        }
        case Tok::KwFunction: return parse_deklarasi_fungsi();
        case Tok::KwImport: {
            // `impor("path")` — impor dinamis sebagai ekspresi.
            const std::size_t dm = idx_;
            ++idx_;
            auto* dyn = buat<ast::ImporDinamisExpr>(rentang_dari(dm));
            lewati_asi();
            aspek_ke_close(Tok::LParen, "S001", "\"(\" sawise impor dinamis");
            lewati_asi();
            dyn->spesifikasi = parse_ekspresi();
            lewati_asi();
            aspek_ke_close(Tok::RParen, "S002", "\")\" sawise impor dinamis");
            return dyn;
        }
        case Tok::Ident: {
            if (cek(1, Tok::Arrow)) {
                auto* fn = buat<ast::FungsiDeklarasi>(rentang_dari(m));
                fn->panah = true;
                auto* par = buat<ast::ParamDeklarasi>(saat().range);
                par->nama = saat().teks;
                fn->param.push_back(par);
                idx_ += 2;
                lewati_asi();
                if (cek(Tok::LBrace)) {
                    fn->awak = parse_blok();
                } else {
                    fn->ekspresi_badan = true;
                    fn->badan_ekspresi = parse_assignment();
                }
                return fn;
            }
            auto* r = buat<ast::RefIdent>(rentang_dari(m));
            r->nama = saat().teks;
            ++idx_;
            return r;
        }
        default: break;
    }
    if (NodePtr lit = parse_literal(); lit != nullptr) return lit;

    diagnosa_di("S001", "Nemu \"" + std::string(saat().tampilan()) + "\" nanging ngarep-arep ekspresi.",
                "Coba bungkus ekspresi nganggo kurung, utawa priksa operator sing didol disabane.");
    sinkronisasi_statement();
    return nullptr;
}

// ===========================================================================
// LITERAL: bener / salah / kosong / mboh / DuduAngka / Tak_Wates
// ===========================================================================

NodePtr Parser::parse_literal() {
    const std::size_t m = idx_;
    switch (jenis_sekarang()) {
        case Tok::KwTrue: {
            auto* n = buat<ast::NomorLit>(rentang_dari(m));
            n->nilai = 1.0;
            n->bentuk = ast::NomorLit::Bentuk::Bener;
            n->teks_mentah = "bener";
            ++idx_;
            return n;
        }
        case Tok::KwFalse: {
            auto* n = buat<ast::NomorLit>(rentang_dari(m));
            n->nilai = 0.0;
            n->bentuk = ast::NomorLit::Bentuk::Salah;
            n->teks_mentah = "salah";
            ++idx_;
            return n;
        }
        case Tok::KwNull: {
            auto* n = buat<ast::NomorLit>(rentang_dari(m));
            n->nilai = 0.0;
            n->teks_mentah = "kosong";
            n->range = saat().range;
            n->bentuk = ast::NomorLit::Bentuk::Kosong;
            ++idx_;
            return n;
        }
        case Tok::KwUndefined: {
            auto* n = buat<ast::NomorLit>(rentang_dari(m));
            n->teks_mentah = "mboh";
            n->bentuk = ast::NomorLit::Bentuk::Mboh;
            ++idx_;
            return n;
        }
        case Tok::KwNaNIdent: {
            auto* n = buat<ast::NomorLit>(rentang_dari(m));
            n->teks_mentah = "DuduAngka";
            n->bentuk = ast::NomorLit::Bentuk::NaN;
            ++idx_;
            return n;
        }
        case Tok::KwInfinityIdent: {
            auto* n = buat<ast::NomorLit>(rentang_dari(m));
            n->teks_mentah = "Tak_Wates";
            n->bentuk = ast::NomorLit::Bentuk::Infinity;
            ++idx_;
            return n;
        }
        default: return nullptr;
    }
}

// ===========================================================================
// DHAPTAR
// ===========================================================================

NodePtr Parser::parse_array_literal() {
    const std::size_t m = idx_;
    auto* arr = buat<ast::ArrayLit>(rentang_dari(m));
    aspek_ke_close(Tok::LBracket, "S001", "\"[\"");
    lewati_asi();
    while (!cek(Tok::RBracket) && !cek(Tok::Eof) && !bag_.penuh()) {
        auto* el = buat<ast::ElemenArr>(rentang_dari(m));
        if (makan(Tok::Ellipsis)) {
            el->spread = true;
            // `...liyane` — nama rest boleh kata kunci (DECISIONS.md D-013),
            // tapi hanya bila token berikutnya tidak bisa memulai pemanggilan.
            if (cek_nama() && (cek(1, Tok::Comma) || cek(1, Tok::RBracket) || cek(1, Tok::RBrace))) {
                auto* r = buat<ast::RefIdent>(rentang_dari(m));
                r->nama = ambil_nama();
                el->nilai = r;
            } else {
                el->nilai = parse_assignment();
            }
        } else {
            el->nilai = parse_assignment();
        }
        el->range.selesai = peek(-1).range.selesai;
        arr->elemen.push_back(el);
        lewati_asi();
        if (!makan(Tok::Comma)) break;
        lewati_asi();
    }
    aspek_ke_close(Tok::RBracket, "S002", "\"]\" sawise dhaptar");
    return arr;
}

// ===========================================================================
// OBJEK
// ===========================================================================

NodePtr Parser::parse_objek_literal() {
    const std::size_t m = idx_;
    auto* obj = buat<ast::ObjectLit>(rentang_dari(m));
    aspek_ke_close(Tok::LBrace, "S001", "\"{\"");
    lewati_asi();
    while (!cek(Tok::RBrace) && !cek(Tok::Eof) && !bag_.penuh()) {
        const std::size_t pm = idx_;
        auto* p = buat<ast::PropertiObj>(rentang_dari(pm));

        if (makan(Tok::Ellipsis)) {
            p->jenis = ast::PropertiObj::Jenis::Spread;
            // `...liyane` — nama rest boleh berupa kata kunci (lihat DECISIONS.md D-013).
            if (cek_nama()) {
                auto* r = buat<ast::RefIdent>(rentang_dari(pm));
                r->nama = ambil_nama();
                p->computed = r;
            } else {
                p->computed = parse_assignment();
            }
            p->range.selesai = peek(-1).range.selesai;
            obj->properti.push_back(p);
            lewati_asi();
            if (!makan(Tok::Comma)) break;
            lewati_asi();
            continue;
        }

        // accessor nampa / nyetel
        if ((cek(Tok::KwGet) || cek(Tok::KwGet) || cek(Tok::KwSet) || cek(Tok::KwSet)) &&
            (cek(1, Tok::Ident) || cek(1, Tok::Text) || cek(1, Tok::LBracket))) {
            const bool getter = cek(Tok::KwGet) || cek(Tok::KwGet);
            ++idx_;
            lewati_asi();
            p->jenis = ast::PropertiObj::Jenis::Accessor;
            p->getter = getter;
            p->setter = !getter;
            if (makan(Tok::LBracket)) {
                lewati_asi();
                p->jenis = ast::PropertiObj::Jenis::Komputat;
                p->kunci = parse_assignment();
                lewati_asi();
                aspek_ke_close(Tok::RBracket, "S002", "\"]\" sawise kunci accessor");
            } else if (cek(Tok::Text)) {
                p->kunci_nama = saat().nilai_teks;
                ++idx_;
            } else if (cek_nama()) {
                p->kunci_nama = ambil_nama();
            } else {
                diagnosa_di("S001", "Ngarep-arep jeneng accessor.");
                sinkronisasi_statement();
                break;
            }
            // badan accessor
            auto* fn = buat<ast::FungsiDeklarasi>(rentang_dari(pm));
            fn->nama = p->kunci_nama;
            fn->method_nama = p->kunci_nama;
            fn->metode = true;
            fn->getter = getter;
            fn->setter = !getter;
            if (makan(Tok::LParen)) {
                lewati_asi();
                if (!cek(Tok::RParen)) {
                            auto* par = buat<ast::ParamDeklarasi>(saat().range);
                    if (cek(Tok::Ident)) { par->nama = saat().teks; ++idx_; }
                    par->range.selesai = peek(-1).range.selesai;
                    fn->param.push_back(par);
                }
                lewati_asi();
                aspek_ke_close(Tok::RParen, "S002", "\")\" sawise parametru accessor");
            }
            lewati_asi();
            fn->awak = parse_blok();
            p->nilai = fn;
            p->range.selesai = peek(-1).range.selesai;
            obj->properti.push_back(p);
            lewati_asi();
            if (!makan(Tok::Comma)) break;
            lewati_asi();
            continue;
        }

        // kunci
        bool punya_nilai = true;
        if (makan(Tok::LBracket)) {
            lewati_asi();
            p->jenis = ast::PropertiObj::Jenis::Komputat;
            p->kunci = parse_assignment();
            lewati_asi();
            aspek_ke_close(Tok::RBracket, "S002", "\"]\" sawise kunci komputat");
        } else if (cek(Tok::Text)) {
            p->jenis = ast::PropertiObj::Jenis::Nilai;
            p->kunci_nama = saat().nilai_teks;
            ++idx_;
        } else if (cek(Tok::PrivateName)) {
            p->jenis = ast::PropertiObj::Jenis::Nilai;
            p->privat = true;
            p->kunci_nama = saat().teks;  // termasuk '#'
            ++idx_;
        } else if (cek_nama()) {
            p->jenis = ast::PropertiObj::Jenis::Nilai;
            p->kunci_nama = ambil_nama();
        } else {
            diagnosa_di("S001", "Ngarep-arep jeneng properti, nanging nemu \"" + std::string(saat().tampilan()) + "\".");
            sinkronisasi_statement();
            break;
        }
        lewati_asi();

        // method
        if (cek(Tok::LParen)) {
            auto* fn = buat<ast::FungsiDeklarasi>(rentang_dari(pm));
            fn->nama = p->kunci_nama;
            fn->method_nama = p->kunci_nama;
            fn->metode = true;
            ++idx_;  // '('
            if (makan(Tok::Star)) fn->generator = true;
            lewati_asi();
            // param
            while (!cek(Tok::RParen) && !cek(Tok::Eof) && !bag_.penuh()) {
                auto* par = buat<ast::ParamDeklarasi>(saat().range);
                if (makan(Tok::Ellipsis)) par->rest = true;
                if (cek_nama()) {
                    par->nama = ambil_nama();
                } else if (cek(Tok::LBrace) || cek(Tok::LBracket)) {
                    par->destructuring = true;
                    par->pola = cek(Tok::LBrace) ? parse_objek_literal() : parse_array_literal();
                } else {
                    diagnosa_di("S001", "Ngarep-arep jeneng parametru method.");
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
            aspek_ke_close(Tok::RParen, "S002", "\")\" sawise parametru method");
            lewati_asi();
            if (cek(Tok::Colon)) { ++idx_; fn->tipe_bali = parse_tipe(); lewati_asi(); }
            fn->awak = parse_blok();
            p->nilai = fn;
            p->jenis = ast::PropertiObj::Jenis::Metode;
            p->range.selesai = peek(-1).range.selesai;
            obj->properti.push_back(p);
            lewati_asi();
            if (!makan(Tok::Comma)) break;
            lewati_asi();
            continue;
        }

        // `nampa nama` (getter tanpa kurung) & shorthand
        if ((cek(Tok::KwGet) || cek(Tok::KwGet) || cek(Tok::KwSet) || cek(Tok::KwSet)) &&
            (cek(1, Tok::LParen) || cek(1, Tok::LBrace))) {
            p->jenis = ast::PropertiObj::Jenis::Accessor;
            p->getter = cek(Tok::KwGet) || cek(Tok::KwGet);
            p->setter = !p->getter;
            ++idx_;
            auto* fn = buat<ast::FungsiDeklarasi>(rentang_dari(pm));
            fn->nama = p->kunci_nama;
            fn->method_nama = p->kunci_nama;
            fn->metode = true;
            fn->getter = p->getter;
            fn->setter = p->setter;
            lewati_asi();
            if (makan(Tok::LParen)) {
                lewati_asi();
                if (!cek(Tok::RParen)) {
                    auto* par = buat<ast::ParamDeklarasi>(saat().range);
                    if (cek(Tok::Ident)) { par->nama = saat().teks; ++idx_; }
                    par->range.selesai = peek(-1).range.selesai;
                    fn->param.push_back(par);
                }
                lewati_asi();
                aspek_ke_close(Tok::RParen, "S002", "\")\" sawise parametru accessor");
            }
            lewati_asi();
            fn->awak = parse_blok();
            p->nilai = fn;
            p->range.selesai = peek(-1).range.selesai;
            obj->properti.push_back(p);
            lewati_asi();
            if (!makan(Tok::Comma)) break;
            lewati_asi();
            continue;
        }

        if (makan(Tok::Colon)) {
            lewati_asi();
            p->jenis = ast::PropertiObj::Jenis::Nilai;
            p->nilai = parse_assignment();
        } else if (makan(Tok::Eq)) {
            // `nama = nilai`: dipakai untuk destructuring dengan nilai bawaan,
            // contoh `({jeneng, umur = 0}) => ...`.
            lewati_asi();
            p->jenis = ast::PropertiObj::Jenis::Nilai;
            auto* r = buat<ast::RefIdent>(rentang_dari(pm));
            r->nama = p->kunci_nama;
            r->range = rentang_dari(pm);
            p->nilai = r;
            p->computed = parse_assignment();  // nilai bawaan
            p->setter = true;                  // tandai "shorthand dengan default"
        } else {
            // shorthand: `{a}` -> `a: a`
            p->jenis = ast::PropertiObj::Jenis::Shorthand;
            auto* r = buat<ast::RefIdent>(rentang_dari(pm));
            r->nama = p->kunci_nama;
            p->nilai = r;
            punya_nilai = false;
        }
        (void)punya_nilai;
        p->range.selesai = peek(-1).range.selesai;
        obj->properti.push_back(p);
        lewati_asi();
        if (!makan(Tok::Comma)) break;
        lewati_asi();
    }
    aspek_ke_close(Tok::RBrace, "S003", "\"}\" sawise objek");
    return obj;
}

}  // namespace jawa::parse
