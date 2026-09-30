// Kompiler Basa Jawa bagian 2: statement.
#include <algorithm>
#include <cstdint>
#include <functional>

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
                if (adalah_sel(d->jeneng)) {
                    // Variabel modul yang diekspor: slotnya berisi `SelObj`,
                    // bukan nilai. Sel dibuat lebih dulu lalu diisi -- itulah
                    // yang membuat `ekspor` menjadi live binding (importer
                    // mengikat sel yang sama, jadi penulisan di sini terlihat
                    // di sana, dan sebaliknya).
                    emit(Op::MBOH);
                    emit(Op::SEL_BUAT);
                    emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(s));
                    if (d->nilai != nullptr) {
                        ekspresi(d->nilai);
                    } else {
                        emit(Op::MBOH);
                    }
                    emit(Op::SET_CELL, static_cast<std::uint16_t>(s));
                    emit(Op::POP);
                } else {
                    if (d->nilai != nullptr) {
                        ekspresi(d->nilai);
                    } else {
                        emit(Op::MBOH);
                    }
                    emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(s));
                }
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
            // `bali` di dalam `coba` yang punya `pungkasan` harus menjalankan
            // badan `pungkasan` lebih dulu (ECMAScript). Nilai `bali`
            // disimpan di slot sementara, badan `pungkasan` disalin di sini,
            // lalu nilai itu dipop dan dikembalikan.
            //
            // Kalau `bali` ada di dalam badan `pungkasan` itu sendiri, ia
            //langsung mengembalikan nilainya: `pungkasan` yang sedang berjalan
            // sudah cukup, dan menyalinnya lagi akan tak berujung.
            if (fn().coba_pungkasan != nullptr && !fn().dalam_pungkasan) {
                emit(Op::DUP);
                const std::size_t s = fn().n_slot_terpakai++;
                fn().n_slot_maks = std::max(fn().n_slot_maks, fn().n_slot_terpakai);
                emit(Op::SET_LOCAL, static_cast<std::uint16_t>(s));
                fn().dalam_pungkasan = true;
                stmt_awak(fn().coba_pungkasan);
                fn().dalam_pungkasan = false;
                emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s));
            }
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
            // Buang nilai kondisi `yen`/`nalika` yang masih tertunda; `POP`
            // di akhir statement itu dilewati oleh lompatan ini.
            for (std::size_t i = 0; i < fn().loop.back().kondisi_tertunda; ++i) emit(Op::POP);
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
            for (std::size_t i = 0; i < fn().loop.back().kondisi_tertunda; ++i) emit(Op::POP);
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
    // Nilai kondisi tetap di stack sampai `POP` di akhir statement ini, jadi
    // `mandheg`/`terusna` di dalam badan harus ikut mengetahuinya.
    for (FungsiKonteks::Loop& l : fn().loop) ++l.kondisi_tertunda;
    stmt_awak(n->lalu);
    const std::size_t lompat_akhir = emit(Op::JUMP, 0);
    patch(lompat_lanjut, fn().chunk->ukuran_kode());
    if (n->ada_liyane) statement(n->liyane);
    for (FungsiKonteks::Loop& l : fn().loop) --l.kondisi_tertunda;
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
    loop.kondisi_tertunda = 1;  // nilai kondisi menunggu `POP` di akhir
    fn().loop.push_back(std::move(loop));
    for (std::size_t i = 0; i + 1 < fn().loop.size(); ++i) ++fn().loop[i].kondisi_tertunda;
    stmt_awak(n->awak);
    for (std::size_t i = 0; i + 1 < fn().loop.size(); ++i) --fn().loop[i].kondisi_tertunda;
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

std::size_t Compiler::slot_iterasi(const ast::Node* target) {
    // `kanggo (ana i = 0; ...)` dan `kanggo (ana x saka ...)`: pengikatnya
    // per-iterasi. Slotnya berisi `SelObj`, dan tiap awal iterasi sel itu
    // DISALIN (`SEL_SALIN`) sehingga closure yang dibuat pada iterasi ini
    // menangkap sel yang tidak berubah pada iterasi berikutnya.
    //
    // Tanpa ini slot kompilator bersifat fungsi-wide: seluruh closure dalam
    // loop membaca `i` yang sama, jadi setelah loop semuanya melihat nilai
    // iterasi terakhir -- bukan nilai iterasinya (lihat D-037).
    if (target == nullptr || target->kind != NK::DeklarasiVar) return std::string::npos;
    const auto* d = static_cast<const ast::DeklarasiVarStmt*>(target);
    if (d->destruktur || d->jeneng.empty()) return std::string::npos;
    const std::size_t s = slot_baru_tdz(d->jeneng);
    fn().sel_nama.insert(std::string(d->jeneng));
    return s;
}

