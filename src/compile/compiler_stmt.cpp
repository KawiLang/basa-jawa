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

void Compiler::statement(const ast::Node* n) { statement(n, false); }

void Compiler::statement(const ast::Node* n, bool sudah_hoist) {
    if (n == nullptr) return;
    // Tandai perpindahan baris SEBELUM opcode statement ini diterbitkan, supaya
    // galat yang terjadi saat ekspresi dievaluasi menunjuk baris yang benar.
    // Tanpa ini `pos_sumber_` VM tertinggal di baris 1 untuk seluruh program, dan
    // setiap pesan galat runtime menunjuk baris yang sama.
    tandai_baris(n->range.mulai.baris);
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
        case NK::EksporDeklarasi:
            stmt_ekspor(static_cast<const ast::EksporDeklarasi*>(n), sudah_hoist);
            return;
        case NK::ImporDeklarasi: stmt_impor(static_cast<const ast::ImporDeklarasi*>(n)); return;
        case NK::FungsiDeklarasi: deklarasi_fungsi(static_cast<const ast::FungsiDeklarasi*>(n), false); return;
        case NK::DeklarasiVar: {
            const auto* d = static_cast<const ast::DeklarasiVarStmt*>(n);
            // Pengikat leksikal punya zona mati-temporal: membaca nama sebelum
            // deklarasinya dievaluasi adalah galat (bukan `undefined`).
            // Slot ditandai dulu, lalu ip deklarasi dicatat setelah statement ini
            // selesai dikompilasi.
            std::vector<std::size_t> tdz_baru;
            if (d->destruktur) {
                ekspresi(d->nilai);
                eks_destructur(n, d->nilai, true);
                if (!d->jeneng.empty()) {
                    tdz_baru.push_back(slot_baru_tdz(d->jeneng));
                }
            } else {
                const std::size_t s = slot_baru_tdz(d->jeneng);
                tdz_baru.push_back(s);
                if (d->nilai != nullptr) {
                    ekspresi(d->nilai);
                } else {
                    emit(Op::MBOH);
                }
                emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(s));
            }
            for (const ast::Node* lain : d->deklarator_lain) statement(lain);
            // Tutup TDZ: dari titik ini nama boleh dibaca.
            for (std::size_t sl : tdz_baru) {
                if (const auto it = fn().tdz_menunggu.find(sl); it != fn().tdz_menunggu.end()) {
                    fn().chunk->tdz_daftar[it->second] =
                        static_cast<std::uint16_t>(fn().chunk->ukuran_kode());
                    fn().tdz_menunggu.erase(it);
                }
            }
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
    // `kondisi; JUMP_IF_FALSE lanjut; <lalu> JUMP akhir; [liyane] ; POP`
    //
    // Nilai kondisi dibuang oleh SATU `POP` di akhir, yang dipakai kedua jalur:
    // jalur benar lewat `JUMP akhir`, jalur salah mendarat langsung
    // di `POP`. Kalau `POP` diletakkan sebelum `<lalu>`, jalur salah akan
    // mendarat di `POP` yang sudah dipakai jalur benar -- dan kalau body-nya
    // kosong, `POP` itu dieksekusi dua kali -- sekali lewat jalur benar, sekali
    // lagi karena jalur salah mendarat di instruksi yang sama.
    ekspresi(n->kondisi);
    const std::size_t lompat_lanjut = emit(Op::JUMP_IF_FALSE, 0);
    stmt_awak(n->lalu);
    const std::size_t lompat_akhir = emit(Op::JUMP, 0);
    patch(lompat_lanjut, fn().chunk->ukuran_kode());
    if (n->ada_liyane) statement(n->liyane);
    patch(lompat_akhir, fn().chunk->ukuran_kode());
    emit(Op::POP);
}

