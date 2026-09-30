#include "gc/heap.h"
#include "rt/object.h"
#include "rt/tanggal.h"
#include "rt/string.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "rt/number.h"

namespace jawa::rt {

// ---------------------------------------------------------------------------
// Panjang & substring berbasis code point
// ---------------------------------------------------------------------------

std::size_t panjang_code_point(std::string_view s) {
    std::size_t n = 0;
    for (char c : s) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    }
    return n;
}

std::string_view potong_code_point(std::string_view s, std::size_t dari, std::size_t sampai) {
    std::size_t i = 0;
    std::size_t cp = 0;
    while (i < s.size() && cp < dari) {
        i += (static_cast<unsigned char>(s[i]) < 0x80) ? 1
                                                     : ((static_cast<unsigned char>(s[i]) & 0xE0) == 0xC0
                                                            ? 2
                                                            : ((static_cast<unsigned char>(s[i]) & 0xF0) == 0xE0 ? 3 : 4));
        ++cp;
    }
    const std::size_t mulai = i;
    while (i < s.size() && cp < sampai) {
        i += (static_cast<unsigned char>(s[i]) < 0x80) ? 1
                                                     : ((static_cast<unsigned char>(s[i]) & 0xE0) == 0xC0
                                                            ? 2
                                                            : ((static_cast<unsigned char>(s[i]) & 0xF0) == 0xE0 ? 3 : 4));
        ++cp;
    }
    return s.substr(mulai, i - mulai);
}

bool indeks_bulat(std::string_view s, std::size_t& keluar) {
    if (s.empty()) return false;
    std::size_t n = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + static_cast<std::size_t>(c - '0');
    }
    keluar = n;
    return true;
}

