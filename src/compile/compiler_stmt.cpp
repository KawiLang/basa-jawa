// Kompiler Basa Jawa bagian 2: statement.
#include <algorithm>

#include "compile/compiler.h"

#include "rt/object.h"
#include "rt/string.h"
#include "rt/number.h"
#include "rt/string.h"

namespace jawa::compile {

using ast::NK;
using ast::NodePtr;
using vm::Op;

// ===========================================================================
// Statement
// ===========================================================================

void Compiler::statement(const ast::Node* n) {
    if (n == nullptr) return;
    switch (n->kind) {
        case NK::EkspresiStmt: {
            ekspresi(static_cast<const ast::EkspresiStmt*>(n)->ekspresi);
            emit(Op::POP);
            return;
        }
        case NK::KosongStmt: return;
        case NK::Blok: stmt_blok(static_cast<const ast::BlokStmt*>(n)); return;
        case NK::YenStmt: stmt_yen(static_cast<const ast::YenStmt*>(n)); return;
        case NK::NalikaStmt: stmt_nalika(static_cast<const ast::NalikaStmt*>(n)); return;
        case NK::LakoniStmt: stmt_lakoni(static_cast<const ast::LakoniStmt*>(n)); return;
        case NK::KanggoStmt: stmt_kanggo(static_cast<const ast::KanggoStmt*>(n)); return;
        case NK::KanggoOfStmt: stmt_kanggo_of(static_cast<const ast::KanggoOfStmt*>(n)); return;
        case NK::PilihStmt: stmt_pilih(static_cast<const ast::PilihStmt*>(n)); return;
        case NK::CobaStmt: stmt_coba(static_cast<const ast::CobaStmt*>(n)); return;
        case NK::GolonganDeklarasi: stmt_golongan(static_cast<const ast::GolonganDeklarasi*>(n)); return;
        case NK::EksporDeklarasi: stmt_ekspor(static_cast<const ast::EksporDeklarasi*>(n)); return;
        case NK::ImporDeklarasi: stmt_impor(static_cast<const ast::ImporDeklarasi*>(n)); return;
        case NK::FungsiDeklarasi: deklarasi_fungsi(static_cast<const ast::FungsiDeklarasi*>(n), false); return;
        case NK::DeklarasiVar: {
            const auto* d = static_cast<const ast::DeklarasiVarStmt*>(n);
            if (d->destruktur) {
                ekspresi(d->nilai);
                eks_destructur(n, d->nilai, true);
            } else {
                const std::size_t s = slot_baru(d->jeneng);
                if (d->nilai != nullptr) {
                    ekspresi(d->nilai);
                } else {
                    emit(Op::MBOH);
                }
                emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(s));
            }
            for (const ast::Node* lain : d->deklarator_lain) statement(lain);
            return;
        }
        case NK::BaliStmt: {
            const auto* b = static_cast<const ast::BaliStmt*>(n);
            if (b->nilai != nullptr) ekspresi(b->nilai);
            else emit(Op::MBOH);
            emit(Op::RETURN);
            return;
        }
        case NK::MandhegStmt: {
            // `mandheg` keluar dari loop terdekat. Tanpa loop, error keras.
            if (fn().loop.empty()) {
                diagnosa_di("S501", "\"mandheg\" mung sah ing loop (kanggo / nalika / lakoni).",
                            "Cethakaké `mandheg` ana ing jero loop utawa hapus.");
                emit(Op::JUMP, 0);
                return;
            }
            fn().loop.back().patch_mandheg.push_back(emit(Op::JUMP, 0));
            return;
        }
        case NK::TerusnaStmt: {
            if (fn().loop.empty()) {
                diagnosa_di("S502", "\"terusna\" mung sah ing loop (kanggo / nalika / lakoni).",
                            "Cethakaké `terusna` ana ing jero loop utawa hapus.");
                emit(Op::JUMP, 0);
                return;
            }
            fn().loop.back().patch_terusna.push_back(emit(Op::JUMP, 0));
            return;
        }
        case NK::UncalStmt: {
            ekspresi(static_cast<const ast::UncalStmt*>(n)->nilai);
            emit(Op::THROW);
            return;
        }
        case NK::LabelStmt: statement(static_cast<const ast::LabelStmt*>(n)->awak); return;
        case NK::DebuggerStmt: emit(Op::DEBUGGER); return;
        default:
            ekspresi(n);
            emit(Op::POP);
            return;
    }
}