void Compiler::stmt_nalika(const ast::NalikaStmt* n) {
    // Sama seperti `stmt_yen`: satu `POP` di akhir dipakai jalur keluar. Kalau
    // tidak, setiap statement `nalika` yang selesai membocorkan nilai kondisi.
    const std::size_t mulai = fn().chunk->ukuran_kode();
    ekspresi(n->kondisi);
    const std::size_t keluar = emit(Op::JUMP_IF_FALSE, 0);
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
    emit(Op::POP);
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
            const std::size_t s = slot_baru_tdz(d->jeneng);
            emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(s));
            // Tutup TDZ target loop: `i` terikat sejak header dievaluasi.
            if (const auto it = fn().tdz_menunggu.find(s); it != fn().tdz_menunggu.end()) {
                fn().chunk->tdz_daftar[it->second] =
                    static_cast<std::uint16_t>(fn().chunk->ukuran_kode());
                fn().tdz_menunggu.erase(it);
            }
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
    // Jalur keluar normal: `ITER_NEXT` menyisakan nilai `mboh` yang belum
    // dipakai, jadi masih ada 3 nilai di stack: nilai, indeks, iterable.
    patch(keluar, fn().chunk->ukuran_kode());
    emit(Op::POP);  // nilai sisa
    const std::size_t keluar_bersih = fn().chunk->ukuran_kode();
    // Jalur `mandheg`: nilai iterasi SUDAH dikuras `DEF_LOCAL`/`SET_LOCAL`
    // sebelum body, jadi stack-nya hanya 2 nilai (indeks + iterable). Kalau
    // `mandheg` melompat ke titik yang sama dengan jalur keluar normal, ada
    // satu `POP` terlalu banyak -- dan `POP` berikutnya memakan nilai milik
    // frame pemanggil. Gejalanya: program dilanjutkan dengan stack rusak, dan
    // loop berhenti setelah iterasi pertama.
    for (std::size_t p : Loop.patch_mandheg) patch(p, keluar_bersih);
    for (std::size_t p : Loop.patch_terusna) patch(p, mulai);
    emit(Op::POP);  // indeks
    emit(Op::POP);  // iterable
}

void Compiler::stmt_pilih(const ast::PilihStmt* n) {
    // `pilih (subjek) { kasus <test|pola>: ...; baku: ... }`
    //
    //   ekspresi(subjek) ; SET_LOCAL s_subjek
    // L_kasus_i:
    //   [<test> ; EQ]  atau  [<pola>]        ; sisakan satu boolean
    //   JUMP_IF_FALSE L_gagal_i
    //   POP                            ; jalur cocok: buang boolean
    //   <body_i> ; JUMP L_akhir
    // L_gagal_i:  POP                    ; jalur gagal: buang boolean juga
    // ...
    // L_akhir:
    //
    // Tiga hal penting di sini:
    // 1. `Op::EQ` adalah perbandingan BIASA (mendorong boolean), bukan lompatan
    //    bersyarat -- harus diikuti `JUMP_IF_FALSE` eksplisit.
    // 2. Lompatan bersyarat hanya MEMBATAS nilai (peek), jadi kedua jalur harus
    //    membuang boolean-nya. `cocog` melakukan hal yang sama; bedanya di sini
    //    `pilih` adalah statement, jadi tidak ada nilai hasil yang disimpan.
    // 3. `mandheg`/`terusna` di dalam badan kasus melompat ke titik yang SUDAH
    //    membuang boolean, jadi keduanya melompati `POP` di `L_gagal_i`.
    //
    // Subjek disimpan di slot supaya tiap kasus bisa membacanya ulang.
    ekspresi(n->subjek);
    const std::size_t s_subjek = fn().n_slot_terpakai++;
    fn().n_slot_maks = std::max(fn().n_slot_maks, fn().n_slot_terpakai);
    emit(Op::SET_LOCAL, static_cast<std::uint16_t>(s_subjek));

    std::vector<std::size_t> lompat_akhir;
    for (const ast::Node* cn : n->kasus) {
        const auto* k = static_cast<const ast::KasusKlap*>(cn);
        if (k == nullptr) continue;
        const bool ada_uji = k->pola != nullptr || k->test != nullptr;
        std::vector<std::size_t> gagal_pola;
        if (k->pola != nullptr) {
            // Kasus pola: compile seperti `cocog` -- pola membaca subjek dari
            // slot dan mengikat nama polanya ke slot lokal.
            susun_pola(static_cast<const ast::Pola*>(k->pola), s_subjek, gagal_pola);
        } else if (k->test != nullptr) {
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_subjek));
            ekspresi(k->test);
            emit(Op::EQ);
        }
        // `baku:` tidak menguji apa pun: body-nya langsung jalan.
        std::size_t gagal_kasus = 0;
        if (ada_uji) {
            gagal_kasus = emit(Op::JUMP_IF_FALSE, 0);
            emit(Op::POP);
        }
        for (const ast::Node* st : k->body) statement(st);
        lompat_akhir.push_back(emit(Op::JUMP, 0));
        if (ada_uji) {
            // Titik gagal kasus ini = awal kasus berikutnya.
            for (std::size_t g : gagal_pola) patch(g, fn().chunk->ukuran_kode());
            patch(gagal_kasus, fn().chunk->ukuran_kode());
            emit(Op::POP);  // buang boolean yang tidak terpakai
        }
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

