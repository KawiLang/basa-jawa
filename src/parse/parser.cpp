#include "parser.h"

#include <cctype>
#include <cstring>

namespace jawa::parse {

using ast::NK;
using ast::NodePtr;
using lex::Tok;

// ===========================================================================
// TOKEN
// ===========================================================================

Parser::Parser(lex::TokenList& token, support::Arena& arena, support::DiagnosticBag& bag, std::string_view nama_berkas)
    : token_(token), arena_(arena), bag_(bag), nama_berkas_(nama_berkas) {}

const lex::Token& Parser::peek(int n) const noexcept {
    if (n < 0) n = 0;
    const std::size_t i = idx_ + static_cast<std::size_t>(n);
    return i < token_.token.size() ? token_.token[i] : token_.token.back();
}

lex::Tok Parser::jenis(int n) const noexcept { return peek(n).jenis; }

bool Parser::makan(Tok t) noexcept {
    if (jenis_sekarang() == t) {
        ++idx_;
        return true;
    }
    return false;
}

void Parser::lewati_asi() noexcept {
    while (cek(Tok::Semi)) ++idx_;
}

void Parser::diagnosa(const char* kode, std::string pesan, SourceRange r, std::string saran) {
    support::Diagnostic d;
    d.code = kode;
    d.kind = support::DiagKind::Sintaks;
    d.level = support::DiagLevel::Galat;
    d.berkas = nama_berkas_;
    d.pos = r.mulai;
    d.pesan = std::move(pesan);
    d.saran = std::move(saran);
    bag_.add_galat(std::move(d));
}

void Parser::diagnosa_di(const char* kode, std::string pesan, std::string saran) {
    diagnosa(kode, std::move(pesan), saat().range, std::move(saran));
}

void Parser::aspek_ke_close(Tok t, const char* kode, std::string_view apa) {
    if (makan(t)) return;
    if (cek(Tok::Eof)) {
        diagnosa("S006", "Program cendhek: ngarep-arep " + std::string(apa) + " nanging program wis selesai.", saat().range,
                 "Lengkapi program.");
        return;
    }
    diagnosa(kode,
             "Ngarep-arep " + std::string(apa) + " nanging nemu \"" + std::string(saat().tampilan()) + "\".",
             saat().range, "Sastra sing ana ing kono tempat: " + std::string(lex::token_name(jenis_sekarang())) + ".");
}

bool Parser::token_bisa_nama() const noexcept {
    if (jenis_sekarang() == Tok::Ident) return true;
    return lex::is_reserved_keyword(jenis_sekarang()) || lex::is_contextual_keyword(jenis_sekarang());
}

bool Parser::cek_nama(int n) const noexcept {
    const Tok t = jenis(n);
    if (t == Tok::Ident) return true;
    return lex::is_reserved_keyword(t) || lex::is_contextual_keyword(t);
}

std::string_view Parser::ambil_nama() {
    const std::string_view n = saat().teks;
    ++idx_;
    return n;
}

SourceRange Parser::rentang_dari(std::size_t mulai) const {
    SourceRange r;
    const std::size_t im = mulai < token_.token.size() ? mulai : 0;
    r.mulai = token_.token[im].range.mulai;
    r.selesai = token_.token[idx_ > 0 ? idx_ - 1 : 0].range.selesai;
    if (r.selesai.offset < r.mulai.offset) r.selesai = r.mulai;
    return r;
}

// ===========================================================================
// PROGRAM
// ===========================================================================

NodePtr Parser::parse_program() {
    const std::size_t m = idx_;
    auto* prog = buat<ast::Program>(rentang_dari(m));
    prog->nama_berkas = nama_berkas_;
    lewati_asi();
    while (!cek(Tok::Eof) && !bag_.penuh()) {
        const std::size_t sebelum = idx_;
        NodePtr s = parse_statement();
        if (s != nullptr) prog->body.push_back(s);
        lewati_asi();
        if (idx_ == sebelum) ++idx_;  // pengaman: selalu maju
    }
    prog->range.selesai = saat().range.selesai;
    return prog;
}

NodePtr Parser::parse_ekspresi_tunggal() {
    NodePtr e = parse_ekspresi();
    lewati_asi();
    if (!cek(Tok::Eof)) {
        diagnosa_di("S002", "Nemu \"" + std::string(saat().tampilan()) + "\" sanwise program rika ndelesai.",
                    "Bungkus lebih dari satu ekspresi nganggo kurung kurawal utawa koma.");
    }
    return e;
}

// ===========================================================================
// SINKRONISASI
// ===========================================================================

void Parser::sinkronisasi_statement() {
    int kurang_kurawal = 0;
    while (!cek(Tok::Eof)) {
        if (kurang_kurawal <= 0 && cek(Tok::Semi)) { ++idx_; return; }
        switch (jenis_sekarang()) {
            case Tok::RBrace:
                if (kurang_kurawal == 0) return;
                --kurang_kurawal;
                break;
            case Tok::LBrace: ++kurang_kurawal; break;
            case Tok::KwIf: case Tok::KwFor: case Tok::KwWhile: case Tok::KwFunction:
            case Tok::KwClass: case Tok::KwDo: case Tok::KwTry: case Tok::KwReturn:
            case Tok::KwExport: case Tok::KwImport: case Tok::KwBreak: case Tok::KwThrow:
                return;
            default: break;
        }
        ++idx_;
    }
}

void Parser::parse_params(ast::FungsiDeklarasi* fn) {
    aspek_ke_close(Tok::LParen, "S001", "\"(\"");
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
            diagnosa_di("S001", "Ngarep-arep jeneng parametru, nanging nemu \"" + std::string(saat().tampilan()) + "\".");
            sinkronisasi_statement();
            break;
        }
        tipe_annotation(par->tipe);
        if (makan(Tok::Eq)) par->nilai_default = parse_assignment();
        par->range.selesai = peek(-1).range.selesai;
        fn->param.push_back(par);
        lewati_asi();
        if (!makan(Tok::Comma)) break;
        lewati_asi();
    }
    aspek_ke_close(Tok::RParen, "S002", "\")\" penutup daftar parametru");
    lewati_asi();
}

