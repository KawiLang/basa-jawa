// VM: loop eksekusi bytecode.
//
// Dispatch memakai `switch` (portabel). Bentuk `computed goto` tersedia lewat
// JAWA_COMPUTED_GOTO pada GCC/Clang; lihat docs/bytecode.md.
// PEMBATASAN: sementara ini `CALL` memakai reentrancy (panggil loop baru),
// sehingga pemanggilan JS->JS masih menambah frame C++. Batas kedalaman
// dijaga `maks_tumpukan` sehingga rekursi tak hingga menghasilkan galat, bukan
// crash native. Plans: Fiber penuh (Fase 7).
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "rt/number.h"
#include "rt/object.h"
#include "rt/string.h"
#include "vm/vm.h"

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
using rt::Value;

namespace {

Obj* objek(Value v) {
    if (!v.is_obyek() && !v.is_bigint() && !v.is_simbol()) return nullptr;
    const void* p = v.pointer();
    return p == nullptr ? nullptr : const_cast<Obj*>(static_cast<const Obj*>(p));
}

TeksObj* teks(Value v) {
    Obj* o = objek(v);
    if (o == nullptr || o->h.kind != OK::Teks) return nullptr;
    return static_cast<TeksObj*>(o);
}

std::string_view sv(Value v) {
    TeksObj* t = teks(v);
    return t == nullptr ? std::string_view() : std::string_view(t->str());
}

/// Emit galat & kembalikan Status::Galat.
///
/// Untuk galat INTERNAL (instruksi rusak, langkah maksimum) yang langsung
/// menghentikan program. Galat yang LEVEL bahasa harus lewat `galat_tertangkap`
/// supaya bisa ditangkap `coba`/`tangkep`.
Status gagal(VM& vm, const char* kode, const char* pesan) {
    std::fprintf(stderr, "KleruRuntime [%s] %s\n", kode, pesan);
    std::fputs(vm.jejak_stack().c_str(), stderr);
    vm.galat_.ada = true;
    return Status::Galat;
}

/// Galat level bahasa (mis. `1 + "a"`): bentuknya objek `KleruObj` sehingga
/// bisa ditangkap `coba`/`tangkep` dan dibaca `.jeneng`/`.pesan`.
///
/// Mengembalikan true kalau ada handler `coba` yang menangkap -- pemanggil
/// harus `break` (bukan `return`) supaya loop bytecode lanjut di ip handler.
/// Kalau false, tidak ada handler: pemanggil mencetak pesan lalu mengembalikan
/// `Status::Galat`.
bool lempar_dan_tangkap(VM& vm, const char* jeneng, const char* pesan) {
    return vm.unwind_galat(vm.buat_kleru(jeneng, pesan));
}

}  // namespace

// ===========================================================================

