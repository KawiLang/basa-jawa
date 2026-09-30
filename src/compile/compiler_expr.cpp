// Kompiler Basa Jawa bagian 3: ekspresi.
#include <algorithm>
#include <cmath>
#include <functional>

#include "compile/compiler.h"
#include "rt/number.h"
#include "rt/object.h"
#include "rt/string.h"

namespace jawa::compile {

using ast::NK;
using ast::NodePtr;
using vm::Op;

namespace {
vm::Op op_biner(ast::BinOp op) {
    using B = ast::BinOp;
    switch (op) {
        case B::Tambah: return Op::ADD;
        case B::Kurang: return Op::SUB;
        case B::Kali: return Op::MUL;
        case B::Bagi: return Op::DIV;
        case B::Modulo: return Op::MOD;
        case B::Pangkat: return Op::POW;
        case B::Lt: return Op::LT;
        case B::Le: return Op::LE;
        case B::Gt: return Op::GT;
        case B::Ge: return Op::GE;
        case B::Eq: return Op::EQ;
        case B::Ne: return Op::NE;
        case B::EqKetat: return Op::SEQ;
        case B::NeKetat: return Op::SNE;
        case B::instanceSaka: return Op::INSTANCEOF;
        case B::ing: return Op::IN;
        case B::BitDan: return Op::BIT_AND;
        case B::BitXor: return Op::BIT_XOR;
        case B::BitOr: return Op::BIT_OR;
        case B::GeserKiri: return Op::SHL;
        case B::GeserKanan: return Op::SHR;
        case B::GeserKananTanpaTanda: return Op::USHR;
        case B::Koma: return Op::POP;
        default: return Op::NOP;
    }
}

vm::Op op_assign(ast::AssignOp a) {
    using A = ast::AssignOp;
    switch (a) {
        case A::Tambah: return Op::ADD;
        case A::Kurang: return Op::SUB;
        case A::Kali: return Op::MUL;
        case A::Bagi: return Op::DIV;
        case A::Modulo: return Op::MOD;
        case A::Pangkat: return Op::POW;
        case A::GeserKiri: return Op::SHL;
        case A::GeserKanan: return Op::SHR;
        case A::GeserKananTanpaTanda: return Op::USHR;
        case A::BitDan: return Op::BIT_AND;
        case A::BitOr: return Op::BIT_OR;
        case A::BitXor: return Op::BIT_XOR;
        default: return Op::NOP;
    }
}
}  // namespace

// ===========================================================================
// Konstanta / konstanta folding
// ===========================================================================

bool Compiler::konstan(const ast::Node* n, Value& keluar) {
    if (n == nullptr) return false;
    if (n->kind == NK::Nomor) {
        const auto* num = static_cast<const ast::NomorLit*>(n);
        switch (num->bentuk) {
            case ast::NomorLit::Bentuk::Kosong: keluar = Value::kosong(); return true;
            case ast::NomorLit::Bentuk::Mboh: keluar = Value::mboh(); return true;
            case ast::NomorLit::Bentuk::NaN: keluar = Value::number(std::nan("")); return true;
            case ast::NomorLit::Bentuk::Infinity: keluar = Value::number(HUGE_VAL); return true;
            case ast::NomorLit::Bentuk::Bener: keluar = Value::boolean(true); return true;
            case ast::NomorLit::Bentuk::Salah: keluar = Value::boolean(false); return true;
            case ast::NomorLit::Bentuk::Angka:
                keluar = rt::exactly_int32(num->nilai) ? Value::angka_int32(static_cast<std::int32_t>(num->nilai))
                                                        : Value::number(num->nilai);
                return true;
        }
    }
    if (n->kind == NK::TeksLit) {
        keluar = Value::obyek(rt::buat_teks(heap_, static_cast<const ast::TeksLit*>(n)->nilai));
        return true;
    }
    if (n->kind == NK::Unary) {
        const auto* u = static_cast<const ast::UnaryExpr*>(n);
        Value dalam;
        if (!konstan(u->operand, dalam)) return false;
        switch (u->op) {
            case ast::UnOp::Neg: keluar = Value::number(-dalam.as_number()); return true;
            case ast::UnOp::Pos: keluar = dalam; return true;
                return false;
            case ast::UnOp::Jinis: keluar = Value::obyek(rt::buat_teks(heap_, rt::nama_jenis(dalam))); return true;
            default: return false;
        }
    }
    if (n->kind == NK::Biner) {
        const auto* b = static_cast<const ast::BinerExpr*>(n);
        Value a1;
        Value a2;
        if (!konstan(b->kiri, a1) || !konstan(b->kanan, a2)) return false;
        if (!a1.is_angka() || !a2.is_angka()) return false;
        const double x = a1.as_number();
        const double y = a2.as_number();
        switch (b->op) {
            case ast::BinOp::Tambah: keluar = Value::number(x + y); return true;
            case ast::BinOp::Kurang: keluar = Value::number(x - y); return true;
            case ast::BinOp::Kali: keluar = Value::number(x * y); return true;
            case ast::BinOp::Bagi: keluar = Value::number(x / y); return true;
            case ast::BinOp::Modulo: keluar = Value::number(std::fmod(x, y)); return true;
            case ast::BinOp::Pangkat: keluar = Value::number(std::pow(x, y)); return true;
            default: return false;
        }
    }
    return false;
}

// ===========================================================================
// Ekspresi
// ===========================================================================

void Compiler::ekspresi(const ast::Node* n) {
    if (n == nullptr) {
        emit(Op::MBOH);
        return;
    }
    // Constant folding.
    if (optimise_aktif_ != 0) {
        Value v;
        if (konstan(n, v)) {
            emit(Op::KONSTAN, static_cast<std::uint16_t>(tambah_konstanta(v)));
            return;
        }
    }

    switch (n->kind) {
        case NK::Nomor:
        case NK::TeksLit: {
            Value v;
            konstan(n, v);
            emit(Op::KONSTAN, static_cast<std::uint16_t>(tambah_konstanta(v)));
            return;
        }
        case NK::BigIntLit: {
            // BigInt belum ada runtime-nya; emit sebagai teks sementara.
            emit(Op::TEKS, static_cast<std::uint16_t>(tambah_konstanta(
                     Value::obyek(rt::buat_teks(heap_, std::string(static_cast<const ast::BigIntLit*>(n)->digit) + "n")))));
            return;
        }
        case NK::RegexLit: {
            // `/pola/flag` -> objek regex runtime. Pola & flag disimpan sebagai
            // konstanta berurutan; `MAKE_REGEX` membungkusnya (dan memvalidasi
            // polanya -- pola salah jadi galat runtime yang bisa ditangkap).
            const auto* r = static_cast<const ast::RegexLit*>(n);
            const std::size_t pola = tambah_konstanta(Value::obyek(rt::buat_teks(heap_, r->pola)));
            const std::size_t flag = tambah_konstanta(Value::obyek(rt::buat_teks(heap_, r->flag)));
            emit(Op::KONSTAN, static_cast<std::uint16_t>(pola));
            emit(Op::KONSTAN, static_cast<std::uint16_t>(flag));
            emit(Op::MAKE_REGEX, static_cast<std::uint16_t>(0));
            return;
        }
        case NK::TemplateLit: eks_template(static_cast<const ast::TemplateLit*>(n)); return;
        case NK::CocogExpr: eks_cocog(static_cast<const ast::CocogExpr*>(n)); return;
        case NK::RefIdent: emit_baca_nama(static_cast<const ast::RefIdent*>(n)->nama); return;
        case NK::ThisExpr: emit(Op::GET_LOCAL, 0); return;  // slot 0 dicadangkan untuk `this`
        case NK::SuperExpr: emit(Op::MBOH); return;  // `super` belum didukung (Fase 5)
        case NK::ArrayLit: eks_dhaptar(static_cast<const ast::ArrayLit*>(n)); return;
        case NK::ObjectLit: eks_objek(static_cast<const ast::ObjectLit*>(n)); return;
        case NK::AksesProperti: {
            const auto* a = static_cast<const ast::AksesProperti*>(n);
            if (a->objek != nullptr && a->objek->kind == NK::SuperExpr) {
                // `induk.x()` -> cari method di prototipe class induk.
                emit(Op::GET_LOCAL, 0);
                emit(Op::GET_SUPER, static_cast<std::uint16_t>(tambah_nama(
                         Value::obyek(rt::buat_teks(heap_, a->nama)))));
                return;
            }
            ekspresi(a->objek);
            if (a->komputat != nullptr) {
                ekspresi(a->komputat);
                emit(Op::GET_INDEX);
            } else {
                const std::size_t k = tambah_nama(Value::obyek(rt::buat_teks(heap_, a->nama)));
                emit(Op::GET_PROP, static_cast<std::uint16_t>(k));
            }
            return;
        }
        case NK::Panggilan: eks_panggilan(static_cast<const ast::Panggilan*>(n)); return;
        case NK::Unary: {
            const auto* u = static_cast<const ast::UnaryExpr*>(n);
            if (u->op == ast::UnOp::Jinis) {
                ekspresi(u->operand);
                emit(Op::TYPEOF);
                return;
            }
            if (u->op == ast::UnOp::Neg) { ekspresi(u->operand); emit(Op::NEG); return; }
            if (u->op == ast::UnOp::Ora) { ekspresi(u->operand); emit(Op::NOT); return; }
            if (u->op == ast::UnOp::BitNot) { ekspresi(u->operand); emit(Op::BIT_NOT); return; }
            if (u->op == ast::UnOp::Busak) { ekspresi(u->operand); emit(Op::DELETE); return; }
            if (u->op == ast::UnOp::Entani) {
                ekspresi(u->operand);
                if (!ada_fungsi_&&fungsi_stack_.back().chunk != nullptr) {
                    fungsi_stack_.back().chunk->await_tingkat_modul = true;
                }
                emit(Op::AWAIT);
                return;
            }
            if (u->op == ast::UnOp::Metokake) { ekspresi(u->operand); emit(Op::YIELD); return; }
            if (u->op == ast::UnOp::Pos) { ekspresi(u->operand); return; }
            ekspresi(u->operand);
            return;
        }
        case NK::Biner: eks_biner(static_cast<const ast::BinerExpr*>(n)); return;
        case NK::Logika: {
            // `a lan b` / `a utawa b` / `a ?? b`: short-circuit; nilai kiri
            // dipertahankan bila lompatan diambil (lompatan hanya "peek").
            const auto* l = static_cast<const ast::LogikaExpr*>(n);
            ekspresi(l->kiri);
            const std::size_t lompat = emit(l->op == ast::LogOp::Lan ? Op::JUMP_IF_FALSE
                                                  : (l->op == ast::LogOp::Utawa ? Op::JUMP_IF_TRUE : Op::JUMP_IF_NOT_NULLISH), 0);
            emit(Op::POP);
            ekspresi(l->kanan);
            patch(lompat, fn().chunk->ukuran_kode());
            return;
        }
        case NK::Rangkaian: {
            // `x |> f` == `f(x)`. Stack: [this=mboh, f, x]
            const auto* r = static_cast<const ast::RangkaianExpr*>(n);
            emit(Op::MBOH);
            ekspresi(r->kanan);
            ekspresi(r->kiri);
            emit(Op::CALL, 1);
            return;
        }
        case NK::Kondisional: {
            const auto* k = static_cast<const ast::KondisionalExpr*>(n);
            ekspresi(k->kondisi);
            const std::size_t lompat_salah = emit(Op::JUMP_IF_FALSE, 0);
            emit(Op::POP);
            ekspresi(k->bila_benar);
            const std::size_t lompat_akhir = emit(Op::JUMP, 0);
            patch(lompat_salah, fn().chunk->ukuran_kode());
            emit(Op::POP);
            ekspresi(k->bila_salah);
            patch(lompat_akhir, fn().chunk->ukuran_kode());
            return;
        }
        case NK::Penugasan: {
            const auto* p = static_cast<const ast::PenugasanExpr*>(n);
            const ast::Node* t = p->target;
            if (p->op == ast::AssignOp::Set) {
                if (t != nullptr && t->kind == NK::AksesProperti) {
                    const auto* a = static_cast<const ast::AksesProperti*>(t);
                    ekspresi(a->objek);
                    ekspresi(p->nilai);
                    if (a->objek != nullptr && a->objek->kind == NK::SuperExpr) {
                        // `induk.x = v` -> cari setter di prototipe induk.
                        const std::size_t k =
                            tambah_nama(Value::obyek(rt::buat_teks(heap_, a->nama)));
                        emit(Op::SET_SUPER, static_cast<std::uint16_t>(k));
                        return;
                    }
                    // `SET_PROP`/`SET_INDEX` sudah menyisakan nilainya di stack,
                    // jadi penugasan sebagai ekspresi tidak perlu `DUP`.
                    if (a->komputat != nullptr) {
                        emit(Op::SET_INDEX);
                    } else {
                        const std::size_t k = tambah_nama(Value::obyek(rt::buat_teks(heap_, a->nama)));
                        emit(Op::SET_PROP, static_cast<std::uint16_t>(k));
                    }
                    return;
                }
                if (t != nullptr && t->kind == NK::RefIdent) {
                    ekspresi(p->nilai);
                    // Penugasan adalah ekspresi: sisakan nilainya di stack
                    // (`x = 1` bernilai 1), lalu tulis ulang.
                    emit(Op::DUP);
                    emit_tulis_nama(static_cast<const ast::RefIdent*>(t)->nama);
                    return;
                }
                (void)0;
                // bentuk lain: nilai saja
                ekspresi(p->nilai);
                return;
            }
            // Penugasan majemuk: baca target, hitung, tulis balik.
            if (t != nullptr && t->kind == NK::AksesProperti) {
                const auto* a = static_cast<const ast::AksesProperti*>(t);
                ekspresi(a->objek);
                emit(Op::DUP);
                if (a->komputat != nullptr) {
                    ekspresi(a->komputat);
                    emit(Op::DUP2);
                    emit(Op::GET_INDEX);
                } else {
                    const std::size_t k = tambah_nama(Value::obyek(rt::buat_teks(heap_, a->nama)));
                    emit(Op::GET_PROP, static_cast<std::uint16_t>(k));
                }
                ekspresi(p->nilai);
                emit(op_assign(p->op));
                if (a->komputat != nullptr) emit(Op::SET_INDEX);
                else emit(Op::SET_PROP, static_cast<std::uint16_t>(0));
                return;
            }
            if (t != nullptr && t->kind == NK::RefIdent) {
                emit_baca_nama(static_cast<const ast::RefIdent*>(t)->nama);
                ekspresi(p->nilai);
                emit(op_assign(p->op));
                emit(Op::DUP);
                emit_tulis_nama(static_cast<const ast::RefIdent*>(t)->nama);
                return;
            }
            ekspresi(p->nilai);
            return;
        }
        case NK::Pembaruan: {
            const auto* u = static_cast<const ast::PembaruanExpr*>(n);
            const ast::Node* t = u->target;
            if (t != nullptr && t->kind == NK::RefIdent) {
                // Postfix menyisakan nilai LAMA, prefix menyisakan nilai BARU.
                const auto* r = static_cast<const ast::RefIdent*>(t);
                emit_baca_nama(r->nama);
                if (!u->prefiks) emit(Op::DUP);
                emit(u->op == ast::UnOp::PlusPlus ? Op::INC : Op::DEC);
                if (u->prefiks) emit(Op::DUP);
                emit_tulis_nama(r->nama);
                return;
            }
            if (t != nullptr && t->kind == NK::AksesProperti) {
                const auto* a = static_cast<const ast::AksesProperti*>(t);
                ekspresi(a->objek);
                emit(Op::DUP);
                const std::size_t k = tambah_nama(Value::obyek(rt::buat_teks(heap_, a->nama)));
                emit(Op::GET_PROP, static_cast<std::uint16_t>(k));
                emit(u->op == ast::UnOp::PlusPlus ? Op::INC : Op::DEC);
                emit(u->prefiks ? Op::DUP : Op::POP);
                emit(Op::SET_PROP, static_cast<std::uint16_t>(k));
                return;
            }
            return;
        }
        case NK::Fungsi: eks_fungsi(static_cast<const ast::FungsiDeklarasi*>(n)); return;
        case NK::_anyar: {
            const auto* a = static_cast<const ast::AnyarExpr*>(n);
            ekspresi(a->konstruktor);
            for (const ast::Node* arg : a->argumen) {
                if (arg != nullptr && arg->kind == NK::ElemenArr && static_cast<const ast::ElemenArr*>(arg)->spread) {
                    ekspresi(static_cast<const ast::ElemenArr*>(arg)->nilai);
                    emit(Op::SPREAD);
                } else {
                    ekspresi(arg);
                }
            }
            emit(Op::NEW, static_cast<std::uint16_t>(a->argumen.size()));
            return;
        }
        case NK::KunciPrivat: {
            // Field privat memakai nama berawalan "#" sebagai kunci storage.
            // Sifat privat dijaga kompilator (nama tidak bisa ditulis user di
            // luar kelas); runtime hanya melihat string "#nama".
            const auto* k = static_cast<const ast::KunciPrivatExpr*>(n);
            std::string mangled(k->nama);
            if (mangled.empty() || mangled[0] != '#') mangled.insert(mangled.begin(), '#');
            emit(Op::NOMOR, static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, mangled)))));
            return;
        }
        case NK::ImporDinamis: {
            const auto* im = static_cast<const ast::ImporDinamisExpr*>(n);
            ekspresi(im->spesifikasi);
            emit(Op::IMPORT);
            return;
        }
        case NK::FungsiDeklarasi: eks_fungsi(static_cast<const ast::FungsiDeklarasi*>(n)); return;
        default: {
            // Fallback: bukan ekspresi yang sudah dikenali -> mboh.
            emit(Op::MBOH);
            return;
        }
    }
}