// ===========================================================================
// STATEMENT
// ===========================================================================

bool Parser::masuk_kedalaman() {
    if (kedalaman_ >= kKedalamanMaks) {
        // Diagnostik hanya sekali, tepat saat batas pertama tercapai, supaya
        // program yang bersarang terlalu dalam tidak membanjiri bag (yang punya
        // batas sendiri) dan supaya pelaporannya menunjuk ke tempat yang
        // sebenarnya terlalu dalam.
        if (kedalaman_ == kKedalamanMaks) {
            diagnosa_di("S002",
                        "Nestoring program kelewat dangkal (batas " +
                            std::to_string(kKedalamanMaks) + " tingkat).",
                        "Sederhanakan ekspresi, atau pecah jadi beberapa fungsi.");
        }
        return false;
    }
    ++kedalaman_;
    return true;
}

NodePtr Parser::parse_statement() {
    const RakKedalaman guard(this);
    if (!guard.aktif) {
        sinkronisasi_statement();
        return nullptr;
    }
    lewati_asi();
    const std::size_t m = idx_;

    switch (jenis_sekarang()) {
        case Tok::LBrace: return parse_blok();
        case Tok::Semi: {
            ++idx_;
            return buat<ast::KosongStmt>(rentang_dari(m));
        }
        case Tok::KwIf: return parse_yen();
        case Tok::KwWhile: return parse_nalika();
        case Tok::KwDo: return parse_lakoni();
        case Tok::KwFor: return parse_kanggo();
        case Tok::KwSwitch: return parse_pilih();
        case Tok::KwTry: return parse_coba();
        case Tok::KwClass: return parse_golongan();
        case Tok::KwExport: return parse_ekspor();
        case Tok::KwImport: return parse_impor();
        case Tok::KwFunction: return parse_deklarasi_fungsi();
        case Tok::KwAsync: {
            // `mengko gawe f() { ... }` adalah DEKLARASI (bukan ekspresi), agar
            // `f` menjadi slot lokal modul dan bisa dipanggil langsung.
            const std::size_t simpan = idx_;
            ++idx_;
            lewati_asi();
            if (cek(Tok::Star) || cek(Tok::Ident)) {
                idx_ = simpan;
                break;  // bentuk arrow: `mengko x => ...`, tangani sebagai ekspresi
            }
            NodePtr f = nullptr;
            if (cek(Tok::KwFunction)) {
                f = parse_deklarasi_fungsi();
                if (f != nullptr) static_cast<ast::FungsiDeklarasi*>(f)->mengko = true;
            }
            if (f != nullptr) return f;
            idx_ = simpan;
            break;
        }
        case Tok::KwReturn: {
            ++idx_;
            auto* s = buat<ast::BaliStmt>(rentang_dari(m));
            if (!cek(Tok::Semi) && !cek(Tok::RBrace) && !cek(Tok::Eof) && !saat().baris_baru_sebelum) {
                s->nilai = parse_ekspresi();
            }
            lewati_asi();
            return s;
        }
        case Tok::KwBreak: {
            ++idx_;
            auto* s = buat<ast::MandhegStmt>(rentang_dari(m));
            if (cek(Tok::Ident) && !saat().baris_baru_sebelum) {
                s->label = saat().teks;
                ++idx_;
            }
            lewati_asi();
            return s;
        }
        case Tok::KwContinue: {
            ++idx_;
            auto* s = buat<ast::TerusnaStmt>(rentang_dari(m));
            if (cek(Tok::Ident) && !saat().baris_baru_sebelum) {
                s->label = saat().teks;
                ++idx_;
            }
            lewati_asi();
            return s;
        }
        case Tok::KwThrow: {
            ++idx_;
            auto* s = buat<ast::UncalStmt>(rentang_dari(m));
            s->nilai = parse_ekspresi();
            lewati_asi();
            return s;
        }
        case Tok::KwVar: case Tok::KwConst: case Tok::KwAna: {
            const bool tetep = !cek(Tok::KwAna);
            ++idx_;
            auto* dvl = buat<ast::DeklarasiVarStmt>(rentang_dari(m));
            dvl->tetep = tetep;
            if (cek(Tok::Ident)) {
                dvl->jeneng = saat().teks;
                ++idx_;
                tipe_annotation(dvl->tipe);
            } else if (cek(Tok::LBrace) || cek(Tok::LBracket)) {
                dvl->destruktur = true;
                dvl->pola = cek(Tok::LBrace) ? parse_objek_literal() : parse_array_literal();
            } else {
                diagnosa_di("S001", "Nemu \"" + std::string(saat().tampilan()) + "\" nanging ngarep-arep jeneng variabel.");
            }
            if (makan(Tok::Eq)) {
                dvl->nilai = parse_assignment();
            } else if (tetep) {
                diagnosa_di("S408", "Konstanta (tetep) kudu duwe nilai awale.",
                            "Tulis `tetep jeneng = <nilai>`.");
            }
            while (makan(Tok::Comma)) {
                lewati_asi();
                auto* lain = buat<ast::DeklarasiVarStmt>(rentang_dari(m));
                lain->tetep = tetep;
                if (cek(Tok::Ident)) {
                    lain->jeneng = saat().teks;
                    ++idx_;
                    tipe_annotation(lain->tipe);
                } else if (cek(Tok::LBrace) || cek(Tok::LBracket)) {
                    lain->destruktur = true;
                    lain->pola = cek(Tok::LBrace) ? parse_objek_literal() : parse_array_literal();
                }
                if (makan(Tok::Eq)) lain->nilai = parse_assignment();
                dvl->range.selesai = peek(-1).range.selesai;
                dvl->deklarator_lain.push_back(lain);
            }
            lewati_asi();
            return dvl;
        }
        case Tok::KwFinally: case Tok::KwCatch: {
            // `pungkasan` / `tangkep` hanya sah di dalam `coba`.
            const bool lastly = cek(Tok::KwFinally);
            ++idx_;
            auto* s2 = buat<ast::KosongStmt>(rentang_dari(m));
            diagnosa("S015", std::string("\"") + (lastly ? "pungkasan" : "tangkep") +
                                "\" mung bisa ana ing jero `coba`.",
                     rentang_dari(m), "Tulis `coba { ... } tangkep (e) { ... }-pungkasan { ... }`.");
            return s2;
        }
        case Tok::Ident: {
            if (cek(1, Tok::Colon)) return parse_labeled();
            break;
        }
        default: break;
    }

    NodePtr e = parse_ekspresi();
    auto* s = buat<ast::EkspresiStmt>(rentang_dari(m));
    s->ekspresi = e;
    lewati_asi();
    return s;
}