Status VM::jalankan_loop(const std::size_t kedalaman_awal) {
    // Reentrancy (native -> JS) menjalankan loop bersarang. Loop ini berhenti
    // begitu kedalaman frame turun kembali ke `kedalaman_awal`. Resume rantai
    // `async` memakai `0`: berjalan sampai `frames_` benar-benar kosong.
    for (;;) {
        if (frames_.size() < kedalaman_awal) return Status::Selesai;
        if (frames_.empty()) return Status::Selesai;
        Frame& f = frames_.back();
        const Chunk* c = f.chunk;
        if (f.ip >= c->kode.size()) {
            // Jatuh dari akhir chunk -> return undefined.
            const std::size_t base = f.slot_base;
            tutup_upvalue_frame(base);
            stack_.resize(base);
            frames_.pop_back();
            if (frames_.size() < kedalaman_awal || frames_.empty()) {
                dorong(Value::mboh());
                return Status::Selesai;
            }
            dorong(Value::mboh());
            continue;
        }

        if (opt_.maks_langkah != 0 && ++langkah_ > opt_.maks_langkah) {
            return gagal(*this, "R011", "Jumlah langkah maksimum terlampaui.");
        }

        const Instruksi& ins = c->kode[f.ip];
        // `NOP_LINE` menandai pergantian baris sumber: operand `a`-nya adalah
        // baris baru. Instruksi lain tidak membawa baris (selalu 0), jadi
        // `pos_sumber_` hanya berubah di titik ini.
        //
        // CATATAN: `NOP_LINE` tidak di-skip di bawah advance `f.ip` -- kalau
        // di-skip, `pos_sumber_` tidak pernah berubah dan semua pesan galat
        // menunjuk baris 1. Ini yang membuat nomor baris pada pesan galat
        // runtime & laporan `jawa tes` selalu benar.
        if (ins.op == Op::NOP_LINE) {
            f.baris = static_cast<int>(ins.a);
            pos_sumber_.baris = ins.a;
            pos_sumber_.offset = static_cast<std::uint32_t>(f.ip);
        }
        f.ip++;

        switch (ins.op) {
            case Op::NOP: break;

            case Op::KONSTAN: dorong(c->konstanta[ins.a]); break;
            case Op::NOMOR: dorong(c->nama_properti[ins.a]); break;
            case Op::TEKS: dorong(c->konstanta[ins.a]); break;
            case Op::MBOH: case Op::UNDEF: case Op::NIL: dorong(Value::mboh()); break;
            case Op::KOSONG: dorong(Value::kosong()); break;
            case Op::BENER: dorong(Value::boolean(true)); break;
            case Op::SALAH: dorong(Value::boolean(false)); break;

            case Op::GET_LOCAL: dorong(stack_[f.slot_base + ins.a]); break;
            case Op::SET_LOCAL: stack_[f.slot_base + ins.a] = ambil(); break;
            case Op::DEF_LOCAL: {
                const Value v = ambil();
                stack_[f.slot_base + ins.a] = v;
                break;
            }
            case Op::GET_GLOBAL: {
                const std::string_view nama = sv(c->nama_properti[ins.a]);
                dorong(ambil_global(nama));
                break;
            }
            case Op::SET_GLOBAL: {
                const Value v = ambil();
                const std::string_view nama = sv(c->nama_properti[ins.a]);
                set_global(nama, v);
                dorong(v);
                break;
            }
            // Sel upvalue null berarti nama tersebut ternyata global (resolusi
            // saat closure dibuat tidak menemukan lokal/upvalue nenek moyang).
            case Op::GET_UPVAL: {
                Value v = Value::mboh();
                bool global = true;
                if (f.closure != nullptr && ins.a < f.closure->upvalue.size()) {
                    auto* cell = static_cast<Upvalue*>(f.closure->upvalue[ins.a]);
                    if (cell != nullptr) {
                        v = cell->get();
                        global = false;
                    }
                }
                if (global && ins.a < c->upvalue.size()) {
                    const std::string_view nama = c->upvalue[ins.a];
                    v = ambil_global(nama);
                }
                dorong(v);
                break;
            }
            case Op::SET_UPVAL: {
                const Value v = ambil();
                bool global = true;
                if (f.closure != nullptr && ins.a < f.closure->upvalue.size()) {
                    auto* cell = static_cast<Upvalue*>(f.closure->upvalue[ins.a]);
                    if (cell != nullptr) {
                        cell->set(v);
                        global = false;
                    }
                }
                if (global && ins.a < c->upvalue.size()) {
                    set_global(c->upvalue[ins.a], v);
                }
                dorong(v);
                break;
            }
            case Op::POP: (void)ambil(); break;
            case Op::DUP: dorong(puncak()); break;
            case Op::SWAP: std::swap(puncak(), puncak(1)); break;

            // ---------------------------------------------------------- aritmetika
            case Op::ADD: {
                const Value b = ambil();
                const Value a = ambil();
                if (a.is_angka() && b.is_angka()) {
                    dorong(Value::number(a.as_number() + b.as_number()));
                } else {
                    // Tanpa koersi implisit: teks + teks tetap teks.
                    TeksObj* ta = teks(a);
                    TeksObj* tb = teks(b);
                    if (ta != nullptr && tb != nullptr) {
                        dorong(Value::obyek(TeksObj::gabung(heap_, ta->str(), tb->str())));
                    } else {
                        // `break` (bukan `return`) supaya loop lanjut di ip
                        // handler. Kalau tidak ada handler, `unwind_galat` sudah
                        // mengisi `galat_`; pencetakan pesannya dilakukan
                        // `VM::jalankan_sumber` supaya tidak dobel.
                        if (lempar_dan_tangkap(*this, "KleruJenis",
                                               "Ora bisa nambahake nilai iki (tanpa koersi).")) {
                            break;
                        }
                        return Status::Galat;
                    }
                }
                break;
            }
            case Op::SUB: case Op::MUL: case Op::DIV: case Op::MOD: case Op::POW: {
                const Value b = ambil();
                const Value a = ambil();
                if (!a.is_angka() || !b.is_angka()) {
                    if (lempar_dan_tangkap(*this, "KleruJenis",
                                           "Operator aritmetika mung bisa kanggo angka.")) {
                        break;
                    }
                    return Status::Galat;
                }
                const double x = a.as_number();
                const double y = b.as_number();
                double hasil = 0.0;
                switch (ins.op) {
                    case Op::SUB: hasil = x - y; break;
                    case Op::MUL: hasil = x * y; break;
                    case Op::DIV: hasil = x / y; break;
                    case Op::MOD: hasil = std::fmod(x, y); break;
                    default: hasil = std::pow(x, y); break;
                }
                if (rt::exactly_int32(hasil) && ins.op != Op::DIV) dorong(Value::angka_int32(static_cast<std::int32_t>(hasil)));
                else dorong(Value::number(hasil));
                break;
            }
            case Op::NEG: {
                const Value a = ambil();
                dorong(Value::number(-a.as_number()));
                break;
            }
            case Op::INC: case Op::DEC: {
                const Value a = ambil();
                const double n = a.as_number() + (ins.op == Op::INC ? 1.0 : -1.0);
                dorong(Value::number(n));
                break;
            }
            case Op::BIT_AND: case Op::BIT_OR: case Op::BIT_XOR: case Op::SHL: case Op::SHR: case Op::USHR: {
                const Value b = ambil();
                const Value a = ambil();
                std::int64_t x = static_cast<std::int64_t>(a.as_number());
                std::int64_t y = static_cast<std::int64_t>(b.as_number());
                std::int64_t hasil = 0;
                switch (ins.op) {
                    case Op::BIT_AND: hasil = x & y; break;
                    case Op::BIT_OR: hasil = x | y; break;
                    case Op::BIT_XOR: hasil = x ^ y; break;
                    case Op::SHL: hasil = x << (y & 31); break;
                    case Op::SHR: hasil = x >> (y & 31); break;
                    default: hasil = static_cast<std::int64_t>(static_cast<std::uint64_t>(x) >> (y & 31)); break;
                }
                dorong(Value::number(static_cast<double>(hasil)));
                break;
            }
            case Op::BIT_NOT: dorong(Value::number(static_cast<double>(~static_cast<std::int32_t>(ambil().as_number())))); break;

            // ---------------------------------------------------------- perbandingan
            case Op::EQ: case Op::NE: case Op::SEQ: case Op::SNE: {
                const Value b = ambil();
                const Value a = ambil();
                const bool ketat = ins.op == Op::SEQ || ins.op == Op::SNE;
                bool sama = false;
                if (a.is_angka() && b.is_angka()) {
                    if (ketat) {
                        sama = !a.is_nan_angka() && !b.is_nan_angka() && a.as_number() == b.as_number();
                    } else {
                        // `NaN !== NaN`; selain itu perbandingan numerik biasa.
                        sama = a.as_number() == b.as_number();
                    }
                } else if (a.is_obyek() && b.is_obyek()) {
                    sama = rt::nilai_sama(a, b);
                } else if (a.is_mboh() && b.is_mboh()) {
                    sama = true;
                } else if (a.is_kosong() && b.is_kosong()) {
                    sama = true;
                } else if (a.is_boole() && b.is_boole()) {
                    sama = a.bool_value() == b.bool_value();
                } else {
                    // Tipe berbeda: `==` tetap salah (tanpa koersi, D-007).
                    sama = false;
                }
                dorong(Value::boolean(ketat ? sama : (ins.op == Op::EQ ? sama : !sama)));
                break;
            }
            case Op::LT: case Op::LE: case Op::GT: case Op::GE: {
                const Value b = ambil();
                const Value a = ambil();
                double hasil = 0.0;
                if (a.is_angka() && b.is_angka()) {
                    const double x = a.as_number();
                    const double y = b.as_number();
                    switch (ins.op) {
                        case Op::LT: hasil = x < y ? 1 : 0; break;
                        case Op::LE: hasil = x <= y ? 1 : 0; break;
                        case Op::GT: hasil = x > y ? 1 : 0; break;
                        default: hasil = x >= y ? 1 : 0; break;
                    }
                } else {
                    const int cmp = rt::bandingkan_teks(rt::nilai_ke_teks(*this, a), rt::nilai_ke_teks(*this, b));
                    switch (ins.op) {
                        case Op::LT: hasil = cmp < 0 ? 1 : 0; break;
                        case Op::LE: hasil = cmp <= 0 ? 1 : 0; break;
                        case Op::GT: hasil = cmp > 0 ? 1 : 0; break;
                        default: hasil = cmp >= 0 ? 1 : 0; break;
                    }
                }
                dorong(Value::boolean(hasil != 0.0));
                break;
            }
            case Op::NOT: dorong(Value::boolean(!rt::benar(ambil()))); break;
            case Op::TYPEOF: dorong(Value::obyek(rt::buat_teks(heap_, rt::nama_jenis(ambil())))); break;

            // ---------------------------------------------------------- objek
            case Op::MAKE_OBJECT: dorong(Value::obyek(buat_obyek())); break;
            // `MAKE_REGEX`: dua konstanta teks (pola, flag) di stack ->
            // objek RegexObj. Pola salah adalah galat runtime biasa, supaya
            // bisa ditangkap `coba`/`tangkep` -- bukan crash.
            case Op::MAKE_REGEX: {
                const Value flag_v = ambil();
                const Value pola_v = ambil();
                const std::string_view pola = sv(pola_v);
                const std::string_view flag = sv(flag_v);
                std::string pesan;
                auto program = rt::RegexProgram::kompilasi(pola, flag, pesan);
                if (program == nullptr) {
                    const Value k =
                        buat_kleru("KleruRegex", "Pola regex ora sah: " + pesan);
                    if (unwind_galat(k)) break;
                    return Status::Galat;
                }
                dorong(buat_regex(std::move(program), pola, flag));
                break;
            }
            // `MAKE_TANGGAL`: teks ISO-8601 -> objek Tanggal. Teks yang tidak
            // bisa diparse menghasilkan `mboh`, mengikuti `Teks("x")` -> NaN.
            case Op::MAKE_TANGGAL: {
                const Value v = ambil();
                dorong(buat_tanggal_dari_teks(sv(v)));
                break;
            }
            case Op::MAKE_ARRAY_SPREAD: {
                const std::size_t dasar = spread_base_.empty() ? stack_.size() : spread_base_.front();
                if (!spread_base_.empty()) spread_base_.clear();
                const std::size_t n = stack_.size() > dasar ? stack_.size() - dasar : 0;
                ArrayObj* a = buat_dhaptar(n);
                a->panjang = n;
                for (std::size_t i = n; i > 0; --i) a->elemen[i - 1] = ambil();
                dorong(Value::obyek(a));
                break;
            }
            // Tandai tinggi stack sebagai awal elemen spread. Berpasangan dengan
            // `MAKE_ARRAY_SPREAD` (yang membaca `spread_base_`).
            case Op::MARK_SPREAD: spread_base_.push_back(stack_.size()); break;
            case Op::MAKE_ARRAY: {
                // Elemen sudah tersusun di stack (urutan kiri-ke-kanan).
                const std::size_t n = ins.a;
                ArrayObj* a = buat_dhaptar(n);
                a->panjang = n;
                for (std::size_t i = n; i > 0; --i) a->elemen[i - 1] = ambil();
                dorong(Value::obyek(a));
                break;
            }
            // `[...iterable]` dorong isi iterable satu per satu.
            // `[...a, ...b]`: jumlah elemen tidak diketahui saat kompilasi, jadi
            // `SPREAD_PUSH` mencatat indeks awal tiap spread dan
            // `MAKE_ARRAY_SPREAD` menghitung totalnya dari situ.
            case Op::SPREAD_PUSH: {
                const Value src = ambil();
                // `src` dipop dari stack, jadi selama statement ini berjalan
                // (yang bisa memicu ribuan alokasi -- `metokake`,
                // `langkah_generator`) nilainya tidak ada di root GC. Tanpa akar
                // ini generator bisa tersapu di tengah, dan nilai yang dipegang
                // frame yang disuspensi ikut hilang.
                const gc::ScopedRoot akar_src(heap_, src);
                const std::size_t spread_dasar = stack_.size();
                spread_base_.push_back(spread_dasar);
                Obj* o = objek(src);
                if (o == nullptr) break;
                if (o->h.kind == OK::Array) {
                    auto* a = static_cast<ArrayObj*>(o);
                    for (std::size_t i = 0; i < a->panjang; ++i) dorong(a->elemen[i]);
                } else if (o->h.kind == OK::Teks) {
                    const std::string_view s = static_cast<const TeksObj*>(o)->str();
                    for (std::size_t i = 0; i < rt::panjang_code_point(s); ++i) {
                        dorong(Value::obyek(rt::buat_teks(heap_, rt::potong_code_point(s, i, i + 1))));
                    }
                } else if (o->h.kind == OK::Generator) {
                    // Spread menjalankan generator sampai habis, sama seperti
                    // `[...gen]`. Generator tak berhingga akan menghabiskan
                    // space tanpa akhir, persis seperti di JavaScript.
                    //
                    // Nilai dikumpulkan di buffer LOKAL dulu, bukan langsung
                    // didorong ke stack: setiap `metokake` memangkas stack ke
                    // `slot_base` frame generator, jadi nilai yang sudah
                    // didorong akan terhapus.
                    constexpr std::size_t kMaks = 1u << 24;
                    std::vector<Value> kumpulan;
                    kumpulan.reserve(8);
                    auto* g = static_cast<rt::GeneratorObj*>(o);
                    for (std::size_t ke = 0; ke < kMaks; ++ke) {
                        ObyekObj* langkah = langkah_generator(g, Value::mboh(), false);
                        if (langkah == nullptr) return Status::Galat;  // galat di body
                        Value selesai = Value::mboh();
                        langkah->get(Value::obyek(rt::buat_teks(heap_, "selesai")), selesai);
                        if (selesai.bool_value()) break;
                        Value v = Value::mboh();
                        langkah->get(Value::obyek(rt::buat_teks(heap_, "nilai")), v);
                        kumpulan.push_back(v);
                    }
                    if (kumpulan.size() >= kMaks) {
                        const Value k = buat_kleru(
                            "KleruWates",
                            "Generator ngalihake luwih saka 16777216 nilai nalika disebarake "
                            "menyang Dhaptar.");
                        if (unwind_galat(k)) break;
                        return Status::Galat;
                    }
                    for (const Value& v : kumpulan) dorong(v);
                }
                break;
            }
            case Op::SPREAD: {
                const Value src = ambil();
                dorong(src);
                break;
            }
            case Op::GET_PROP: {
                const Value obj = ambil();
                const Value kunci = c->nama_properti[ins.a];
                // Membaca properti `mboh`/`kosong` adalah galat runtime
                // (seperti `TypeError` pada bahasa lain).
                if (objek(obj) == nullptr) {
                    const Value k = buat_kleru("KleruJinis", "Ora bisa maca properti \"" +
                                                                           std::string(sv(kunci)) +
                                                                           "\" saka nilai " +
                                                                           std::string(rt::nilai_ke_teks(*this, obj)) +
                                                                           ".");
                    if (unwind_galat(k)) break;
                    return Status::Galat;
                }
                // Getter (`nampa`) dijalankan: frame didorong, hasilnya di-push
                // oleh `RETURN` frame tersebut pada iterasi loop berikutnya.
                Value getter;
                if (cari_getter(obj, kunci, getter)) {
                    std::vector<Value> tanpa_argumen;
                    panggil_objek(getter, obj, tanpa_argumen);
                    break;
                }
                dorong(ambil_properti(objek(obj), kunci));
                break;
            }
            case Op::SET_PROP: {
                const Value nilai = ambil();
                const Value obj = ambil();
                if (objek(obj) == nullptr) {
                    const Value k = buat_kleru("KleruJenis", "Ora bisa nulis properti \"" +
                                                                           std::string(sv(c->nama_properti[ins.a])) +
                                                                           "\" saka nilai " +
                                                                           std::string(rt::nilai_ke_teks(*this, obj)) + ".");
                    if (unwind_galat(k)) break;
                    return Status::Galat;
                }
                set_index_value(obj, c->nama_properti[ins.a], nilai);
                dorong(nilai);
                break;
            }
            case Op::GET_INDEX: {
                const Value kunci = ambil();
                const Value obj = ambil();
                if (objek(obj) == nullptr) {
                    const Value k = buat_kleru("KleruJinis", "Ora bisa maca indeks " +
                                                                           std::string(rt::nilai_ke_teks(*this, kunci)) +
                                                                           " saka nilai " +
                                                                           std::string(rt::nilai_ke_teks(*this, obj)) + ".");
                    if (unwind_galat(k)) break;
                    return Status::Galat;
                }
                dorong_index(obj, kunci);
                break;
            }
            case Op::SET_INDEX: {
                const Value nilai = ambil();
                const Value kunci = ambil();
                const Value obj = ambil();
                set_index_value(obj, kunci, nilai);
                dorong(nilai);
                break;
            }
            // Stack: [this] -> method dari prototipe class INDUK (`induk.x()`).
            case Op::GET_SUPER: {
                const Value this_v = ambil();
                Obj* o = objek(this_v);
                Value ditemukan = Value::mboh();
                if (o != nullptr && o->h.kind == OK::Instance) {
                    auto* inst = static_cast<InstanceObj*>(o);
                    if (inst->kelas != nullptr) {
                        if (Obj* ip = objek(inst->kelas->induk); ip != nullptr && ip->h.kind == OK::Golongan) {
                            ditemukan = ambil_properti(ip, c->nama_properti[ins.a]);
                        }
                    }
                }
                dorong(ditemukan);
                break;
            }
            // Stack: [this, nilai] -> setter pada prototipe class induk.
            case Op::SET_SUPER: {
                const std::size_t n = stack_.size();
                const Value this_v = n >= 2 ? stack_[n - 2] : Value::mboh();
                const Value nilai = n >= 1 ? stack_[n - 1] : Value::mboh();
                stack_.resize(n >= 2 ? n - 2 : 0);
                Obj* o = objek(this_v);
                if (o != nullptr && o->h.kind == OK::Instance) {
                    auto* inst = static_cast<InstanceObj*>(o);
                    if (inst->kelas != nullptr) {
                        if (Obj* ip = objek(inst->kelas->induk); ip != nullptr && ip->h.kind == OK::Golongan) {
                            const Value set = ambil_properti(ip, c->nama_properti[ins.a]);
                            std::vector<Value> satu{nilai};
                            panggil_objek(set, this_v, satu);
                            if (galat_.ada) return Status::Galat;
                        }
                    }
                }
                dorong(nilai);
                break;
            }
            case Op::DEFINE: {
                const Value nilai = ambil();
                const Value obj = ambil();
                Obj* o = objek(obj);
                if (o != nullptr && o->h.kind == OK::Obyek) {
                    static_cast<ObyekObj*>(o)->define(heap_, c->nama_properti[ins.a], nilai, rt::AttrWritable);
                }
                // Literal objek bernilai objek, bukan nilai properti terakhir:
                // objek yang sama tetap di stack untuk properti berikutnya.
                dorong(obj);
                break;
            }
            // Stack: [class, closure]. `b == 1` -> method statis (disimpan pada
            // objek class), selain itu pada prototipe. Method dengan nama
            // `wiwit` menjadi konstruktor.
            case Op::DEFINE_METHOD: {
                const Value fnv = ambil();
                const Value cls_val = ambil();
                Obj* o = objek(cls_val);
                if (o != nullptr && o->h.kind == OK::Golongan) {
                    auto* kls = static_cast<ClassObj*>(o);
                    const std::string_view nama = sv(c->nama_properti[ins.a]);
                    if (nama == "wiwit" && ins.b == 0) {
                        kls->konstruktor = fnv;
                                            } else if (ins.b == 1) {
                        // Method/field statis disimpan terpisah dari field instance
                        // supaya `inst.x` tidak salah membaca slot statis.
                        kls->nama_statis.push_back(nama);
                        kls->nilai_statis.push_back(fnv);
                    } else if (Obj* p = objek(kls->prototipe); p != nullptr && p->h.kind == OK::Obyek) {
                        static_cast<ObyekObj*>(p)->define(heap_, c->nama_properti[ins.a], fnv, rt::AttrWritable);
                    }
                } else if (o != nullptr && o->h.kind == OK::Obyek) {
                    static_cast<ObyekObj*>(o)->define(heap_, c->nama_properti[ins.a], fnv, rt::AttrWritable);
                }
                dorong(cls_val);
                break;
            }
            // Stack: [class, closure]. `b == 1` -> getter, selain itu setter.
            case Op::DEFINE_ACCESSOR: {
                const Value fnv = ambil();
                const Value cls_val = ambil();
                Obj* o = objek(cls_val);
                if (o != nullptr && o->h.kind == OK::Golongan) {
                    auto* kls = static_cast<ClassObj*>(o);
                    Obj* p = objek(kls->prototipe);
                    if (p != nullptr && p->h.kind == OK::Obyek) {
                        static_cast<ObyekObj*>(p)->pasang_aksesor(c->nama_properti[ins.a], fnv, ins.b != 0);
                    }
                } else if (o != nullptr && o->h.kind == OK::Obyek) {
                    static_cast<ObyekObj*>(o)->pasang_aksesor(c->nama_properti[ins.a], fnv, ins.b != 0);
                }
                dorong(cls_val);
                break;
            }
            // Stack: [class, nama]. Mendaftarkan nama field instance.
            case Op::DEFINE_FIELD: {
                const Value cls_val = ambil();
                Obj* o = objek(cls_val);
                if (o != nullptr && o->h.kind == OK::Golongan) {
                    auto* kls = static_cast<ClassObj*>(o);
                    const std::string_view nama = sv(c->nama_properti[ins.a]);
                    // Field privat diberi nama berawalan "#" supaya tidak bentrok
                    // antar class (lihat `KunciPrivatExpr` di kompilator).
                    const std::string_view kunci = ins.b != 0 ? std::string_view(nama) : nama;
                    bool ada = false;
                    for (std::string_view n : kls->nama_field) {
                        if (n == kunci) {
                            ada = true;
                            break;
                        }
                    }
                    if (!ada) {
                        kls->nama_field.push_back(kunci);
                        kls->field_statis.push_back(Value::mboh());
                    }
                }
                dorong(cls_val);
                break;
            }
            // Stack: [kelas, closure] -> [kelas].
            case Op::DEFINE_FIELD_INIT: {
                const Value fnv = ambil();
                const Value cls_val = ambil();
                if (Obj* o = objek(cls_val); o != nullptr && o->h.kind == OK::Golongan) {
                    static_cast<ClassObj*>(o)->inisial_field = fnv;
                }
                dorong(cls_val);
                break;
            }
            // Stack: [kelas, nilai] -> [kelas].
            case Op::DEFINE_STATIC: {
                const Value nilai = ambil();
                const Value cls_val = ambil();
                if (Obj* o = objek(cls_val); o != nullptr && o->h.kind == OK::Golongan) {
                    auto* kls = static_cast<ClassObj*>(o);
                    const std::string_view nama = sv(c->nama_properti[ins.a]);
                    bool ada = false;
                    for (std::size_t i = 0; i < kls->nama_statis.size(); ++i) {
                        if (kls->nama_statis[i] == nama) {
                            kls->nilai_statis[i] = nilai;
                            ada = true;
                            break;
                        }
                    }
                    if (!ada) {
                        kls->nama_statis.push_back(nama);
                        kls->nilai_statis.push_back(nilai);
                    }
                }
                dorong(cls_val);
                break;
            }
            case Op::GET_PROTO: {
                const Value obj = ambil();
                Obj* o = objek(obj);
                Value p = Value::mboh();
                if (o != nullptr) {
                    if (o->h.kind == OK::Obyek) p = static_cast<ObyekObj*>(o)->prototipe;
                    else if (o->h.kind == OK::Array) p = static_cast<ArrayObj*>(o)->prototipe;
                }
                dorong(p);
                break;
            }
            case Op::DELETE: {
                const Value kunci = ambil();
                const Value obj = ambil();
                Obj* o = objek(obj);
                bool ok = false;
                if (o != nullptr && o->h.kind == OK::Obyek) ok = static_cast<ObyekObj*>(o)->hapus(kunci);
                dorong(Value::boolean(ok));
                break;
            }
            // Protokol iterator minimal (Bagian 3.6): iterable adalah Dhaptar,
            // Teks, atau objek dengan `nilai` (dhaptar) / pasangan `kunci`+`nilai`.
            // Stack setelah `ITER_INIT`: [iterable, indeks].
            case Op::ITER_INIT: {

                const Value obj = ambil();
                dorong(obj);
                dorong(Value::angka_int32(0));
                break;
            }
            case Op::ITER_NEXT: {

                if (stack_.size() < 2) return gagal(*this, "I003", "ITER_NEXT tanpa iterator aktif.");
                const Value idx = stack_[stack_.size() - 1];
                const Value iter = stack_[stack_.size() - 2];
                Obj* o = objek(iter);
                bool ada = false;
                Value hasil = Value::mboh();
                if (o != nullptr && o->h.kind == OK::Generator) {
                    // Generator: satu langkah = satu `metokake`. Indeks di stack
                    // tidak dipakai; kontinuasi yang menentukan posisi. Ini yang
                    // membuat `for..of` dan spread bisa berhenti kapan saja --
                    // termasuk untuk generator tak berhingga.
                    auto* g = static_cast<rt::GeneratorObj*>(o);
                    ObyekObj* langkah = langkah_generator(g, Value::mboh(), false);
                    if (langkah == nullptr) return Status::Galat;  // galat di body
                    bool selesai = false;
                    langkah->get(Value::obyek(rt::buat_teks(heap_, "selesai")), hasil);
                    selesai = hasil.bool_value();
                    hasil = Value::mboh();
                    if (!selesai) {
                        langkah->get(Value::obyek(rt::buat_teks(heap_, "nilai")), hasil);
                        ada = true;
                    }
                    dorong(hasil);
                    dorong(Value::boolean(ada));
                    break;
                }
                if (o != nullptr && o->h.kind == OK::Array) {
                    auto* a = static_cast<ArrayObj*>(o);
                    const std::size_t i = static_cast<std::size_t>(idx.as_number());
                    if (i < a->panjang) {
                        hasil = a->elemen[i];
                        ada = true;
                    }
                } else if (o != nullptr && o->h.kind == OK::Teks) {
                    const std::string_view s = static_cast<const TeksObj*>(o)->str();
                    const std::size_t i = static_cast<std::size_t>(idx.as_number());
                    if (i < rt::panjang_code_point(s)) {
                        hasil = Value::obyek(rt::buat_teks(heap_, rt::potong_code_point(s, i, i + 1)));
                        ada = true;
                    }
                } else if (o != nullptr) {
                    const Value daftar =
                        ambil_properti(o, rt::Value::obyek(rt::buat_teks(heap_, "nilai")));
                    if (Obj* dl = objek(daftar); dl != nullptr && dl->h.kind == OK::Array) {
                        auto* a = static_cast<ArrayObj*>(dl);
                        const std::size_t i = static_cast<std::size_t>(idx.as_number());
                        if (i < a->panjang) {
                            hasil = a->elemen[i];
                            ada = true;
                        }
                    } else {
                        const std::string_view s = sv(iter);
                        const std::size_t i = static_cast<std::size_t>(idx.as_number());
                        if (i < rt::panjang_code_point(s)) {
                            hasil = Value::obyek(rt::buat_teks(heap_, std::string(rt::potong_code_point(s, i, i + 1))));
                            ada = true;
                        }
                    }
                }
                if (ada) {
                    stack_[stack_.size() - 1] = Value::angka_int32(static_cast<std::int32_t>(idx.as_number() + 1));
                }
                dorong(hasil);
                dorong(Value::boolean(ada));
                break;
            }

            // ---------------------------------------------------------- fungsi
            case Op::CLOSURE: {
                if (ins.a >= c->anak.size()) {
                    return gagal(*this, "I002", "Indeks closure di luar jangkauan.");
                }
                FungsiObj* child = heap_.alokasi<FungsiObj>();
                child->h.kind = OK::Fungsi;
                child->kode = c->anak[ins.a];
                child->nama = c->anak[ins.a]->nama;
                child->jumlah_param = c->anak[ins.a]->jumlah_param;
                child->variadic = c->anak[ins.a]->variadic;

                auto* clo = heap_.alokasi<ClosureObj>();
                clo->h.kind = OK::Closure;
                clo->fungsi = child;
                clo->nama = child->nama;
                if (f.closure != nullptr) clo->prototipe = f.closure->prototipe;
                clo->upvalue.resize(c->anak[ins.a]->upvalue_sumber.size());
                for (std::size_t i = 0; i < c->anak[ins.a]->upvalue_sumber.size(); ++i) {
                    const std::int32_t dari = c->anak[ins.a]->upvalue_sumber[i];
                    if (dari == -0x40000000) {
                        // Sentinel: nama global -> sel null (lihat GET_UPVAL).
                        clo->upvalue[i] = nullptr;
                    } else if (dari >= 0) {
                        clo->upvalue[i] = cari_atau_buat_upvalue(f.slot_base + static_cast<std::size_t>(dari));
                    } else {
                        const std::size_t idx = static_cast<std::size_t>(-dari - 1);
                        if (f.closure != nullptr && idx < f.closure->upvalue.size()) {
                            clo->upvalue[i] = f.closure->upvalue[idx];
                        } else {
                            clo->upvalue[i] = nullptr;
                        }
                    }
                }
                dorong(Value::obyek(clo));
                break;
            }
            case Op::CALL: {
                const std::size_t n = ins.a;
                // stack: this, callee, arg0..argN
                std::vector<Value> args(n);
                for (std::size_t i = 0; i < n; ++i) args[i] = stack_[stack_.size() - n + i];
                stack_.resize(stack_.size() - n);
                const Value callee = ambil();
                const Value this_val = ambil();
                panggil_objek(callee, this_val, args);
                if (galat_.ada) {
                    // Galat dari kode native (mis. pola regex terlalu rumit, atau
                    // divide by zero di dalam fungsi bawaan) harus bisa ditangkap
                    // `coba`/`tangkep` di pemanggil, sama seperti `THROW`. Tanpa
                    // `unwind_galat` di sini, galat apa pun yang dilempar native
                    // langsung keluar dari program.
                    const Value v = galat_.nilai;
                    galat_.ada = false;
                    if (unwind_galat(v)) break;
                    return Status::Galat;
                }
                break;
            }
            case Op::NEW: {
                const std::size_t n = ins.a;
                std::vector<Value> args(n);
                for (std::size_t i = 0; i < n; ++i) args[i] = stack_[stack_.size() - n + i];
                stack_.resize(stack_.size() - n);
                const Value ctor = ambil();
                Obj* o = objek(ctor);
                if (o != nullptr && o->h.kind == OK::Golongan) {
                    auto* kls = static_cast<ClassObj*>(o);
                    rt::InstanceObj* inst = instans_baru(kls);
                    // Konstruktor dipanggil dengan `this` = instans. Argumen
                    // diambil dari `args` (bukan dari stack), jadi jangan dorong
                    // apa pun ke stack sebelum memanggilnya.
                    // Slot hasil dipesan lebih dulu supaya `RETURN` konstruktor
                    // menuliskannya ke tempat yang benar (lihat `Frame::target_balas`).
                    dorong(Value::obyek(inst));
                    // Field instance berinisialisasi (`y = <ekspresi>`) berjalan
                    // SEBELUM badan konstruktor, dari induk ke anak. Rantai
                    // dikumpulkan dulu, lalu frame didorong terbalik supaya yang
                    // paling induk berjalan paling dulu (LIFO).
                    std::vector<ClassObj*> rantai;
                    for (ClassObj* k = kls; k != nullptr;) {
                        rantai.push_back(k);
                        Obj* ip = objek(k->induk);
                        k = (ip != nullptr && ip->h.kind == OK::Golongan) ? static_cast<ClassObj*>(ip)
                                                                         : nullptr;
                    }
                    if (kls->konstruktor.is_obyek()) {
                        if (Obj* ko = objek(kls->konstruktor);
                            ko != nullptr && ko->h.kind == OK::Closure) {
                            mulai_frame(static_cast<ClosureObj*>(ko), Value::obyek(inst), args,
                                        Frame::kBuangHasil);
                            if (galat_.ada) return Status::Galat;
                        }
                    }
                    for (ClassObj* k : rantai) {
                        if (!k->inisial_field.is_obyek()) continue;
                        if (Obj* fo = objek(k->inisial_field);
                            fo != nullptr && fo->h.kind == OK::Closure) {
                            std::vector<Value> tanpa_arg;
                            mulai_frame(static_cast<ClosureObj*>(fo), Value::obyek(inst), tanpa_arg,
                                        Frame::kBuangHasil);
                            if (galat_.ada) return Status::Galat;
                        }
                    }
                } else {
                    dorong(Value::mboh());
                }
                break;
            }
            case Op::RETURN: {
                const Value v = ambil();
                const std::size_t base = f.slot_base;
                const std::size_t target = f.target_balas;
                if (f.janji_async.is_obyek()) {
                    if (auto* j = static_cast<rt::JanjiObj*>(f.janji_async.mutable_pointer());
                        j != nullptr) {
                        selesaikan_janji(j, v, false);
                    }
                    if (f.akar_async) dasar_async_ = kTanpaAsync;
                }
                if (f.agen != nullptr && f.akar_agen) {
                    // Body generator selesai (`bali` atau jatuh dari akhir).
                    // Nilai balik disimpan di generator, bukan dikembalikan ke
                    // pemanggil -- pemanggil tetap memegang objek Generator.
                    f.agen->status = rt::GeneratorStatus::Selesai;
                    f.agen->nilai = v;
                    f.agen->nilai_bali = v;
                    f.agen->lanjutan = nullptr;
                }
                tutup_upvalue_frame(base);
                stack_.resize(base);
                frames_.pop_back();
                if (target == Frame::kBuangHasil) {
                    // Nilai balik dibuang (konstruktor `anyaar`).
                } else if (target != Frame::kTanpaTarget && target < stack_.size()) {
                    stack_[target] = v;
                    stack_.resize(target + 1);
                } else {
                    dorong(v);
                }
                if (frames_.empty() || frames_.size() < kedalaman_awal) return Status::Selesai;
                break;
            }
            case Op::RETURN_UNDEF: {
                const Value v = ambil();
                const std::size_t base = f.slot_base;
                const std::size_t target = f.target_balas;
                if (f.janji_async.is_obyek()) {
                    if (auto* j = static_cast<rt::JanjiObj*>(f.janji_async.mutable_pointer());
                        j != nullptr) {
                        selesaikan_janji(j, v, false);
                    }
                    if (f.akar_async) dasar_async_ = kTanpaAsync;
                }
                if (f.agen != nullptr && f.akar_agen) {
                    // Body generator selesai (`bali` atau jatuh dari akhir).
                    // Nilai balik disimpan di generator, bukan dikembalikan ke
                    // pemanggil -- pemanggil tetap memegang objek Generator.
                    f.agen->status = rt::GeneratorStatus::Selesai;
                    f.agen->nilai = v;
                    f.agen->nilai_bali = v;
                    f.agen->lanjutan = nullptr;
                }
                tutup_upvalue_frame(base);
                stack_.resize(base);
                frames_.pop_back();
                if (target == Frame::kBuangHasil) {
                    // Nilai balik dibuang (konstruktor `anyaar`).
                } else if (target != Frame::kTanpaTarget && target < stack_.size()) {
                    stack_[target] = Value::mboh();
                    stack_.resize(target + 1);
                } else {
                    dorong(Value::mboh());
                }
                if (frames_.empty() || frames_.size() < kedalaman_awal) return Status::Selesai;
                break;
            }

            // ---------------------------------------------------------- kontrol
            case Op::JUMP: {
                f.ip = ins.a;
                break;
            }
            // Catatan: lompatan bersyarat MEMBATAS (peek), tidak pops. Nilai
            // dibuang oleh `POP` eksplisit dari kompilator, supaya nilai bisa
            // dipakai lagi pada cabang lain (lihat `LogikaExpr`).
            case Op::JUMP_IF_FALSE: {
                if (!rt::benar(puncak())) f.ip = ins.a;
                break;
            }
            case Op::JUMP_IF_TRUE: {
                if (rt::benar(puncak())) f.ip = ins.a;
                break;
            }
            case Op::JUMP_IF_NOT_NULLISH: {
                const Value v = puncak();
                if (!v.is_mboh() && !v.is_kosong()) f.ip = ins.a;
                break;
            }
            case Op::JUMP_IF_NULLISH: {
                const Value v = puncak();
                if (v.is_mboh() || v.is_kosong()) f.ip = ins.a;
                break;
            }
            // Berapa argumen yang benar-benar diberikan pemanggil? Prolog
            // parameter default butuh ini: `f(mboh)` HARUS menghasilkan `mboh`,
            // bukan nilai default (lihat D-036).
            case Op::PARAM_HADAH: {
                dorong(Value::boolean(ins.a < f.n_argumen));
                break;
            }
            case Op::LOOP: f.ip = ins.a; break;
            case Op::TEST_TRUTHY: {
                const Value v = ambil();
                dorong(Value::boolean(rt::benar(v)));
                break;
            }

            // ---------------------------------------------------------- galat
            case Op::THROW: {
                const Value v = ambil();
                if (unwind_galat(v)) break;
                return Status::Galat;
            }
            // Mendaftarkan handler `coba` pada frame berjalan. `ins.b` = ip
            // jalur tolak (badan `pungkasan`); `0` berarti tidak ada
            // `pungkasan`, jadi galat yang tidak cocok langsung naik ke luar.
            // Klausa `tangkep` didaftarkan terpisah lewat `TRY_KLAUSUL`.
            case Op::TRY_BEGIN: {
                Frame::Handler h;
                h.ip_tolak = ins.b;
                h.stack_base = stack_.size();
                f.handlers.push_back(std::move(h));
                break;
            }
            // Mendaftarkan satu klausa `tangkep` pada handler `TRY_BEGIN` yang
            // baru. `ins.a == 0` = tanpa anotasi tipe (tangkap semua).
            case Op::TRY_KLAUSUL: {
                if (f.handlers.empty()) break;
                Frame::HandlerKlausul k;
                // `ins.a` = indeks nama + 1; `0` = klausula tanpa tipe.
                k.tipe = ins.a != 0 ? sv(c->nama_properti[ins.a - 1]) : std::string_view();
                k.ip = ins.b;
                f.handlers.back().klausul.push_back(k);
                break;
            }
            case Op::TRY_END: {
                if (!f.handlers.empty()) f.handlers.pop_back();
                break;
            }
            case Op::FINALLY_END: break;

            // ---------------------------------------------------------- lain
            case Op::TOSTRING: {
                const Value v = ambil();
                dorong(Value::obyek(rt::buat_teks(heap_, rt::nilai_ke_teks(*this, v))));
                break;
            }
            case Op::CONCAT: {
                const Value b = ambil();
                const Value a = ambil();
                dorong(
                    Value::obyek(TeksObj::gabung(heap_, rt::nilai_ke_teks(*this, a), rt::nilai_ke_teks(*this, b))));
                break;
            }
            case Op::CLASS: {
                // Stack: [prototipe, nama, induk?]
                const Value induk = ambil();
                const Value nama = ambil();
                const Value proto = ambil();
                auto* kls = heap_.alokasi<ClassObj>();
                kls->h.kind = OK::Golongan;
                kls->nama = sv(nama);
                kls->prototipe = proto;
                kls->induk = induk;
                // Salin prototipe induk supaya method turunan ikut terlihat.
                if (Obj* ip = objek(induk); ip != nullptr && ip->h.kind == OK::Golongan) {
                    auto* induk_kls = static_cast<ClassObj*>(ip);
                    kls->nama_field.assign(induk_kls->nama_field.begin(), induk_kls->nama_field.end());
                    kls->field_statis.assign(induk_kls->field_statis.begin(), induk_kls->field_statis.end());
                    Obj* pp = objek(induk_kls->prototipe);
                    Obj* cp = objek(proto);
                    if (pp != nullptr && cp != nullptr && pp->h.kind == OK::Obyek && cp->h.kind == OK::Obyek) {
                        auto* prot_induk = static_cast<ObyekObj*>(pp);
                        auto* prot_anak = static_cast<ObyekObj*>(cp);
                        for (const auto& kv : prot_induk->dict) {
                            prot_anak->define(heap_, kv.first, kv.second, rt::AttrWritable);
                        }
                        // Accessor (`nampa`/`setel`) ikut diwariskan.
                        for (const auto& ak : prot_induk->aksesor) {
                            prot_anak->pasang_aksesor(ak.kunci, ak.getter, true);
                            prot_anak->pasang_aksesor(ak.kunci, ak.setter, false);
                        }
                    }
                    // Konstruktor diwariskan; deklarasi `wiwit` milik anak
                    // (yang diproses setelah `CLASS`) akan menimpanya.
                    kls->konstruktor = induk_kls->konstruktor;
                }
                dorong(Value::obyek(kls));
                break;
            }
            case Op::INHERIT: break;
            // `impor ... saka "spesifikasi"`. Linker memuat, mengompilasi, dan
            // mengevaluasi modul (lihat `vm/linker.cpp`), lalu mendorong objek
            // ekspornya. Modul yang gagal sudah melempar galat lewat `lempar`.
            case Op::IMPORT: {
                const Value nama = ambil();
                const std::string_view spesifikasi = sv(nama);
                const std::string dir = pos_berkas_.empty() ? std::string()
                                                            : pos_berkas_.substr(0, pos_berkas_.find_last_of('/'));
                // Catat modul ini di frame SEBELUM evaluates, supaya impor
                // siklik (`a impor b; b impor a`) menemukan `b` yang belum selesai
                // dievaluasi -- dan supaya `GET_IMPORT` tahu modul mana yang
                // dibaca. Entri dibuat kosong dulu karena `muat_modul` baru
                // mendaftarkan modul saat dipanggil.
                //
                // CATATAN: `muat_modul` mendorong & mem-pop frame modul, jadi
                // `f` (referensi ke `frames_.back()`) SETELAH pemanggilan itu
                // menunjuk memori yang sudah dibebaskan. Karena itu akses frame
                // sesudahnya ditulis ulang lewat `frames_.back()`.
                const std::size_t idx = static_cast<std::size_t>(ins.a);
                if (idx == frames_.back().impor_modul.size()) frames_.back().impor_modul.push_back(nullptr);
                const Value ekspor = muat_modul(spesifikasi, dir);
                if (galat_.ada) {
                    // `muat_modul` sudah menyiapkan galat; teruskan sebagai galat
                    // ordinary supaya `coba`/`tangkep` di pemanggil bisa menangkap.
                    galat_.ada = false;
                    const Value v = galat_.nilai;
                    if (unwind_galat(v)) break;
                    return Status::Galat;
                }
                if (!frames_.empty() && idx < frames_.back().impor_modul.size()) {
                    frames_.back().impor_modul[idx] = cari_modul(selesaikan_path(spesifikasi, dir));
                }
                dorong(ekspor);
                break;
            }
            // `GET_IMPORT a b`: sel live binding untuk nama `nama_properti[b]`
            // dari modul yang diimpor pada indeks `a`. Sel inilah yang membuat
            // `ekspor` bersifat live: importer menyimpan sel, bukan salinan
            // nilainya, jadi penulisan dari modul pengekspor langsung terlihat.
            case Op::GET_IMPORT: {
                const std::string_view kunci = sv(c->nama_properti[ins.b]);
                ModuleRecord* asal = nullptr;
                if (ins.a < f.impor_modul.size()) asal = f.impor_modul[ins.a];
                ObyekObj* ekspor = nullptr;
                if (asal != nullptr && asal->ekspor.is_obyek()) {
                    ekspor = static_cast<ObyekObj*>(asal->ekspor.mutable_pointer());
                } else if (modul_aktif != nullptr && modul_aktif->ekspor.is_obyek()) {
                    ekspor = static_cast<ObyekObj*>(modul_aktif->ekspor.mutable_pointer());
                }
                Value hasil;
                const bool ada = ekspor != nullptr &&
                                 ekspor->get(Value::obyek(rt::buat_teks(heap_, kunci)), hasil) &&
                                 hasil.is_obyek() && hasil.pointer() != nullptr &&
                                 static_cast<const Obj*>(hasil.pointer())->h.kind == OK::Sel;
                if (!ada) {
                    const Value k = buat_kleru("KleruModul",
                                              "Modul ora ninggekspor \"" + std::string(kunci) +
                                                  "\" minangka variabel (live binding).");
                    if (unwind_galat(k)) break;
                    return Status::Galat;
                }
                dorong(hasil);
                break;
            }
            // `SEL_BUAT`: bungkus nilai di puncak stack menjadi sel baru.
            case Op::SEL_BUAT: {
                const Value v = ambil();
                auto* s = heap_.alokasi<rt::SelObj>();
                s->h.kind = OK::Sel;
                s->nilai = v;
                dorong(Value::obyek(s));
                break;
            }
            // `SEL_ALIAS a`: arahkan sel di slot `a` ke sel yang baru dipop.
            //
            // Sel pengikat impor dibuat lebih awal (agar closure ter-hoist bisa
            // menangkapnya), lalu diarah ke sel milik modul pengekspor begitu
            // `impor` dijalankan. Tanpa ini, upvalue closure ter-hoist akan
            // menangkap slot stack yang isinya diganti, dan pembacaannya
            // menghasilkan objek `SelObj` alih-alih nilainya.
            case Op::SEL_ALIAS: {
                const Value target = ambil();
                Obj* o = objek(stack_[f.slot_base + ins.a]);
                auto* sel = (o != nullptr && o->h.kind == OK::Sel) ? static_cast<rt::SelObj*>(o) : nullptr;
                auto* t = (target.is_obyek() && target.pointer() != nullptr &&
                           static_cast<const Obj*>(target.pointer())->h.kind == OK::Sel)
                              ? static_cast<rt::SelObj*>(target.mutable_pointer())
                              : nullptr;
                if (sel != nullptr && t != nullptr && t != sel) sel->alias = t;
                break;
            }
            // `SEL_SALIN a`: sel BARU berisi nilai sel lama. Dipakai pengikat
            // per-iterasi `kanggo`, jadi closure yang dibuat pada iterasi
            // berikutnya menangkap sel berbeda dan tidak melihat perubahan
            // iterasi berikutnya (sifat ECMAScript untuk `for (let i ...)`).
            case Op::SEL_SALIN: {
                const Value& s = stack_[f.slot_base + ins.a];
                Obj* o = objek(s);
                auto* lama = (o != nullptr && o->h.kind == OK::Sel) ? static_cast<rt::SelObj*>(o) : nullptr;
                auto* baru = heap_.alokasi<rt::SelObj>();
                baru->h.kind = OK::Sel;
                baru->nilai = lama != nullptr ? lama->baca() : s;
                stack_[f.slot_base + ins.a] = Value::obyek(baru);
                break;
            }
            // Baca/tulis lewat sel live binding. Slot lokal pengikatan impor
            // berisi `SelObj`; variabel modul yang diekspor memakai sel yang
            // sama, jadi pengekspor dan pengimpor berbagi satu nilai.
            case Op::GET_CELL: {
                const Value& s = stack_[f.slot_base + ins.a];
                Obj* o = objek(s);
                auto* sel = (o != nullptr && o->h.kind == OK::Sel) ? static_cast<rt::SelObj*>(o) : nullptr;
                dorong(sel != nullptr ? sel->baca() : s);
                break;
            }
            case Op::SET_CELL: {
                const Value v = ambil();
                Obj* o = objek(stack_[f.slot_base + ins.a]);
                auto* sel = (o != nullptr && o->h.kind == OK::Sel) ? static_cast<rt::SelObj*>(o) : nullptr;
                if (sel != nullptr) {
                    // `tulis`, bukan `nilai = ...`: slot pengikatan impor berisi
                    // sel yang DIARAHKAN ke sel modul pengekspor. Menulis ke
                    // `nilai` langsung hanya mengubah sel pengikat, sehingga
                    // modul asal (dan semua pengimpor lain) tidak melihatnya.
                    sel->tulis(v);
                } else {
                    stack_[f.slot_base + ins.a] = v;
                }
                dorong(v);
                break;
            }
            // `GET_EXPORT`: baca nama dari objek ekspor modul. Berbeda dengan
            // `GET_PROP`, nama yang tidak ada adalah GALAT (bukan `mboh`) —
            // impor salah ketik adalah kesalahan program, bukan nilai kosong.
            case Op::GET_EXPORT: {
                const Value obj = ambil();
                const Value kunci = c->nama_properti[ins.a];
                Obj* o = objek(obj);
                auto* ekspor = o != nullptr && o->h.kind == OK::Obyek ? static_cast<ObyekObj*>(o) : nullptr;
                if (ekspor == nullptr) {
                    const Value k = buat_kleru(
                        "KleruModul",
                        "Nilai iki dudu objek ekspor modul, ora bisa maca \"" + std::string(sv(kunci)) + "\".");
                    if (unwind_galat(k)) break;
                    return Status::Galat;
                }
                Value hasil;
                if (!ekspor->get(kunci, hasil)) {
                    std::string daftar;
                    for (const auto& kv : ekspor->dict) {
                        if (!daftar.empty()) daftar += ", ";
                        daftar += std::string(sv(kv.first));
                    }
                    if (daftar.empty()) daftar = "(kosong)";
                    const Value k = buat_kleru("KleruModul", "Modul ora ninggekspor \"" +
                                                                     std::string(sv(kunci)) +
                                                                     "\". N sing diekspor: " + daftar + ".");
                    if (unwind_galat(k)) break;
                    return Status::Galat;
                }
                dorong(hasil);
                break;
            }
            // `ekspor`: stack berisi nilai lalu nama. Simpan ke objek ekspor
            // modul aktif supaya importer berikutnya bisa membacanya.
            case Op::EXPORT: {
                const Value nama_v = ambil();
                const Value nilai = ambil();
                if (modul_aktif == nullptr || !modul_aktif->ekspor.is_obyek()) {
                    const Value k = buat_kleru(
                        "KleruKonteks",
                        "`ekspor` mung bisa digunakake ing modul, dudu ing fungsi.");
                    if (unwind_galat(k)) break;
                    return Status::Galat;
                }
                auto* ekspor = static_cast<ObyekObj*>(modul_aktif->ekspor.mutable_pointer());
                if (ekspor != nullptr) ekspor->set(heap_, nama_v, nilai);
                break;
            }
            case Op::YIELD: {
                // `metokake nilai`. Di dalam generator, seluruh frame generator
                // disuspensi (disalin ke `Lanjutan`) supaya pemanggil langsung
                // melanjutkan -- lihat `vm/vm_gen.cpp` & D-028.
                const Value v = ambil();
                if (f.agen != nullptr) {
                    if (suspensi_generator(f.agen, v)) break;  // pemanggil lanjut
                    const Value k = buat_kleru(
                        "KleruKonteks",
                        "`metokake` mungake ing generator (fungsi `gawe*`).");
                    if (unwind_galat(k)) break;
                    return Status::Galat;
                }
                // `metokake` di luar generator: tidak ada yang menunda, jadi
                // nilai dikembalikan sebagai ekspresi biasa.
                dorong(v);
                break;
            }
            // `entani nilai`. Kalau nilainya Janji yang sudah selesai, hasilnya
            // langsung dipakai; kalau masih menunggu, seluruh rantai `async`
            // disuspensi (lihat `vm_async.cpp`) dan kode pemanggil lanjut.
            case Op::AWAIT: {
                const Value v = ambil();
                auto* j = v.is_obyek() ? static_cast<rt::JanjiObj*>(v.mutable_pointer()) : nullptr;
                if (j != nullptr && j->h.kind == rt::OK::Janji) {
                    if (j->status == rt::JanjiStatus::Slamet) {
                        dorong(j->hasil);
                        break;
                    }
                    if (j->status == rt::JanjiStatus::Gagal) {
                        if (unwind_galat(j->hasil)) break;
                        return Status::Galat;
                    }
                    if (suspensi_async(j)) break;  // pemanggil melanjutkan
                    const Value k = buat_kleru(
                        "KleruJinis",
                        "Nilai Janji isih nunggu, nanging `entani` wis nalika ing luar rantai async. "
                        "Pakai `entani` mungake ing fungsi utawa method sing marked `mengko`.");
                    if (unwind_galat(k)) break;
                    return Status::Galat;
                }
                // `entani` atas nilai biasa bersifat identitas.
                dorong(v);
                break;
            }
            case Op::DEBUGGER: break;
            case Op::NOP_LINE: case Op::UNDEF_LINE: break;
            case Op::FUNCTION: dorong(Value::mboh()); break;
            case Op::CALL_SPREAD: case Op::CALL_METHOD: case Op::TAIL_CALL: {
                const std::size_t n = ins.a;
                for (std::size_t i = 0; i < n; ++i) (void)ambil();
                dorong(Value::mboh());
                break;
            }
            // Cek tipe bertahap (Bagian 4.6): nilai harus cocok dengan nama
            // tipe, kalau tidak lempar KleruTipe.
            case Op::CEK_TIPE: {
                const Value v = ambil();
                const std::string_view nama_tipe = sv(c->nama_properti[ins.a]);
                bool ok = true;
                if (nama_tipe == "angka") {
                    ok = v.is_angka();
                } else if (nama_tipe == "teks") {
                    Obj* o = objek(v);
                    ok = o != nullptr && o->h.kind == OK::Teks;
                } else if (nama_tipe == "dhaptar") {
                    Obj* o = objek(v);
                    ok = o != nullptr && o->h.kind == OK::Array;
                } else if (nama_tipe == "peta") {
                    Obj* o = objek(v);
                    ok = o != nullptr && o->h.kind == OK::Peta;
                } else if (nama_tipe == "fungsi") {
                    Obj* o = objek(v);
                    ok = o != nullptr && (o->h.kind == OK::Closure || o->h.kind == OK::Fungsi ||
                                          o->h.kind == OK::Native);
                } else if (nama_tipe == "bener") {
                    ok = v.is_boole();
                } else if (nama_tipe == "mboh" || nama_tipe == "apa_wae") {
                    ok = true;
                } else {
                    // Tipe kustom/union: belum diperiksa (Fase 4), selalu lolos.
                    ok = true;
                }
                if (!ok) {
                    const Value k = buat_kleru("KleruTipe", "Nilai jinis " +
                                                                          std::string(rt::nama_jenis(v)) +
                                                                          " ora cocog karo tipe " +
                                                                          std::string(nama_tipe) + ".");
                    if (unwind_galat(k)) break;
                    return Status::Galat;
                }
                dorong(v);
                break;
            }
            case Op::IS_OBJECT: {
                Obj* o = objek(puncak());
                const bool ya = o != nullptr &&
                                (o->h.kind == OK::Obyek || o->h.kind == OK::Instance ||
                                 o->h.kind == OK::Golongan || o->h.kind == OK::Peta);
                dorong(Value::boolean(ya));
                break;
            }
            case Op::IS_ARRAY: {
                Obj* o = objek(puncak());
                dorong(Value::boolean(o != nullptr && o->h.kind == OK::Array));
                break;
            }
            // `kasus <Kelas>:` pada `pilih` & pola `cocog`: instans dari class
            // itu atau dari salah satu induknya. Rantai induk ditelusuri sampai
            // habis supaya `kasus Kucing:` juga cocok untuk `KucingPriba`.
            // `COCOK_TIPE n`: bandingkan jenis nilai dengan nama tipe. Dipakai
            // pola bertipe `cocog` (`kasus n: teks => ...`).
            case Op::COCOK_TIPE: {
                const Value v = ambil();
                dorong(Value::boolean(rt::nama_jenis(v) == sv(c->nama_properti[ins.a])));
                break;
            }
            case Op::INSTAN_DARI: {
                const std::string_view cari = sv(c->nama_properti[ins.a]);
                Obj* o = objek(puncak());
                bool ya = false;
                if (o != nullptr && o->h.kind == OK::Instance) {
                    ClassObj* k = static_cast<InstanceObj*>(o)->kelas;
                    for (int d = 0; d < 64 && k != nullptr; ++d) {
                        if (k->nama == cari) {
                            ya = true;
                            break;
                        }
                        Obj* ip = objek(k->induk);
                        k = (ip != nullptr && ip->h.kind == OK::Golongan) ? static_cast<ClassObj*>(ip)
                                                                         : nullptr;
                    }
                }
                dorong(Value::boolean(ya));
                break;
            }
            case Op::MATCH_TEST: case Op::MATCH_BIND: dorong(Value::boolean(false)); break;
            // Zona mati-temporal: lempar kalau slot leksikal ini belum
            // diinisialisasi. `f.ip` sudah melewati instruksi SEBELUM `TDZ_CHECK`,
            // jadi bandingkannya langsung dengan ip deklarasi yang dicatat di
            // `Chunk::tdz_daftar` (lihat `Compiler::slot_baru_tdz`).
            case Op::TDZ_CHECK: {
                if (ins.a < c->tdz_daftar.size() && f.ip <= c->tdz_daftar[ins.a]) {
                    const Value k = buat_kleru(
                        "KleruCakupan",
                        "Jeneng iki durung bisa diakses: deklarasi dudu wis dievaluasi "
                        "(zona mati-temporal).");
                    if (unwind_galat(k)) break;
                    return Status::Galat;
                }
                break;
            }
            case Op::CLOSE_UPVAL: {
                dorong(Value::mboh());
                break;
            }
            // Opcode yang belum diimplementasikan pada Fase 3 Minimal.
            case Op::BIGINT: case Op::GET_MODULE: case Op::SET_MODULE:
            case Op::DUP2: case Op::ROT3: case Op::SAME_VALUE:
            case Op::SET_PROTO: case Op::GET_GLOBAL_FUNC:
            case Op::INSTANCEOF: case Op::IN: case Op::TYPEOF_KIND: {
                std::fprintf(stderr, "KleruInternal [I001] Opcode belum diimplementasikan: %s\n", opcode_nama(ins.op));
                galat_.ada = true;
                return Status::Galat;
            }
            default:
                // `JumlahOp` sentinel: jangan sampai jatuh ke sini.
                std::fprintf(stderr, "KleruInternal [I001] Opcode tak dikenal: %d\n", static_cast<int>(ins.op));
                galat_.ada = true;
                return Status::Galat;
        }
    }
}

}  // namespace jawa::vm