const ast::Node* Compiler::deklarasi_ekspor(const ast::Node* n) {
    if (n == nullptr || n->kind != NK::EksporDeklarasi) return nullptr;
    return static_cast<const ast::EksporDeklarasi*>(n)->deklarasi;
}

std::size_t Compiler::slot_impor(const std::string_view nama) {
    auto it = impor_slot_.find(std::string(nama));
    if (it != impor_slot_.end()) return it->second;
    return slot_baru(nama);
}

std::string_view Compiler::nama_deklarasi(const ast::Node* d) {    if (d == nullptr) return {};
    switch (d->kind) {
        case NK::FungsiDeklarasi: return static_cast<const ast::FungsiDeklarasi*>(d)->nama;
        case NK::GolonganDeklarasi: return static_cast<const ast::GolonganDeklarasi*>(d)->nama;
        case NK::DeklarasiVar: return static_cast<const ast::DeklarasiVarStmt*>(d)->jeneng;
        default: return {};
    }
}

void Compiler::stmt_ekspor(const ast::EksporDeklarasi* n, bool sudah_hoist) {
    // `sudah_hoist`: statement `ekspor` ini sudah dikompilasi utuh di awal modul
    // (deklarasi + ekspor), jadi jangan apa-apa lagi. Melewati langkah ini juga
    // membuat ekspor tersedia SEBELUM `impor` dievaluasi -- syarat agar impor
    // siklik (`a impor b; b impor a;`) bisa saling memanggil.
    if (sudah_hoist) return;

    // --- `ekspor { a, b minangka c }` (+ opsional `saka "mod"`) ---
    if (!n->daftar.empty() || n->ada_modul) {
        if (n->ada_modul) {
            // Re-export: impor dulu, lalu ekspor ulang selective.
            const std::size_t km = tambah_konstanta(Value::obyek(rt::buat_teks(heap_, n->modul)));
            emit(Op::TEKS, static_cast<std::uint16_t>(km));
            emit(Op::IMPORT, 0);
        } else {
            emit(Op::MBOH);
        }
        for (const ast::EksporSpesifikasi& sp : n->daftar) {
            const auto it = cari_slot(sp.lokal);
            // Tunda ke akhir modul kalau slot-nya belum ada ATAU masih di zona
            // mati-temporal. Kasus kedua muncul karena `pradaftar_tdz`
            // mengalokasikan slot lebih dulu: `ekspor { x }; tetep x = 5;` akan
            // mengekspor nilai yang belum diinisialisasi kalau tidak ditunda.
            const bool masih_tdz =
                it != std::string::npos && fn().tdz_menunggu.count(it) != 0;
            if (it == std::string::npos || masih_tdz) {
                ekspor_tunda_.push_back(EksporTunda{sp.lokal, sp.ekspor});
                continue;
            }
            emit(Op::DUP);
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(it));
            const std::size_t k = tambah_konstanta(Value::obyek(rt::buat_teks(heap_, sp.ekspor)));
            emit(Op::TEKS, static_cast<std::uint16_t>(k));
            emit(Op::EXPORT, 0);
        }
        if (n->ada_modul) emit(Op::POP);
        return;
    }

    // --- `ekspor <deklarasi>` / `ekspor baku <ekspresi>` ---
    // Deklarasi dikompilasi normal (jadi slot lokal modul), lalu nilainya dibaca
    // ulang dan ditaruh ke objek ekspor. Cara ini berlaku seragam untuk
    // `gawe`, `golongan`, dan `const`, tanpa bentuk bytecode khusus.
    if (n->default_ekspor) {
        if (n->deklarasi == nullptr) {
            emit(Op::MBOH);
        } else if (n->deklarasi->kind == NK::FungsiDeklarasi ||
                   n->deklarasi->kind == NK::GolonganDeklarasi ||
                   n->deklarasi->kind == NK::DeklarasiVar) {
            if (!sudah_hoist) statement(n->deklarasi, sudah_hoist);
            const auto s = cari_slot(nama_deklarasi(n->deklarasi));
            if (s != std::string::npos) emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s));
            else emit(Op::MBOH);
        } else if (n->deklarasi->kind == NK::EkspresiStmt) {
            // `ekspor baku <ekspresi>`: NILAI ekspresi yang diekspor, bukan
            // statement-nya. `statement()` akan menambah `POP` yang menghapus
            // nilai itu, jadi ekspresinya dikompilasi langsung.
            ekspresi(static_cast<const ast::EkspresiStmt*>(n->deklarasi)->ekspresi);
        } else {
            statement(n->deklarasi);
        }
        const std::size_t kd = tambah_konstanta(Value::obyek(rt::buat_teks(heap_, "default")));
        emit(Op::TEKS, static_cast<std::uint16_t>(kd));
        emit(Op::EXPORT, 0);
        return;
    }

    const std::string_view nama = nama_deklarasi(n->deklarasi);
    if (n->deklarasi == nullptr || nama.empty()) {
        statement(n->deklarasi);
        return;
    }
    // `sudah_hoist`: fungsi/kelas sudah dibuat di awal modul (lihat
    // `Compiler::compile`), jadi jangan dibuat dua kali -- cukup ekspor.
    if (!sudah_hoist) statement(n->deklarasi, sudah_hoist);
    const auto s = cari_slot(nama);
    if (s == std::string::npos) return;  // deklarasi tanpa nama: tidak ada yang bisa diekspor
    emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s));
    const std::size_t k = tambah_konstanta(Value::obyek(rt::buat_teks(heap_, nama)));
    emit(Op::TEKS, static_cast<std::uint16_t>(k));
    emit(Op::EXPORT, 0);
}