NodePtr Parser::parse_labeled() {
    const std::size_t m = idx_;
    auto* l = buat<ast::LabelStmt>(rentang_dari(m));
    l->label = saat().teks;
    idx_ += 2;
    lewati_asi();
    l->awak = parse_statement();
    return l;
}

NodePtr Parser::parse_blok() {
    const std::size_t m = idx_;
    aspek_ke_close(Tok::LBrace, "S001", "\"{\"");
    auto* blk = buat<ast::BlokStmt>(rentang_dari(m));
    lewati_asi();
    while (!cek(Tok::RBrace) && !cek(Tok::Eof) && !bag_.penuh()) {
        const std::size_t sebelum = idx_;
        NodePtr s = parse_statement();
        if (s != nullptr) blk->body.push_back(s);
        lewati_asi();
        if (idx_ == sebelum) ++idx_;
    }
    aspek_ke_close(Tok::RBrace, "S003", "\"}\" penutup blok");
    return blk;
}

NodePtr Parser::parse_yen() {
    const std::size_t m = idx_;
    ++idx_;
    auto* s = buat<ast::YenStmt>(rentang_dari(m));
    aspek_ke_close(Tok::LParen, "S001", "\"(\"");
    lewati_asi();
    s->kondisi = parse_ekspresi();
    aspek_ke_close(Tok::RParen, "S005", "\")\" sawise kondisi yen");
    lewati_asi();
    s->lalu = cek(Tok::LBrace) ? parse_blok() : parse_statement();
    if (cek(Tok::KwElse)) {
        ++idx_;
        s->ada_liyane = true;
        lewati_asi();
        s->liyane = cek(Tok::KwIf) ? parse_yen() : parse_statement();
    }
    return s;
}

