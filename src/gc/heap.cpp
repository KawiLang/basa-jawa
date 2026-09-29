// Implementasi heap garbage collector.
//
// Strategi: mark-and-sweep presisi, non-moving, stop-the-world.
// - Semua objek dialokasikan satu per satu lewat `::operator new`; tidak ada
//   `new`/`delete` mentah di luar heap (objek dilepas di `sweep()`).
// - Penandaan memakai worklist eksplisit sehingga struktur dalam (mis. linked
//   list panjang) tidak menyebabkan stack overflow.
// - Root datang dari visitor yang didaftarkan (stack VM, frame, upvalue, global,
//   handle native) — lihat `VM::kunjungi_root`.
#include "gc/heap.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "rt/object.h"
#include "rt/value.h"
#include "vm/chunk.h"
// `jawa::vm::Lanjutan` & `jawa::vm::Upvalue` dibutuhkan untuk menandai isi
// continuation generator. Aman di sini (bukan header) karena `gc/heap.h` sudah
// selesai di-include di baris sebelumnya, jadi include keduanya tidakberputar.
#include "vm/vm.h"

namespace jawa::gc {

/// Worklist penandaan. Dipakai `std::vector` karena hanya berisi pointer dan
/// realloc tidak meng-*invalidate* objek yang ditandai (yang penting: objek itu
/// tetap hidup karena ada di `semua_`).
struct Heap::Worklist {
    std::vector<Obj*> item;
    void push(Obj* o) { item.push_back(o); }
    [[nodiscard]] bool kosong() const { return item.empty(); }
    Obj* ambil() {
        Obj* o = item.back();
        item.pop_back();
        return o;
    }
};

namespace {
/// Heap aktif, dipakai `Handle` tanpa scope.
thread_local Heap* g_heap_aktif = nullptr;
}  // namespace

// ---------------------------------------------------------------------------
// Handle & HandleScope
// ---------------------------------------------------------------------------

Handle::Handle(Value v) : v_(v), ptr_(&v_) {}

Handle& Handle::operator=(Handle&& o) noexcept {
    if (this != &o) {
        v_ = o.v_;
        ptr_ = o.ptr_;
        o.ptr_ = nullptr;
    }
    return *this;
}

HandleScope::HandleScope(Heap& h) : heap_(h), basis_(h.handle_arena_.size()) {
    basis_ = h.handle_arena_.size();
    h.buka_scope(this);
}

HandleScope::~HandleScope() { heap_.tutup_scope(this); }

Value* HandleScope::slot(Value v) { return heap_.tambah_handle(v); }

// ---------------------------------------------------------------------------
// Heap — alokasi
// ---------------------------------------------------------------------------

Heap::Heap(bool stress, std::size_t maks_mb)
    : stress_(stress), maks_bytes_(maks_mb * 1024u * 1024u) {
    g_heap_aktif = this;
}

Heap::~Heap() {
    for (Obj* o : semua_) {
        o->~Obj();
        ::operator delete(o);
    }
    semua_.clear();
    if (g_heap_aktif == this) g_heap_aktif = nullptr;
}

void* Heap::raw_allocate(std::size_t bytes) {
    if (bytes == 0) bytes = 1;
    return ::operator new(bytes);
}

void Heap::raw_free(void* mem, std::size_t /*bytes*/) noexcept { ::operator delete(mem); }

void Heap::periksa_batas_memori() const {
    if (maks_bytes_ == 0 || stat_.bytes_aktif <= maks_bytes_) return;
    std::fprintf(stderr, "KleruMemori [R010] Memori melewati batas (watas: %zu MB, terpakai: %zu MB).\n",
                 maks_bytes_ / (1024 * 1024), stat_.bytes_aktif / (1024 * 1024));
    std::fflush(stderr);
    std::_Exit(1);
}

void Heap::daftarkan(Obj* o, std::size_t bytes) {
    semua_.push_back(o);
    // Masukkan karantina: objek ini belum bisa di-mark (belum ada root) dan
    // `daftarkan` bisa memicu koleksi (terutama pada mode stress).
    karantina_.push_back(o);
    while (karantina_.size() > kKarantina) karantina_.pop_front();
    ++stat_.total_alokasi;
    stat_.jumlah_objek = semua_.size();
    stat_.bytes_aktif += bytes;
    if (!semua_.empty()) o->h.aban = static_cast<std::uint16_t>(0);

    periksa_batas_memori();
    if (stress_) {
        koleksi_full();
    } else {
        mungkin_koleksi();
    }
}

// ---------------------------------------------------------------------------
// Heap — roots
// ---------------------------------------------------------------------------

void Heap::tambah_root_visitor(std::function<void(RootVisitor&)> fn) { root_visitor_.push_back(std::move(fn)); }

void Heap::akar(Value v) {
    if (akar_.size() > 4096) return;  // jangan menumpuk tanpa batas
    RootExtra r;
    r.cadangan = v;
    akar_.push_back(r);
}

Value* Heap::tambah_handle(Value v) {
    handle_arena_.emplace_back(v);
    return &handle_arena_.back();
}

void Heap::buka_scope(HandleScope* /*s*/) {}

void Heap::tutup_scope(HandleScope* /*s*/) {}

// ---------------------------------------------------------------------------
// Heap — penandaan
// ---------------------------------------------------------------------------

void Heap::tandai_objek_objek(Value v) {
    if (!v.is_obyek() && !v.is_bigint() && !v.is_simbol()) return;
    const void* p = v.pointer();
    if (p == nullptr) return;
    auto* o = const_cast<Obj*>(static_cast<const Obj*>(p));
    if (o->h.mark == 0) tandai_objek(o);
}

void Heap::tandai_objek(Obj* o) {
    if (o == nullptr || o->h.mark != 0) return;
    Worklist wl;
    o->h.mark = 1;
    wl.push(o);
    while (!wl.kosong()) {
        Obj* cur = wl.ambil();
        tandai_anak(cur, wl);
    }
}

void Heap::tandai_anak(Obj* o, Worklist& wl) {
    auto mark = [&wl](Value v) {
        if (!v.is_obyek() && !v.is_bigint() && !v.is_simbol()) return;
        const void* p = v.pointer();
        if (p == nullptr) return;
        auto* child = const_cast<Obj*>(static_cast<const Obj*>(p));
        if (child->h.mark == 0) {
            child->h.mark = 1;
            wl.push(child);
        }
    };
    auto mark_obj = [&wl](Obj* child) {
        if (child != nullptr && child->h.mark == 0) {
            child->h.mark = 1;
            wl.push(child);
        }
    };

    switch (o->h.kind) {
        case rt::OK::Obyek: {
            auto* ob = static_cast<rt::ObyekObj*>(o);
            mark(ob->prototipe);
            for (std::size_t i = 0; i < ob->jumlah_slot; ++i) mark(ob->slot[i]);
            for (const auto& kv : ob->dict) {
                mark(kv.first);
                mark(kv.second);
            }
            break;
        }
        case rt::OK::Array: {
            auto* ab = static_cast<rt::ArrayObj*>(o);
            mark(ab->prototipe);
            for (std::size_t i = 0; i < ab->panjang; ++i) mark(ab->elemen[i]);
            if (ab->ekstra != nullptr) {
                auto* ek = static_cast<rt::ObyekObj*>(ab->ekstra);
                for (std::size_t i = 0; i < ek->jumlah_slot; ++i) mark(ek->slot[i]);
            }
            break;
        }
        case rt::OK::Teks:
        case rt::OK::Regex:
        case rt::OK::Tanggal:
        case rt::OK::Simbol:
        case rt::OK::BigInt:
            break;
        case rt::OK::Generator: {
            // Generator memegang closure-nya dan continuation yang disuspensi.
            // Nilai di stack continuation sudah disalin ke sana, tapi upvalue
            // yang di-frame masih hidup dan harus ditandai.
            auto* g = static_cast<rt::GeneratorObj*>(o);
            mark(g->nilai);
            mark(g->nilai_bali);
            mark(g->galat);
            mark(g->kirim);
            mark(g->fungsi);
            if (g->lanjutan != nullptr) {
                for (const Value& v : g->lanjutan->stack) mark(v);
                for (const auto& f : g->lanjutan->frame) {
                    mark(f.this_val);
                    mark(f.janji_async);
                    if (f.closure == nullptr) continue;
                    for (void* pv : f.closure->upvalue) {
                        auto* cell = static_cast<vm::Upvalue*>(pv);
                        if (cell != nullptr) mark(cell->get());
                    }
                }
            }
            break;
        }
        case rt::OK::Fungsi: {
            auto* f = static_cast<rt::FungsiObj*>(o);
            mark(f->prototipe);
            mark_obj(f->home_object);
            for (rt::FungsiObj* u : f->upvalue_def) mark_obj(u);
            // Pool konstanta & nama properti adalah root via objek fungsi ini.
            if (f->kode) {
                for (const rt::Value& v : f->kode->konstanta) mark(v);
                for (const rt::Value& v : f->kode->nama_properti) mark(v);
            }
            for (const auto& anak : f->kode ? f->kode->anak : std::vector<vm::ChunkPtr>{}) {
                (void)anak;  // chunk anak dimiliki shared_ptr; tidak perlu ditandai
            }
            break;
        }
        case rt::OK::Closure: {
            // Upvalue ditangani visitor VM (butuh `vm::Upvalue` yang tidak
            // terlihat dari lapisan gc), tapi nilainya bisa ditandai di sini
            // lewat sel closure: `upvalue` menyimpan `void*` ke `Upvalue`.
            auto* c = static_cast<rt::ClosureObj*>(o);
            mark(c->prototipe);
            mark_obj(c->fungsi);
            break;
        }
        case rt::OK::Native: {
            mark(static_cast<rt::NativeFnObj*>(o)->prototipe);
            break;
        }
        case rt::OK::Golongan: {
            auto* c = static_cast<rt::ClassObj*>(o);
            mark(c->prototipe);
            mark(c->induk);
            mark(c->konstruktor);
            mark(c->wiwit_pabrik);
            mark(c->inisial_field);
            mark(c->privat);
            for (const Value& v : c->field_statis) mark(v);
            for (const Value& v : c->nilai_statis) mark(v);
            break;
        }
        case rt::OK::Instance: {
            auto* i2 = static_cast<rt::InstanceObj*>(o);
            mark(i2->prototipe);
            for (const Value& v : i2->slot) mark(v);
            mark_obj(i2->kelas);
            break;
        }
        case rt::OK::Janji: {
            auto* j = static_cast<rt::JanjiObj*>(o);
            mark(j->hasil);
            for (const auto& t : j->then_daftar) {
                mark(t.on_slamet);
                mark(t.on_tolak);
                mark(t.asli);
            }
            for (const Value& v : j->tangkap_daftar) mark(v);
            for (const Value& v : j->intriguasan_daftar) mark(v);
            break;
        }
        case rt::OK::Peta: {
            auto* p = static_cast<rt::PetaObj*>(o);
            mark(p->prototipe);
            for (const auto& e : p->entri) {
                mark(e.kunci);
                mark(e.nilai);
            }
            break;
        }
        case rt::OK::Himpunan: {
            auto* hs = static_cast<rt::HimpunanObj*>(o);
            mark(hs->prototipe);
            for (const auto& e : hs->isi.entri) {
                mark(e.kunci);
                mark(e.nilai);
            }
            break;
        }
        case rt::OK::Kleru: {
            mark(static_cast<rt::KleruObj*>(o)->sebab);
            break;
        }
        case rt::OK::BoundFn: {
            auto* b = static_cast<rt::BoundFnObj*>(o);
            mark(b->target);
            mark(b->this_val);
            for (const Value& v : b->bound) mark(v);
            break;
        }
        default: break;
    }
}

void Heap::tandai_roots() {
    // Handle native (dijaga RAII oleh HandleScope).
    for (const Value& v : handle_arena_) tandai_objek_objek(v);
    // Visitor dari VM / modul. `visit` memakai `tandai_objek_objek`, jadi
    // `rooted(v)` langsung menandai objek yang dirujuk nilai `v`.
    RootVisitor rv{};
    rv.heap = this;
    rv.visit = [this](Value v) { tandai_objek_objek(v); };
    for (auto& fn : root_visitor_) fn(rv);
    // Root value sementara.
    for (const RootExtra& r : akar_) tandai_objek_objek(r.cadangan);
    // Akar ber-scope (lihat `Heap::akar_scope_push`).
    for (const Value& v : akar_scope_) tandai_objek_objek(v);
    // Akar sementara kompilator (D-018). Tanpa baris ini, konstanta &
    // nama-properti yang baru dibuat `Compiler::tambah_konstanta` /
    // `tambah_nama` TIDAK punya akar selama masih dikompilasi -- `chunk_akar_`
    // baru berlaku setelah `compile()` selesai, jadi `--gc-stress` (yang
    // mengoleksi tiap alokasi) akan membebaskannya di tengah kompilasi.
    for (const Value& v : akar_sementara_) tandai_objek_objek(v);
}

// ---------------------------------------------------------------------------
// Heap — sweep
// ---------------------------------------------------------------------------

void Heap::sweep() {
    std::size_t w = 0;
    for (std::size_t i = 0; i < semua_.size(); ++i) {
        Obj* o = semua_[i];
        bool karantina = false;
        for (Obj* k : karantina_) {
            if (k == o) {
                karantina = true;
                break;
            }
        }
        if (karantina) {
            o->h.mark = 0;
            semua_[w++] = o;
        } else if (o->h.mark != 0) {
            o->h.mark = 0;
            semua_[w++] = o;
        } else {
            o->~Obj();
            ::operator delete(o);
        }
    }
    semua_.resize(w);
    stat_.jumlah_objek = w;
}

void Heap::mungkin_koleksi() {
    if (stress_) {
        koleksi_full();
        return;
    }
    if (stat_.bytes_aktif < ambang_bytes()) return;
    koleksi_full();
}

void Heap::koleksi_full() {
    const auto t0 = std::chrono::steady_clock::now();
    ++stat_.gc_koleksi;
    tandai_roots();
    sweep();
    hitung_ambang();
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    stat_.ms_koleksi += ms;
    if (log_gc_) {
        std::fprintf(stderr, "[gc] koleksi #%zu: %zu objek, %.3f ms\n", stat_.gc_koleksi, stat_.jumlah_objek, ms);
    }
}

void Heap::hitung_ambang() {
    // Ambang adaptif: 1.5x bytes hidup, minimal 256 KB, maksimal 32 MB.
    std::size_t baru = stat_.bytes_aktif * 3 / 2;
    if (baru < 256u * 1024u) baru = 256u * 1024u;
    if (baru > 32u * 1024u * 1024u) baru = 32u * 1024u * 1024u;
    amb_kb_ = baru / 1024u;
}

}  // namespace jawa::gc