void Compiler::stmt_impor(const ast::ImporDeklarasi* n) {
    // IMPOR: muat modul, taruh ekspornya di stack, lalu bind tiap nama.
    // Objek ekspor didupe-kan tiap iterasi supaya `GET_PROP` tidak
    // menghabiskan satu-satunya rujukan.
    if (!n->ada_modul) {
        emit(Op::MBOH);
    } else {
        const std::size_t k = tambah_konstanta(Value::obyek(rt::buat_teks(heap_, n->modul)));
        emit(Op::TEKS, static_cast<std::uint16_t>(k));
        emit(Op::IMPORT, 0);
    }
    if (n->ada_namespace) {
        // `impor * minangka M` (atau `impor M saka "..."`): objek ekspor
        // langsung diikat sebagai satu nilai.
        emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(slot_impor(n->alias_namespace)));
        return;
    }
    if (n->daftar.empty()) {
        // `impor "./modul.jw"` tanpa pengikat: cukup dievaluasi (sisi
        // effected modul dijalankan), lalu buang objek ekspornya.
        emit(Op::POP);
        return;
    }
    for (const ast::ImporSpesifikasi& sp : n->daftar) {
        emit(Op::DUP);
        // `GET_EXPORT` (bukan `GET_PROP`): nama yang tidak diekspor modul harus
        // menjadi galat, bukan `mboh` -- impor salah ketik adalah kesalahan
        // program, bukan nilai kosong.
        // `baku` = `default`: nama baku untuk ekspor default, ditulis dengan
        // ejaan Jawa supaya konsisten dengan bahasa.
        const std::string_view sumber = sp.sumber == "baku" ? std::string_view("default") : sp.sumber;
        const std::size_t k = tambah_nama(Value::obyek(rt::buat_teks(heap_, sumber)));
        emit(Op::GET_EXPORT, static_cast<std::uint16_t>(k));
        emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(slot_impor(sp.impor)));
    }
    emit(Op::POP);
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