std::size_t Compiler::slot_pola(std::string_view nama) {
    const std::size_t s = cari_slot(nama);
    if (s != static_cast<std::size_t>(-1)) return s;
    return slot_baru(nama);
}

void Compiler::susun_pola(const ast::Pola* p, std::size_t s_subj, std::vector<std::size_t>& lompat_gagal) {
    if (p == nullptr) {
        emit(Op::BENER);
        return;
    }
    switch (p->jenis) {
        case ast::Pola::Jenis::Wildcard:
            emit(Op::BENER);
            return;
        case ast::Pola::Jenis::Nama: {
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_subj));
            emit_tulis_nama_statement(p->nama);
            emit(Op::BENER);
            return;
        }
        case ast::Pola::Jenis::Literal: {
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_subj));
            ekspresi(p->nilai);
            emit(Op::SEQ);
            return;
        }
        case ast::Pola::Jenis::Ekspresi: {
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_subj));
            ekspresi(p->nilai);
            emit(Op::SEQ);
            return;
        }
        case ast::Pola::Jenis::Alternatif: {
            for (const ast::Node* alt : p->alternatif) {
                susun_pola(static_cast<const ast::Pola*>(alt), s_subj, lompat_gagal);
                lompat_gagal.push_back(emit(Op::JUMP_IF_TRUE, 0));
                emit(Op::POP);
            }
            emit(Op::SALAH);
            return;
        }
        case ast::Pola::Jenis::Dhaptar: {
            // Subjek harus dhaptar dengan panjang yang cocok (`...sisa` = +1).
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_subj));
            emit(Op::IS_ARRAY);
            lompat_gagal.push_back(emit(Op::JUMP_IF_FALSE, 0));
            emit(Op::POP);
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_subj));
            emit(Op::GET_PROP, static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, "dawa")))));
            // Tanpa `...sisa` panjang harus PERSIS sama. Dengan `...sisa`,
            // cukup "minimal" -- sisa elemennya yang disusun ke dhaptar baru.
            if (p->sisanya != nullptr) {
                emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_subj));
                emit(Op::GET_PROP, static_cast<std::uint16_t>(tambah_nama(
                         Value::obyek(rt::buat_teks(heap_, "dawa")))));
                emit(Op::KONSTAN, static_cast<std::uint16_t>(tambah_konstanta(
                         Value::number(static_cast<double>(p->elemen.size())))));
                emit(Op::GE);
            } else {
                emit(Op::KONSTAN, static_cast<std::uint16_t>(tambah_konstanta(
                         Value::number(static_cast<double>(p->elemen.size())))));
                emit(Op::SEQ);
            }
            lompat_gagal.push_back(emit(Op::JUMP_IF_FALSE, 0));
            emit(Op::POP);
            for (std::size_t i = 0; i < p->elemen.size(); ++i) {
                emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_subj));
                emit(Op::KONSTAN, static_cast<std::uint16_t>(tambah_konstanta(Value::number(static_cast<double>(i)))));
                emit(Op::GET_INDEX);
                susun_pola_stack(static_cast<const ast::Pola*>(p->elemen[i]), lompat_gagal);
                lompat_gagal.push_back(emit(Op::JUMP_IF_FALSE, 0));
                emit(Op::POP);
            }
            if (p->sisanya != nullptr) {
                // `...sisa` mengikat SISA dhaptar sebagai dhaptar baru, bukan
                // seluruh subjek. Jumlahnya hanya diketahui saat runtime, jadi
                // elemen dikumpulkan ke stack lalu `MAKE_ARRAY_SPREAD` yang
                // menyusunnya (batas ditandai `MARK_SPREAD`).
                const auto* pola_sisa = static_cast<const ast::Pola*>(p->sisanya);
                const std::size_t s_i = fn().n_slot_terpakai++;
                fn().n_slot_maks = std::max(fn().n_slot_maks, fn().n_slot_terpakai);
                emit(Op::KONSTAN, static_cast<std::uint16_t>(tambah_konstanta(
                         Value::number(static_cast<double>(p->elemen.size())))));
                emit(Op::SET_LOCAL, static_cast<std::uint16_t>(s_i));
                emit(Op::MARK_SPREAD);
                const std::size_t l_loop = fn().chunk->ukuran_kode();
                // `len > i` ?
                emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_subj));
                emit(Op::GET_PROP, static_cast<std::uint16_t>(tambah_nama(
                         Value::obyek(rt::buat_teks(heap_, "dawa")))));
                emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_i));
                emit(Op::GT);
                const std::size_t l_keluar = emit(Op::JUMP_IF_FALSE, 0);
                emit(Op::POP);
                // dorong subjek[i]
                emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_subj));
                emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_i));
                emit(Op::GET_INDEX);
                // i = i + 1
                emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_i));
                emit(Op::KONSTAN, static_cast<std::uint16_t>(tambah_konstanta(Value::number(1))));
                emit(Op::ADD);
                emit(Op::SET_LOCAL, static_cast<std::uint16_t>(s_i));
                emit(Op::JUMP, static_cast<std::uint16_t>(l_loop));
                patch(l_keluar, fn().chunk->ukuran_kode());
                emit(Op::POP);
                emit(Op::MAKE_ARRAY_SPREAD);
                emit_tulis_nama_statement(pola_sisa->nama);
            }
            emit(Op::BENER);
            return;
        }
        case ast::Pola::Jenis::Objek: {
            // Subjek harus objek; setiap properti diambil & di-binding.
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_subj));
            emit(Op::IS_OBJECT);
            lompat_gagal.push_back(emit(Op::JUMP_IF_FALSE, 0));
            emit(Op::POP);
            for (const ast::Node* pn : p->properti) {
                const auto* pp = static_cast<const ast::PropertiPola*>(pn);
                if (pp == nullptr || pp->spread) continue;
                std::string_view kunci;
                if (pp->nama.empty() && pp->kunci != nullptr && pp->kunci->kind == NK::TeksLit) {
                    kunci = static_cast<const ast::TeksLit*>(pp->kunci)->nilai;
                } else {
                    kunci = pp->nama;
                }
                if (kunci.empty()) continue;
                emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_subj));
                emit(Op::GET_PROP, static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, kunci)))));
                if (!pp->nama.empty()) {
                    emit_tulis_nama_statement(pp->nama);
                } else if (pp->pola != nullptr) {
                    const std::size_t s = slot_pola(static_cast<const ast::Pola*>(pp->pola)->nama);
                    emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(s));
                }
            }
            emit(Op::BENER);
            return;
        }
        case ast::Pola::Jenis::Tipe: {
            // Pola bertipe: `kasus n: teks => ...` atau `kasus n: Kucing => ...`.
            //
            // BUG LAMA: sebelumnya `emit(Op::TYPEOF)` lalu langsung `BENER`,
            // tanpa perbandingan -- jadi pola ini SELALU cocok apa pun nilainya.
            //
            // Nama yang di-binding adalah NILAI subjek, bukan nama tipenya.
            susun_uji_tipe(p->tipe, s_subj, p->nama);
            return;
        }
    }
    emit(Op::SALAH);
}