NodePtr Parser::parse_nalika() {
    const std::size_t m = idx_;
    ++idx_;
    auto* s = buat<ast::NalikaStmt>(rentang_dari(m));
    aspek_ke_close(Tok::LParen, "S001", "\"(\"");
    lewati_asi();
    s->kondisi = parse_ekspresi();
    aspek_ke_close(Tok::RParen, "S007", "\")\" sawise kondisi nalika");
    lewati_asi();
    s->awak = cek(Tok::LBrace) ? parse_blok() : parse_statement();
    return s;
}

NodePtr Parser::parse_lakoni() {
    const std::size_t m = idx_;
    ++idx_;
    auto* s = buat<ast::LakoniStmt>(rentang_dari(m));
    lewati_asi();
    s->awak = cek(Tok::LBrace) ? parse_blok() : parse_statement();
    if (makan(Tok::KwWhile)) {
        lewati_asi();
        aspek_ke_close(Tok::LParen, "S001", "\"(\"");
        lewati_asi();
        s->kondisi = parse_ekspresi();
        aspek_ke_close(Tok::RParen, "S002", "\")\" sawise kondisi lakoni-nalika");
    }
    lewati_asi();
    return s;
}

NodePtr Parser::parse_kanggo() {
    const std::size_t m = idx_;
    ++idx_;  // kanggo
    bool enteni = makan(Tok::KwAwait) || makan(Tok::KwAwait);
    lewati_asi();
    aspek_ke_close(Tok::LParen, "S001", "\"(\"");

    // --- inisialisasi opsional ---
    NodePtr init = nullptr;
    if (!cek(Tok::Semi) && !cek(Tok::RParen)) {
        // `ana` (wonten) maupun `tetep` boleh jadi pengikat loop -- bentuk
        // `kanggo (const x saka ...)` adalah yang paling sering ditulis, dan
        // menolaknya membuat `kanggo`|`saka` terasa rusak padahal deklarasi
        // biasa sudah menerimanya.
        if (cek(Tok::KwAna) || cek(Tok::KwConst)) {
            const std::size_t im = idx_;
            ++idx_;
            auto* dvl = buat<ast::DeklarasiVarStmt>(rentang_dari(im));
            if (cek(Tok::Ident)) {
                dvl->jeneng = saat().teks;
                ++idx_;
                tipe_annotation(dvl->tipe);
            } else if (cek(Tok::LBrace) || cek(Tok::LBracket)) {
                dvl->destruktur = true;
                dvl->pola = cek(Tok::LBrace) ? parse_objek_literal() : parse_array_literal();
            } else {
                diagnosa_di("S001", "Ngarep-arep jeneng variabel kanggo kanggo.");
            }
            if (makan(Tok::Eq)) dvl->nilai = parse_assignment();
            init = dvl;
        } else {
            init = parse_ekspresi();
        }
    }

    // --- `saka` (for-of) / `ing` (for-in) ---
    if (cek(Tok::KwFrom) || cek(Tok::KwFrom) || cek(Tok::KwFrom) || cek(Tok::KwIn) || cek(Tok::KwIn)) {
        const bool in = cek(Tok::KwIn) || cek(Tok::KwIn);
        ++idx_;
        lewati_asi();
        NodePtr iterable = parse_ekspresi();
        aspek_ke_close(Tok::RParen, "S002", "\")\" sawise iterable kanggo");
        lewati_asi();
        if (in) {
            auto* s = buat<ast::KanggoInStmt>(rentang_dari(m));
            s->enteni = enteni;
            s->target = init;
            s->objek = iterable;
            s->awak = cek(Tok::LBrace) ? parse_blok() : parse_statement();
            return s;
        }
        auto* s = buat<ast::KanggoOfStmt>(rentang_dari(m));
        s->enteni = enteni;
        s->target = init;
        s->iterable = iterable;
        s->awak = cek(Tok::LBrace) ? parse_blok() : parse_statement();
        return s;
    }

    // --- kanggo klasik ---
    auto* s = buat<ast::KanggoStmt>(rentang_dari(m));
    s->inisialisasi = init;
    aspek_ke_close(Tok::Semi, "S002", "\";\" sawise inisialisasi kanggo");
    lewati_asi();
    if (!cek(Tok::Semi)) s->kondisi = parse_ekspresi();
    aspek_ke_close(Tok::Semi, "S002", "\";\" sawise kondisi kanggo");
    lewati_asi();
    if (!cek(Tok::RParen)) s->pembaruan = parse_ekspresi();
    aspek_ke_close(Tok::RParen, "S002", "\")\" sawise bagian pembaruan kanggo");
    lewati_asi();
    s->awak = cek(Tok::LBrace) ? parse_blok() : parse_statement();
    return s;
}

