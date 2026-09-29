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

    // 0. Slot untuk pengikat impor dialokasikan lebih dulu (tanpa emits), supaya
    //    fungsi yang di-hoist.capture-nya sebagai upvalue SLOT, bukan sebagai
    //    nama global. Tanpa ini, `a impor b; b impor a` menghasilkan fungsi yang
    //    memanggil binding global yang belum pernah diisi.
    for (const ast::Node* s : semua_statement) {
        if (s == nullptr || s->kind != NK::ImporDeklarasi) continue;
        const auto* im = static_cast<const ast::ImporDeklarasi*>(s);
        if (im->ada_namespace && !im->alias_namespace.empty()) {
            impor_slot_.emplace(std::string(im->alias_namespace), slot_baru(im->alias_namespace));
        }
        for (const ast::ImporSpesifikasi& sp : im->daftar) {
            impor_slot_.emplace(std::string(sp.impor), slot_baru(sp.impor));
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

    for (const ast::Node* s : semua_statement) {
        const bool sudah = std::find(dinaikkan.begin(), dinaikkan.end(), s) != dinaikkan.end();
        statement(s, sudah);
    }

    // Ekspor tertunda (`ekspor { x }` mendahului deklarasinya).
    for (const EksporTunda& t : ekspor_tunda_) {
        const auto s = cari_slot(t.lokal);
        if (s == std::string::npos) {
            diagnosa_di("S503", "Jeneng \"" + std::string(t.lokal) + "\" ora kanggo diekspor.",
                        "Gawe variabel utawa fungsi luwih dhisik, banjur `ekspor` iku.");
            continue;
        }
        emit(Op::GET_LOCAL, static_cast<std::uint16_t>(s));
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
