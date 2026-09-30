// VM: akses properti (dengan rantai prototipe) & helper indeks.
#include "vm/vm.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "rt/number.h"
#include "rt/object.h"
#include "rt/object.h"
#include "rt/string.h"
#include "stdlib/stdlib.h"

namespace jawa::vm {

using rt::ArrayObj;
using rt::ClassObj;
using rt::InstanceObj;
using rt::ObyekObj;
using rt::Obj;
using rt::OK;
using rt::SelObj;
using rt::TeksObj;
using rt::Value;

namespace {

Obj* objek(Value v) {
    if (!v.is_obyek() && !v.is_bigint() && !v.is_simbol()) return nullptr;
    const void* p = v.pointer();
    return p == nullptr ? nullptr : const_cast<Obj*>(static_cast<const Obj*>(p));
}

TeksObj* sb(Value v) {
    Obj* o = objek(v);
    return (o != nullptr && o->h.kind == OK::Teks) ? static_cast<TeksObj*>(o) : nullptr;
}

std::string_view sv(Value v) {
    TeksObj* t = sb(v);
    return t == nullptr ? std::string_view() : std::string_view(t->str());
}

}  // namespace

// ---------------------------------------------------------------------------
// Rantai prototipe: `ambil_properti`
// ---------------------------------------------------------------------------

Value VM::ambil_properti(Obj* o, Value kunci) {
    // Objek ekspor modul menyimpan `SelObj` (lihat `docs/modules.md`): impor
    // mengikat sel supaya bisa live. Impor namespace mengikat objek ekspor itu
    // langsung, jadi di sini sel harus dibongkar supaya `U.tambah` dibaca
    // sebagai Closure, bukan `SelObj`. Tanpa ini `U.tambah(1, 2)` gagal dengan
    // "iki dudu fungsi".
    if (o != nullptr && o->h.kind == OK::Obyek) {
        for (const auto& rec : modul_store_) {
            if (!rec.ekspor.is_obyek() || rec.ekspor.pointer() != o) continue;
            if (Value v; static_cast<ObyekObj*>(o)->get(kunci, v)) {
                if (v.is_obyek() && v.pointer() != nullptr &&
                    static_cast<const Obj*>(v.pointer())->h.kind == OK::Sel) {
                    return static_cast<SelObj*>(v.mutable_pointer())->baca();
                }
            }
            break;
        }
    }
    for (Obj* cur = o; cur != nullptr;) {
        switch (cur->h.kind) {
            case OK::Obyek: {
                auto* ob = static_cast<ObyekObj*>(cur);
                // Properti data.
                if (ob->shape != nullptr) {
                    if (const rt::Properti* p = ob->shape->find(kunci)) {
                        if ((p->attr & rt::AttrAccessor) == 0 && p->index >= 0 && ob->slot != nullptr) {
                            return ob->slot[p->index];
                        }
                    }
                }
                for (const auto& kv : ob->dict) {
                    if (rt::nilai_sama(kv.first, kunci)) return kv.second;
                }
                // Method native (Teks/Dhaptar/etc).
                if (kunci.is_obyek()) {
                    const std::string_view nama = sv(kunci);
                    Value f = stdlib::cari_metode_builtin(*this, nama, static_cast<std::uint8_t>(cur->h.kind));
                    if (f.is_obyek()) return f;
                }
                cur = objek(ob->prototipe);
                continue;
            }
            case OK::Array: {
                auto* a = static_cast<ArrayObj*>(cur);
                std::size_t idx = 0;
                {
                    const std::string_view k = sv(kunci);
                    if (k == "dawa" || k == "panjang" || k == "length") {
                        return Value::number(static_cast<double>(a->panjang));
                    }
                }
                if (kunci.is_angka()) {
                    const double d = kunci.as_number();
                    idx = d < 0 ? 0 : static_cast<std::size_t>(d);
                    if (static_cast<double>(idx) != d) return Value::mboh();
                } else {
                    const std::string_view k = sv(kunci);
                    if (!rt::indeks_bulat(k, idx)) {
                        const std::string_view nama = k;
                        Value f = stdlib::cari_metode_builtin(*this, nama, static_cast<std::uint8_t>(OK::Array));
                        if (f.is_obyek()) return f;
                        cur = objek(a->prototipe);
                        continue;
                    }
                }
                if (idx < a->panjang) return a->elemen[idx];
                return Value::mboh();
            }
            case OK::Teks: {
                // Method bawaan Teks (dawa, huruf_gedhe, ganti, ...).
                const std::string_view nama = sv(kunci);
                const Value f = stdlib::cari_metode_builtin(*this, nama, static_cast<std::uint8_t>(OK::Teks));
                if (f.is_obyek()) return f;
                // Indeks numerik pada teks memberi satu karakter.
                std::size_t idx = 0;
                if (kunci.is_angka()) {
                    const double d = kunci.as_number();
                    idx = d < 0 ? 0 : static_cast<std::size_t>(d);
                    if (static_cast<double>(idx) != d) return Value::mboh();
                } else if (rt::indeks_bulat(nama, idx)) {
                    const auto* t = static_cast<const rt::TeksObj*>(cur);
                    const std::string_view s = t->str();
                    if (idx >= rt::panjang_code_point(s)) return Value::mboh();
                    return Value::obyek(rt::buat_teks(heap_, rt::potong_code_point(s, idx, idx + 1)));
                }
                return Value::mboh();
            }
            case OK::Instance: {
                auto* inst = static_cast<InstanceObj*>(cur);
                const std::string_view nama = sv(kunci);
                // Field instance.
                for (std::size_t i = 0; i < inst->nama_slot.size(); ++i) {
                    if (inst->nama_slot[i] == nama) return inst->slot[i];
                }
                // Field & method statis pada class.
                if (inst->kelas != nullptr) {
                    for (std::size_t i = 0; i < inst->kelas->nama_statis.size(); ++i) {
                        if (inst->kelas->nama_statis[i] == nama) return inst->kelas->nilai_statis[i];
                    }
                    // Method di prototipe class.
                    cur = objek(inst->kelas->prototipe);
                    continue;
                }
                cur = objek(inst->prototipe);
                continue;
            }
            // Objek regex & tanggal: method built-in-nya dipasang lewat tabel
            // yang sama dengan Dhaptar/Teks, jadi hanya perlu dicari.
            case OK::Peta: {
                // Kunci langsung: `peta["a"]`. Selain itu method bawaan
                // (`dhawa`, `get`, `set`, `hapus`, ...) dilayani
                // `cari_metode_builtin` supaya `Object.keys` tetap bersih.
                auto* p = static_cast<rt::PetaObj*>(cur);
                if (const std::string_view n = sv(kunci); n == "dhawa" || n == "panjang" ||
                                                n == "dawa" || n == "length") {
                    return Value::number(static_cast<double>(p->jumlah_aktif));
                }
                if (rt::PetaObj::Entri* e = p->cari(kunci)) return e->nilai;
                const std::string_view nama = sv(kunci);
                const Value f = stdlib::cari_metode_builtin(*this, nama, static_cast<std::uint8_t>(OK::Peta));
                if (f.is_obyek()) return f;
                cur = objek(p->prototipe);
                continue;
            }
            case OK::Himpunan: {
                auto* h = static_cast<rt::HimpunanObj*>(cur);
                const std::string_view nama = sv(kunci);
                if (nama == "dhapa") return Value::number(static_cast<double>(h->isi.jumlah_aktif));
                const Value f = stdlib::cari_metode_builtin(*this, nama, static_cast<std::uint8_t>(OK::Himpunan));
                if (f.is_obyek()) return f;
                cur = objek(h->prototipe);
                continue;
            }
            case OK::Berkas: {
                auto* b = static_cast<rt::BerkasObj*>(cur);
                const std::string_view nama = sv(kunci);
                // Properti baca-saja; semuanya mencerminkan keadaan handle.
                if (nama == "nama") return Value::obyek(rt::buat_teks(heap_, b->nama));
                if (nama == "baris") return b->baris;
                if (nama == "akhir") return Value::boolean(b->akhir);
                if (nama == "ditutup") return Value::boolean(b->ditutup);
                if (nama == "bisa_baca") return Value::boolean(b->bisa_baca());
                if (nama == "bisa_tulis") return Value::boolean(b->bisa_tulis());
                if (nama == "mode") {
                    const char* m = b->mode == rt::BerkasObj::Mode::Baca      ? "baca"
                                    : b->mode == rt::BerkasObj::Mode::Tulis  ? "tulis"
                                                                           : "tambah";
                    return Value::obyek(rt::buat_teks(heap_, m));
                }
                const Value f = stdlib::cari_metode_builtin(*this, nama, static_cast<std::uint8_t>(OK::Berkas));
                if (f.is_obyek()) return f;
                return Value::mboh();
            }
            case OK::Regex:
            case OK::Tanggal: {
                if (kunci.is_obyek()) {
                    const Value f = stdlib::cari_metode_builtin(*this, sv(kunci),
                                                                  static_cast<std::uint8_t>(cur->h.kind));
                    if (f.is_obyek()) return f;
                }
                return Value::mboh();
            }
            case OK::Janji: {
                // Method Janji: `then` / `tangkep` (lihat `stdlib::method_janji`).
                const std::string_view nama_j = sv(kunci);
                const auto& tabel_j = stdlib::method_janji(*this);
                const auto cari_j = tabel_j.find(std::string(nama_j));
                if (cari_j != tabel_j.end()) {
                    auto* n = heap_.alokasi<rt::NativeFnObj>();
                    n->h.kind = OK::Native;
                    n->fn = cari_j->second;
                    n->nama = singsan(nama_j);
                    n->jumlah_param = 1;
                    return Value::obyek(n);
                }
                auto* j = static_cast<rt::JanjiObj*>(cur);
                if (nama_j == "jenis") {
                    const char* n = j->status == rt::JanjiStatus::Slamet  ? "slamet"
                                  : j->status == rt::JanjiStatus::Gagal ? "gagal"
                                                                      : "nunggu";
                    return Value::obyek(rt::buat_teks(heap_, n));
                }
                if (nama_j == "hasil") return j->hasil;
                return Value::mboh();
            }
            case OK::Generator: {
                // Properti & method generator (lihat `vm/vm_gen.cpp`).
                const std::string_view nama_g = sv(kunci);
                auto* g = static_cast<rt::GeneratorObj*>(cur);
                if (nama_g == "next") {
                    auto* n = heap_.alokasi<rt::NativeFnObj>();
                    n->h.kind = OK::Native;
                    n->fn = [](VM& v, Value this_val, std::vector<Value>& args) -> Value {
                        auto* gen = this_val.is_obyek()
                                        ? static_cast<rt::GeneratorObj*>(this_val.mutable_pointer())
                                        : nullptr;
                        if (gen == nullptr || gen->h.kind != OK::Generator) {
                            return v.buat_kleru("KleruJinis", "Ora bisa nelep `next`: iki dudu generator.");
                        }
                        const bool ada_argumen = !args.empty();
                        ObyekObj* langkah =
                            v.langkah_generator(gen, ada_argumen ? args[0] : Value::mboh(), ada_argumen);
                        if (langkah == nullptr) {
                            // Galat di body generator dilempar ke pemanggil `next`.
                            return v.galat_.nilai;
                        }
                        return Value::obyek(langkah);
                    };
                    n->nama = singsan(nama_g);
                    n->jumlah_param = 0;
                    return Value::obyek(n);
                }
                if (nama_g == "jenis") {
                    const char* n = g->status == rt::GeneratorStatus::Selesai ? "selesai"
                                  : g->status == rt::GeneratorStatus::Gagal   ? "galat"
                                                                           : "jalan";
                    return Value::obyek(rt::buat_teks(heap_, n));
                }
                if (nama_g == "selesai") {
                    return Value::boolean(g->status == rt::GeneratorStatus::Selesai);
                }
                // Nilai `metokake` terakhir.
                if (nama_g == "nilai") return g->nilai;
                // Nilai balik `bali` (bukan hasil `metokake`).
                if (nama_g == "bali") return g->nilai_bali;
                return Value::mboh();
            }
            case OK::Kleru: {
                auto* k = static_cast<const rt::KleruObj*>(cur);
                const std::string_view nama = sv(kunci);
                if (nama == "jeneng" || nama == "nama") return Value::obyek(rt::buat_teks(heap_, k->jeneng));
                if (nama == "pesan") return Value::obyek(rt::buat_teks(heap_, k->pesan));
                if (nama == "sebab") return k->sebab;
                if (nama == "jejak") {
                    ArrayObj* jejak = buat_dhaptar(k->jejak.size());
                    for (std::size_t i = 0; i < k->jejak.size(); ++i) {
                        jejak->dorong(Value::obyek(rt::buat_teks(heap_, k->jejak[i])), &heap_);
                    }
                    return Value::obyek(jejak);
                }
                if (nama == "baris") return Value::number(static_cast<double>(k->pos.baris));
                if (nama == "kolom") return Value::number(static_cast<double>(k->pos.kolom));
                if (nama == "berkas") return Value::obyek(rt::buat_teks(heap_, k->berkas));
                cur = nullptr;
                continue;
            }
            case OK::Golongan: {
                auto* kls = static_cast<ClassObj*>(cur);
                const std::string_view nama = sv(kunci);
                for (std::size_t i = 0; i < kls->nama_statis.size(); ++i) {
                    if (kls->nama_statis[i] == nama) return kls->nilai_statis[i];
                }
                cur = objek(kls->prototipe);
                continue;
            }
            case OK::Fungsi: {
                // `f.nama`, `f.dawa` (jumlah parameter).
                auto* f = static_cast<rt::FungsiObj*>(cur);
                const std::string_view nama = sv(kunci);
                if (nama == "nama") return Value::obyek(rt::buat_teks(heap_, f->nama));
                if (nama == "dawa") return Value::number(static_cast<double>(f->kode->jumlah_param));
                cur = objek(f->prototipe);
                continue;
            }
            case OK::Closure: {
                auto* c = static_cast<rt::ClosureObj*>(cur);
                cur = objek(c->prototipe);
                continue;
            }
            case OK::Native: {
                auto* n = static_cast<rt::NativeFnObj*>(cur);
                const std::string_view nama = sv(kunci);
                if (nama == "nama") return Value::obyek(rt::buat_teks(heap_, n->nama));
                if (nama == "dawa") return Value::number(static_cast<double>(n->jumlah_param));
                // Properti yang ditempel pada fungsi native-nya sendiri --
                // method statis konstruktor (`Tanggal.dari`, `Tanggal.sekarang`).
                for (const auto& kv : n->sifat) {
                    if (rt::nilai_sama(kv.first, kunci)) return kv.second;
                }
                cur = objek(n->prototipe);
                continue;
            }
            default: return Value::mboh();
        }
    }
    return Value::mboh();
}

void VM::dorong_index_helper(Obj* o, Value kunci) { dorong(ambil_properti(o, kunci)); }

bool VM::cari_getter(Value obj, Value kunci, Value& keluar) {
    // Cari getter pada rantai prototipe. Dipisah dari `ambil_properti` karena
    // getter harus DIJALANKAN: `panggil_objek` untuk closure hanya mendorong
    // frame, lalu dieksekusi pada iterasi loop berikutnya.
    for (Obj* cur = objek(obj); cur != nullptr;) {
        if (cur->h.kind == OK::Obyek) {
            auto* ob = static_cast<ObyekObj*>(cur);
            if (ObyekObj::Aksesor* ak = ob->cari_aksesor(kunci); ak != nullptr && ak->getter.is_obyek()) {
                keluar = ak->getter;
                return true;
            }
            cur = objek(ob->prototipe);
            continue;
        }
        if (cur->h.kind == OK::Instance) {
            auto* inst = static_cast<InstanceObj*>(cur);
            cur = inst->kelas != nullptr ? objek(inst->kelas->prototipe) : objek(inst->prototipe);
            continue;
        }
        if (cur->h.kind == OK::Array) {
            cur = objek(static_cast<ArrayObj*>(cur)->prototipe);
            continue;
        }
        return false;
    }
    return false;
}

void VM::dorong_index(Value obj, Value kunci) {
    Obj* o = objek(obj);
    if (o == nullptr) {
        dorong(Value::mboh());
        return;
    }
    dorong_index_helper(o, kunci);
}

void VM::set_index_value(Value obj, Value kunci, Value nilai) {
    Obj* o = objek(obj);
    if (o == nullptr) return;
    if (o->h.kind == OK::Array) {
        auto* a = static_cast<ArrayObj*>(o);
        std::size_t idx = 0;
        if (kunci.is_angka()) {
            const double d = kunci.as_number();
            if (d < 0) return;
            a->set(static_cast<std::size_t>(d), nilai);
        } else {
            const std::string_view k = sv(kunci);
            if (rt::indeks_bulat(k, idx)) a->set(idx, nilai);
        }
    } else if (o->h.kind == OK::Obyek) {
        auto* ob = static_cast<ObyekObj*>(o);
        if (ObyekObj::Aksesor* ak = ob->cari_aksesor(kunci); ak != nullptr && ak->setter.is_obyek()) {
            std::vector<Value> satu{nilai};
            panggil_objek(ak->setter, obj, satu);
            return;
        }
        ob->set(heap_, kunci, nilai);
    } else if (o->h.kind == OK::Peta) {
        static_cast<rt::PetaObj*>(o)->pasang(kunci, nilai);
    } else if (o->h.kind == OK::Instance) {
        // Setter pada prototipe class (Accessor didefinisikan di prototipe).
        if (auto* inst = static_cast<InstanceObj*>(o); inst->kelas != nullptr) {
            if (Obj* proto = objek(inst->kelas->prototipe); proto != nullptr && proto->h.kind == OK::Obyek) {
                auto* pob = static_cast<ObyekObj*>(proto);
                if (ObyekObj::Aksesor* ak = pob->cari_aksesor(kunci); ak != nullptr && ak->setter.is_obyek()) {
                    std::vector<Value> satu{nilai};
                    panggil_objek(ak->setter, obj, satu);
                    return;
                }
            }
        }
        auto* inst = static_cast<InstanceObj*>(o);
        const std::string_view nama = sv(kunci);
        if (std::getenv("JAWA_DBG") != nullptr) {
            std::fprintf(stderr, "[T] set-instance %s slot=%zu\n", std::string(nama).c_str(),
                         inst->nama_slot.size());
        }
        for (std::size_t i = 0; i < inst->nama_slot.size(); ++i) {
            if (inst->nama_slot[i] == nama) {
                inst->slot[i] = nilai;
                return;
            }
        }
        // Field baru: tambahkan slot secara dinamis (field tanpa deklarasi
        // eksplisit, mis. `iki.x = 1`).
        inst->nama_slot.emplace_back(nama);
        inst->slot.push_back(nilai);
    } else if (o->h.kind == OK::Golongan) {
        // Penugasan statis (`Kelas.x = v`): perbarui atau tambah entri statis.
        auto* kls = static_cast<ClassObj*>(o);
        const std::string_view nama = sv(kunci);
        for (std::size_t i = 0; i < kls->nama_statis.size(); ++i) {
            if (kls->nama_statis[i] == nama) {
                kls->nilai_statis[i] = nilai;
                return;
            }
        }
        kls->nama_statis.emplace_back(nama);
        kls->nilai_statis.push_back(nilai);
    }
}

}  // namespace jawa::vm