NodePtr Parser::parse_pilih() {
    const std::size_t m = idx_;
    ++idx_;
    auto* s = buat<ast::PilihStmt>(rentang_dari(m));
    aspek_ke_close(Tok::LParen, "S001", "\"(\"");
    lewati_asi();
    s->subjek = parse_ekspresi();
    aspek_ke_close(Tok::RParen, "S002", "\")\" sawise subjek pilih");
    aspek_ke_close(Tok::LBrace, "S003", "\"{\" sawise pilihan");
    lewati_asi();
    while (!cek(Tok::RBrace) && !cek(Tok::Eof) && !bag_.penuh()) {
        const bool kasus = cek(Tok::KwCase) || cek(Tok::KwCase);
        const bool bakuan = cek(Tok::KwDefault) || cek(Tok::KwDefault);
        if (!kasus && !bakuan) {
            diagnosa_di("S002", "Nemu \"" + std::string(saat().tampilan()) + "\" ing jero pilih; ngarep-arep kasus utawa baku.");
            sinkronisasi_statement();
            continue;
        }
        const std::size_t km = idx_;
        ++idx_;
        auto* k = buat<ast::KasusKlap>(rentang_dari(km));
        if (kasus) {
            lewati_asi();
            // `kasus [ ... ]:` / `kasus { ... }:` adalah POLA (sejak pada
            // `cocog`), bukan literal. Ekspresi yang diawali kurung kurawal
            // selalu pola supaya `kasus {a: 1}:` tidak tertukar dengan blok.
            //
            // `kasus <Kelas>:` adalah pencocokan TIPE. Dibedakan dari perbandingan
            // nilai biasa dengan dua syarat: pengenal langsung diikuti `:` (bukan
            // `.`/`?`/`(`), dan huruf pertamanya kapital. Tanpa syarat kapital,
            // `kasus warna:` (variabel) ikut tertukar sebagai class.
            if (cek(Tok::LBracket) || cek(Tok::LBrace)) {
                k->pola = parse_pola();
            } else if (cek(Tok::Ident) && cek(1, Tok::Colon) && saat().teks[0] >= 'A' && saat().teks[0] <= 'Z') {
                k->nama_kelas = saat().teks;
                ++idx_;
            } else {
                k->test = parse_ekspresi();
            }
        }
        aspek_ke_close(Tok::Colon, "S008", "\":\" sawise nilai kasus");
        lewati_asi();
        while (!cek(Tok::KwCase) && !cek(Tok::KwCase) && !cek(Tok::KwDefault) && !cek(Tok::KwDefault) &&
               !cek(Tok::RBrace) && !cek(Tok::Eof) && !bag_.penuh()) {
            const std::size_t sblm = idx_;
            NodePtr st = parse_statement();
            if (st) k->body.push_back(st);
            lewati_asi();
            if (idx_ == sblm) ++idx_;
        }
        k->range.selesai = peek(-1).range.selesai;
        s->kasus.push_back(k);
    }
    aspek_ke_close(Tok::RBrace, "S003", "\"}\" penutup pilih");
    return s;
}