int bandingkan_teks(std::string_view a, std::string_view b) {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

TeksObj* buat_teks(jawa::gc::Heap& heap, std::string_view s) { return StringTable::buat(heap, s); }

TeksObj* buat_teks_dari(std::string s, jawa::gc::Heap& heap) {
    auto* t = heap.alokasi_baru<TeksObj>();
    t->h.kind = OK::Teks;
    t->data_ = std::move(s);
    t->hitung_panjang();
    heap.tambah_bytes(t->data_.capacity() + sizeof(t->data_));
    return t;
}

std::string nilai_ke_teks_inspect_dummy(Value v) {
    if (v.is_obyek() && v.pointer() != nullptr) {
        const auto* o = static_cast<const Obj*>(v.pointer());
        if (o->h.kind == OK::Teks) return static_cast<const TeksObj*>(o)->str();
    }
    if (v.is_mboh()) return "mboh";
    if (v.is_kosong()) return "kosong";
    if (v.is_boole()) return v.bool_value() ? "bener" : "salah";
    if (v.is_angka()) return std::to_string(v.as_number());
    return "?";
}

std::string_view nama_jenis(Value v) {
    if (v.is_mboh()) return "mboh";
    if (v.is_kosong()) return "kosong";
    if (v.is_boole()) return "boole";
    if (v.is_obyek() || v.is_bigint() || v.is_simbol()) {
        const void* p = v.pointer();
        if (p == nullptr) return "obyek";
        const auto* o = static_cast<const Obj*>(p);
        switch (o->h.kind) {
            case OK::Teks: return "teks";
            case OK::Array: return "dhaptar";
            case OK::Fungsi:
            case OK::Closure:
            case OK::Native:
            case OK::BoundFn: return "fungsi";
            case OK::Golongan: return "golongan";
            case OK::Instance: return "obyek";
            case OK::Janji: return "janji";
            case OK::Generator: return "generator";
            case OK::Peta: return "peta";
            case OK::Himpunan: return "himpunan";
            case OK::Berkas: return "berkas";
            case OK::Regex: return "regex";
            case OK::Tanggal: return "tanggal";
            case OK::Kleru: return "kleru";
            case OK::Simbol: return "simbol";
            case OK::BigInt: return "bigint";
            default: return "obyek";
        }
    }
    if (v.is_angka()) return "angka";
    if (v.is_boole()) return "boole";
    return "mboh";
}

// ---------------------------------------------------------------------------
// Peta helper (dipakai nilai_ke_teks;definisi lengkap di vm.cpp untuk akses VM)
// ---------------------------------------------------------------------------

std::string nilai_ke_teks(VM& vm, Value v) {
    if (v.is_obyek() || v.is_bigint() || v.is_simbol()) {
        const void* p = v.pointer();
        if (p == nullptr) return "undefined";
        const auto* o = static_cast<const Obj*>(p);
        switch (o->h.kind) {
            case OK::Teks: return static_cast<const TeksObj*>(o)->str();
            case OK::Array: return dhaptar_ke_teks(vm, const_cast<ArrayObj*>(static_cast<const ArrayObj*>(o)), 0);
            case OK::Peta: return peta_ke_teks(vm, const_cast<PetaObj*>(static_cast<const PetaObj*>(o)), 0);
            case OK::Native: return "function " + std::string(static_cast<const NativeFnObj*>(o)->nama) + "() { [native] }";
            case OK::Janji: return "Janji { <pending> }";
            case OK::Regex: {
                const auto* r = static_cast<const RegexObj*>(o);
                return "/" + r->pola + "/" + r->flag;
            }
            case OK::Kleru: {
                const auto* k = static_cast<const KleruObj*>(o);
                return std::string(k->jeneng) + ": " + k->pesan;
            }
            case OK::Golongan: return "function " + std::string(static_cast<const ClassObj*>(o)->nama) + "() { [class] }";
            case OK::Tanggal:
                // ISO-8601 jauh lebih berguna daripada angka milidetik: nilai ini
                // biasanya dipakai untuk dicetak atau dibandingkan dengan teks.
                return Tanggal(static_cast<const TanggalObj*>(o)->milidetik).ke_teks();
            case OK::Simbol: return "Simbol(" + static_cast<const SimbolObj*>(o)->deskripsi + ")";
            default: return "[objek]";
        }
    }
    if (v.is_mboh()) return "undefined";
    if (v.is_kosong()) return "null";
    if (v.is_boole()) return v.bool_value() ? "true" : "false";
    if (v.is_int32()) return std::to_string(v.as_i32());
    if (v.is_nan_angka()) return "DuduAngka";
    if (v.is_number()) return number_to_string(v.as_double_pure());
    return "";
}

std::string dhaptar_ke_teks(VM& vm, ArrayObj* a, int kedalaman) {
    if (a == nullptr) return "[]";
    if (kedalaman > 6) return "[...]";
    std::string out = "[";
    for (std::size_t i = 0; i < a->panjang; ++i) {
        if (i > 0) out += ", ";
        out += nilai_ke_teks(vm, a->elemen[i]);
    }
    out += "]";
    return out;
}

std::string nilai_ke_teks_inspect(VM& vm, Value v, int kedalaman) {
    // Bentuk "dalam": teks diapit tanda kutip supaya pesan kegagalan assertion
    // (`pratelas("a", "b")`) bisa langsung dibaca -- tanpa itu, nilai `"a"` dan
    // `a` tampil sama dan penyebab kegagalan hilang.
    if (v.is_obyek() || v.is_bigint() || v.is_simbol()) {
        const void* p = v.pointer();
        if (p == nullptr) return "undefined";
        const auto* o = static_cast<const Obj*>(p);
        if (o->h.kind == OK::Teks) {
            return "\"" + static_cast<const TeksObj*>(o)->str() + "\"";
        }
        if (o->h.kind == OK::Obyek && kedalaman <= 6) {
            // Obyek datar: tampilkan properti yang bisa dibaca, Supaya kegagalan
            // `pratelas({a: 1}, {a: 2})` langsung menunjukkan selisihnya.
            const auto* ob = static_cast<const ObyekObj*>(o);
            std::string out = "{";
            bool first = true;
            auto tulis = [&](Value kunci, Value nilai) {
                if (!first) out += ", ";
                first = false;
                out += nilai_ke_teks_inspect(vm, kunci, kedalaman + 1);
                out += ": ";
                out += nilai_ke_teks_inspect(vm, nilai, kedalaman + 1);
            };
            if (ob->slot != nullptr && ob->shape != nullptr) {
                for (std::size_t i = 0; i < ob->shape->jumlah(); ++i) {
                    const auto& prop = ob->shape->at(i);
                    if (prop.index < 0) continue;
                    const auto idx = static_cast<std::size_t>(prop.index);
                    if (idx < ob->jumlah_slot) tulis(prop.kunci, ob->slot[idx]);
                }
            } else {
                for (const auto& [kunci, nilai] : ob->dict) tulis(kunci, nilai);
            }
            out += "}";
            return out;
        }
    }
    return nilai_ke_teks(vm, v);
}

std::string peta_ke_teks(VM& vm, PetaObj* p, int kedalaman) {    if (p == nullptr) return "{}";
    if (kedalaman > 6) return "{...}";
    std::string out = "{";
    bool first = true;
    for (const auto& e : p->entri) {
        if (e.dihapus) continue;
        if (!first) out += ", ";
        first = false;
        out += nilai_ke_teks(vm, e.kunci);
        out += ": ";
        out += nilai_ke_teks(vm, e.nilai);
    }
    out += "}";
    return out;
}

std::string normalisasi_sederhana(std::string_view s, std::string_view /*bentuk*/) {
    // Cakupan terbatas: hanya komposisi sederhana; see docs/stdlib.md.
    return std::string(s);
}

}  // namespace jawa::rt