// Susun uji tipe untuk pola `n: Tipe`. `sumber` adalah slot lokal yang memuat
// nilai subjek; hasilnya `bener`/`salah` di stack.
//
// Bentuk yang didukung:
//   `teks`, `angka`, `dhaptar`, ... -> bandingkan `rt::nama_jenis` (COCOK_TIPE)
//   `dhaptar<T>`                    -> hanya memeriksa jenisnya
//   `T?`                            -> `T` ATAU `mboh`
//   `T | U`                         -> salah satunya
//   `{ ... }` (TipeObjek)           -> IS_OBJECT
//   `Kucing` (huruf kapital)        -> instans class itu atau induknya
//   `apa_wae` / bentuk tak dikenal  -> selalu cocok
void Compiler::susun_uji_tipe_dasar(const ast::Node* tipe, std::size_t sumber) {
    if (tipe == nullptr) {
        emit(Op::BENER);
        return;
    }
    switch (tipe->kind) {
        case NK::TipeAnotasi: {
            const std::string_view nama = static_cast<const ast::TipeAnotasi*>(tipe)->nama;
            if (nama.empty() || nama == "apa_wae") {
                emit(Op::BENER);
                return;
            }
            const bool kapital = nama[0] >= 'A' && nama[0] <= 'Z';
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(sumber));
            if (kapital) {
                emit(Op::INSTAN_DARI,
                     static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, nama)))));
            } else {
                emit(Op::COCOK_TIPE,
                     static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, nama)))));
            }
            return;
        }
        case NK::TipeArray: {
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(sumber));
            emit(Op::COCOK_TIPE,
                 static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, "dhaptar")))));
            return;
        }
        case NK::TipeOpsional: {
            // `T?` = `T` atau `mboh`.
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(sumber));
            emit(Op::COCOK_TIPE,
                 static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, "mboh")))));
            const std::size_t sudah = emit(Op::JUMP_IF_TRUE, 0);
            emit(Op::POP);
            susun_uji_tipe_dasar(static_cast<const ast::TipeOpsional*>(tipe)->dasar, sumber);
            patch(sudah, fn().chunk->ukuran_kode());
            return;
        }
        case NK::TipeReferensi: {
            // `dhaptar<angka>` sudah ditangani `TipeArray`; bentuk referensi
            // lain (mis. `Janji<angka>`) belum dinilai -- selalu cocok.
            emit(Op::BENER);
            return;
        }
        case NK::TipeUnion: {
            // Semua varian diuji berurutan; yang cocok langsung lompat ke
            // `L_akhir` (setelah semua varian), bukan ke varian berikutnya --
            // kalau dilompat ke varian berikutnya, emission-nya ikut dijalankan
            // dan sisanya menumpuk boolean di stack.
            const auto* un = static_cast<const ast::TipeUnion*>(tipe);
            std::vector<std::size_t> lompat;
            for (const ast::Node* v : un->varian) {
                susun_uji_tipe_dasar(v, sumber);
                lompat.push_back(emit(Op::JUMP_IF_TRUE, 0));
                emit(Op::POP);
            }
            emit(Op::SALAH);
            const std::size_t akhir = fn().chunk->ukuran_kode();
            for (std::size_t l : lompat) patch(l, akhir);
            return;
        }
        case NK::TipeObjek: {
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(sumber));
            emit(Op::IS_OBJECT);
            return;
        }
        default:
            // Bentuk yang belum dinilai (referensi generics, tipe fungsi):
            // belum diperiksa -- selalu cocok, sama seperti `CEK_TIPE` untuk
            // tipe kustom.
            emit(Op::BENER);
            return;
    }
}