NodePtr Parser::parse_coba() {
    const std::size_t m = idx_;
    ++idx_;
    auto* s = buat<ast::CobaStmt>(rentang_dari(m));
    lewati_asi();
    s->blok = parse_blok();

    while (cek(Tok::KwCatch) || cek(Tok::KwCatch)) {
        const std::size_t cm = idx_;
        ++idx_;
        auto* k = buat<ast::TangkepKlausul>(rentang_dari(cm));
        lewati_asi();
        if (makan(Tok::LParen)) {
            lewati_asi();
            // `tangkep (KleruJenis) { ... }` -- tanpa binding, tipe kleru
            // langsung. Bentuk ini tidak ambigu karena `Kleru*` ALWAYS
            // diawali huruf besar kapital, sedangkan `tangkep (e)` tanpa
            // anotasi adalah binding.
            if (cek(Tok::Ident) && cek(1, Tok::RParen) && std::isupper(static_cast<unsigned char>(saat().teks[0]))) {
                k->tipe = buat<ast::TipeAnotasi>(rentang_dari(idx_));
                static_cast<ast::TipeAnotasi*>(k->tipe)->nama = saat().teks;
                ++idx_;
            } else if (cek(Tok::Ident)) {
                k->binding = saat().teks;
                k->ada_binding = true;
                ++idx_;
                tipe_annotation(k->tipe);
            } else if (cek(Tok::LBrace) || cek(Tok::LBracket)) {
                k->ada_binding = false;
                k->tipe = cek(Tok::LBrace) ? parse_objek_literal() : parse_array_literal();
                k->binding = {};  // destruktur: ditangani compiler
                k->ada_binding = true;
            } else {
                diagnosa_di("S001", "Ngarep-arep jeneng variabel tangkep.");
            }
            lewati_asi();
            aspek_ke_close(Tok::RParen, "S002", "\")\" sawise binding tangkep");
        }
        lewati_asi();
        k->body = parse_blok();
        s->tangkep.push_back(k);
    }

    if (cek(Tok::KwFinally) || cek(Tok::KwFinally)) {
        const std::size_t fm = idx_;
        ++idx_;
        auto* k = buat<ast::PungkasanKlausul>(rentang_dari(fm));
        lewati_asi();
        k->body = parse_blok();
        s->pungkasan = k;
    }

    if (s->tangkep.empty() && s->pungkasan == nullptr) {
        diagnosa("S002", "coba tanpa tangkep utawa intrigasan ora ana gunanya.", rentang_dari(m),
                 "Tambahake `tangkep (e) { ... }` utawa `pungkasan { ... }`.");
    }
    return s;
}