void Compiler::stmt_blok(const ast::BlokStmt* n) {
    if (n == nullptr) return;
    for (const ast::Node* s : n->body) statement(s);
}

void Compiler::stmt_awak(const ast::Node* n) {
    if (n == nullptr) return;
    if (n->kind == NK::Blok) {
        stmt_blok(static_cast<const ast::BlokStmt*>(n));
        return;
    }
    statement(n);
}

void Compiler::stmt_yen(const ast::YenStmt* n) {
    ekspresi(n->kondisi);
    const std::size_t lompat_lanjut = emit(Op::JUMP_IF_FALSE, 0);
    emit(Op::POP);
    stmt_awak(n->lalu);
    if (n->ada_liyane) {
        const std::size_t lompat_akhir = emit(Op::JUMP, 0);
        patch(lompat_lanjut, fn().chunk->ukuran_kode());
        statement(n->liyane);
        patch(lompat_akhir, fn().chunk->ukuran_kode());
    } else {
        patch(lompat_lanjut, fn().chunk->ukuran_kode());
    }
}

void Compiler::stmt_nalika(const ast::NalikaStmt* n) {
    const std::size_t mulai = fn().chunk->ukuran_kode();
    ekspresi(n->kondisi);
    const std::size_t keluar = emit(Op::JUMP_IF_FALSE, 0);
    emit(Op::POP);
    // Target `terusna` = awal body (setelah uji kondisi).
    FungsiKonteks::Loop loop;
    loop.terusna_tujuan = fn().chunk->ukuran_kode();
    fn().loop.push_back(std::move(loop));
    stmt_awak(n->awak);
    const FungsiKonteks::Loop Loop = fn().loop.back();
    fn().loop.pop_back();
    emit(Op::JUMP, static_cast<std::uint16_t>(mulai));
    for (std::size_t p : Loop.patch_mandheg) patch(p, fn().chunk->ukuran_kode());
    // Lompatan `terusna` menunjuk ke `mulai` (uji kondisi lagi).
    for (std::size_t p : Loop.patch_terusna) patch(p, mulai);
    patch(keluar, fn().chunk->ukuran_kode());
}

void Compiler::stmt_lakoni(const ast::LakoniStmt* n) {
    const std::size_t mulai = fn().chunk->ukuran_kode();
    FungsiKonteks::Loop loop;
    loop.terusna_tujuan = mulai;
    fn().loop.push_back(std::move(loop));
    stmt_awak(n->awak);
    const FungsiKonteks::Loop Loop = fn().loop.back();
    fn().loop.pop_back();
    if (n->kondisi != nullptr) {
        ekspresi(n->kondisi);
        const std::size_t lompat = emit(Op::JUMP_IF_TRUE, 0);
        emit(Op::POP);
        patch(lompat, mulai);
    } else {
        emit(Op::JUMP, static_cast<std::uint16_t>(mulai));
    }
    for (std::size_t p : Loop.patch_mandheg) patch(p, fn().chunk->ukuran_kode());
    for (std::size_t p : Loop.patch_terusna) patch(p, mulai);
}