void Compiler::susun_uji_tipe(const ast::Node* tipe, std::size_t s_subj, std::string_view bind) {
    // `bind` diisi dengan NILAI subjek (bukan nama tipenya). Nilai disalin ke
    // slot temporer lebih dulu supaya uji tipe tetap bisa membacanya, lalu
    // ditulis ke nama pengikat.
    if (bind.empty()) {
        susun_uji_tipe_dasar(tipe, s_subj);
        return;
    }
    emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_subj));
    const std::size_t tmp = fn().n_slot_terpakai++;
    fn().n_slot_maks = std::max(fn().n_slot_maks, fn().n_slot_terpakai);
    emit(Op::SET_LOCAL, static_cast<std::uint16_t>(tmp));
    emit(Op::GET_LOCAL, static_cast<std::uint16_t>(tmp));
    emit_tulis_nama_statement(bind);
    susun_uji_tipe_dasar(tipe, tmp);
}

void Compiler::susun_pola_stack(const ast::Pola* p, std::vector<std::size_t>& lompat_gagal) {
    // Subjek sudah di puncak stack (hasil `GET_INDEX` atau `GET_PROP`).
    if (p == nullptr) {
        emit(Op::POP);
        emit(Op::BENER);
        return;
    }
    switch (p->jenis) {
        case ast::Pola::Jenis::Wildcard:
            emit(Op::POP);
            emit(Op::BENER);
            return;
        case ast::Pola::Jenis::Nama: {
            const std::size_t s = slot_pola(p->nama);
            emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(s));
            emit(Op::BENER);
            return;
        }
        case ast::Pola::Jenis::Literal:
        case ast::Pola::Jenis::Ekspresi:
            ekspresi(p->nilai);
            emit(Op::SEQ);
            return;
        case ast::Pola::Jenis::Alternatif: {
            for (const ast::Node* alt : p->alternatif) {
                emit(Op::DUP);
                susun_pola_stack(static_cast<const ast::Pola*>(alt), lompat_gagal);
                lompat_gagal.push_back(emit(Op::JUMP_IF_TRUE, 0));
                emit(Op::POP);
            }
            emit(Op::POP);
            emit(Op::SALAH);
            return;
        }
        default: {
            // Bentuk lain (pola `[..]`/pola `{..}`): simpan sementara ke slot lalu
            // pakai jalur berbasis-slot.
            const std::size_t s = fn().n_slot_terpakai++;
            fn().n_slot_maks = std::max(fn().n_slot_maks, fn().n_slot_terpakai);
            emit(Op::SET_LOCAL, static_cast<std::uint16_t>(s));
            susun_pola(p, s, lompat_gagal);
            return;
        }
    }
}

