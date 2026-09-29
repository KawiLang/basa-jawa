#include "compile/compiler.h"

#include <algorithm>
#include <cmath>

#include "rt/number.h"
#include "rt/string.h"

namespace jawa::compile {

using ast::NK;
using ast::NodePtr;
using vm::Chunk;
using vm::Op;

// ===========================================================================
// Setup
// ===========================================================================

Compiler::Compiler(gc::Heap& heap, support::DiagnosticBag& bag, std::string_view nama_berkas)
    : heap_(heap), bag_(bag), nama_berkas_(nama_berkas) {}

std::uint32_t Compiler::baris_sekarang() const {
    return static_cast<std::uint32_t>(fn().chunk->kode.empty() ? 0 : fn().chunk->kode.back().baris);
}

std::uint16_t Compiler::emit(Op op, std::uint16_t a, std::uint16_t b) {
    return fn().chunk->emit(op, a, b, 0);
}

/// Terbitkan `NOP_LINE` bila baris sumber berpindah.
///
/// Tanpa ini `pos_sumber_` di VM selalu tertinggal di baris pertama chunk:
/// setiap `Instruksi` memang menyimpan `baris` sendiri, tapi baris placeholder
/// `0` hanya diisi oleh `NOP_LINE`. Akibatnya pesan galat runtime, jejak stack,
/// dan laporan assertion `jawa tes` menunjuk baris yang salah.
void Compiler::tandai_baris(uint32_t baris) {
    FungsiKonteks& f = fn();
    if (baris == 0 || baris == f.baris_terakhir) return;
    f.baris_terakhir = baris;
    f.chunk->emit(Op::NOP_LINE, static_cast<std::uint16_t>(baris), 0);
}

std::uint16_t Compiler::emit_at(std::size_t idx, Op op, std::uint16_t a, std::uint16_t b) {
    (void)idx;
    return emit(op, a, b);
}

std::size_t Compiler::tambah_konstanta(Value v) {
    // Akar sementara: sebelum frame VM ada, konstanta ini belum punya akar lain,
    // sehingga `--gc-stress` (yang mengoleksi tiap alokasi) akan membebaskannya.
    heap_.akar_sementara(v);
    return fn().chunk->tambah_konstanta(v);
}

std::size_t Compiler::tambah_nama(Value v) {
    heap_.akar_sementara(v);
    return fn().chunk->tambah_nama_properti(v);
}

void Compiler::patch(std::size_t idx, std::size_t tujuan) {
    if (idx < fn().chunk->kode.size()) {
        fn().chunk->kode[idx].a = static_cast<std::uint16_t>(tujuan);
    }
}

void Compiler::patch_sebalik(std::size_t idx, std::size_t tujuan) {
    if (idx < fn().chunk->kode.size()) {
        fn().chunk->kode[idx].a = static_cast<std::uint16_t>(tujuan);
    }
}

void Compiler::diagnosa(const char* kode, std::string pesan, SourceRange r, std::string saran) {
    support::Diagnostic d;
    d.code = kode;
    d.kind = support::DiagKind::Scope;
    d.level = support::DiagLevel::Galat;
    d.berkas = nama_berkas_;
    d.pos = r.mulai;
    d.pesan = std::move(pesan);
    d.saran = std::move(saran);
    bag_.add_galat(std::move(d));
}

void Compiler::diagnosa_di(const char* kode, std::string pesan, std::string saran) {
    SourceRange r;
    if (!fn().chunk->kode.empty()) r.selesai.baris = fn().chunk->kode.back().baris;
    diagnosa(kode, std::move(pesan), r, std::move(saran));
}

// ===========================================================================
// Slot & upvalue
// ===========================================================================

std::size_t Compiler::slot_baru(const std::string_view nama) {
    FungsiKonteks& f = fn();
    if (f.lokal.find(std::string(nama)) != f.lokal.end()) {
        diagnosa_di("S401", "Jeneng \"" + std::string(nama) + "\" wis kinandal ing iki tanpa var utawa tetep.",
                    "Ganti jeneng utawa watesi ing scope liya.");
    }
    const std::size_t s = f.n_slot_terpakai++;
    if (f.n_slot_terpakai > f.n_slot_maks) f.n_slot_maks = f.n_slot_terpakai;
    f.lokal[std::string(nama)] = s;
    return s;
}

std::size_t Compiler::slot_baru_tdz(const std::string_view nama) {
    FungsiKonteks& f = fn();
    // Pakai slot yang sudah dipra-daftarkan kalau ada, supaya pra-walk dan
    // statement deklarasi menunjuk slot yang sama.
    std::size_t s;
    const auto it = f.tdz_slot.find(nama);
    if (it != f.tdz_slot.end()) {
        s = it->second;
    } else {
        s = slot_baru(nama);
    }
    if (f.tdz_menunggu.count(s) != 0) return s;  // sudah dipra-daftarkan
    const std::size_t pos = f.chunk->tdz_daftar.size();
    f.chunk->tdz_daftar.push_back(0);  // diisi setelah statement deklarasi selesai
    f.tdz_menunggu[s] = pos;
    return s;
}

namespace {

/// Semua statement anak dari sebuah node (untuk pra-walk TDZ). Vektor kosong
/// untuk node yang tidak memuat statement.
std::vector<const ast::Node*> anak_stmt(const ast::Node* n) {
    using ast::NK;
    std::vector<const ast::Node*> out;
    if (n == nullptr) return out;
    switch (n->kind) {
        case NK::EkspresiStmt:
        case NK::KosongStmt:
        case NK::BaliStmt:
        case NK::MandhegStmt:
        case NK::TerusnaStmt:
        case NK::UncalStmt:
        case NK::FungsiDeklarasi:
        case NK::ImporDeklarasi:
        case NK::DebuggerStmt:
            break;
        case NK::Blok: {
            const auto* b = static_cast<const ast::BlokStmt*>(n);
            for (const ast::Node* x : b->body) out.push_back(x);
            break;
        }
        case NK::YenStmt: {
            const auto* y = static_cast<const ast::YenStmt*>(n);
            out.push_back(y->lalu);
            if (y->ada_liyane) out.push_back(y->liyane);
            break;
        }
        case NK::NalikaStmt: out.push_back(static_cast<const ast::NalikaStmt*>(n)->awak); break;
        case NK::LakoniStmt: out.push_back(static_cast<const ast::LakoniStmt*>(n)->awak); break;
        case NK::KanggoStmt: {
            const auto* k = static_cast<const ast::KanggoStmt*>(n);
            out.push_back(k->inisialisasi);
            out.push_back(k->awak);
            break;
        }
        case NK::KanggoOfStmt:
            out.push_back(static_cast<const ast::KanggoOfStmt*>(n)->target);
            out.push_back(static_cast<const ast::KanggoOfStmt*>(n)->awak);
            break;
        case NK::PilihStmt: {
            const auto* p2 = static_cast<const ast::PilihStmt*>(n);
            for (const ast::Node* c : p2->kasus) {
                out.push_back(static_cast<const ast::KasusKlap*>(c));
            }
            break;
        }
        case NK::CobaStmt: out.push_back(static_cast<const ast::CobaStmt*>(n)->blok); break;
        case NK::KanggoInStmt: {
            const auto* k = static_cast<const ast::KanggoInStmt*>(n);
            out.push_back(k->target);
            out.push_back(k->awak);
            break;
        }
        case NK::GolonganDeklarasi: {
            // Badan class = field/method/accessor (bukan statement), tapi blok
            // statisnya adalah statement biasa.
            const auto* g = static_cast<const ast::GolonganDeklarasi*>(n);
            for (const ast::Node* x : g->statis_blok) out.push_back(x);
            break;
        }
        case NK::EksporDeklarasi:
            out.push_back(static_cast<const ast::EksporDeklarasi*>(n)->deklarasi);
            break;
        case NK::DeklarasiVar:
            for (const ast::Node* lain : static_cast<const ast::DeklarasiVarStmt*>(n)->deklarator_lain) {
                out.push_back(lain);
            }
            break;
        case NK::LabelStmt: out.push_back(static_cast<const ast::LabelStmt*>(n)->awak); break;
        default: break;
    }
    return out;
}

}  // namespace

void Compiler::pradaftar_tdz(const ast::Node* n, int kedalaman) {
    if (n == nullptr) return;
    // Fungsi anak punya badan sendiri; jangan rekursi ke dalamnya.
    if (n->kind == NK::FungsiDeklarasi) return;
    if (kedalaman > 64) return;  // pengaman untuk AST tak wajar
    FungsiKonteks& f = fn();
    if (n->kind == NK::DeklarasiVar) {
        const auto* d = static_cast<const ast::DeklarasiVarStmt*>(n);
        std::vector<const ast::Node*> deklarator{d};
        for (const ast::Node* lain : d->deklarator_lain) deklarator.push_back(lain);
        for (const ast::Node* dek : deklarator) {
            const auto* dv = static_cast<const ast::DeklarasiVarStmt*>(dek);
            if (dv->jeneng.empty()) continue;
            if (f.tdz_slot.count(dv->jeneng) != 0) continue;
            const std::size_t s = slot_baru(dv->jeneng);
            f.tdz_slot[dv->jeneng] = s;
            const std::size_t pos = f.chunk->tdz_daftar.size();
            f.chunk->tdz_daftar.push_back(0);
            f.tdz_menunggu[s] = pos;
        }
    }
    for (const ast::Node* anak : anak_stmt(n)) pradaftar_tdz(anak, kedalaman + 1);
}

std::size_t Compiler::cari_slot(const std::string_view nama) const {
    auto it = fn().lokal.find(std::string(nama));
    return it == fn().lokal.end() ? static_cast<std::size_t>(-1) : it->second;
}

std::size_t Compiler::cari_upvalue(const std::string_view nama) {
    FungsiKonteks& f = fn();
    auto it = f.upvalue.find(std::string(nama));
    if (it != f.upvalue.end()) return it->second;
    const std::size_t idx = f.info.ambil_upvalue.size();
    f.info.ambil_upvalue.emplace_back(0, nama);
    f.upvalue[std::string(nama)] = idx;
    return idx;
}

// ===========================================================================
// Live binding modul ES
// ===========================================================================

bool Compiler::adalah_sel(std::string_view nama) const {
    return sel_slot_.find(std::string(nama)) != sel_slot_.end();
}

/// Catat nama yang diekspor sebagai **variabel** (bukan fungsi/kelas), supaya
/// statement mana pun yang membacanya tahu aksesnya harus lewat sel.
///
/// Harus dipanggil SEBELUM statement apa pun dikompilasi: `ekspor { n }`
/// boleh ditulis setelah penggunaan `n`, jadi statement yang membaca `n`
/// lebih dulu harus sudah tahu bahwa aksesnya lewat sel.
///
/// Hanya variabel yang ditandai. `ekspor { f }` untuk fungsi `f` juga muncul
/// di `daftar`, tapi nilainya tidak pernah berubah -- membungkusnya dalam sel
/// yang tidak pernah ditulis hanya menambah satu alokasi tanpa guna.
void Compiler::kumpulkan_ekspor_variabel(const ast::Node* n, int kedalaman) {
    if (n == nullptr || kedalaman > 32) return;
    if (n->kind == ast::NK::FungsiDeklarasi) return;  // badan sendiri
    if (n->kind == ast::NK::EksporDeklarasi) {
        const auto* e = static_cast<const ast::EksporDeklarasi*>(n);
        for (const ast::EksporSpesifikasi& sp : e->daftar) ekspor_nama_.emplace(std::string(sp.lokal), 0);
        if (e->deklarasi != nullptr && e->deklarasi->kind == ast::NK::DeklarasiVar) {
            const auto* d = static_cast<const ast::DeklarasiVarStmt*>(e->deklarasi);
            if (!d->jeneng.empty()) ekspor_nama_.emplace(std::string(d->jeneng), 0);
        }
    }
    for (const ast::Node* anak : anak_stmt(n)) kumpulkan_ekspor_variabel(anak, kedalaman + 1);
}

/// Daftar nama variabel modul (non-destruktur) yang diekspor. `FungsiDeklarasi`
/// & `GolonganDeklarasi` sengaja TIDAK termasuk: nilainya tetap, jadi tidak
/// perlu sel.

/// Kumpulkan SEMUA statement `impor` di bawah `n` (rekursif), termasuk yang
/// ada di dalam blok. Impor boleh ditulis di mana saja dalam badan modul,
/// tapi slot pengikatnya selalu milik frame modul -- jadi sel pengikatnya juga
/// harus dibuat di frame modul, bukan di dalam blok tempat statement itu berada.
void Compiler::kumpulkan_impor(const ast::Node* n, std::vector<const ast::Node*>& keluar) {
    if (n == nullptr) return;
    if (n->kind == ast::NK::FungsiDeklarasi) return;  // badan sendiri
    if (n->kind == ast::NK::ImporDeklarasi) keluar.push_back(n);
    for (const ast::Node* anak : anak_stmt(n)) kumpulkan_impor(anak, keluar);
}

void Compiler::tandai_sel_ekspor(const std::vector<ast::Node*>& statement_s) {
    for (const ast::Node* s : statement_s) {
        if (s == nullptr) continue;
        const ast::Node* deklarasi = deklarasi_ekspor(s);
        if (deklarasi != nullptr && deklarasi->kind == ast::NK::DeklarasiVar) {
            const auto* d = static_cast<const ast::DeklarasiVarStmt*>(deklarasi);
            if (!d->destruktur && !d->jeneng.empty()) {
                sel_slot_.emplace(std::string(d->jeneng), 0);
            }
            continue;
        }
        if (s->kind != ast::NK::EksporDeklarasi) continue;
        const auto* e = static_cast<const ast::EksporDeklarasi*>(s);
        for (const ast::EksporSpesifikasi& sp : e->daftar) {
            if (ekspor_nama_.count(std::string(sp.lokal)) == 0) continue;
            // `minangka` (alias ekspor) tidak mengubah jenis nilainya.
            if (variabel_modul_.count(std::string(sp.lokal)) != 0) {
                sel_slot_.emplace(std::string(sp.lokal), 0);
            }
        }
    }
}

// ===========================================================================
// Top-level
// ===========================================================================

HasilKompilasi Compiler::compile(const ast::Program* prog) {
    HasilKompilasi hasil;
    if (prog == nullptr) return hasil;

    // Konteks fungsi modul.
    FungsiKonteks f;
    f.chunk = std::make_shared<Chunk>();
    f.chunk->nama = "<modul>";
    f.info.nama = "<modul>";
    f.info.ini_boleh = false;
    // Slot 0 dicadangkan untuk `this` modul, supaya `GET_LOCAL 0` bermakna
    // sama di modul maupun di fungsi (lihat `VM::mulai_frame`).
    f.n_slot_terpakai = 1;
    f.n_slot_maks = 1;
    fungsi_stack_.push_back(std::move(f));

    // --- Hoisting ekspor (Fase 5) -----------------------------------------
    // Dua hal perlu hoist agar `ekspor` dan `impor` bekerja pada modul nyata:
    //
    // 1. Deklarasi fungsi/kelas yang DIEKSPOR dibuat lebih dulu, sebelum
    //    statement `impor`. Tanpa ini, impor siklik
    //    (`a impor b; b impor a;`) tidak bisa saling memanggil, sebab saat
    //    `b` diimpor, `a` belum punya nilai untuk `f`.
    //
    // 2. `ekspor { x, y }` yang ditulis SEBELUM deklarasinya ditunda sampai
    //    akhir body modul, ketika slot-nya sudah pasti ada.
    const std::vector<ast::Node*>& semua_statement = prog->body;
    std::unordered_set<std::string> sudah_dinaikkan;
    std::vector<const ast::Node*> dinaikkan;

    // 0. Kumpulkan nama yang diekspor. Harus SEBELUM statement apa pun
    //    dikompilasi, karena `ekspor { n }` boleh ditulis setelah `n` dipakai;
    //    statement yang membaca `n` lebih dulu harus sudah tahu bahwa aksesnya
    //    lewat sel, bukan `GET_LOCAL` biasa.
    for (const ast::Node* s : semua_statement) kumpulkan_ekspor_variabel(s);
    for (const ast::Node* s : semua_statement) {
        if (s == nullptr) continue;
        if (s->kind == ast::NK::DeklarasiVar) {
            const auto* d = static_cast<const ast::DeklarasiVarStmt*>(s);
            if (!d->destruktur && !d->jeneng.empty()) {
                variabel_modul_.emplace(std::string(d->jeneng), 0);
            }
        }
        if (s->kind == ast::NK::EksporDeklarasi) {
            const auto* e = static_cast<const ast::EksporDeklarasi*>(s);
            const ast::Node* d = e->deklarasi;
            if (d != nullptr && d->kind == ast::NK::DeklarasiVar) {
                const auto* dv = static_cast<const ast::DeklarasiVarStmt*>(d);
                if (!dv->destruktur && !dv->jeneng.empty()) {
                    variabel_modul_.emplace(std::string(dv->jeneng), 0);
                }
            }
        }
    }
    tandai_sel_ekspor(semua_statement);

    // 0b. Slot untuk pengikat impor dialokasikan lebih dulu (tanpa emits), supaya
    //    fungsi yang di-hoist.capture-nya sebagai upvalue SLOT, bukan sebagai
    //    nama global. Tanpa ini, `a impor b; b impor a` menghasilkan fungsi yang
    //    memanggil binding global yang belum pernah diisi.
    // Slot pengikat impor milik FRAME MODUL, jadi alokasi harus terjadi di
    // konteks modul -- termasuk untuk `impor` yang ditulis di dalam blok
    // (`coba { impor { x } saka "..."; ... }`).
    std::vector<const ast::Node*> semua_impor;
    kumpulkan_impor(semua_statement, semua_impor);
    for (const ast::Node* s : semua_impor) {
        const auto* im = static_cast<const ast::ImporDeklarasi*>(s);
        // `slot_baru` SELALU dievaluasi sebagai argumen `emplace`, jadi harus
        // dicek dulu: nama yang sama bisa diimpor dari dua modul
        // (`impor { x } saka "a"; impor { x } saka "b";`) dan hanya boleh punya
        // satu slot.
        if (im->ada_namespace && !im->alias_namespace.empty() &&
            impor_slot_.count(std::string(im->alias_namespace)) == 0) {
            impor_slot_.emplace(std::string(im->alias_namespace), slot_baru(im->alias_namespace));
        }
        for (const ast::ImporSpesifikasi& sp : im->daftar) {
            if (impor_slot_.count(std::string(sp.impor)) != 0) continue;
            impor_slot_.emplace(std::string(sp.impor), slot_baru(sp.impor));
        }
    }

    // 0c. Sel PENGIKAT untuk tiap nama impor, dibuat paling awal -- sebelum
    // hoisting ekspor pun. Closure yang ter-hoist menangkap sel ini sebagai
    // upvalue, dan upvalue terikat-sel membaca lewat sel, bukan lewat slot
    // stack. Kalau slot-nya masih kosong saat `CLOSURE` berjalan,
    // `cari_atau_buat_upvalue` akan mengikat upvalue ke slot stack biasa; begitu
    // `impor` mengisi slot dengan `SelObj`, pembacaannya menghasilkan objek sel
    // yang tidak bisa dipanggil.
    //
    // `SEL_ALIAS` (dalam `stmt_impor`) lalu mengarahkan sel pengikat ini ke sel
    // milik modul pengekspor, sehingga keduanya benar-benar berbagi nilai.
    // Iterasi mengikuti urutan statement (bukan `impor_slot_`, yang urutannya
    // tidak dijamin) supaya bytecode deterministik.
    //
    // Pemindaian masuk ke blok anak: `coba { impor { x } saka "..."; ... }`
    // juga butuh sel pengikat. Impor boleh muncul di mana saja dalam badan
    // modul, dan slot pengikatnya selalu milik frame modul.
    for (const ast::Node* s : semua_impor) {
        const auto* im = static_cast<const ast::ImporDeklarasi*>(s);
        if (im->ada_namespace) continue;  // impor namespace mengikat objek ekspor
        for (const ast::ImporSpesifikasi& sp : im->daftar) {
            const std::size_t slot = slot_impor(sp.impor);
            emit(Op::MBOH);
            emit(Op::SEL_BUAT);
            emit(Op::DEF_LOCAL, static_cast<std::uint16_t>(slot));
            sel_slot_.emplace(std::string(sp.impor), 0);
        }
    }

    for (const ast::Node* s : semua_statement) {
        const ast::Node* deklarasi = deklarasi_ekspor(s);
        if (deklarasi == nullptr) continue;
        if (deklarasi->kind != NK::FungsiDeklarasi && deklarasi->kind != NK::GolonganDeklarasi) continue;
        const std::string_view nama = nama_deklarasi(deklarasi);
        if (nama.empty() || sudah_dinaikkan.count(std::string(nama)) != 0) continue;
        // Statement `ekspor` dikompilasi UTUH (deklarasi + ekspor) di sini, jadi
        // nama sudah ada di objek ekspor sebelum `impor` mana pun dijalankan.
        statement(s);
        dinaikkan.push_back(s);
        sudah_dinaikkan.insert(std::string(nama));
    }

    // 0d. Statement `impor` dijalankan SETELAH hoisting ekspor, tapi SEBELUM
    // statement biasa apa pun.
    //
    // Alasannya urutan sel. Pengikatan impor disimpan sebagai `SelObj` di slot
    // lokal (dibuat di 0c). Kalau `impor` baru dijalankan SETELAH fungsi
    // ter-hoist dibuat, `CLOSURE`-nya sudah berjalan dan upvalue-nya terikat ke
    // SEL pengikat yang saat itu belum diarahkan -- nilainya masih kosong.
    //
    // Urutannya berlawanan dengan yang terlihat: `ekspor` fungsi harus lebih
    // dulu, supaya impor siklik (`a impor b; b impor a`) menemukan fungsi yang
    // sudah di-hoist di modul lain. `impor` sendiri baru setelah itu, tapi
    // tetap sebelum statement biasa -- sesuai semantik ES, impor selalu
    // di-hoist.
    for (const ast::Node* s : semua_statement) {
        if (s == nullptr || s->kind != NK::ImporDeklarasi) continue;
        statement(s);
        dinaikkan.push_back(s);
    }

    // Pra-walk TDZ: semua pengikat leksikal di modul ini dapat slot lebih dulu.
    for (const ast::Node* s : semua_statement) pradaftar_tdz(s);

    for (const ast::Node* s : semua_statement) {
        if (std::find(dinaikkan.begin(), dinaikkan.end(), s) != dinaikkan.end()) continue;
        statement(s, false);
    }

    // Ekspor tertunda (`ekspor { x }` mendahului deklarasinya, atau re-export).
    for (const EksporTunda& t : ekspor_tunda_) {
        if (t.dari_modul) {
            // Re-export: teruskan sel yang sama dari modul asal. Membungkus ulang
            // akan membuat dua sel terpisah dan live binding ikut terputus.
            const std::size_t k = tambah_nama(Value::obyek(rt::buat_teks(heap_, t.lokal)));
            emit(Op::GET_IMPORT, static_cast<std::uint16_t>(t.idx_modul), static_cast<std::uint16_t>(k));
            const std::size_t ke = tambah_konstanta(Value::obyek(rt::buat_teks(heap_, t.ekspor)));
            emit(Op::TEKS, static_cast<std::uint16_t>(ke));
            emit(Op::EXPORT, 0);
            continue;
        }
        const auto s = cari_slot(t.lokal);
        if (s == std::string::npos) {
            diagnosa_di("S503", "Jeneng \"" + std::string(t.lokal) + "\" ora kanggo diekspor.",
                        "Gawe variabel utawa fungsi luwih dhisik, banjur `ekspor` iku.");
            continue;
        }
        emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s));
        if (!adalah_sel(t.lokal)) emit(Op::SEL_BUAT);
        const std::size_t k = tambah_konstanta(Value::obyek(rt::buat_teks(heap_, t.ekspor)));
        emit(Op::TEKS, static_cast<std::uint16_t>(k));
        emit(Op::EXPORT, 0);
    }

    // Tutup blok: emit RETURN_UNDEF bila perlu.
    emit(Op::NOP);
    emit(Op::RETURN_UNDEF);
    fn().chunk->jumlah_slot = static_cast<std::uint8_t>(std::min<std::size_t>(fn().n_slot_maks, 250));
    fn().chunk->peta_baris.finalize();
    fn().chunk->nama = "<modul>";

    // Susun upvalue untuk setiap fungsi anak.
    for (std::size_t i = 0; i < fungsi_stack_.size(); ++i) {
        FungsiKonteks& fc = fungsi_stack_[i];
        fc.chunk->jumlah_upvalue = static_cast<std::uint8_t>(fc.info.ambil_upvalue.size());
        for (const auto& [indeks, nama] : fc.info.ambil_upvalue) {
            (void)indeks;
            fc.chunk->tambah_nama_upvalue(nama);
        }
    }

    hasil.modul = fungsi_stack_[0].chunk;
    for (FungsiKonteks& fc : fungsi_stack_) semua_chunk_.push_back(fc.chunk);
    hasil.semua = semua_chunk_;
    hasil.ada_galat = bag_.ada_galat();
    return hasil;
}