void Compiler::stmt_kanggo(const ast::KanggoStmt* n) {
    if (n == nullptr) return;
    // Inisialisasi (dijalankan sekali).
    if (n->inisialisasi != nullptr) {
        if (n->inisialisasi->kind == NK::DeklarasiVar) {
            statement(n->inisialisasi);
        } else {
            ekspresi(n->inisialisasi);
            emit(Op::POP);
        }
    }
    const std::size_t mulai = fn().chunk->ukuran_kode();
    FungsiKonteks::Loop loop;
    fn().loop.push_back(std::move(loop));

    if (n->kondisi != nullptr) {
        ekspresi(n->kondisi);
        const std::size_t keluar = emit(Op::JUMP_IF_FALSE, 0);
        emit(Op::POP);
        fn().loop.back().terusna_tujuan = fn().chunk->ukuran_kode();
        stmt_awak(n->awak);
        // `terusna` pada `kanggo` melompat ke BAGIAN PEMBARUAN (bukan ke kondisi),
        // kalau tidak `i` tidak pernah bertambah.
        const std::size_t ip_pembaruan = fn().chunk->ukuran_kode();
        if (n->pembaruan != nullptr) {
            ekspresi(n->pembaruan);
            emit(Op::POP);
        }
        emit(Op::JUMP, static_cast<std::uint16_t>(mulai));
        const FungsiKonteks::Loop Loop = fn().loop.back();
        fn().loop.pop_back();
        for (std::size_t p : Loop.patch_mandheg) patch(p, fn().chunk->ukuran_kode());
        for (std::size_t p : Loop.patch_terusna) patch(p, ip_pembaruan);
        patch(keluar, fn().chunk->ukuran_kode());
        return;
    }

    fn().loop.back().terusna_tujuan = mulai;
    stmt_awak(n->awak);
    const std::size_t ip_pembaruan = fn().chunk->ukuran_kode();
    if (n->pembaruan != nullptr) {
        ekspresi(n->pembaruan);
        emit(Op::POP);
    }
    emit(Op::JUMP, static_cast<std::uint16_t>(mulai));
    const FungsiKonteks::Loop Loop = fn().loop.back();
    fn().loop.pop_back();
    for (std::size_t p : Loop.patch_mandheg) patch(p, fn().chunk->ukuran_kode());
    for (std::size_t p : Loop.patch_terusna) patch(p, ip_pembaruan);
}

void Compiler::stmt_kanggo_of(const ast::KanggoOfStmt* n) {
    // `kanggo (target saka iterable)`:
    //   iterable ; ITER_INIT          -> [iter, i]
    // L: ITER_NEXT                    -> [iter, i, nilai, ada?]
    //    JUMP_IF_FALSE L1 ; POP       -> [iter, i, nilai]
    //    <target = nilai> ; <body> ; JUMP L
    // L1: POP POP                     -> [iter, i]
    //    POP POP                      -> []
    ekspresi(n->iterable);
    emit(Op::ITER_INIT, 0);

    const std::size_t mulai = fn().chunk->ukuran_kode();
    emit(Op::ITER_NEXT, 0);
    const std::size_t keluar = emit(Op::JUMP_IF_FALSE, 0);
    emit(Op::POP);  // buang flag

    // Target harus berupa pengenal (destruktur perlu opcode sendiri).
    if (n->target != nullptr && n->target->kind == NK::DeklarasiVar) {
        const auto* d = static_cast<const ast::DeklarasiVarStmt*>(n->target);
        if (!d->destruktur && !d->jeneng.empty()) {
            const std::size_t s = slot_baru(d->jeneng);
            emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(s));
        } else {
            diagnosa_di("S503", "Target kanggo-saka kudu jeneng tunggal (destruktur durung ora didukung).",
                        "Conto: `kanggo (saka [1,2,3]) { ... }` utawa `kanggo (n saka ...) { ... }`.");
            emit(Op::POP);
        }
    } else if (n->target != nullptr && n->target->kind == NK::RefIdent) {
        emit_tulis_nama_statement(static_cast<const ast::RefIdent*>(n->target)->nama);
    } else {
        emit(Op::POP);
    }

    FungsiKonteks::Loop loop;
    loop.terusna_tujuan = mulai;
    fn().loop.push_back(std::move(loop));
    stmt_awak(n->awak);
    const FungsiKonteks::Loop Loop = fn().loop.back();
    fn().loop.pop_back();

    emit(Op::JUMP, static_cast<std::uint16_t>(mulai));
    for (std::size_t p : Loop.patch_mandheg) patch(p, fn().chunk->ukuran_kode());
    for (std::size_t p : Loop.patch_terusna) patch(p, mulai);
    patch(keluar, fn().chunk->ukuran_kode());
    emit(Op::POP);  // nilai sisa saat keluar loop
    emit(Op::POP);  // indeks
    emit(Op::POP);  // iterable
}

void Compiler::stmt_pilih(const ast::PilihStmt* n) {
    ekspresi(n->subjek);
    const std::size_t s_subjek = fn().n_slot_terpakai++;
    fn().n_slot_maks = std::max(fn().n_slot_maks, fn().n_slot_terpakai);
    emit(Op::SET_LOCAL, static_cast<std::uint16_t>(s_subjek));

    std::vector<std::size_t> lompat_akhir;
    for (const ast::Node* cn : n->kasus) {
        const auto* k = static_cast<const ast::KasusKlap*>(cn);
        std::size_t lompat_kasus = 0;
        bool ada_lompat = false;
        if (k->test != nullptr) {
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_subjek));
            ekspresi(k->test);
            lompat_kasus = emit(Op::EQ, 0);
            ada_lompat = true;
        }
        for (const ast::Node* s : k->body) statement(s);
        lompat_akhir.push_back(emit(Op::JUMP, 0));
        if (ada_lompat) patch(lompat_kasus, fn().chunk->ukuran_kode());
    }
    for (std::size_t l : lompat_akhir) patch(l, fn().chunk->ukuran_kode());
}