void Compiler::eks_cocog(const ast::CocogExpr* n) {
    // `<subjek>` disimpan di slot sementara. Setiap kasus diuji berurutan; kasus
    // yang tidak cocok lompat ke label gagalnya (membuang satu nilai `bener`/
    // `salah` yang tersisa), lalu jatuh ke kasus berikutnya.
    ekspresi(n->subjek);
    const std::size_t s_subj = fn().n_slot_terpakai++;
    fn().n_slot_maks = std::max(fn().n_slot_maks, fn().n_slot_terpakai);
    emit(Op::SET_LOCAL, static_cast<std::uint16_t>(s_subj));

    std::vector<std::size_t> lompat_akhir;
    for (const ast::Node* kn : n->kasus) {
        const auto* k = static_cast<const ast::KasusKocog*>(kn);
        if (k == nullptr) continue;
        const auto* pola = static_cast<const ast::Pola*>(k->pola);

        std::vector<std::size_t> lompat_gagal;
        susun_pola(pola, s_subj, lompat_gagal);
        const std::size_t gagal_kasus = emit(Op::JUMP_IF_FALSE, 0);
        emit(Op::POP);
        if (pola != nullptr && pola->penjaga != nullptr) {
            ekspresi(pola->penjaga);
            const std::size_t lompat_cocok = emit(Op::JUMP_IF_TRUE, 0);
            emit(Op::POP);
            // Penjaga gagal: Treat as "kasus tidak cocok".
            const std::size_t lanjut = emit(Op::JUMP, 0);
            patch(lompat_cocok, fn().chunk->ukuran_kode());
            emit(Op::POP);
            ekspresi(k->nilai);
            lompat_akhir.push_back(emit(Op::JUMP, 0));
            patch(lanjut, fn().chunk->ukuran_kode());
            // Di sini nilai boolean penjaga sudah dibuang; goto label gagal.
        } else {
            ekspresi(k->nilai);
            lompat_akhir.push_back(emit(Op::JUMP, 0));
        }
        for (std::size_t p : lompat_gagal) patch(p, fn().chunk->ukuran_kode());
        patch(gagal_kasus, fn().chunk->ukuran_kode());
        emit(Op::POP);
    }
    // Tidak ada kasus yang cocok -> `mboh`. Lompatan keluar harus menunjuk ke
    // SETELAH `MBOH` ini, jadi emit dulu baru patch.
    emit(Op::MBOH);
    for (std::size_t l : lompat_akhir) patch(l, fn().chunk->ukuran_kode());
}