// ===========================================================================
// EKSPOR / IMPOR
// ===========================================================================

NodePtr Parser::parse_ekspor() {
    const std::size_t m = idx_;
    ++idx_;  // ekspor
    lewati_asi();

    // `ekspor baku <stmt>` — ekspor default
    if (cek(Tok::KwDefault) || cek(Tok::KwDefault)) {
        ++idx_;
        auto* e = buat<ast::EksporDeklarasi>(rentang_dari(m));
        e->default_ekspor = true;
        lewati_asi();
        e->deklarasi = parse_statement();
        lewati_asi();
        return e;
    }

    // `ekspor { a, b minangka c }` (opsional `saka "mod"`)
    if (cek(Tok::LBrace)) {
        auto* e = buat<ast::EksporDeklarasi>(rentang_dari(m));
        ++idx_;
        lewati_asi();
        while (!cek(Tok::RBrace) && !cek(Tok::Eof) && !bag_.penuh()) {
            if (!cek(Tok::Ident)) {
                diagnosa_di("S001", "Ngarep-arep jeneng sing arep diekspor.");
                sinkronisasi_statement();
                break;
            }
            ast::EksporSpesifikasi s;
            s.lokal = saat().teks;
            s.ekspor = s.lokal;
            ++idx_;
            if (cek(Tok::KwAs)) {
                ++idx_;
                lewati_asi();
                if (cek(Tok::Ident)) {
                    s.ekspor = saat().teks;
                    ++idx_;
                } else {
                    diagnosa_di("S001", "Ngarep-arep jeneng exportsi.");
                }
            }
            e->daftar.push_back(s);
            lewati_asi();
            // Pemisah opsional: koma, titik koma, atau langsung nama berikutnya
            // (sesuai tata bahasa `daftar_impor` yang tidak mewajibkan koma).
            makan(Tok::Comma);
            makan(Tok::Semi);
            lewati_asi();
        }
        aspek_ke_close(Tok::RBrace, "S003", "\"}\" penutup daftar ekspor");
        lewati_asi();
        if (cek(Tok::KwFrom) || cek(Tok::KwFrom)) {
            ++idx_;
            if (cek(Tok::Text)) {
                e->ada_modul = true;
                e->modul = saat().nilai_teks;
                ++idx_;
            } else {
                diagnosa_di("S001", "Ngarep-arep nama modul (teks) sawise saka.");
            }
        }
        lewati_asi();
        return e;
    }

    // `ekspor deklarasi` (fungsi, golongan, variabel, kelas)
    auto* e = buat<ast::EksporDeklarasi>(rentang_dari(m));
    e->deklarasi_lengkap = true;
    lewati_asi();
    e->deklarasi = parse_statement();
    lewati_asi();
    return e;
}