void Compiler::stmt_coba(const ast::CobaStmt* n) {
    // Emit:
    //   TRY_BEGIN a=<ip tangkep> b=<ip intrigusan>
    //   <blok terlindungi>
    //   TRY_END                    ; handler dilepas setelah blok normal
    //   JUMP L_akhir
    // L_tangkep:     <binding> ; <body tangkep> ; JUMP L_akhir
    // L_intrigusan:  <body intriguasan>
    // L_akhir:
    //
    // Handler dicatat di `Frame::handlers` saat `TRY_BEGIN` dieksekusi. Unwinder
    // mencari handler terdekat, memangkas stack ke tinggi saat `TRY_BEGIN`, lalu
    // lompat ke `L_tangkep` dengan nilai galat di puncak stack.
    //
    // CATATAN Fase 3: hanya `tangkep` PERTAMA yang dipakai sebagai handler
    // (seleksi berdasarkan tipe kleru belum ada). Klausul tambahan dilaporkan
    // sebagai peringatan, bukan diam-diam diabaikan.
    if (n->tangkep.size() > 1) {
        diagnosa_di("S504", "Klausa `tangkep` kanggo luwih saka siji durung ora dideftiningake.",
                    "Nggabungake awake dadi klausa `tangkep` siji nganti pawsh Helper Basa Jawa.");
    }
    const std::size_t patch_tangkep = emit(Op::TRY_BEGIN, 0, 0);
    stmt_blok(static_cast<const ast::BlokStmt*>(n->blok));
    emit(Op::TRY_END, 0);
    const std::size_t lompat_akhir = emit(Op::JUMP, 0);

    const std::size_t ip_tangkep = fn().chunk->ukuran_kode();
    patch(patch_tangkep, ip_tangkep);
    if (!n->tangkep.empty()) {
        const ast::Node* kn = n->tangkep[0];
        if (kn != nullptr) {
            const auto* k = static_cast<const ast::TangkepKlausul*>(kn);
            if (k->ada_binding && !k->binding.empty()) {
                const std::size_t s = slot_baru(k->binding);
                emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(s));
            } else {
                emit(Op::POP);
            }
            stmt_awak(k->body);
        }
    } else {
        emit(Op::POP);
    }
    const std::size_t ip_intrigusan = fn().chunk->ukuran_kode();
    if (n->pungkasan != nullptr) {
        fn().chunk->kode[patch_tangkep].b = static_cast<std::uint16_t>(ip_intrigusan);
        const auto* f = static_cast<const ast::PungkasanKlausul*>(n->pungkasan);
        stmt_awak(f->body);
    }
    patch(lompat_akhir, fn().chunk->ukuran_kode());
}