void Compiler::emit_baca_nama(std::string_view nama) {
    const std::size_t s = cari_slot(nama);
    // Zona mati-temporal: kalau slot ini pengikat leksikal yang deklarasinya
    // BELUM terkompilasi, pembacaan sekarang adalah pelanggaran TDZ. Setelah
    // deklarasi terkompilasi entrinya dibuang, jadi pembacaan berikutnya gratis.
    if (s != static_cast<std::size_t>(-1)) {
        if (const auto it = fn().tdz_menunggu.find(s); it != fn().tdz_menunggu.end()) {
            emit(Op::TDZ_CHECK, static_cast<std::uint16_t>(it->second));
        }
    }
    if (s != static_cast<std::size_t>(-1)) {
        // Pengikatan impor & variabel modul yang diekspor disimpan sebagai sel;
        // membacanya berarti membaca isi sel, bukan objek sel itu sendiri.
        emit(adalah_sel(nama) ? Op::GET_CELL : Op::GET_LOCAL, static_cast<std::uint16_t>(s));
        return;
    }
    if (fn().dalam_fungsi) {
        // Nama mungkin milik fungsi di luar: catat sebagai upvalue. Resolusinya
        // (lokal nenek moyang atau upvalue induk) dilakukan `eks_fungsi` setelah
        // body selesai dikompilasi.
        emit(Op::GET_UPVAL, static_cast<std::uint16_t>(cari_upvalue(nama)));
        return;
    }
    emit(Op::GET_GLOBAL, static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, nama)))));
}

void Compiler::emit_tulis_nama(std::string_view nama) {
    // Efek bersih harus `v - -` untuk semua jalur; lihat `compiler.h`.
    const std::size_t s = cari_slot(nama);
    if (s != std::string::npos && !adalah_sel(nama)) {
        emit(Op::SET_LOCAL, static_cast<std::uint16_t>(s));  // sudah consuming
        return;
    }
    if (s != std::string::npos) {
        emit(Op::SET_CELL, static_cast<std::uint16_t>(s));
    } else if (fn().dalam_fungsi) {
        emit(Op::SET_UPVAL, static_cast<std::uint16_t>(cari_upvalue(nama)));
    } else {
        emit(Op::SET_GLOBAL,
             static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, nama)))));
    }
    emit(Op::POP);
}

void Compiler::emit_tulis_nama_statement(std::string_view nama) {
    // `emit_tulis_nama` sudah memakai efek bersih `v - -` untuk semua jalur,
    // jadi bentuk statement tidak menambah apa pun.
    emit_tulis_nama(nama);
}

void Compiler::eks_biner(const ast::BinerExpr* n) {
    ekspresi(n->kiri);
    ekspresi(n->kanan);
    emit(op_biner(n->op));
}

void Compiler::eks_panggilan(const ast::Panggilan* n) {
    // Nilai `this` implisit bila callee adalah akses properti.
    const ast::Node* c = n->callee;
    if (c != nullptr && c->kind == NK::AksesProperti) {
        const auto* a = static_cast<const ast::AksesProperti*>(c);
        const std::size_t k = tambah_nama(Value::obyek(rt::buat_teks(heap_, a->nama)));
        if (a->objek != nullptr && a->objek->kind == NK::SuperExpr) {
            // `induk.f()` -> method dari prototipe class induk, `this` tetap sama.
            emit(Op::GET_LOCAL, 0);  // this
            emit(Op::DUP);
            emit(Op::GET_SUPER, static_cast<std::uint16_t>(k));
        } else {
            ekspresi(a->objek);
            emit(Op::DUP);
            if (a->komputat != nullptr) {
                ekspresi(a->komputat);
                emit(Op::GET_INDEX);
            } else {
                emit(Op::GET_PROP, static_cast<std::uint16_t>(k));
            }
        }
        // stack: this, this.f
    } else {
        emit(Op::MBOH);
        ekspresi(c);
    }
    std::size_t n_arg = 0;
    bool spread = false;
    for (const ast::Node* arg : n->argumen) {
        if (arg != nullptr && arg->kind == NK::ElemenArr && static_cast<const ast::ElemenArr*>(arg)->spread) {
            ekspresi(static_cast<const ast::ElemenArr*>(arg)->nilai);
            emit(Op::SPREAD);
            spread = true;
            ++n_arg;
            continue;
        }
        ekspresi(arg);
        ++n_arg;
    }
    emit(spread ? Op::CALL_SPREAD : Op::CALL, static_cast<std::uint16_t>(n_arg));
}