HasilKompilasi Compiler::compile_ekspresi(const ast::Node* expr) {
    HasilKompilasi hasil;
    FungsiKonteks f;
    f.chunk = std::make_shared<Chunk>();
    f.chunk->nama = "<expr>";
    f.info.nama = "<expr>";
    f.info.ini_boleh = false;
    f.n_slot_terpakai = 1;
    f.n_slot_maks = 1;
    fungsi_stack_.push_back(std::move(f));
    ekspresi(expr);
    emit(Op::RETURN);
    fn().chunk->jumlah_slot = static_cast<std::uint8_t>(std::min<std::size_t>(fn().n_slot_maks, 250));
    fn().chunk->peta_baris.finalize();
    for (FungsiKonteks& fc : fungsi_stack_) {
        fc.chunk->jumlah_upvalue = static_cast<std::uint8_t>(fc.info.ambil_upvalue.size());
        for (const auto& [indeks, nama] : fc.info.ambil_upvalue) {
            (void)indeks;
            fc.chunk->tambah_nama_upvalue(nama);
        }
    }
    hasil.modul = fungsi_stack_[0].chunk;
    for (FungsiKonteks& fc : fungsi_stack_) semua_chunk_.push_back(fc.chunk);
    hasil.semua = semua_chunk_;
    hasil.ada_galat = bag_.ada_galat();
    return hasil;
}

}  // namespace jawa::compile
