// VM:(Source -> program) pipeline & frame bootstrap.
#include "compile/compiler.h"
#include "lex/lexer.h"
#include "parse/parser.h"
#include "rt/number.h"
#include "rt/object.h"
#include "rt/string.h"
#include "stdlib/stdlib.h"
#include "vm/vm.h"

#include <algorithm>
#include <cstdio>

namespace jawa::vm {

using rt::ClosureObj;
using rt::FungsiObj;
using rt::Obj;
using rt::OK;
using rt::Value;

// ===========================================================================
// Pipeline: lexer -> parser -> compiler -> objek fungsi -> loop
// ===========================================================================

Status VM::jalankan_sumber(std::string_view sumber, std::string_view nama_berkas, std::string_view dir) {
    (void)dir;
    support::DiagnosticBag bag{50};
    lex::TokenList token;
    lex::Lexer lx(sumber, nama_berkas, dir, lex::LexOptions{opt_.strict_titik_koma, opt_.strict_krama});
    lx.lex_semua(token);
    bag.gabung(lx.bag());
    if (bag.ada_galat()) {
        support::cetak_diagnostik(stderr, bag);
        return Status::Galat;
    }

    parse::Parser parser(token, arena_scratch_, bag, nama_berkas);
    const ast::NodePtr prog = parser.parse_program();
    if (bag.ada_galat()) {
        support::cetak_diagnostik(stderr, bag);
        return Status::Galat;
    }

    compile::Compiler kompiler(heap_, bag, nama_berkas);
    const compile::HasilKompilasi hasil = kompiler.compile(static_cast<const ast::Program*>(prog));
    if (bag.ada_galat() || hasil.modul == nullptr) {
        support::cetak_diagnostik(stderr, bag);
        return Status::Galat;
    }

    // Tidak ada galat, tapi bisa ada peringatan -- terutama diagnosa kata
    // boring: `return 1;` yang salah ketik TIDAK menghasilkan galat apa pun,
    // hanya peringatan. Kalau yang di-cetak cuma cabang galat, peringatan itu
    // hilang tepat di tempat program akan berjalan dengan hasil yang salah.
    support::cetak_diagnostik(stderr, bag);

    return jalankan_modul(hasil.modul, hasil.semua, nama_berkas, sumber);
}

Status VM::jalankan_modul(ChunkPtr modul, const std::vector<ChunkPtr>& semua, std::string_view nama_berkas,
                          std::string_view sumber) {
    // Bentuk objek fungsi + closure modul.
    FungsiObj* fn = heap_.alokasi<FungsiObj>();
    fn->h.kind = OK::Fungsi;
    fn->kode = modul;
    fn->nama = "<modul>";

    auto* clo = heap_.alokasi<ClosureObj>();
    clo->h.kind = OK::Closure;
    clo->fungsi = fn;
    clo->nama = "<modul>";

    // Chunk (dan dengan itu pool konstantanya) kini dimiliki `FungsiObj` yang
    // sudah jadi root modul, sehingga akar sementara kompilator boleh dilepas.
    daftarkan_modul(nama_berkas, nama_berkas, sumber);
    ModuleRecord* rec = cari_modul(nama_berkas);
    if (rec != nullptr) {
        // Modul utama juga didorong ke tumpukan modul: `evaluasi_modul`
        // memulihkan `modul_aktif` ke puncak tumpukan, jadi modul utama harus
        // ada di sana agar `ekspor` setelah `impor` tetap melihat modul ini.
        rec->entri = clo;
        // Objek ekspor modul utama juga dibuat di sini, supaya `ekspor` di
        // berkas yang dijalankan langsung bisa bekerja (modul yang diimpor
        // membuatnya di `VM::evaluasi_modul`).
        rec->ekspor = Value::obyek(buat_obyek());
        modul_tumpukan_.push_back(rec);
        modul_aktif = rec;
    }
    // Semua chunk (modul utama + fungsi anak) di-root selama program berjalan;
    // lihat `VM::chunk_akar_`.
    for (const ChunkPtr& c : semua) chunk_akar_.push_back(c);
    heap_.bersihkan_akar_sementara();
    pos_berkas_ = std::string(nama_berkas);

    reset_stack();
    dorong(Value::mboh());          // this modul
    dorong(Value::obyek(clo));       // callee
    Status s = jalankan();
    if (s == Status::Selesai) {
        // Kode sinkron selesai. Jalankan loop acara: mikrotugas lalu timer, agar
        // rantai `async` yang tertunda oleh `entani` bisa dilanjutkan.
        (void)jalankan_loop_acara();
        if (galat_.ada) s = Status::Galat;
    }
    if (s == Status::Galat && galat_.ada) {
        const std::string pesan = rt::nilai_ke_teks(*this, galat_.nilai);
        std::fprintf(stderr, "%s\n", pesan.c_str());
        const std::string jejak = jejak_stack();
        std::fwrite(jejak.data(), 1, jejak.size(), stderr);
    }
    modul_tumpukan_.pop_back();
    modul_aktif = modul_tumpukan_.empty() ? nullptr : modul_tumpukan_.back();
    return s;
}

// ===========================================================================
// Bootstrap frame
// ===========================================================================

Status VM::jalankan() {
    frames_.clear();
    const Value clo_val = stack_.back();
    if (!clo_val.is_obyek() || clo_val.pointer() == nullptr) return Status::Galat;
    auto* clo = static_cast<ClosureObj*>(const_cast<void*>(clo_val.pointer()));
    Frame f;
    f.closure = clo;
    f.chunk = clo->fungsi->kode.get();
    f.ip = 0;
    f.slot_base = 0;
    f.n_argumen = 0;
    f.this_val = stack_[0];
    // Stack modul: slot 0 = `this`, lalu seluruh slot lokal modul. Frame harus
    // punya ruang cukup SEBELUM bytecode pertama menulis (DEF_LOCAL).
    const std::size_t total = std::max<std::size_t>(f.chunk->jumlah_slot, 2u);
    while (stack_.size() < f.slot_base + total) stack_.push_back(Value::mboh());
    frames_.push_back(std::move(f));
    // Frame modul menjadi akar rantai `async` HANYA bila modul memang memakai
    // `enteni` di tingkat modul. Tanpa ini, setiap panggilan `mengko` di
    // tingkat modul ikut menunda modul, dan `dhisik` tercetak setelah `42`.
    if (clo->fungsi->kode->await_tingkat_modul) {
        frames_.back().akar_async = true;
        dasar_async_ = 0;
    } else {
        dasar_async_ = kTanpaAsync;
    }
    return jalankan_loop(frames_.size());
}

}  // namespace jawa::vm
