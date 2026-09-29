// Parser Basa Jawa bagian 2: golongan, fungsi, tipe, dan ekspresi.
//
// Dipisah dari parser.cpp agar tiap berkas tetap ramping dan mudah ditinjau.
#include <cstring>

#include "parser.h"

namespace jawa::parse {

using ast::NK;
using ast::NodePtr;
using lex::Tok;

// ===========================================================================
// GOLONGAN
// ===========================================================================

NodePtr Parser::parse_golongan() {
    const std::size_t m = idx_;
    ++idx_;  // golongan
    auto* g = buat<ast::GolonganDeklarasi>(rentang_dari(m));
    if (cek(Tok::Ident)) {
        g->nama = saat().teks;
        ++idx_;
    } else {
        diagnosa_di("S001", "Nemu \"" + std::string(saat().tampilan()) + "\" nanging ngarep-arep jeneng golongan.");
    }
    lewati_asi();
    if (cek(Tok::KwExtends) || cek(Tok::KwExtends)) {
        ++idx_;
        lewati_asi();
        g->induk = parse_call_member();
        lewati_asi();
    }
    aspek_ke_close(Tok::LBrace, "S003", "\"{\" sawise jeneng golongan");
    lewati_asi();

    while (!cek(Tok::RBrace) && !cek(Tok::Eof) && !bag_.penuh()) {
        const std::size_t sblm = idx_;
        if (makan(Tok::Semi)) { lewati_asi(); continue; }

        // --- `statis` modifier ---
        bool statis = false;
        if ((cek(Tok::KwStatic) || cek(Tok::KwStatic)) && !cek(1, Tok::LParen) && !cek(1, Tok::Eq) && !cek(1, Tok::Semi) &&
            !cek(1, Tok::RBrace)) {
            statis = true;
            ++idx_;
            lewati_asi();
        }
        if (statis && cek(Tok::LBrace)) {
            auto* blok = parse_blok();
            if (blok != nullptr) g->statis_blok.push_back(blok);
            lewati_asi();
            if (idx_ == sblm) ++idx_;
            continue;
        }

        // --- accessor (nampa / nyetel) ---
        const bool accessor_token = cek(Tok::KwGet) || cek(Tok::KwGet) || cek(Tok::KwSet) || cek(Tok::KwSet);
        if (accessor_token && (cek(1, Tok::Ident) || cek(1, Tok::PrivateName))) {
            NodePtr acc = parse_accessor_kelas();
            if (acc != nullptr) {
                static_cast<ast::PropertyAccessorDeklarasi*>(acc)->statis = statis;
                g->badan.push_back(acc);
            }
            lewati_asi();
            if (idx_ == sblm) ++idx_;
            continue;
        }

        // --- constructor (wiwit) ---
        if (cek(Tok::KwConstructor) || cek(Tok::KwConstructor)) {
            const std::size_t wm = idx_;
            ++idx_;
            auto* fn = buat<ast::FungsiDeklarasi>(rentang_dari(wm));
            fn->nama = "wiwit";
            fn->method_nama = "wiwit";
            fn->metode = true;
            parse_params(fn);
            if (cek(Tok::Colon)) { ++idx_; fn->tipe_bali = parse_tipe(); lewati_asi(); }
            fn->awak = parse_blok();
            auto* md = buat<ast::MetodeDeklarasi>(rentang_dari(wm));
            md->fungsi = fn;
            md->statis = statis;
            g->badan.push_back(md);
            lewati_asi();
            if (idx_ == sblm) ++idx_;
            continue;
        }

        // --- method privat `#nama(...)` ---
        if (cek(Tok::PrivateName) && cek(1, Tok::LParen)) {
            const std::size_t pm = idx_;
            auto* fn = buat<ast::FungsiDeklarasi>(rentang_dari(pm));
            fn->nama = saat().teks;
            fn->method_nama = fn->nama;
            fn->metode = true;
            fn->privat = true;
            ++idx_;  // #nama
            parse_params(fn);
            if (cek(Tok::Colon)) { ++idx_; fn->tipe_bali = parse_tipe(); }
            fn->awak = parse_blok();
            auto* md = buat<ast::MetodeDeklarasi>(rentang_dari(pm));
            md->fungsi = fn;
            md->privat = true;
            md->statis = statis;
            g->badan.push_back(md);
            lewati_asi();
            if (idx_ == sblm) ++idx_;
            continue;
        }

        // --- method (termasuk mengko & generator) ---
        if (cek(Tok::Ident) && cek(1, Tok::LParen)) {
            NodePtr met = parse_metode_kelas();
            if (met != nullptr) {
                static_cast<ast::MetodeDeklarasi*>(met)->statis = statis;
                g->badan.push_back(met);
            }
            lewati_asi();
            if (idx_ == sblm) ++idx_;
            continue;
        }
        if (cek(Tok::Star) && cek(1, Tok::Ident) && cek(2, Tok::LParen)) {
            ++idx_;  // *
            NodePtr met = parse_metode_kelas();
            if (met != nullptr) {
                static_cast<ast::FungsiDeklarasi*>(static_cast<ast::MetodeDeklarasi*>(met)->fungsi)->generator = true;
                g->badan.push_back(met);
            }
            lewati_asi();
            if (idx_ == sblm) ++idx_;
            continue;
        }
        if (cek(Tok::KwAsync) && (cek(1, Tok::Ident) || cek(1, Tok::Star))) {
            const std::size_t am = idx_;
            ++idx_;  // mengko
            lewati_asi();
            bool gen = makan(Tok::Star);
            if (cek(Tok::Ident)) {
                auto* fn = buat<ast::FungsiDeklarasi>(rentang_dari(am));
                fn->nama = saat().teks;
                fn->method_nama = fn->nama;
                fn->metode = true;
                fn->mengko = true;
                fn->generator = gen;
                ++idx_;
                parse_params(fn);
                if (cek(Tok::Colon)) { ++idx_; fn->tipe_bali = parse_tipe(); lewati_asi(); }
                fn->awak = parse_blok();
                auto* md = buat<ast::MetodeDeklarasi>(rentang_dari(am));
                md->fungsi = fn;
                md->statis = statis;
                g->badan.push_back(md);
            } else {
                diagnosa_di("S001", "Ngarep-arep jeneng method mengko.");
                sinkronisasi_statement();
            }
            lewati_asi();
            if (idx_ == sblm) ++idx_;
            continue;
        }

        // --- field ---
        NodePtr f = parse_field_kelas();
        if (f != nullptr) {
            static_cast<ast::FieldKelas*>(f)->statis = statis;
            g->badan.push_back(f);
        } else {
            sinkronisasi_statement();
        }
        lewati_asi();
        if (idx_ == sblm) ++idx_;
    }
    aspek_ke_close(Tok::RBrace, "S003", "\"}\" penutup golongan");
    return g;
}

NodePtr Parser::parse_field_kelas() {
    const std::size_t m = idx_;
    auto* f = buat<ast::FieldKelas>(rentang_dari(m));
    if (cek(Tok::PrivateName)) {
        f->privat = true;
        f->nama = saat().teks;  // termasuk '#'
        ++idx_;
    } else if (cek(Tok::LBracket)) {
        f->komputat = true;
        ++idx_;
        lewati_asi();
        f->kunci = parse_assignment();
        lewati_asi();
        aspek_ke_close(Tok::RBracket, "S002", "\"]\" sawise kunci komputat");
    } else if (cek(Tok::Ident)) {
        f->nama = saat().teks;
        ++idx_;
    } else {
        diagnosa_di("S001", "Nemu \"" + std::string(saat().tampilan()) + "\" nanging ngarep-arep jeneng field class.");
        return nullptr;
    }
    tipe_annotation(f->tipe);
    lewati_asi();
    if (makan(Tok::Eq)) f->nilai = parse_assignment();
    return f;
}

NodePtr Parser::parse_metode_kelas() {
    const std::size_t m = idx_;
    auto* fn = buat<ast::FungsiDeklarasi>(rentang_dari(m));
    fn->nama = saat().teks;
    fn->method_nama = fn->nama;
    fn->metode = true;
    if (makan(Tok::Star)) fn->generator = true;
    ++idx_;  // nama
    parse_params(fn);
    if (cek(Tok::Colon)) { ++idx_; fn->tipe_bali = parse_tipe(); lewati_asi(); }
    fn->awak = parse_blok();
    auto* md = buat<ast::MetodeDeklarasi>(rentang_dari(m));
    md->fungsi = fn;
    return md;
}

NodePtr Parser::parse_accessor_kelas() {
    const std::size_t m = idx_;
    const bool getter = cek(Tok::KwGet) || cek(Tok::KwGet);
    ++idx_;
    auto* acc = buat<ast::PropertyAccessorDeklarasi>(rentang_dari(m));
    acc->getter = getter;
    acc->setter = !getter;
    if (cek(Tok::PrivateName)) {
        acc->privat = true;
        acc->nama = saat().teks;
        ++idx_;
    } else if (cek(Tok::Ident)) {
        acc->nama = saat().teks;
        ++idx_;
    } else if (makan(Tok::LBracket)) {
        acc->komputat = true;
        lewati_asi();
        acc->kunci = parse_assignment();
        lewati_asi();
        aspek_ke_close(Tok::RBracket, "S002", "\"]\" sawise kunci accessor");
    } else {
        diagnosa_di("S001", "Ngarep-arep jeneng accessor.");
        return nullptr;
    }

    auto* fn = buat<ast::FungsiDeklarasi>(rentang_dari(m));
    fn->nama = acc->nama;
    fn->method_nama = acc->nama;
    fn->metode = true;
    fn->getter = getter;
    fn->setter = !getter;

    if (makan(Tok::LParen)) {
        lewati_asi();
        if (!cek(Tok::RParen)) {
            auto* p = buat<ast::ParamDeklarasi>(saat().range);
            if (cek(Tok::Ident)) { p->nama = saat().teks; ++idx_; }
            tipe_annotation(p->tipe);
            p->range.selesai = peek(-1).range.selesai;
            fn->param.push_back(p);
        }
        lewati_asi();
        aspek_ke_close(Tok::RParen, "S002", "\")\" sawise parametru accessor");
    } else {
        // getter tanpa kurung: `nampa jeneng { ... }`
        if (!getter) {
            diagnosa_di("S001", "Setter tanpa kurung ora sah.", "Tulis `nyetel jeneng(nilai) { ... }`.");
        }
    }
    lewati_asi();
    fn->awak = parse_blok();
    acc->fungsi = fn;
    return acc;
}

NodePtr Parser::parse_deklarasi_fungsi() {
    const std::size_t m = idx_;
    const bool mengko = makan(Tok::KwAsync) || makan(Tok::KwAsync);
    if (!makan(Tok::KwFunction)) {
        diagnosa_di("S001", "Ngarep-arep kata kunci gawe.");
        sinkronisasi_statement();
        return nullptr;
    }
    const bool gen = makan(Tok::Star);
    auto* fn = buat<ast::FungsiDeklarasi>(rentang_dari(m));
    fn->mengko = mengko;
    fn->generator = gen;
    if (cek(Tok::Ident)) {
        fn->nama = saat().teks;
        ++idx_;
        // Parameter generics sederhana: `gawe petakan<T, U>(...)`
        if (makan(Tok::Lt)) {
            while (!cek(Tok::Gt) && !cek(Tok::Eof) && !bag_.penuh()) {
                if (cek(Tok::Ident)) ++idx_;
                if (!makan(Tok::Comma)) break;
            }
            aspek_ke_close(Tok::Gt, "S002", "\">\" sawise parameter generics");
        }
    } else if (cek(Tok::LBracket) || cek(Tok::LBrace)) {
        // `gawe [a,b](x) {}` / `gawe {a}(x) {}` — destruktur nama
        // tangani sebagai ekspresi: biarkan parse_ekspresi yang menangani
        idx_ = m;
        return parse_statement();
    }
    parse_params(fn);
    if (cek(Tok::Colon)) { ++idx_; fn->tipe_bali = parse_tipe(); lewati_asi(); }
    lewati_asi();
    fn->awak = parse_blok();
    return fn;
}

NodePtr Parser::parse_antarmuka() {
    const std::size_t m = idx_;
    ++idx_;  // "antarmuka" (di-tokenize sebagai Ident)
    auto* itf = buat<ast::AntarmukaDeklarasi>(rentang_dari(m));
    if (cek(Tok::Ident)) {
        itf->nama = saat().teks;
        ++idx_;
    }
    aspek_ke_close(Tok::LBrace, "S003", "\"{\" sawise jeneng antarmuka");
    lewati_asi();
    while (!cek(Tok::RBrace) && !cek(Tok::Eof) && !bag_.penuh()) {
        const std::size_t sblm = idx_;
        if (makan(Tok::Semi)) { lewati_asi(); continue; }
        if (cek(Tok::Ident) && cek(1, Tok::LParen)) {
            NodePtr met = parse_metode_kelas();
            if (met) itf->badan.push_back(met);
        } else {
            NodePtr f = parse_field_kelas();
            if (f) itf->badan.push_back(f);
            else sinkronisasi_statement();
        }
        lewati_asi();
        if (idx_ == sblm) ++idx_;
    }
    aspek_ke_close(Tok::RBrace, "S003", "\"}\" penutup antarmuka");
    return itf;
}

// ===========================================================================
// TIPE (anotasi bertahap)
// ===========================================================================

void Parser::tipe_annotation(Node*& keluar) {
    if (!makan(Tok::Colon)) return;
    keluar = parse_tipe();
    lewati_asi();
}

NodePtr Parser::parse_tipe() {
    const std::size_t m = idx_;
    NodePtr hasil = nullptr;

    // Tipe fungsi: `(T, U) => R`
    if (cek(Tok::LParen)) {
        ++idx_;
        auto* tf = buat<ast::TipeFungsi>(rentang_dari(m));
        lewati_asi();
        while (!cek(Tok::RParen) && !cek(Tok::Eof) && !bag_.penuh()) {
            tf->parameter.push_back(parse_tipe());
            lewati_asi();
            if (!makan(Tok::Comma)) break;
            lewati_asi();
        }
        aspek_ke_close(Tok::RParen, "S002", "\")\" sawise parameter tipe fungsi");
        if (makan(Tok::Arrow)) {
            lewati_asi();
            tf->bali = parse_tipe();
        }
        return tf;
    }

    if (cek(Tok::KwAny)) {
        ++idx_;
        auto* t = buat<ast::TipeAnotasi>(rentang_dari(m));
        t->nama = "apa_wae";
        return t;
    }
    if (cek(Tok::KwNever)) {
        ++idx_;
        auto* t = buat<ast::TipeAnotasi>(rentang_dari(m));
        t->nama = "ora_tau";
        return t;
    }

    // Tipe bawaan yang berupa kata kunci: `kosong`, `mboh`.
    if (cek(Tok::KwNull) || cek(Tok::KwUndefined)) {
        const char* nama_tipe = cek(Tok::KwNull) ? "kosong" : "mboh";
        ++idx_;
        auto* t = buat<ast::TipeAnotasi>(rentang_dari(m));
        t->nama = nama_tipe;
        hasil = t;
    } else if (cek(Tok::Ident)) {
        auto* t = buat<ast::TipeAnotasi>(rentang_dari(m));
        t->nama = saat().teks;
        ++idx_;
        hasil = t;
        // tipe generik sederhana `dhaptar<T>`
        if (cek(Tok::Lt)) {
            ++idx_;
            std::vector<NodePtr> arg;
            lewati_asi();
            if (!cek(Tok::Gt)) {
                for (;;) {
                    lewati_asi();
                    arg.push_back(parse_tipe());
                    lewati_asi();
                    if (!makan(Tok::Comma)) break;
                }
            }
            aspek_ke_close(Tok::Gt, "S002", "\">\" sawise argumen tipe");
            if (t->nama == "dhaptar" && !arg.empty()) {
                auto* arr = buat<ast::TipeArray>(rentang_dari(m));
                arr->elemen = arg[0];
                return arr;
            }
            auto* ref = buat<ast::TipeReferensi>(rentang_dari(m));
            ref->nama = t->nama;
            ref->argumen = arg;
            return ref;
        }
    } else {
        diagnosa_di("S001", "Nemu \"" + std::string(saat().tampilan()) + "\" nanging ngarep-arep anotasi tipe.");
        return nullptr;
    }

    // postfix: `[]` (dhaptar), `?` (opsional)
    for (;;) {
        if (cek(Tok::LBracket) && cek(1, Tok::RBracket)) {
            idx_ += 2;
            auto* arr = buat<ast::TipeArray>(rentang_dari(m));
            arr->elemen = hasil;
            hasil = arr;
            continue;
        }
        if (cek(Tok::Question)) {
            ++idx_;
            auto* opt = buat<ast::TipeOpsional>(rentang_dari(m));
            opt->dasar = hasil;
            hasil = opt;
            continue;
        }
        break;
    }

    // union `|`
    if (cek(Tok::Pipe)) {
        auto* uni = buat<ast::TipeUnion>(rentang_dari(m));
        uni->varian.push_back(hasil);
        while (makan(Tok::Pipe)) {
            lewati_asi();
            uni->varian.push_back(parse_tipe());
        }
        return uni;
    }
    return hasil;
}

}  // namespace jawa::parse