void Compiler::stmt_golongan(const ast::GolonganDeklarasi* n) {
    // Emit (stack tumbuh ke kanan):
    //   MAKE_OBJECT              -> prototipe
    //   NOMOR(kunci)              -> nama (untuk diagnostik)
    //   [ekspresi induk | MBOH]
    //   CLASS                    -> objek class
    //   DEFINE_METHOD k 0|1      -> method (0 = prototipe, 1 = statis)
    //   DEFINE_FIELD k           -> nama field instance
    const std::size_t s_kelas = fn().n_slot_terpakai++;
    fn().n_slot_maks = std::max(fn().n_slot_maks, fn().n_slot_terpakai);

    emit(Op::MAKE_OBJECT, 0);
    emit(Op::NOMOR, static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, n->nama)))));
    if (n->induk != nullptr) {
        ekspresi(n->induk);
    } else {
        emit(Op::MBOH);
    }
    emit(Op::CLASS, 0);
    emit(Op::SET_LOCAL, static_cast<std::uint16_t>(s_kelas));

    for (const ast::Node* m : n->badan) {
        if (m == nullptr) continue;
        if (m->kind == NK::MetodeDeklarasi) {
            const auto* md = static_cast<const ast::MetodeDeklarasi*>(m);
            const auto* fd = static_cast<const ast::FungsiDeklarasi*>(md->fungsi);
            if (fd == nullptr) continue;
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_kelas));
            eks_fungsi(fd);
            emit(Op::DEFINE_METHOD,
                 static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, fd->method_nama)))),
                 static_cast<std::uint16_t>(md->statis ? 1u : 0u));
        } else if (m->kind == NK::PropertyAccessor) {
            const auto* ac = static_cast<const ast::PropertyAccessorDeklarasi*>(m);
            const auto* fd = static_cast<const ast::FungsiDeklarasi*>(ac->fungsi);
            if (fd == nullptr) continue;
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_kelas));
            eks_fungsi(fd);
            emit(Op::DEFINE_ACCESSOR,
                 static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, ac->nama)))),
                 static_cast<std::uint16_t>(ac->getter ? 1u : 0u));
        } else if (m->kind == NK::FieldKelas) {
            const auto* fk = static_cast<const ast::FieldKelas*>(m);
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_kelas));
            emit(Op::DEFINE_FIELD, static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, fk->nama)))),
                 static_cast<std::uint16_t>(fk->privat ? 1u : 0u));
        }
    }

    for (const ast::Node* st : n->statis_blok) statement(st);

    if (!n->nama.empty()) {
        emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_kelas));
        const std::size_t g = slot_baru(n->nama);
        emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(g));
    }
}

void Compiler::stmt_ekspor(const ast::EksporDeklarasi* n) {
    statement(n->deklarasi);
    if (n->deklarasi != nullptr) {
        // Ambil nama yang diekspor lalu taruh di objek ekspor modul.
        const ast::Node* d = n->deklarasi;
        std::string_view nama;
        if (d->kind == NK::FungsiDeklarasi) nama = static_cast<const ast::FungsiDeklarasi*>(d)->nama;
        else if (d->kind == NK::GolonganDeklarasi) nama = static_cast<const ast::GolonganDeklarasi*>(d)->nama;
        else if (d->kind == NK::DeklarasiVar) nama = static_cast<const ast::DeklarasiVarStmt*>(d)->jeneng;
        if (!nama.empty()) {
            const std::size_t k = tambah_nama(Value::obyek(rt::buat_teks(heap_, nama)));
            emit(Op::NOMOR, static_cast<std::uint16_t>(k));
            emit(Op::EXPORT, 0);
        }
    }
}

void Compiler::stmt_impor(const ast::ImporDeklarasi* n) {
    // IMPOR: muat modul, taruh ekspornya di stack, lalu bind tiap nama.
    if (!n->ada_modul) {
        emit(Op::MBOH);
    } else {
        const std::size_t k = tambah_konstanta(Value::obyek(rt::buat_teks(heap_, n->modul)));
        emit(Op::TEKS, static_cast<std::uint16_t>(k));
        emit(Op::IMPORT, 0);
    }
    if (n->ada_namespace && !n->alias_namespace.empty()) {
        const std::size_t s = slot_baru(n->alias_namespace);
        emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(s));
        return;
    }
    for (const ast::ImporSpesifikasi& sp : n->daftar) {
        // <modul> "nama" -> nilai
        const std::size_t k = tambah_konstanta(Value::obyek(rt::buat_teks(heap_, sp.sumber)));
        emit(Op::TEKS, static_cast<std::uint16_t>(k));
        emit(Op::GET_PROP, static_cast<std::uint16_t>(0));
        const std::size_t s = slot_baru(sp.impor);
        emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(s));
    }
}


// ===========================================================================
// Deklarasi fungsi (hoisting + rekursi)
// ===========================================================================

void Compiler::deklarasi_fungsi(const ast::FungsiDeklarasi* n, bool eksport) {
    if (n == nullptr || n->nama.empty()) {
        // Fungsi anonymous pada posisi statement: evaluasi lalu buang.
        eks_fungsi(n);
        emit(Op::POP);
        return;
    }
    // Slot dialokasikan SEBELUM body dikompilasi supaya panggilan rekursif
    // (`function f() { f(); }`)resolve ke slot yang sama (hoisting).
    const std::size_t s = slot_baru(n->nama);
    eks_fungsi(n);
    emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(s));
    (void)eksport;
}

}  // namespace jawa::compile
