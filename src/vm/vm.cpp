#include "vm/vm.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "compile/compiler.h"
#include "rt/number.h"
#include "rt/object.h"
#include "rt/string.h"
#include "stdlib/stdlib.h"

namespace jawa::vm {

using rt::ArrayObj;
using rt::BoundFnObj;
using rt::ClassObj;
using rt::ClosureObj;
using rt::FungsiObj;
using rt::InstanceObj;
using rt::NativeFnObj;
using rt::ObyekObj;
using rt::Obj;
using rt::OK;
using rt::ShapeTable;
using rt::TeksObj;

// ===========================================================================
// Upvalue & frame
// ===========================================================================

// ===========================================================================
// VM — Konstruksi & destruktor
// ===========================================================================

VM::VM(const VMOptions& opt) : opt_(opt), heap_(opt.gc_stress, opt.maks_memori_mb) {
    heap_.set_log_gc(opt.log_gc);
    heap_.tambah_root_visitor([this](gc::RootVisitor& v) {
        for (const auto& fn : root_visitor_) fn(v);
    });
    // Visitor root didaftarkan DI KONSTRUKTOR (bukan per program) supaya heap
    // benar sejak awal, termasuk saat kompilator membuat konstanta.
    registrasikan_root_visitor([this](gc::RootVisitor& rv) {
        for (const Value& v : stack_) rv.rooted(v);
        for (const Frame& f : frames_) {
            rv.rooted(f.this_val);
            if (f.closure == nullptr) continue;
            for (void* pv : f.closure->upvalue) {
                auto* cell = static_cast<Upvalue*>(pv);
                if (cell != nullptr) rv.rooted(cell->get());
            }
        }
        for (Upvalue* u : open_upvalues_) {
            if (u != nullptr) rv.rooted(u->get());
        }
        for (const auto& sel : sel_tutup_) {
            if (sel != nullptr && !sel->open()) rv.rooted(sel->nilai);
        }
        rv.rooted(proto_dasar_);
        rv.rooted(proto_dhaptar_);
        if (modul_aktif != nullptr) {
            for (const auto& kv : modul_aktif->global) rv.rooted(kv.second);
        }
    });
    prototipe_dasar();
    prototipe_dhaptar();
    stdlib::pasang_semua(*this);
    reset_stack();
}

// Sel upvalue dimiliki `sel_tutup_` (vector<unique_ptr>); `open_upvalues_` hanya
// menyimpan pointer(non-owning) ke sel yang masih terbuka. Karena itu di sini
// TIDAK boleh `delete`: Ownership ada pada `sel_tutup_`.
VM::~VM() = default;

void VM::reset_stack() {
    for (Upvalue* u : open_upvalues_) u->close();
    open_upvalues_.clear();
    sel_tutup_.clear();
    stack_.clear();
    frames_.clear();
    galat_ = GalatRuntime{};
    langkah_ = 0;
}

void VM::registrasikan_root_visitor(std::function<void(gc::RootVisitor&)> fn) {
    root_visitor_.push_back(std::move(fn));
}

// ===========================================================================
// Objek helper
// ===========================================================================

ArrayObj* VM::buat_dhaptar(std::size_t kapasitas) {
    auto* a = heap_.alokasi<ArrayObj>();
    a->h.kind = OK::Array;
    a->init(ShapeTable::instance().kosong(), kapasitas);
    a->prototipe = Value::obyek(prototipe_dhaptar());
    return a;
}

ObyekObj* VM::buat_obyek() {
    auto* o = heap_.alokasi<ObyekObj>();
    o->h.kind = OK::Obyek;
    o->shape = ShapeTable::instance().kosong();
    o->slot = nullptr;
    o->jumlah_slot = 0;
    o->prototipe = Value::obyek(prototipe_dasar());
    return o;
}

TeksObj* VM::buat_teks(std::string_view s) { return rt::buat_teks(heap_, s); }

ObyekObj* VM::prototipe_dasar() {
    if (proto_dasar_.is_obyek() && proto_dasar_.pointer() != nullptr) {
        return static_cast<ObyekObj*>(proto_dasar_.mutable_pointer());
    }
    auto* p = heap_.alokasi<ObyekObj>();
    p->h.kind = OK::Obyek;
    p->shape = ShapeTable::instance().kosong();
    p->slot = nullptr;
    p->jumlah_slot = 0;
    proto_dasar_ = Value::obyek(p);
    return p;
}

ArrayObj* VM::prototipe_dhaptar() {
    if (proto_dhaptar_.is_obyek() && proto_dhaptar_.pointer() != nullptr) {
        return static_cast<ArrayObj*>(proto_dhaptar_.mutable_pointer());
    }
    auto* p = heap_.alokasi<ArrayObj>();
    p->h.kind = OK::Array;
    p->init(ShapeTable::instance().kosong(), 0);
    p->panjang = 0;
    proto_dhaptar_ = Value::obyek(p);
    return p;
}

// ===========================================================================
// Modul
// ===========================================================================

void VM::daftarkan_modul(std::string_view nama, std::string_view path, std::string_view sumber) {
    ModuleRecord m;
    m.nama = nama;
    m.path = path;
    m.sumber = sumber;
    m.ekspor = Value::mboh();
    modul_store_.push_back(std::move(m));
    modul_[std::string(nama)] = &modul_store_.back();
}

ModuleRecord* VM::cari_modul(std::string_view nama) const {
    auto it = modul_.find(std::string(nama));
    return it == modul_.end() ? nullptr : it->second;
}

Value VM::ambil_global(std::string_view nama) {
    if (modul_aktif != nullptr) {
        auto it = modul_aktif->global.find(std::string(nama));
        if (it != modul_aktif->global.end()) return it->second;
        // prototype chain: walk up module? (modul tunggal untuk sekarang)
    }
    return stdlib::ambil_global(*this, nama);
}

void VM::set_global(std::string_view nama, Value v) {
    if (modul_aktif != nullptr) modul_aktif->global[std::string(nama)] = v;
    else stdlib::set_global(*this, nama, v);
}

// ===========================================================================
// Error
// ===========================================================================

void VM::lempar(Value v) {
    galat_.nilai = v;
    galat_.ada = true;
}

rt::Value VM::buat_kleru(std::string_view jeneng, std::string pesan) {
    auto* k = heap_.alokasi<rt::KleruObj>();
    k->h.kind = rt::OK::Kleru;
    k->jeneng = singsan(jeneng);
    k->pesan = std::move(pesan);
    k->berkas = singsan(pos_berkas_);
    k->pos = pos_sumber_;
    return Value::obyek(k);
}

void VM::set_posisi_sumber(std::string_view berkas, SourcePos pos) {
    pos_berkas_ = std::string(berkas);
    pos_sumber_ = pos;
}

std::string VM::jejak_stack() const {
    std::string out;
    for (std::size_t i = frames_.size(); i > 0; --i) {
        const Frame& f = frames_[i - 1];
        out += "  ing " + std::string(f.chunk != nullptr ? f.chunk->nama : "?") + "()\n";
    }
    if (!pos_berkas_.empty()) {
        out = "  ing " + pos_berkas_ + ":" + std::to_string(pos_sumber_.baris) + ":" +
              std::to_string(pos_sumber_.kolom) + "\n" + out;
    }
    return out;
}

// ===========================================================================
// Panggil
// ===========================================================================

namespace {
/// Objek dari Value, atau nullptr.
Obj* objek(Value v) {
    if (!v.is_obyek() && !v.is_bigint() && !v.is_simbol()) return nullptr;
    const void* p = v.pointer();
    return p == nullptr ? nullptr : const_cast<Obj*>(static_cast<const Obj*>(p));
}
}  // namespace

/// Panggil closure dari kode native (reentrancy terkontrol).
Value VM::panggil(Value callee, Value this_val, const std::vector<Value>& args) {
    if (galat_.ada) return Value::mboh();
    if (++reentrancy_ > static_cast<int>(opt_.maks_reentrancy)) {
        --reentrancy_;
        std::fprintf(stderr, "KleruRentang [R003] Reentrancy native->JS kebijauan (wates: %zu).\n",
                     opt_.maks_reentrancy);
        galat_.ada = true;
        return Value::mboh();
    }
    const std::size_t simpan = stack_.size();
    std::vector<Value> salinan(args);
    if (Obj* o = objek(callee); o != nullptr && o->h.kind == OK::Closure) {
        // Reentrancy native -> JS: kita masuk ke loop yang sama, tapi dengan
        // penjaga kedalaman sehingga berhenti tepat saat frame ini selesai.
        mulai_frame(static_cast<ClosureObj*>(o), this_val, salinan);
        (void)jalankan_loop();
    } else {
        panggil_objek(callee, this_val, salinan);
    }
    --reentrancy_;
    const Value hasil = stack_.empty() ? Value::mboh() : stack_.back();
    stack_.resize(simpan);
    if (galat_.ada) return Value::mboh();
    return hasil;
}

void VM::panggil_objek(Value callee, Value this_val, std::vector<Value>& args) {
    Obj* o = nullptr;
    if (callee.is_obyek() || callee.is_bigint() || callee.is_simbol()) {
        const void* p = callee.pointer();
        if (p != nullptr) o = const_cast<Obj*>(static_cast<const Obj*>(p));
    }
    if (o == nullptr) {
        std::fprintf(stderr, "KleruJinis [R002] Ora bisa nelep nilai: dudu fungsi.\n");
        galat_.ada = true;
        dorong(Value::mboh());
        return;
    }
    (void)0;
    switch (o->h.kind) {
        case OK::Native: {
            auto* n = static_cast<NativeFnObj*>(o);
            dorong(stdlib::panggil_native(*this, n, this_val, args));
            return;
        }
        case OK::Closure: {
            auto* c = static_cast<ClosureObj*>(o);
            if (c->fungsi->kode->generator) {
                jalankan_generator_eager(c, this_val, args);
                return;
            }
            // Panggilan JS -> JS TIDAK memakai rekursi C++: frame baru didorong
            // ke `frames_` dan langsung ditangani iterasi `jalankan_loop`
            // berikutnya (lihat DECISIONS.md D-003). Ini membuat rekursi tak
            // hingga pada kode Basa Jawa menghasilkan `KleruRentang`, bukan
            // stack overflow native.
            mulai_frame(c, this_val, args);
            return;
        }
        case OK::BoundFn: {
            auto* b = static_cast<BoundFnObj*>(o);
            std::vector<Value> gabung(b->bound);
            gabung.insert(gabung.end(), args.begin(), args.end());
            panggil_objek(b->target, b->this_val, gabung);
            return;
        }
        case OK::Golongan: {
            auto* kls = static_cast<ClassObj*>(o);
            if (kls->konstruktor.is_obyek()) {
                panggil_objek(kls->konstruktor, this_val, args);
                return;
            }
            dorong(Value::obyek(instans_baru(kls)));
            return;
        }
        default:
            std::fprintf(stderr, "KleruJinis [R002] Ora bisa nelep: iki dudu fungsi.\n");
            galat_.ada = true;
            dorong(Value::mboh());
            return;
    }
}

void VM::jalankan_generator_eager(ClosureObj* fn, Value this_val, std::vector<Value>& args) {
    // CATATANPhase 3 (lihat DECISIONS.md D-014): generator dijalankan sampai
    // selesai dan semua hasil `metokake` dikumpulkan menjadi Dhaptar. Ini
    // "eager generator": benar untuk generator berhingga (seperti contoh acuan
    // `[...cacah(5)]`), TIDAK untuk generator tak berhingga. Batas jumlah hasil
    // dilindungi supaya program tidak menggantung tanpa news.
    constexpr std::size_t kMaksHasil = 1u << 20;
    rt::ArrayObj* kumpulan = buat_dhaptar();
    tumbles_yield_.push_back(kumpulan);
    if (++reentrancy_ > static_cast<int>(opt_.maks_reentrancy)) {
        --reentrancy_;
        tumbles_yield_.pop_back();
        std::fprintf(stderr, "KleruRentang [R003] Reentrancy generator kebijauan (wates: %zu).\n",
                     opt_.maks_reentrancy);
        galat_.ada = true;
        dorong(Value::mboh());
        return;
    }
    // `kBuangHasil`: nilai balik generator tidak relevan (hasilnya sudah
    // dikumpulkan), jadi jangan sisakan nilai di stack.
    mulai_frame(fn, this_val, args, Frame::kBuangHasil);
    (void)jalankan_loop();
    --reentrancy_;
    tumbles_yield_.pop_back();
    if (galat_.ada) {
        dorong(Value::mboh());
        return;
    }
    if (kumpulan->panjang > kMaksHasil) {
        dorong(buat_kleru("KleruWates",
                         "Generator luwih saka 1048576 hasil. Fase 3 memakai generator mode-eager "
                         "(lihat docs/bytecode.md)."));
        return;
    }
    dorong(Value::obyek(kumpulan));
}

rt::InstanceObj* VM::instans_baru(rt::ClassObj* kls) {
    auto* inst = heap_.alokasi<InstanceObj>();
    inst->h.kind = OK::Instance;
    inst->kelas = kls;
    inst->prototipe = kls->prototipe;
    inst->slot.assign(kls->nama_field.size(), Value::mboh());
    inst->nama_slot = kls->nama_field;
    return inst;
}

void VM::mulai_frame(ClosureObj* fn, Value this_val, std::vector<Value>& args,
                      std::size_t target_balas) {
    if (frames_.size() >= opt_.maks_tumpukan) {
        std::fprintf(stderr, "KleruRentang [R003] Tumpukan luber (wates: %zu frame).\n", opt_.maks_tumpukan);
        galat_.ada = true;
        return;
    }
    const Chunk* kode = fn->fungsi->kode.get();
    Frame f;
    f.closure = fn;
    f.chunk = kode;
    f.ip = 0;
    f.slot_base = stack_.size();
    f.n_argumen = args.size();
    f.this_val = this_val;
    f.target_balas = target_balas;

    const std::size_t n_param = kode->jumlah_param;
    // Slot 0 = `this`; 1..n_param = parameter; sisanya = argumen berlebih.
    stack_.push_back(this_val);
    for (std::size_t i = 0; i < n_param; ++i) {
        stack_.push_back(i < args.size() ? args[i] : Value::mboh());
    }
    if (kode->variadic) {
        ArrayObj* rest = buat_dhaptar();
        for (std::size_t i = n_param; i < args.size(); ++i) rest->dorong(args[i], &heap_);
        stack_.push_back(Value::obyek(rest));
    }
    // Slot temporer tambahan (untuk ekspresi berklaim nilai) sudah dihitung
    // kompilator lewat `jumlah_slot`; sisakan ruang kosong.
    const std::size_t total = std::max<std::size_t>(kode->jumlah_slot, n_param + 2);
    while (stack_.size() < f.slot_base + total) stack_.push_back(Value::mboh());

    frames_.push_back(std::move(f));
}

bool VM::unwind_galat(Value v) {
    while (!frames_.empty()) {
        Frame& cf = frames_.back();
        if (!cf.handlers.empty()) {
            const auto h = cf.handlers.back();
            cf.handlers.pop_back();
            tutup_upvalue_frame(cf.slot_base);
            stack_.resize(h.stack_base < stack_.size() ? h.stack_base : stack_.size());
            cf.ip = h.handler_tangkep;
            dorong(v);
            return true;
        }
        const std::size_t base = cf.slot_base;
        tutup_upvalue_frame(base);
        stack_.resize(base);
        frames_.pop_back();
    }
    galat_.nilai = v;
    galat_.ada = true;
    return false;
}

void VM::tutup_upvalue_frame(std::size_t stack_base) {
    while (!open_upvalues_.empty()) {
        Upvalue* u = open_upvalues_.back();
        if (u == nullptr || u->lokasi == nullptr || static_cast<std::size_t>(u->lokasi - &stack_[0]) < stack_base) {
            break;
        }
        u->close();
        open_upvalues_.pop_back();
    }
}

Upvalue* VM::cari_atau_buat_upvalue(std::size_t stack_index) {
    // `open_upvalues_` diurutkan menurun menurut lokasi stack.
    for (Upvalue* u : open_upvalues_) {
        if (u->lokasi == &stack_[stack_index]) return u;
    }
    auto sel = std::make_unique<Upvalue>();
    sel->lokasi = &stack_[stack_index];
    Upvalue* ptr = sel.get();
    sel_tutup_.push_back(std::move(sel));
    open_upvalues_.push_back(ptr);
    return ptr;
}

}  // namespace jawa::vm