void Compiler::stmt_kanggo(const ast::KanggoStmt* n) {
    if (n == nullptr) return;
    // Inisialisasi (dijalankan sekali).
    std::size_t s_iterasi = std::string::npos;
    if (n->inisialisasi != nullptr) {
        if (n->inisialisasi->kind == NK::DeklarasiVar) {
            s_iterasi = slot_iterasi(n->inisialisasi);
            const auto* d = static_cast<const ast::DeklarasiVarStmt*>(n->inisialisasi);
            if (d->destruktur || d->jeneng.empty()) {
                statement(n->inisialisasi);
            } else {
                // Nilainya dihitung sekali, lalu disimpan di dalam sel.
                if (d->nilai != nullptr) {
                    ekspresi(d->nilai);
                } else {
                    emit(Op::MBOH);
                }
                emit(Op::SEL_BUAT);
                emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(s_iterasi));
                // Tutup TDZ pengikat loop.
                if (const auto it = fn().tdz_menunggu.find(s_iterasi); it != fn().tdz_menunggu.end()) {
                    fn().chunk->tdz_daftar[it->second] =
                        static_cast<std::uint16_t>(fn().chunk->ukuran_kode());
                    fn().tdz_menunggu.erase(it);
                }
            }
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
        // Sel BARU tiap akhir iterasi, sebelum bagian pembaruan -- urutan yang
        // sama dengan `CreatePerIterationEnvironment` ECMAScript: closure di
        // body menangkap sel iterasinya, lalu salinan baru dipakai iterasi
        // berikutnya (dan pembaruan menulis ke salinan itu).
        // `terusna` pada `kanggo` melompat ke BAGIAN PEMBARUAN (bukan ke
        // kondisi), kalau tidak `i` tidak pernah bertambah. Titik lompatnya
        // TETAP SEBELUM salinan sel: salinan baru wajib dibuat juga pada jalur
        // `terusna`. Kalau tidak, pembaruan menulis ke sel yang sudah ditangkap
        // closure iterasi itu -- hasilnya `0 2 2 3` untuk
        // `kanggo (ana i=0;i<4;i=i+1) { t.tambah(()=>i); yen (i==1) terusna; }`,
        // bukan `0 1 2 3`.
        const std::size_t ip_terusna = fn().chunk->ukuran_kode();
        if (s_iterasi != std::string::npos) {
            emit(Op::SEL_SALIN, static_cast<std::uint16_t>(s_iterasi));
        }
        if (n->pembaruan != nullptr) {
            ekspresi(n->pembaruan);
            emit(Op::POP);
        }
        emit(Op::JUMP, static_cast<std::uint16_t>(mulai));
        const FungsiKonteks::Loop Loop = fn().loop.back();
        fn().loop.pop_back();
        for (std::size_t p : Loop.patch_mandheg) patch(p, fn().chunk->ukuran_kode());
        for (std::size_t p : Loop.patch_terusna) patch(p, ip_terusna);
        patch(keluar, fn().chunk->ukuran_kode());
        return;
    }

    fn().loop.back().terusna_tujuan = mulai;
    stmt_awak(n->awak);
    const std::size_t ip_terusna = fn().chunk->ukuran_kode();
    if (s_iterasi != std::string::npos) {
        emit(Op::SEL_SALIN, static_cast<std::uint16_t>(s_iterasi));
    }
    if (n->pembaruan != nullptr) {
        ekspresi(n->pembaruan);
        emit(Op::POP);
    }
    emit(Op::JUMP, static_cast<std::uint16_t>(mulai));
    const FungsiKonteks::Loop Loop = fn().loop.back();
    fn().loop.pop_back();
    for (std::size_t p : Loop.patch_mandheg) patch(p, fn().chunk->ukuran_kode());
    for (std::size_t p : Loop.patch_terusna) patch(p, ip_terusna);
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
    //
    // `kanggo (ana x saka ...)` juga mengikat per-iterasi (D-037): nilainya
    // dibungkus sel baru tiap iterasi, jadi closure yang dibuat di dalam loop
    // melihat nilai iterasinya sendiri, bukan nilai terakhir.
    if (n->target != nullptr && n->target->kind == NK::DeklarasiVar) {
        const auto* d = static_cast<const ast::DeklarasiVarStmt*>(n->target);
        const std::size_t s_per = slot_iterasi(n->target);
        if (!d->destruktur && !d->jeneng.empty()) {
            if (s_per != std::string::npos) {
                // Nilai iterasi ada di puncak stack: bungkus jadi sel dulu,
                // lalu salin sel itu agar iterasi ini punya sel sendiri.
                emit(Op::SEL_BUAT);
                emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(s_per));
                emit(Op::SEL_SALIN, static_cast<std::uint16_t>(s_per));
            } else {
                const std::size_t s = slot_baru_tdz(d->jeneng);
                emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(s));
            }
            // Tutup TDZ target loop: `i` terikat sejak header dievaluasi.
            const std::size_t s_tdz = s_per != std::string::npos ? s_per : slot_baru_tdz(d->jeneng);
            if (const auto it = fn().tdz_menunggu.find(s_tdz); it != fn().tdz_menunggu.end()) {
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
    // Jalur keluar normal. `ITER_NEXT` mendorong DUA nilai (hasil + flag) dan
    // `JUMP_IF_FALSE` hanya MENGINTIP flag-nya, jadi saat lompatan diambil
    // stack berisi 4 nilai: flag, hasil, indeks, iterable.
    //
    // BUG LAMA: hanya 3 yang dibuang, jadi `iterable` leftover. Di module atau
    // fungsi terluar sisa itu tidak terlihat (frame langsung `RETURN_UNDEF`),
    // tapi pada loop BERSARANG leftover itu menutupi nilai `ITER_NEXT` milik
    // loop luar -- sehingga loop luar berhenti setelah satu iterasi:
    // `kanggo (a saka [1,2]) { kanggo (b saka [1,2]) { ... } }` menghasilkan 2
    // baris, bukan 4.
    patch(keluar, fn().chunk->ukuran_kode());
    emit(Op::POP);  // flag hasil
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
        const bool ada_uji = k->pola != nullptr || k->test != nullptr || !k->nama_kelas.empty();
        std::vector<std::size_t> gagal_pola;
        if (k->pola != nullptr) {
            // Kasus pola: compile seperti `cocog` -- pola membaca subjek dari
            // slot dan mengikat nama polanya ke slot lokal.
            susun_pola(static_cast<const ast::Pola*>(k->pola), s_subjek, gagal_pola);
        } else if (!k->nama_kelas.empty()) {
            // `kasus <Kelas>:` -- subjek adalah instans class itu (atau induknya).
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_subjek));
            emit(Op::INSTAN_DARI, static_cast<std::uint16_t>(
                                       tambah_nama(Value::obyek(rt::buat_teks(heap_, k->nama_kelas)))));
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
    //   TRY_BEGIN b=<ip jalur tolak>            ; 0 = tidak ada `pungkasan`
    //   TRY_KLAUSUL t=<tipe> k=<ip>             ; satu per klausa `tangkep`
    //   <blok terlindungi>
    //   TRY_END
    //   JUMP L_akhir                            ; jalur normal
    // L_klausula_i:  <binding> ; <body klausula_i> ; JUMP L_akhir
    // L_tolak:       <pungkasan> ; THROW
    // L_intrigusan:  <pungkasan>
    // L_akhir:
    //
    // Handler dicatat di `Frame::handlers` saat `TRY_BEGIN` dieksekusi; tiap
    // klausa `tangkep` menambah satu entri (nama tipe kleru + ip) lewat
    // `TRY_KLAUSUL`. Unwinder memilih klausula PERTAMA yang cocok dengan galat;
    // kalau tidak ada, handler ini tidak menanganinya dan pencarian lanjut ke
    // luar (D-038).
    //
    // Badan `pungkasan` diiemit DUA kali karena ada dua jalur masuk: normal
    // (dan setelah klausula `tangkep` jalan) mendarat di `L_intrigusan`,
    // sedangkan "tidak ada klausula yang cocok" mendarat di `L_tolak` yang
    // menjalankan `pungkasan` lalu melempar ulang dengan `THROW` -- persis
    // seperti ECMAScript.
    //
    // BUG YANG DIPERBAIKI: jalur NORMAL dulu melompati badan `pungkasan`
    // sama sekali, karena `lompat_akhir` diarahkan ke akhir emit. Gejalanya
    // `coba { tulis("body"); } tangkep (e) { } pkt { tulis("FIN"); }`
    // mencetak hanya `body`.
    const std::size_t patch_tolak = emit(Op::TRY_BEGIN, 0, 0);
    std::vector<std::size_t> patch_klausul;
    const ast::PungkasanKlausul* pf = nullptr;
    if (n->pungkasan != nullptr) pf = static_cast<const ast::PungkasanKlausul*>(n->pungkasan);
    // Simpan badan `pungkasan` yang sedang aktif supaya `NK::BaliStmt` tahu
    // harus menyalinnya lebih dulu. `coba` bersarang menimpanya dan
    // memulihkannya, jadi setiap `bali` hanya menyalin `pungkasan` yang
    // paling dalam yang sedang berjalan.
    const ast::Node* pkt_lama = fn().coba_pungkasan;
    fn().coba_pungkasan = pf != nullptr ? pf->body : nullptr;
    for (const ast::Node* kn : n->tangkep) {
        if (kn == nullptr) continue;
        const auto* k = static_cast<const ast::TangkepKlausul*>(kn);
        // Anotasi tipe kleru: `tangkep (e: KleruJenis) { ... }`. Tanpa
        // anotasi, klausula menangkap apa saja -- harus berada di urutan
        // terakhir, kalau tidak klausula setelahnya tidak akan pernah jalan.
        std::string_view nama_tipe;
        if (k->tipe != nullptr && k->tipe->kind == NK::TipeAnotasi) {
            nama_tipe = static_cast<const ast::TipeAnotasi*>(k->tipe)->nama;
        }
        // Operand `a` disimpan sebagai `indeks + 1` supaya `0` berarti "tanpa
        // tipe". Tanpa offset itu, klausula tanpa tipe akan tertukar dengan
        // klausula bertipe yang kebetulan nama pertamanya di index 0.
        const std::size_t t = nama_tipe.empty()
                                  ? 0
                                  : tambah_nama(Value::obyek(rt::buat_teks(heap_, nama_tipe))) + 1;
        patch_klausul.push_back(emit(Op::TRY_KLAUSUL, static_cast<std::uint16_t>(t), 0));
    }
    stmt_blok(static_cast<const ast::BlokStmt*>(n->blok));
    emit(Op::TRY_END, 0);

    // Semua jalur yang "sudah beres" (blok selesai normal, atau klausula
    // `tangkep` yang cocok sudah jalan) menuju ke `L_intrigusan`, yaitu awal
    // badan `pungkasan`. Kalau tidak ada `pungkasan`, `L_intrigusan` = akhir
    // `coba` -- jadi lompatan ini selalu benar.
    const std::size_t lompat_intrigusan = emit(Op::JUMP, 0);

    // Badan tiap klausa `tangkep`, sesuai urutan penulisannya. Tiap klausula
    // berakhir dengan lompatan sendiri ke `L_intrigusan` supaya tidak jatuh ke
    // klausula berikutnya (atau ke jalur tolak). Target-nya baru diketahui
    // setelah semua klausula & `pungkasan` terbit, jadi dikumpulkan dulu.
    std::vector<std::size_t> lompat_intrigusan_klausul;
    for (std::size_t i = 0; i < n->tangkep.size(); ++i) {
        const ast::Node* kn = n->tangkep[i];
        if (kn == nullptr) continue;
        const auto* k = static_cast<const ast::TangkepKlausul*>(kn);
        patch_b(patch_klausul[i], fn().chunk->ukuran_kode());
        if (k->ada_binding && !k->binding.empty()) {
            const std::size_t s = slot_baru(k->binding);
            emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(s));
        } else {
            emit(Op::POP);
        }
        stmt_awak(k->body);
        lompat_intrigusan_klausul.push_back(emit(Op::JUMP, 0));
    }

    fn().coba_pungkasan = pkt_lama;

    if (pf != nullptr) {
        // Jalur tolak: badan `pungkasan`, lalu lempar ulang. Unwinder
        // meninggalkan nilai galat di puncak stack, jadi `THROW` langsung
        // meneruskannya ke handler di luar `coba` ini.
        const std::size_t ip_tolak = fn().chunk->ukuran_kode();
        fn().chunk->kode[patch_tolak].b = static_cast<std::uint16_t>(ip_tolak);
        stmt_awak(pf->body);
        emit(Op::THROW);
    }

    // `coba` tanpa klausa `tangkep` dan tanpa `pungkasan`: nilai galat di
    // puncak tidak dipakai siapa pun, buang. (Kalau `pungkasan` ada, jalur
    // tolak sudah mengembalikannya lewat `THROW` -- jangan POP dua kali.)
    if (n->tangkep.empty() && pf == nullptr) emit(Op::POP);

    // `L_intrigusan`: badan `pungkasan` pada jalur normal / setelah klausula.
    const std::size_t ip_intrigusan = fn().chunk->ukuran_kode();
    if (pf != nullptr) stmt_awak(pf->body);
    patch(lompat_intrigusan, ip_intrigusan);
    for (std::size_t l : lompat_intrigusan_klausul) patch(l, ip_intrigusan);
}

