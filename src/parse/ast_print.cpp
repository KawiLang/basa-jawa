// Cetak AST Basa Jawa untuk `jawa ast`.
//
// Pendekatan: satu fungsi `anak()` yang mengembalikan daftar anak node untuk
// setiap jenis node. Printer ini generik sehingga tidak perlu satu `case` per
// node untuk pencetakan; detail tambahan (nilai/nama) dicetak terpisah oleh
// `rincian()`.
#include <string>
#include <vector>

#include "parse/ast_print.h"

namespace jawa::parse {

namespace {

const char* label(ast::NK k) noexcept {
    switch (k) {
        case ast::NK::Nomor: return "Nomor";
        case ast::NK::BigIntLit: return "BigInt";
        case ast::NK::TeksLit: return "Teks";
        case ast::NK::TemplateLit: return "Template";
        case ast::NK::RegexLit: return "Regex";
        case ast::NK::ArrayLit: return "Dhaptar";
        case ast::NK::ObjectLit: return "Objek";
        case ast::NK::Fungsi: return "FungsiPanah";
        case ast::NK::RefIdent: return "Ref";
        case ast::NK::AksesProperti: return "Akses";
        case ast::NK::Panggilan: return "Panggilan";
        case ast::NK::_anyar: return "Anyar";
        case ast::NK::Unary: return "Unary";
        case ast::NK::Biner: return "Biner";
        case ast::NK::Logika: return "Logika";
        case ast::NK::Penugasan: return "Penugasan";
        case ast::NK::Kondisional: return "Kondisional";
        case ast::NK::Pembaruan: return "Pembaruan";
        case ast::NK::Rangkaian: return "Rangkaian";
        case ast::NK::CocogExpr: return "Cocog";
        case ast::NK::ImporDinamis: return "ImporDinamis";
        case ast::NK::ThisExpr: return "Ini";
        case ast::NK::SuperExpr: return "Induk";
        case ast::NK::KunciPrivat: return "KunciPrivat";
        case ast::NK::Pola: return "Pola";
        case ast::NK::PropertiPola: return "PropertiPola";
        case ast::NK::PolaNilai: return "PolaNilai";
        case ast::NK::PolaAlternatif: return "PolaAlternatif";
        case ast::NK::PolaDhaptar: return "PolaDhaptar";
        case ast::NK::PolaObjek: return "PolaObjek";
        case ast::NK::PolaTipe: return "PolaTipe";
        case ast::NK::Program: return "Program";
        case ast::NK::EkspresiStmt: return "EkspresiStmt";
        case ast::NK::DeklarasiVar: return "DeklarasiVar";
        case ast::NK::Blok: return "Blok";
        case ast::NK::YenStmt: return "Yen";
        case ast::NK::NalikaStmt: return "Nalika";
        case ast::NK::LakoniStmt: return "Lakoni";
        case ast::NK::KanggoStmt: return "Kanggo";
        case ast::NK::KanggoOfStmt: return "KanggoSaka";
        case ast::NK::KanggoInStmt: return "KanggoIng";
        case ast::NK::PilihStmt: return "Pilih";
        case ast::NK::KasusKlap: return "KasusKlap";
        case ast::NK::BaliStmt: return "Bali";
        case ast::NK::MandhegStmt: return "Mandheg";
        case ast::NK::TerusnaStmt: return "Terusna";
        case ast::NK::UncalStmt: return "Uncal";
        case ast::NK::CobaStmt: return "Coba";
        case ast::NK::TangkepKlausul: return "Tangkep";
        case ast::NK::PungkasanKlausul: return "Pungkasan";
        case ast::NK::GolonganDeklarasi: return "Golongan";
        case ast::NK::MetodeDeklarasi: return "Metode";
        case ast::NK::EksporDeklarasi: return "Ekspor";
        case ast::NK::ImporDeklarasi: return "Impor";
        case ast::NK::LabelStmt: return "Label";
        case ast::NK::KosongStmt: return "Kosong";
        case ast::NK::FungsiDeklarasi: return "GaweFungsi";
        case ast::NK::ParamDeklarasi: return "Param";
        case ast::NK::TipeAnotasi: return "Tipe";
        case ast::NK::TipeUnion: return "TipeUnion";
        case ast::NK::TipeArray: return "TipeArray";
        case ast::NK::TipeReferensi: return "TipeRef";
        case ast::NK::TipeOpsional: return "TipeOpsional";
        case ast::NK::AntarmukaDeklarasi: return "Antarmuka";
        case ast::NK::AliasTipe: return "AliasTipe";
        case ast::NK::PropertyAccessor: return "Accessor";
        
        
        case ast::NK::DebuggerStmt: return "Debugger";
        default: return "Node";
    }
}

const char* binop_label(ast::BinOp op) noexcept {
    switch (op) {
        case ast::BinOp::Tambah: return "+";
        case ast::BinOp::Kurang: return "-";
        case ast::BinOp::Kali: return "*";
        case ast::BinOp::Bagi: return "/";
        case ast::BinOp::Modulo: return "%";
        case ast::BinOp::Pangkat: return "**";
        case ast::BinOp::Lt: return "<";
        case ast::BinOp::Le: return "<=";
        case ast::BinOp::Gt: return ">";
        case ast::BinOp::Ge: return ">=";
        case ast::BinOp::Eq: return "==";
        case ast::BinOp::Ne: return "!=";
        case ast::BinOp::instanceSaka: return "instansi_saka";
        case ast::BinOp::ing: return "ing";
        case ast::BinOp::BitDan: return "&";
        case ast::BinOp::BitXor: return "^";
        case ast::BinOp::BitOr: return "|";
        case ast::BinOp::GeserKiri: return "<<";
        case ast::BinOp::GeserKanan: return ">>";
        case ast::BinOp::GeserKananTanpaTanda: return ">>>";
        case ast::BinOp::Koma: return ",";
        case ast::BinOp::Lan: return "lan";
        case ast::BinOp::Utawa: return "utawa";
        case ast::BinOp::Nullish: return "??";
        case ast::BinOp::Pipeline: return "|>";
        default: return "?";
    }
}

const char* assignop_label(ast::AssignOp op) noexcept {
    switch (op) {
        case ast::AssignOp::Set: return "=";
        case ast::AssignOp::Tambah: return "+=";
        case ast::AssignOp::Kurang: return "-=";
        case ast::AssignOp::Kali: return "*=";
        case ast::AssignOp::Bagi: return "/=";
        case ast::AssignOp::Modulo: return "%=";
        case ast::AssignOp::Pangkat: return "**=";
        case ast::AssignOp::GeserKiri: return "<<=";
        case ast::AssignOp::GeserKanan: return ">>=";
        case ast::AssignOp::GeserKananTanpaTanda: return ">>>=";
        case ast::AssignOp::BitDan: return "&=";
        case ast::AssignOp::BitOr: return "|=";
        case ast::AssignOp::BitXor: return "^=";
        case ast::AssignOp::Lan: return "&&=";
        case ast::AssignOp::Utawa: return "||=";
        case ast::AssignOp::Nullish: return "?\?=";
        default: return "=";
    }
}

const char* unop_label(ast::UnOp op) noexcept {
    switch (op) {
        case ast::UnOp::Neg: return "-";
        case ast::UnOp::Pos: return "+";
        case ast::UnOp::Ora: return "ora";
        case ast::UnOp::BitNot: return "~";
        case ast::UnOp::PlusPlus: return "++";
        case ast::UnOp::MinusMinus: return "--";
        case ast::UnOp::Jinis: return "jinis";
        case ast::UnOp::Busak: return "busak";
        case ast::UnOp::Entani: return "enteni";
        case ast::UnOp::Metokake: return "metokake";
        default: return "?";
    }
}

/// Kumpulkan anak-anak node untuk traversal generik.
void anak(const ast::Node* n, std::vector<const ast::Node*>& keluar) {
    if (n == nullptr) return;
    switch (n->kind) {
        case ast::NK::Program: {
            for (const ast::Node* c : static_cast<const ast::Program*>(n)->body) keluar.push_back(c);
            return;
        }
        case ast::NK::EkspresiStmt:
            keluar.push_back(static_cast<const ast::EkspresiStmt*>(n)->ekspresi);
            return;
        case ast::NK::DeklarasiVar: {
            const auto* x = static_cast<const ast::DeklarasiVarStmt*>(n);
            keluar.push_back(x->nilai);
            keluar.push_back(x->tipe);
            for (const ast::Node* d : x->deklarator_lain) keluar.push_back(d);
            return;
        }
        case ast::NK::Blok: {
            for (const ast::Node* c : static_cast<const ast::BlokStmt*>(n)->body) keluar.push_back(c);
            return;
        }
        case ast::NK::YenStmt: {
            const auto* x = static_cast<const ast::YenStmt*>(n);
            keluar.push_back(x->kondisi);
            keluar.push_back(x->lalu);
            keluar.push_back(x->liyane);
            return;
        }
        case ast::NK::NalikaStmt: {
            const auto* x = static_cast<const ast::NalikaStmt*>(n);
            keluar.push_back(x->kondisi);
            keluar.push_back(x->awak);
            return;
        }
        case ast::NK::LakoniStmt: {
            const auto* x = static_cast<const ast::LakoniStmt*>(n);
            keluar.push_back(x->awak);
            keluar.push_back(x->kondisi);
            return;
        }
        case ast::NK::KanggoStmt: {
            const auto* x = static_cast<const ast::KanggoStmt*>(n);
            keluar.push_back(x->inisialisasi);
            keluar.push_back(x->kondisi);
            keluar.push_back(x->pembaruan);
            keluar.push_back(x->awak);
            return;
        }
        case ast::NK::KanggoOfStmt: {
            const auto* x = static_cast<const ast::KanggoOfStmt*>(n);
            keluar.push_back(x->target);
            keluar.push_back(x->iterable);
            keluar.push_back(x->awak);
            return;
        }
        case ast::NK::KanggoInStmt: {
            const auto* x = static_cast<const ast::KanggoInStmt*>(n);
            keluar.push_back(x->target);
            keluar.push_back(x->objek);
            keluar.push_back(x->awak);
            return;
        }
        case ast::NK::PilihStmt: {
            const auto* x = static_cast<const ast::PilihStmt*>(n);
            keluar.push_back(x->subjek);
            for (const ast::Node* c : x->kasus) keluar.push_back(c);
            return;
        }
        case ast::NK::KasusKlap: {
            const auto* x = static_cast<const ast::KasusKlap*>(n);
            keluar.push_back(x->test);
            for (const ast::Node* c : x->body) keluar.push_back(c);
            return;
        }
        case ast::NK::BaliStmt: keluar.push_back(static_cast<const ast::BaliStmt*>(n)->nilai); return;
        case ast::NK::UncalStmt: keluar.push_back(static_cast<const ast::UncalStmt*>(n)->nilai); return;
        case ast::NK::CobaStmt: {
            const auto* x = static_cast<const ast::CobaStmt*>(n);
            keluar.push_back(x->blok);
            for (const ast::Node* c : x->tangkep) keluar.push_back(c);
            keluar.push_back(x->pungkasan);
            return;
        }
        case ast::NK::TangkepKlausul:
            keluar.push_back(static_cast<const ast::TangkepKlausul*>(n)->body);
            return;
        case ast::NK::PungkasanKlausul:
            keluar.push_back(static_cast<const ast::PungkasanKlausul*>(n)->body);
            return;
        case ast::NK::GolonganDeklarasi: {
            const auto* x = static_cast<const ast::GolonganDeklarasi*>(n);
            keluar.push_back(x->induk);
            for (const ast::Node* c : x->badan) keluar.push_back(c);
            for (const ast::Node* c : x->statis_blok) keluar.push_back(c);
            return;
        }
        case ast::NK::MetodeDeklarasi:
            keluar.push_back(static_cast<const ast::MetodeDeklarasi*>(n)->fungsi);
            return;
        case ast::NK::PropertyAccessor:
            keluar.push_back(static_cast<const ast::PropertyAccessorDeklarasi*>(n)->fungsi);
            return;
        case ast::NK::FieldKelas:
            keluar.push_back(static_cast<const ast::FieldKelas*>(n)->kunci);
            keluar.push_back(static_cast<const ast::FieldKelas*>(n)->nilai);
            keluar.push_back(static_cast<const ast::FieldKelas*>(n)->tipe);
            return;
        case ast::NK::EksporDeklarasi: {
            const auto* x = static_cast<const ast::EksporDeklarasi*>(n);
            keluar.push_back(x->deklarasi);
            return;
        }
        case ast::NK::ImporDeklarasi: return;
        case ast::NK::LabelStmt: keluar.push_back(static_cast<const ast::LabelStmt*>(n)->awak); return;
        case ast::NK::FungsiDeklarasi: {
            const auto* x = static_cast<const ast::FungsiDeklarasi*>(n);
            for (const ast::Node* p : x->param) keluar.push_back(p);
            keluar.push_back(x->awak);
            keluar.push_back(x->badan_ekspresi);
            keluar.push_back(x->tipe_bali);
            return;
        }
        case ast::NK::ParamDeklarasi: {
            const auto* x = static_cast<const ast::ParamDeklarasi*>(n);
            keluar.push_back(x->pola);
            keluar.push_back(x->nilai_default);
            keluar.push_back(x->tipe);
            return;
        }
        case ast::NK::RefIdent: return;
        case ast::NK::TeksLit: case ast::NK::Nomor: case ast::NK::BigIntLit: case ast::NK::RegexLit: return;
        case ast::NK::TemplateLit: {
            for (const ast::TemplateBagian& b : static_cast<const ast::TemplateLit*>(n)->bagian) {
                keluar.push_back(b.ekspresi_node);
            }
            return;
        }
        case ast::NK::ArrayLit: {
            for (const ast::Node* e : static_cast<const ast::ArrayLit*>(n)->elemen) {
                keluar.push_back(e != nullptr ? e : nullptr);
            }
            return;
        }
        case ast::NK::ObjectLit: {
            for (const ast::Node* p : static_cast<const ast::ObjectLit*>(n)->properti) keluar.push_back(p);
            return;
        }
        case ast::NK::AksesProperti: {
            const auto* x = static_cast<const ast::AksesProperti*>(n);
            keluar.push_back(x->objek);
            keluar.push_back(x->komputat);
            return;
        }
        case ast::NK::Panggilan: {
            const auto* x = static_cast<const ast::Panggilan*>(n);
            keluar.push_back(x->callee);
            for (const ast::Node* a : x->argumen) keluar.push_back(a);
            return;
        }
        case ast::NK::_anyar: {
            const auto* x = static_cast<const ast::AnyarExpr*>(n);
            keluar.push_back(x->konstruktor);
            for (const ast::Node* a : x->argumen) keluar.push_back(a);
            return;
        }
        case ast::NK::Unary: keluar.push_back(static_cast<const ast::UnaryExpr*>(n)->operand); return;
        case ast::NK::Biner: {
            const auto* x = static_cast<const ast::BinerExpr*>(n);
            keluar.push_back(x->kiri);
            keluar.push_back(x->kanan);
            return;
        }
        case ast::NK::Logika: {
            const auto* x = static_cast<const ast::LogikaExpr*>(n);
            keluar.push_back(x->kiri);
            keluar.push_back(x->kanan);
            return;
        }
        case ast::NK::Penugasan: {
            const auto* x = static_cast<const ast::PenugasanExpr*>(n);
            keluar.push_back(x->target);
            keluar.push_back(x->nilai);
            return;
        }
        case ast::NK::Kondisional: {
            const auto* x = static_cast<const ast::KondisionalExpr*>(n);
            keluar.push_back(x->kondisi);
            keluar.push_back(x->bila_benar);
            keluar.push_back(x->bila_salah);
            return;
        }
        case ast::NK::Pembaruan: keluar.push_back(static_cast<const ast::PembaruanExpr*>(n)->target); return;
        case ast::NK::Rangkaian: {
            const auto* x = static_cast<const ast::RangkaianExpr*>(n);
            keluar.push_back(x->kiri);
            keluar.push_back(x->kanan);
            return;
        }
        case ast::NK::ImporDinamis: keluar.push_back(static_cast<const ast::ImporDinamisExpr*>(n)->spesifikasi); return;
        case ast::NK::CocogExpr: {
            const auto* x = static_cast<const ast::CocogExpr*>(n);
            keluar.push_back(x->subjek);
            for (const ast::Node* c : x->kasus) keluar.push_back(c);
            return;
        }
        case ast::NK::KasusKocog: {
            const auto* x = static_cast<const ast::KasusKocog*>(n);
            keluar.push_back(x->pola);
            keluar.push_back(x->nilai);
            return;
        }
        case ast::NK::Pola: {
            const auto* x = static_cast<const ast::Pola*>(n);
            for (const ast::Node* c : x->alternatif) keluar.push_back(c);
            for (const ast::Node* c : x->elemen) keluar.push_back(c);
            for (const ast::Node* c : x->properti) keluar.push_back(c);
            keluar.push_back(x->nilai);
            keluar.push_back(x->penjaga);
            return;
        }
        case ast::NK::PropertiPola: {
            const auto* x = static_cast<const ast::PropertiPola*>(n);
            keluar.push_back(x->kunci);
            keluar.push_back(x->pola);
            return;
        }
        case ast::NK::TipeAnotasi: case ast::NK::TipeOpsional: case ast::NK::KosongStmt:
        case ast::NK::ThisExpr: case ast::NK::SuperExpr: case ast::NK::MandhegStmt:
        case ast::NK::TerusnaStmt: case ast::NK::DebuggerStmt: return;
        case ast::NK::TipeArray: keluar.push_back(static_cast<const ast::TipeArray*>(n)->elemen); return;
        case ast::NK::TipeUnion: {
            for (const ast::Node* c : static_cast<const ast::TipeUnion*>(n)->varian) keluar.push_back(c);
            return;
        }
        case ast::NK::TipeReferensi: {
            for (const ast::Node* c : static_cast<const ast::TipeReferensi*>(n)->argumen) keluar.push_back(c);
            return;
        }
        case ast::NK::AntarmukaDeklarasi: {
            for (const ast::Node* c : static_cast<const ast::AntarmukaDeklarasi*>(n)->badan) keluar.push_back(c);
            return;
        }
        case ast::NK::AliasTipe: keluar.push_back(static_cast<const ast::AliasTipe*>(n)->tipe); return;
        default: return;
    }
}

/// Baris detail tambahan per node (nilai, nama, operator).
void rincian(const ast::Node* n, std::string& keluar) {
    switch (n->kind) {
        case ast::NK::Nomor: {
            const auto* x = static_cast<const ast::NomorLit*>(n);
            const char* b = "angka";
            switch (x->bentuk) {
                case ast::NomorLit::Bentuk::Kosong: b = "kosong"; break;
                case ast::NomorLit::Bentuk::Mboh: b = "mboh"; break;
                case ast::NomorLit::Bentuk::NaN: b = "DuduAngka"; break;
                case ast::NomorLit::Bentuk::Infinity: b = "Tak_Wates"; break;
                case ast::NomorLit::Bentuk::Bener: b = "bener"; break;
                case ast::NomorLit::Bentuk::Salah: b = "salah"; break;
                case ast::NomorLit::Bentuk::Angka: break;
            }
            keluar += std::string(" nilai=") + b;
            if (x->bentuk == ast::NomorLit::Bentuk::Angka) keluar += ":" + std::to_string(x->nilai);
            return;
        }
        case ast::NK::TeksLit: keluar += " \"" + static_cast<const ast::TeksLit*>(n)->nilai + "\""; return;
        case ast::NK::Pola: {
            const auto* x = static_cast<const ast::Pola*>(n);
            const char* j = "?";
            switch (x->jenis) {
                case ast::Pola::Jenis::Wildcard: j = "_"; break;
                case ast::Pola::Jenis::Nama: j = "nama"; break;
                case ast::Pola::Jenis::Literal: j = "literal"; break;
                case ast::Pola::Jenis::Alternatif: j = "alternatif"; break;
                case ast::Pola::Jenis::Dhaptar: j = "dhaptar"; break;
                case ast::Pola::Jenis::Objek: j = "objek"; break;
                case ast::Pola::Jenis::Tipe: j = "tipe"; break;
                case ast::Pola::Jenis::Ekspresi: j = "ekspresi"; break;
            }
            keluar += std::string(" ") + j;
            if (!x->nama.empty()) keluar += " :" + std::string(x->nama);
            return;
        }
        case ast::NK::RegexLit:
            keluar += " /" + static_cast<const ast::RegexLit*>(n)->pola + "/" + static_cast<const ast::RegexLit*>(n)->flag;
            return;
        case ast::NK::BigIntLit:
            keluar += " " + std::string(static_cast<const ast::BigIntLit*>(n)->digit) + "n";
            return;
        case ast::NK::RefIdent: keluar += " " + std::string(static_cast<const ast::RefIdent*>(n)->nama); return;
        case ast::NK::DeklarasiVar: {
            const auto* x = static_cast<const ast::DeklarasiVarStmt*>(n);
            keluar += x->tetep ? " tetep " : " ana ";
            keluar += std::string(x->jeneng);
            if (x->destruktur) keluar += " (destruktur)";
            return;
        }
        case ast::NK::FungsiDeklarasi: {
            const auto* x = static_cast<const ast::FungsiDeklarasi*>(n);
            keluar += " " + std::string(x->nama);
            keluar += x->panah ? " (panah)" : " (blok)";
            if (x->mengko) keluar += " (mengko)";
            if (x->generator) keluar += " (generator)";
            return;
        }
        case ast::NK::Biner: keluar += std::string(" '") + binop_label(static_cast<const ast::BinerExpr*>(n)->op) + "'"; return;
        case ast::NK::Logika: {
            const auto* x = static_cast<const ast::LogikaExpr*>(n);
            keluar += x->op == ast::LogOp::Lan ? " lan" : (x->op == ast::LogOp::Utawa ? " utawa" : " ??");
            return;
        }
        case ast::NK::Penugasan: keluar += std::string(" '") + assignop_label(static_cast<const ast::PenugasanExpr*>(n)->op) + "'"; return;
        case ast::NK::Unary: keluar += std::string(" '") + unop_label(static_cast<const ast::UnaryExpr*>(n)->op) + "'"; return;
        case ast::NK::Pembaruan: {
            const auto* x = static_cast<const ast::PembaruanExpr*>(n);
            keluar += x->prefiks ? " prefiks" : " postfiks";
            return;
        }
        case ast::NK::AksesProperti: {
            const auto* x = static_cast<const ast::AksesProperti*>(n);
            if (!x->nama.empty()) keluar += " ." + std::string(x->nama);
            if (x->opsional) keluar += " (opsional)";
            return;
        }
        case ast::NK::GolonganDeklarasi: keluar += " " + std::string(static_cast<const ast::GolonganDeklarasi*>(n)->nama); return;
        case ast::NK::FieldKelas: {
            const auto* x = static_cast<const ast::FieldKelas*>(n);
            keluar += " " + std::string(x->nama);
            if (x->statis) keluar += " (statis)";
            if (x->privat) keluar += " (privat)";
            return;
        }
        case ast::NK::ImporDeklarasi: {
            const auto* x = static_cast<const ast::ImporDeklarasi*>(n);
            keluar += " saka \"" + std::string(x->modul) + "\"";
            for (const auto& s : x->daftar) keluar += " [" + std::string(s.sumber) + " -> " + std::string(s.impor) + "]";
            return;
        }
        case ast::NK::EksporDeklarasi: {
            const auto* x = static_cast<const ast::EksporDeklarasi*>(n);
            if (!x->modul.empty()) keluar += " saka \"" + std::string(x->modul) + "\"";
            for (const auto& s : x->daftar) keluar += " [" + std::string(s.lokal) + " -> " + std::string(s.ekspor) + "]";
            if (x->default_ekspor) keluar += " (default)";
            return;
        }
        case ast::NK::MandhegStmt: {
            const auto* x = static_cast<const ast::MandhegStmt*>(n);
            if (!x->label.empty()) keluar += " " + std::string(x->label);
            return;
        }
        case ast::NK::TerusnaStmt: {
            const auto* x = static_cast<const ast::TerusnaStmt*>(n);
            if (!x->label.empty()) keluar += " " + std::string(x->label);
            return;
        }
        case ast::NK::LabelStmt: keluar += " " + std::string(static_cast<const ast::LabelStmt*>(n)->label); return;
        case ast::NK::TipeAnotasi: keluar += " " + std::string(static_cast<const ast::TipeAnotasi*>(n)->nama); return;
        case ast::NK::ParamDeklarasi: {
            const auto* x = static_cast<const ast::ParamDeklarasi*>(n);
            keluar += " " + std::string(x->nama);
            if (x->rest) keluar += " (rest)";
            if (x->destructuring) keluar += " (destruktur)";
            return;
        }
        case ast::NK::PropertyAccessor: {
            const auto* x = static_cast<const ast::PropertyAccessorDeklarasi*>(n);
            keluar += " " + std::string(x->nama) + (x->getter ? " (nampa)" : " (nyetel)");
            return;
        }
        case ast::NK::AntarmukaDeklarasi: keluar += " " + std::string(static_cast<const ast::AntarmukaDeklarasi*>(n)->nama); return;
        default: return;
    }
}

void cetak(const ast::Node* n, int kedalaman, std::string& keluar) {
    if (n == nullptr) return;
    for (int i = 0; i < kedalaman; ++i) keluar += "  ";
    keluar += label(n->kind);
    rincian(n, keluar);
    keluar += "  @";
    keluar += std::to_string(n->range.mulai.baris);
    keluar += ":";
    keluar += std::to_string(n->range.mulai.kolom);
    keluar += "\n";
    std::vector<const ast::Node*> daftar;
    anak(n, daftar);
    for (const ast::Node* c : daftar) cetak(c, kedalaman + 1, keluar);
}

}  // namespace

std::string cetak_ast(const ast::Program* p) {
    std::string keluar = "Program  @1:1\n";
    if (p == nullptr) return keluar;
    for (const ast::Node* s : p->body) cetak(s, 1, keluar);
    return keluar;
}

}  // namespace jawa::parse