void Compiler::eks_fungsi(const ast::FungsiDeklarasi* n) {
    // --- Konteks fungsi baru -------------------------------------------
    FungsiKonteks f;
    f.chunk = std::make_shared<vm::Chunk>();
    f.chunk->nama = n->nama.empty() ? std::string_view("<anon>") : n->nama;
    f.info = FungsiInfo{};
    f.info.nama = f.chunk->nama;
    f.info.panah = n->panah;
    f.info.mengko = n->mengko;
    f.info.generator = n->generator;
    f.info.method = n->metode;
    f.info.privat = n->privat;
    f.info.ini_boleh = n->metode;
    f.dalam_fungsi = true;

    // Parameter occupy slot 1..n; slot 0 dicadangkan untuk `this` (agar
    // `GET_LOCAL 0` selalu berarti `this`).
    std::size_t s = 1;
    for (const ast::Node* pn : n->param) {
        const auto* p = static_cast<const ast::ParamDeklarasi*>(pn);
        if (p == nullptr || p->nama.empty()) {
            ++s;
            continue;
        }
        f.lokal[std::string(p->nama)] = s;
        f.info.nama_param.push_back(p->nama);
        if (p->rest) f.info.variadic = true;
        ++s;
    }
    f.n_slot_terpakai = s;
    f.n_slot_maks = s;
    f.info.arity = n->param.size();

    // Anotasi tipe parameter disimpan agar bisa diperiksa saat fungsi dipanggil.
    // Nama tipe diambil dari `ParamDeklarasi::tipe` (bukan `FungsiDeklarasi::tipe_param`).
    std::vector<std::string_view> tipe_param;
    for (const ast::Node* pn : n->param) {
        std::string_view nama_tipe;
        if (pn != nullptr) {
            const ast::Node* tn = static_cast<const ast::ParamDeklarasi*>(pn)->tipe;
            if (tn != nullptr && tn->kind == NK::TipeAnotasi) {
                nama_tipe = static_cast<const ast::TipeAnotasi*>(tn)->nama;
            }
        }
        tipe_param.push_back(nama_tipe);
    }
    std::string_view tipe_bali;
    if (n->tipe_bali != nullptr && n->tipe_bali->kind == NK::TipeAnotasi) {
        tipe_bali = static_cast<const ast::TipeAnotasi*>(n->tipe_bali)->nama;
    }

    fungsi_stack_.push_back(std::move(f));
    FungsiKonteks& ctx = fn();

    // --- Body ------------------------------------------------------------
    // Pra-walk TDZ untuk badan fungsi ini juga (bukan hanya modul), supaya
    // `gawe f() { tulis(y); tetep y = 1; }` menghasilkan galat TDZ.
    if (!n->ekspresi_badan) pradaftar_tdz(n->awak);

    // Prolog parameter default: kalau pemanggil TIDAK memberikan argumen pada
    // posisi itu, isi slot dengan nilai default-nya.
    //
    // CATATAN: `nilai_default` sudah lama di-parse tapi TIDAK pernah dikompilasi,
    // jadi `gawe f(a, b = 2) {}` diam-diam memberi `mboh` untuk `b` (ditemukan
    // regression test hasil fuzzing, bukan test yang ditulis orang).
    //
    // Penentuan "argumen tidak diberikan" memakai opcode `PARAM_HADAH a`, yang
    // membaca `Frame::n_argumen` -- BUKAN `JUMP_IF_NOT_NULLISH` atas nilainya.
    // Bedanya menentukan: dengan `JUMP_IF_NOT_NULLISH`, pemanggilan
    // `f(mboh)` yang SENGAJA mengosongkan argumen ikut memakai nilai default,
    // jadi `f(mboh)` dan `f()` tidak bisa dibedakan. Lihat D-036.
    for (std::size_t i = 0; i < n->param.size(); ++i) {
        const auto* par = static_cast<const ast::ParamDeklarasi*>(n->param[i]);
        if (par == nullptr || par->rest || par->nilai_default == nullptr) continue;
        const std::size_t slot = i + 1;
        if (slot > 0xFFu) continue;
        emit(Op::PARAM_HADAH, static_cast<std::uint16_t>(i));
        const std::size_t l_ada = emit(Op::JUMP_IF_TRUE, 0);  // hanya peek
        emit(Op::POP);
        emit(Op::MBOH);
        ekspresi(par->nilai_default);
        emit(Op::SET_LOCAL, static_cast<std::uint16_t>(slot));
        patch(l_ada, fn().chunk->ukuran_kode());
    }

    // Cek tipe awal parameter: dijalankan setiap kali fungsi dipanggil.
    for (std::size_t i = 0; i < tipe_param.size(); ++i) {
        if (tipe_param[i].empty()) continue;
        emit(Op::GET_LOCAL, static_cast<std::uint16_t>(i + 1));
        emit(Op::CEK_TIPE,
             static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, tipe_param[i])))));
        emit(Op::POP);
    }
    const std::size_t k_tipe_bali =
        tipe_bali.empty() ? 0 : tambah_nama(Value::obyek(rt::buat_teks(heap_, tipe_bali)));
    if (n->ekspresi_badan) {
        ekspresi(n->badan_ekspresi);
        if (k_tipe_bali != 0) emit(Op::CEK_TIPE, static_cast<std::uint16_t>(k_tipe_bali));
        emit(Op::RETURN);
    } else {
        stmt_awak(n->awak);
        emit(Op::MBOH);
        if (k_tipe_bali != 0) emit(Op::CEK_TIPE, static_cast<std::uint16_t>(k_tipe_bali));
        emit(Op::RETURN_UNDEF);
    }
    ctx.chunk->jumlah_slot = static_cast<std::uint8_t>(std::min<std::size_t>(ctx.n_slot_maks, 250));
    ctx.chunk->jumlah_param = static_cast<std::uint8_t>(n->param.size());
    // Jumlah parameter TANPA rest. VM memakai ini untuk menyalin argumen ke
    // slot, lalu menaruh dhaptar sisa tepat sesudahnya. Kalau ini sama dengan
    // `jumlah_param`, dhaptar sisa mendarat satu slot terlalu jauh dan
    // parameter rest justru membaca argumen biasa (ditemukan fuzzing:
    // `gawe f(a, ...sisa) { bali jenis(sisa); }` menghasilkan `angka`).
    ctx.chunk->n_argumen_tetap = 0;
    for (const ast::Node* pn : n->param) {
        const auto* pd = static_cast<const ast::ParamDeklarasi*>(pn);
        if (pd != nullptr && !pd->rest) ++ctx.chunk->n_argumen_tetap;
    }
    ctx.chunk->variadic = ctx.info.variadic;
    ctx.chunk->panah = n->panah;
    ctx.chunk->mengko = n->mengko;
    ctx.chunk->generator = n->generator;
    ctx.chunk->peta_baris.finalize();

    // --- Resolusi upvalue (Lox) ---------------------------------------------
    //
    // Untuk setiap nama yang dibaca fungsi ini, cari selnya dengan naik satu
    // tingkat demi satu tingkat dari induk langsung, lalu kembalikan encode:
    //
    //   `>= 0`        -> indeks lokal pada nenek moyang yang memilikinya
    //   `-k-1`         -> upvalue ke-`k` milik nenek moyang itu
    //   `-0x40000000`  -> tidak ada di mana pun: perlakukan sebagai global
    //                     (`GET_UPVAL` dengan sel null)
    //
    // Dua detail yang menentukan benar/tidaknya hasil:
    //
    // 1. Hanya induk langsung boleh "menang" atas nama yang sama di modul.
    //    Kalau semua nenek moyang dipindai dari luar ke dalam sekaligus,
    //    bayangan (shadowing) rusak: closure membaca slot yang salah. Gejalanya
    //    hanya muncul bila ada variabel modul yang namanya sama dengan variabel
    //    lokal di fungsi -- program pendek tidak pernah mengalaminya, jadi
    //    bug-nya mudah sekali tersembunyi.
    //
    // 2. Kalau suatu nenek moyang tidak punya nama itu sebagai lokal DAN belum
    //    punya upvalue untuk nama itu, permintaannya HARUS didaftarkan ke
    //    `ambil_upvalue` nenek moyang itu -- supaya nenek moyang ikut menarik
    //    nama yang sama saat chunk-nya sendiri dirangkai. Tanpa pendaftaran itu,
    //    fungsi yang hanya meneruskan closure (tidak pernah membaca nama itu
    //    sendiri) gagal meneruskan nilainya dan hasilnya `mboh`.
    constexpr std::int32_t kSentinelGlobal = -0x40000000;

    // Sel untuk `nama` yang dibutuhkan fungsi `fungsi_stack_[idx]`.
    // Encode yang dikembalikan SELALU relatif terhadap induk langsung
    // (`fungsi_stack_[idx - 1]`), karena itulah yang dibaca opcode `CLOSURE`.
    std::function<std::int32_t(std::size_t, std::string_view)> petakan_upvalue;
    petakan_upvalue = [&](std::size_t idx, std::string_view nama) -> std::int32_t {
        if (idx == 0) return kSentinelGlobal;  // modul: tidak punya induk
        FungsiKonteks& induk = fungsi_stack_[idx - 1];
        // (1) Lokal pada induk? Slot 0 adalah `this`, tidak bisa jadi upvalue.
        const auto it = induk.lokal.find(std::string(nama));
        if (it != induk.lokal.end() && it->second != 0) {
            return static_cast<std::int32_t>(it->second);
        }
        // (2) Induk sudah menarik nama ini sebagai upvalue? Pakai sel itu juga,
        //     supaya semua pemanggil berbagi satu sel.
        for (std::size_t k = 0; k < induk.info.ambil_upvalue.size(); ++k) {
            if (induk.info.ambil_upvalue[k].second == nama) {
                return -static_cast<std::int32_t>(k) - 1;
            }
        }
        // (3) Induk belum menariknya: daftarkan, lalu minta induk memetakannya
        //     sendiri ke atas. Tanpa langkah ini, fungsi yang hanya MENERUSKAN
        //     closure (tidak pernah membaca nama itu di badannya sendiri) tidak
        //     akan menarik nilainya, dan closure yang dikembalikannya membaca
        //     sel kosong.
        petakan_upvalue(idx - 1, nama);  // induk ikut menarik nama yang sama
        induk.info.ambil_upvalue.emplace_back(0, nama);
        return -static_cast<std::int32_t>(induk.info.ambil_upvalue.size());
    };

    std::vector<std::int32_t> sumber;
    const std::size_t ini = fungsi_stack_.size() - 1;
    for (const auto& up : ctx.info.ambil_upvalue) {
        sumber.push_back(petakan_upvalue(ini, up.second));
    }
    for (const auto& up : ctx.info.ambil_upvalue) ctx.chunk->tambah_nama_upvalue(up.second);
    ctx.chunk->jumlah_upvalue = static_cast<std::uint8_t>(sumber.size());
    ctx.chunk->upvalue_sumber = sumber;

    // --- Kembalikan chunk & emit CLOSURE --------------------------------
    vm::ChunkPtr anak = ctx.chunk;
    semua_chunk_.push_back(anak);
    // Populu DULU: `fn()` harus menunjuk ke fungsi induk agar `CLOSURE`_emitted_
    // mendaftarkan anak pada chunk induk, bukan pada dirinya sendiri.
    fungsi_stack_.pop_back();
    const std::size_t idx_anak = fn().chunk->anak.size();
    fn().chunk->anak.push_back(anak);
    emit(Op::CLOSURE, static_cast<std::uint16_t>(idx_anak));
}