void Compiler::stmt_golongan(const ast::GolonganDeklarasi* n) {
    // Emit (stack tumbuh ke kanan):
    //   MAKE_OBJECT              -> prototipe
    //   NOMOR(kunci)              -> nama (untuk diagnostik)
    //   [ekspresi induk | MBOH]
    //   CLASS                    -> objek class
    //   DEFINE_METHOD k 0|1      -> method (0 = prototipe, 1 = statis)
    //   DEFINE_FIELD k           -> nama field instance
    //   DEFINE_STATIC k          -> field statis bernilai
    //   DEFINE_FIELD_INIT        -> closure inisialisasi field instance
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

    std::vector<const ast::FieldKelas*> field_init;
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
            if (fk->komputat) continue;  // kunci komputat `[...]` belum didukung
            if (fk->statis) {
                // Field statis: nilainya dievaluasi sekali saat definisi kelas.
                emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_kelas));
                if (fk->nilai != nullptr) {
                    ekspresi(fk->nilai);
                } else {
                    emit(Op::MBOH);
                }
                emit(Op::DEFINE_STATIC,
                     static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, fk->nama)))));
                continue;
            }
            emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_kelas));
            emit(Op::DEFINE_FIELD, static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, fk->nama)))),
                 static_cast<std::uint16_t>(fk->privat ? 1u : 0u));
            if (fk->nilai != nullptr) field_init.push_back(fk);
        }
    }
    if (!field_init.empty()) {
        emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_kelas));
        eks_inisial_field(field_init);
        emit(Op::DEFINE_FIELD_INIT);
    }

    for (const ast::Node* st : n->statis_blok) statement(st);

    if (!n->nama.empty()) {
        emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s_kelas));
        const std::size_t g = slot_baru(n->nama);
        emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(g));
    }
}

