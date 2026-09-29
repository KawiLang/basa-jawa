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
        for (const support::Diagnostic& d : bag.galat()) {
            const std::string s = d.format();
            std::fwrite(s.data(), 1, s.size(), stderr);
        }
        return Status::Galat;
    }

    parse::Parser parser(token, arena_scratch_, bag, nama_berkas);
    const ast::NodePtr prog = parser.parse_program();
    if (bag.ada_galat()) {
        for (const support::Diagnostic& d : bag.galat()) {
            const std::string s = d.format();
            std::fwrite(s.data(), 1, s.size(), stderr);
        }
        return Status::Galat;
    }

    compile::Compiler kompiler(heap_, bag, nama_berkas);
    const compile::HasilKompilasi hasil = kompiler.compile(static_cast<const ast::Program*>(prog));
    if (bag.ada_galat() || hasil.modul == nullptr) {
        for (const support::Diagnostic& d : bag.galat()) {
            const std::string s = d.format();
            std::fwrite(s.data(), 1, s.size(), stderr);
        }
        return Status::Galat;
    }

    // Bentuk objek fungsi + closure modul.
    FungsiObj* fn = heap_.alokasi<FungsiObj>();
    fn->h.kind = OK::Fungsi;
    fn->kode = hasil.modul;
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
        modul_aktif = rec;
        rec->entri = clo;
    }
    heap_.bersihkan_akar_sementara();
    pos_berkas_ = std::string(nama_berkas);

    reset_stack();
    dorong(Value::mboh());          // this modul
    dorong(Value::obyek(clo));       // callee
    const Status s = jalankan();
    if (s == Status::Galat && galat_.ada) {
        const std::string pesan = rt::nilai_ke_teks(*this, galat_.nilai);
        std::fprintf(stderr, "%s\n", pesan.c_str());
        const std::string jejak = jejak_stack();
        std::fwrite(jejak.data(), 1, jejak.size(), stderr);
    }
    modul_aktif = nullptr;
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
    return jalankan_loop();
}

}  // namespace jawa::vm