void Compiler::eks_objek(const ast::ObjectLit* n) {
    emit(Op::MAKE_OBJECT, 0);
    for (const ast::Node* pn : n->properti) {
        const auto* p = static_cast<const ast::PropertiObj*>(pn);
        switch (p->jenis) {
            case ast::PropertiObj::Jenis::Nilai: {
                ekspresi(p->nilai);
                const std::size_t k = tambah_nama(Value::obyek(rt::buat_teks(heap_, p->kunci_nama)));
                emit(Op::DEFINE, static_cast<std::uint16_t>(k));
                break;
            }
            case ast::PropertiObj::Jenis::Komputat: {
                ekspresi(p->kunci);
                ekspresi(p->nilai);
                emit(Op::DEFINE, 0);
                break;
            }
            case ast::PropertiObj::Jenis::Shorthand: {
                ekspresi(p->nilai);
                const std::size_t k = tambah_nama(Value::obyek(rt::buat_teks(heap_, p->kunci_nama)));
                emit(Op::DEFINE, static_cast<std::uint16_t>(k));
                break;
            }
            case ast::PropertiObj::Jenis::Spread: {
                ekspresi(p->computed);
                emit(Op::SPREAD);
                break;
            }
            case ast::PropertiObj::Jenis::Metode: {
                ekspresi(p->nilai);
                const std::size_t k = tambah_nama(Value::obyek(rt::buat_teks(heap_, p->kunci_nama)));
                emit(Op::DEFINE_METHOD, static_cast<std::uint16_t>(k));
                break;
            }
            case ast::PropertiObj::Jenis::Accessor: {
                ekspresi(p->nilai);
                const std::size_t k = tambah_nama(Value::obyek(rt::buat_teks(heap_, p->kunci_nama)));
                emit(Op::DEFINE_ACCESSOR, static_cast<std::uint16_t>(k), static_cast<std::uint16_t>(p->getter ? 1u : 0u));
                break;
            }
        }
    }
}

void Compiler::eks_dhaptar(const ast::ArrayLit* n) {
    // Elemen didorong lebih dulu; `MAKE_ARRAY n` lalu synthesizes array dari n
    // nilai teratas (urutan tetap kiri-ke-kanan karena kita ambil dari belakang).
    bool ada_spread = false;
    for (const ast::Node* e : n->elemen) {
        const auto* el = static_cast<const ast::ElemenArr*>(e);
        if (el == nullptr) {
            emit(Op::MBOH);
            continue;
        }
        ekspresi(el->nilai);
        if (el->spread) {
            ada_spread = true;
            emit(Op::SPREAD_PUSH, 0);
        }
    }
    // Tanpa spread jumlah elemen diketahui; dengan spread, `MAKE_ARRAY_SPREAD`
    // menghitungnya dari indeks yang dicatat `SPREAD_PUSH`.
    emit(ada_spread ? Op::MAKE_ARRAY_SPREAD : Op::MAKE_ARRAY,
         static_cast<std::uint16_t>(n->elemen.size()));
}

void Compiler::eks_template(const ast::TemplateLit* n) {
    // Template bertag: panggil tag dengan (string, ...ekspresi).
    if (n->ada_tag && n->tag != nullptr) {
        ekspresi(n->tag);
        // argumen: teks yang sudah digabung
        std::string gabung;
        for (const ast::TemplateBagian& b : n->bagian) {
            if (!b.ekspresi) gabung += b.teks;
        }
        emit(Op::KONSTAN, static_cast<std::uint16_t>(tambah_konstanta(Value::obyek(rt::buat_teks(heap_, gabung)))));
        std::size_t n_arg = 1;
        for (const ast::TemplateBagian& b : n->bagian) {
            if (b.ekspresi) { ekspresi(b.ekspresi_node); ++n_arg; }
        }
        emit(Op::CALL, static_cast<std::uint16_t>(n_arg));
        return;
    }
    // Tanpa tag: mulai dari string kosong, tambah tiap bagian.
    for (const ast::TemplateBagian& b : n->bagian) {
        if (!b.ekspresi) {
            emit(Op::KONSTAN, static_cast<std::uint16_t>(tambah_konstanta(Value::obyek(rt::buat_teks(heap_, b.teks)))));
        } else {
            ekspresi(b.ekspresi_node);
            emit(Op::TOSTRING);
        }
    }
    // Gabungkan: jumlah bagian = N, hasilkan N-1 operasi Tambah.
    if (n->bagian.size() <= 1) {
        if (n->bagian.empty()) emit(Op::KONSTAN, static_cast<std::uint16_t>(tambah_konstanta(Value::obyek(rt::buat_teks(heap_, "")))));
        return;
    }
    // Gabungkan beruntai dengan CONCAT.
    for (std::size_t i = 1; i < n->bagian.size(); ++i) emit(Op::CONCAT);
}

void Compiler::eks_destructur(const ast::Node* /*target*/, const ast::Node* /*nilai*/, bool /*deklarasi*/) {
    emit(Op::MBOH);
}

}  // namespace jawa::compile