void Compiler::eks_inisial_field(const std::vector<const ast::FieldKelas*>& field) {
    // Closure tanpa parameter: slot 0 = `this` (instans baru). Badan:
    //   iki.<f1> = <init1>; iki.<f2> = <init2>; ...
    // Inisialisasi berjalan dalam scope method, jadi `iki` sah dan nama luar
    // ditangkap sebagai upvalue seperti method biasa.
    FungsiKonteks f;
    f.chunk = std::make_shared<vm::Chunk>();
    f.chunk->nama = std::string_view("<field_init>");
    f.info = FungsiInfo{};
    f.info.nama = f.chunk->nama;
    f.info.method = true;
    f.info.ini_boleh = true;
    f.dalam_fungsi = true;
    f.n_slot_terpakai = 1;  // hanya `this`
    f.n_slot_maks = 1;
    f.info.arity = 0;
    fungsi_stack_.push_back(std::move(f));
    FungsiKonteks& ctx = fn();
    for (const ast::FieldKelas* fk : field) {
        if (fk == nullptr || fk->nilai == nullptr) continue;
        emit(Op::GET_LOCAL, 0);  // iki
        ekspresi(fk->nilai);
        emit(Op::SET_PROP, static_cast<std::uint16_t>(tambah_nama(Value::obyek(rt::buat_teks(heap_, fk->nama)))));
        emit(Op::POP);
    }
    emit(Op::MBOH);
    emit(Op::RETURN_UNDEF);
    ctx.chunk->jumlah_slot = static_cast<std::uint8_t>(std::min<std::size_t>(ctx.n_slot_maks, 250));
    ctx.chunk->jumlah_param = 0;
    ctx.chunk->n_argumen_tetap = 0;
    ctx.chunk->variadic = false;
    ctx.chunk->panah = false;
    ctx.chunk->mengko = false;
    ctx.chunk->generator = false;
    ctx.chunk->peta_baris.finalize();

    // Resolusi upvalue — salinan ringkas dari `eks_fungsi` (Lox).
    constexpr std::int32_t kSentinelGlobal = -0x40000000;
    std::function<std::int32_t(std::size_t, std::string_view)> petakan_upvalue;
    petakan_upvalue = [&](std::size_t idx, std::string_view nama) -> std::int32_t {
        if (idx == 0) return kSentinelGlobal;
        FungsiKonteks& induk = fungsi_stack_[idx - 1];
        const auto it = induk.lokal.find(std::string(nama));
        if (it != induk.lokal.end() && it->second != 0) {
            return static_cast<std::int32_t>(it->second);
        }
        for (std::size_t k = 0; k < induk.info.ambil_upvalue.size(); ++k) {
            if (induk.info.ambil_upvalue[k].second == nama) {
                return -static_cast<std::int32_t>(k) - 1;
            }
        }
        petakan_upvalue(idx - 1, nama);
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

    vm::ChunkPtr anak = ctx.chunk;
    semua_chunk_.push_back(anak);
    fungsi_stack_.pop_back();
    const std::size_t idx_anak = fn().chunk->anak.size();
    fn().chunk->anak.push_back(anak);
    emit(Op::CLOSURE, static_cast<std::uint16_t>(idx_anak));
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
    // siklik (`a impor b; b impor a`) bisa saling memanggil.
    if (sudah_hoist) return;

    // --- `ekspor { a, b minangka c }` (+ opsional `saka "mod"`) -------------
    if (!n->daftar.empty() || n->ada_modul) {
        // Re-export `ekspor { y } saka "./lain.jw"`: impor modul itu, lalu
        // TERUSKAN sel yang sama. `GET_IMPORT` mencari modul lewat
        // `Frame::impor_modul`, jadi objek ekspornya tidak perlu di stack.
        std::size_t idx_rekspor = 0;
        if (n->ada_modul) {
            idx_rekspor = impor_modul_ke_++;
            const std::size_t km = tambah_konstanta(Value::obyek(rt::buat_teks(heap_, n->modul)));
            emit(Op::TEKS, static_cast<std::uint16_t>(km));
            emit(Op::IMPORT, static_cast<std::uint16_t>(idx_rekspor));
            emit(Op::POP);  // objek ekspor tidak dipakai langsung
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
                ekspor_tunda_.push_back(EksporTunda{sp.lokal, sp.ekspor, n->ada_modul, idx_rekspor});
                continue;
            }
            if (n->ada_modul) {
                // Re-export: teruskan SEL yang sama, jangan bungkus ulang. Kalau
                // dibungkus, kedua modul punya sel terpisah dan live binding
                // ikut terputus di tengah rantai.
                const std::size_t kn = tambah_nama(Value::obyek(rt::buat_teks(heap_, sp.lokal)));
                emit(Op::GET_IMPORT, static_cast<std::uint16_t>(idx_rekspor), static_cast<std::uint16_t>(kn));
            } else {
                emit(Op::GET_LOCAL, static_cast<std::uint16_t>(it));
                if (!adalah_sel(sp.lokal)) {
                    // Fungsi/kelas: bungkus dalam sel baru supaya impor tetap
                    // live (meski nilainya sendiri tidak pernah berubah). Slot
                    // variabel yang diekspor sudah berisi `SelObj`; yang ini
                    // diekspor apa adanya supaya importer memakai sel yang sama.
                    emit(Op::SEL_BUAT);
                }
            }
            const std::size_t k = tambah_konstanta(Value::obyek(rt::buat_teks(heap_, sp.ekspor)));
            emit(Op::TEKS, static_cast<std::uint16_t>(k));
            emit(Op::EXPORT, 0);
        }
        return;
    }

    // --- `ekspor <deklarasi>` / `ekspor baku <ekspresi>` ---
    // Deklarasi dikompilasi normal (jadi slot lokal modul), lalu slotnya dibaca
    // ulang dan ditaruh ke objek ekspor. Cara ini berlaku seragam untuk
    // `gawe`, `golongan`, dan `tetep`, tanpa bentuk bytecode khusus.
    if (n->default_ekspor) {
        if (n->deklarasi == nullptr) {
            emit(Op::MBOH);
        } else if (n->deklarasi->kind == NK::FungsiDeklarasi ||
                   n->deklarasi->kind == NK::GolonganDeklarasi ||
                   n->deklarasi->kind == NK::DeklarasiVar) {
            if (!sudah_hoist) statement(n->deklarasi, sudah_hoist);
            const auto s = cari_slot(nama_deklarasi(n->deklarasi));
            if (s != std::string::npos) {
                emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s));
                if (!adalah_sel(nama_deklarasi(n->deklarasi))) emit(Op::SEL_BUAT);
            } else {
                emit(Op::MBOH);
                emit(Op::SEL_BUAT);
            }
        } else if (n->deklarasi->kind == NK::EkspresiStmt) {
            // `ekspor baku <ekspresi>`: NILAI ekspresi yang diekspor, bukan
            // statement-nya. `statement()` akan menambah `POP` yang menghapus
            // nilai itu, jadi ekspresinya dikompilasi langsung.
            ekspresi(static_cast<const ast::EkspresiStmt*>(n->deklarasi)->ekspresi);
            emit(Op::SEL_BUAT);
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
    if (!adalah_sel(nama)) emit(Op::SEL_BUAT);
    const std::size_t k = tambah_konstanta(Value::obyek(rt::buat_teks(heap_, nama)));
    emit(Op::TEKS, static_cast<std::uint16_t>(k));
    emit(Op::EXPORT, 0);
}