NodePtr Parser::parse_impor() {
    const std::size_t m = idx_;
    ++idx_;  // impor
    lewati_asi();

    // impor("path") — impor dinamis
    if (cek(Tok::LParen)) {
        auto* dyn = buat<ast::ImporDinamisExpr>(rentang_dari(m));
        ++idx_;
        lewati_asi();
        dyn->spesifikasi = parse_ekspresi();
        aspek_ke_close(Tok::RParen, "S002", "\")\" sawise impor dinamis");
        lewati_asi();
        return dyn;
    }

    auto* im = buat<ast::ImporDeklarasi>(rentang_dari(m));

    if (makan(Tok::Star)) {
        // `* minangka M`
        im->ada_namespace = true;
        lewati_asi();
        if (cek(Tok::KwAs) || cek(Tok::KwAs)) {
            ++idx_;
            if (cek(Tok::Ident)) {
                im->alias_namespace = saat().teks;
                ++idx_;
            } else {
                diagnosa_di("S001", "Ngarep-arep jeneng namespace sawise minangka.");
            }
        }
    } else if (makan(Tok::LBrace)) {
        lewati_asi();
        while (!cek(Tok::RBrace) && !cek(Tok::Eof) && !bag_.penuh()) {
            if (!cek(Tok::Ident) && !cek(Tok::KwDefault)) {
                diagnosa_di("S001", "Ngarep-arep jeneng sing arep diimpor.");
                sinkronisasi_statement();
                break;
            }
            ast::ImporSpesifikasi s;
            s.sumber = saat().teks;
            s.impor = s.sumber;
            ++idx_;
            if (cek(Tok::KwAs)) {
                ++idx_;
                lewati_asi();
                if (cek(Tok::Ident)) {
                    s.alias = saat().teks;
                    s.impor = s.alias;
                    ++idx_;
                } else {
                    diagnosa_di("S001", "Ngarep-arep jeneng alias sawise minangka.");
                }
            }
            im->daftar.push_back(s);
            lewati_asi();
            // Pemisah opsional (sama seperti daftar ekspor).
            makan(Tok::Comma);
            makan(Tok::Semi);
            lewati_asi();
        }
        aspek_ke_close(Tok::RBrace, "S003", "\"}\" penutup daftar impor");
    } else if (cek(Tok::Ident)) {
        ast::ImporSpesifikasi s;
        // `impor NAMA saka "modul"` (tanpa kurung kurawal) = impor ekspor
        // `baku` (default) ke nama lokal `NAMA`. Bandingkan `impor { x } saka
        // "modul"` yang mengimpor nama `x`.
        s.sumber = "baku";
        s.impor = saat().teks;
        ++idx_;
        if (cek(Tok::KwAs) || cek(Tok::KwAs)) {
            ++idx_;
            lewati_asi();
            if (cek(Tok::Ident)) {
                s.alias = saat().teks;
                s.impor = s.alias;
                ++idx_;
            } else {
                diagnosa_di("S001", "Ngarep-arep jeneng alias sawise minangka.");
            }
        }
        im->daftar.push_back(s);
        lewati_asi();
        if (makan(Tok::Comma)) {
            lewati_asi();
            if (makan(Tok::LBrace)) {
                lewati_asi();
                while (!cek(Tok::RBrace) && !cek(Tok::Eof) && !bag_.penuh()) {
                    if (!cek(Tok::Ident)) {
                        sinkronisasi_statement();
                        break;
                    }
                    ast::ImporSpesifikasi s2;
                    s2.sumber = saat().teks;
                    s2.impor = s2.sumber;
                    ++idx_;
                    if (cek(Tok::KwAs) || cek(Tok::KwAs)) {
                        ++idx_;
                        lewati_asi();
                        if (cek(Tok::Ident)) {
                            s2.alias = saat().teks;
                            s2.impor = s2.alias;
                            ++idx_;
                        }
                    }
                    im->daftar.push_back(s2);
                    lewati_asi();
                    if (!makan(Tok::Comma)) break;
                }
                aspek_ke_close(Tok::RBrace, "S003", "\"}\" penutup daftar impor");
            }
        }
    } else if (cek(Tok::Text)) {
        // `impor "./modul.jw"` -- impor untuk efek samping (tanpa pengikat).
        im->ada_modul = true;
        im->modul = saat().nilai_teks;
        ++idx_;
    } else {
        diagnosa_di("S001", "Nemu \"" + std::string(saat().tampilan()) + "\" sawise impor.");
    }

    lewati_asi();
    if (cek(Tok::KwFrom) || cek(Tok::KwFrom)) {
        ++idx_;
        lewati_asi();
        im->ada_modul = true;
        if (cek(Tok::Text)) {
            im->modul = saat().nilai_teks;
            ++idx_;
        } else {
            diagnosa_di("S001", "Ngarep-arep nama modul (teks) sawise saka.");
        }
    }
    lewati_asi();
    return im;
}

}  // namespace jawa::parse