void Compiler::stmt_impor(const ast::ImporDeklarasi* n) {
    // IMPOR: muat modul, taruh ekspornya di stack, lalu bind tiap nama.
    // Objek ekspor didupe-kan tiap iterasi supaya `GET_PROP` tidak
    // menghabiskan satu-satunya rujukan.
    const std::size_t idx_modul = impor_modul_ke_++;
    if (!n->ada_modul) {
        emit(Op::MBOH);
    } else {
        const std::size_t k = tambah_konstanta(Value::obyek(rt::buat_teks(heap_, n->modul)));
        emit(Op::TEKS, static_cast<std::uint16_t>(k));
        // Operand `IMPORT` = indeks modul ini pada `Frame::impor_modul`, dipakai
        // `GET_IMPORT` untuk menemukan sel live binding-nya.
        emit(Op::IMPORT, static_cast<std::uint16_t>(idx_modul));
    }
    if (n->ada_namespace) {
        // `impor * minangka M` (atau `impor M saka "..."`): objek ekspor
        // langsung diikat sebagai satu nilai. Isinya tetap objek ekspor
        // biasa; `VM::ambil_properti` yang membongkar sel di dalamnya.
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
        // `GET_IMPORT` (bukan `GET_PROP`): nama yang tidak diekspor modul harus
        // menjadi galat, bukan `mboh` -- impor salah ketik adalah kesalahan
        // program, bukan nilai kosong. Hasilnya `SelObj` yang diikat ke slot
        // lokal, jadi perubahan di modul asal tetap terlihat.
        // `baku` = `default`: nama baku untuk ekspor default, ditulis dengan
        // ejaan Jawa supaya konsisten dengan bahasa.
        const std::string_view sumber = sp.sumber == "baku" ? std::string_view("default") : sp.sumber;
        const std::size_t k = tambah_nama(Value::obyek(rt::buat_teks(heap_, sumber)));
        emit(Op::GET_IMPORT, static_cast<std::uint16_t>(idx_modul), static_cast<std::uint16_t>(k));
        // Sel pengikat sudah dibuat di awal modul (lihat `Compiler::compile`),
        // jadi di sini sel itu cukup diarahkan ke sel milik modul pengekspor.
        // both pihak lalu benar-benar berbagi satu nilai -- itulah live binding.
        const std::size_t s = slot_impor(sp.impor);
        emit(Op::SEL_ALIAS, static_cast<std::uint16_t>(s));
        // Pengikatan impor adalah sel: tandai supaya semua pembacaan &
        // penulisan namanya memakai `GET_CELL`/`SET_CELL`.
        sel_slot_.emplace(std::string(sp.impor), 0);
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
