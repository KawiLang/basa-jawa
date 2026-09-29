// Implementasi model objek: Shape, Teks, Obyek, Array, Peta, Himpunan.
#include "rt/object.h"

#include <algorithm>
#include <cstring>

#include "gc/heap.h"
#include "rt/number.h"
#include "rt/string.h"

namespace jawa::rt {

const char* ok_name(OK k) noexcept {
    switch (k) {
        case OK::Obyek: return "Objek";
        case OK::Array: return "Dhaptar";
        case OK::Teks: return "Teks";
        case OK::Fungsi: return "Fungsi";
        case OK::Closure: return "Closure";
        case OK::Native: return "Native";
        case OK::Golongan: return "Golongan";
        case OK::Instance: return "Instans";
        case OK::Janji: return "Janji";
        case OK::Peta: return "Peta";
        case OK::Himpunan: return "Himpunan";
        case OK::Regex: return "Regex";
        case OK::Tanggal: return "Tanggal";
        case OK::Kleru: return "Kleru";
        case OK::Simbol: return "Simbol";
        case OK::BigInt: return "BigInt";
        case OK::Generator: return "Generator";
        case OK::Sel: return "Sel";
        case OK::BoundFn: return "BoundFn";
        case OK::Proxy: return "Proxy";
        default: return "Objek";
    }
}

// ===========================================================================
// Perbandingan nilai (kunci)
// ===========================================================================

bool nilai_sama(const Value& a, const Value& b) noexcept {
    if (a == b) return true;  // jalur cepat: bit identik
    // Angka: int32 & double dibandingkan secara numerik.
    if (a.is_angka() && b.is_angka()) {
        if (a.is_nan_angka() || b.is_nan_angka()) return false;
        return a.as_number() == b.as_number();
    }
    // Teks: dibandingkan berdasarkan isi (dua objek berbeda boleh sama isi).
    if (a.is_obyek() && b.is_obyek()) {
        const void* pa = a.pointer();
        const void* pb = b.pointer();
        if (pa == nullptr || pb == nullptr) return pa == pb;
        const auto* oa = static_cast<const Obj*>(pa);
        const auto* ob = static_cast<const Obj*>(pb);
        if (oa->h.kind != ob->h.kind) return false;
        if (oa->h.kind == OK::Teks) {
            const auto* ta = static_cast<const TeksObj*>(oa);
            const auto* tb = static_cast<const TeksObj*>(ob);
            return ta->str() == tb->str();
        }
    }
    return false;
}

// ===========================================================================
// Shape
// ===========================================================================

std::size_t Shape::jumlah_dibuat_ = 0;

Shape* Shape::tambah(Value kunci, std::uint8_t attr, std::int32_t index) const {
    return ShapeTable::instance().transisi(this, kunci, attr, index);
}

Shape* Shape::dengan_slot(std::size_t posisi, std::int32_t index) const {
    // Salin shape lalu ubah index pada posisi tersebut.
    Shape* s = new Shape();
    s->id_ = jumlah_dibuat_++;
    s->jumlah_ = jumlah_;
    s->kapasitas_ = std::max<std::size_t>(jumlah_, 1);
    s->properti_ = new Properti[s->kapasitas_];
    for (std::size_t i = 0; i < jumlah_; ++i) s->properti_[i] = properti_[i];
    if (posisi < jumlah_) s->properti_[posisi].index = index;
    ShapeTable::instance().daftarkan(s);
    return s;
}

ShapeTable::ShapeTable() {
    semua_.reserve(64);
    bucket_.resize(1024);
    // Shape dasar: objek kosong. Dibuat langsung (bukan lewat `instance()`)
    // supaya konstruktor tidak memanggil dirinya sendiri (recursive_init_error).
    kosong_ = new Shape();
    kosong_->id_ = Shape::jumlah_dibuat_++;
    semua_.push_back(kosong_);
}

ShapeTable& ShapeTable::instance() {
    static ShapeTable t;
    return t;
}

void ShapeTable::daftarkan(Shape* s) const { semua_.push_back(s); }

Shape* ShapeTable::transisi(const Shape* dari, Value kunci, std::uint8_t attr, std::int32_t index) const {
    // Cari transisi yang sudah ada (shape bersifat interned).
    for (Shape* s : semua_) {
        if (s->jumlah() != dari->jumlah() + 1) continue;
        bool sama = true;
        for (std::size_t i = 0; i < dari->jumlah(); ++i) {
            if (s->properti_[i].kunci != dari->properti_[i].kunci) { sama = false; break; }
        }
        if (!sama) continue;
        if (s->properti_[dari->jumlah()].kunci != kunci) continue;
        return s;
    }
    // Buat shape baru. CATATAN: `Shape` Odessa friend `ShapeTable`, jadi kita
    // harus menulis `Shape::jumlah_dibuat_` (friendship memberi akses, bukan
    // name lookup unqualified).
    Shape* s = new Shape();
    s->id_ = Shape::jumlah_dibuat_++;
    s->jumlah_ = dari->jumlah_ + 1;
    s->kapasitas_ = s->jumlah_;
    s->properti_ = new Properti[s->kapasitas_];
    for (std::size_t i = 0; i < dari->jumlah(); ++i) s->properti_[i] = dari->properti_[i];
    s->properti_[dari->jumlah()] = Properti{kunci, Value::mboh(), Value::mboh(), attr, index};
    semua_.push_back(s);
    return s;
}

// ===========================================================================
// StringTable
// ===========================================================================

StringTable::StringTable() { aktif_ = true; }

StringTable& StringTable::instance() {
    static StringTable t;
    return t;
}

Value StringTable::intern(std::string_view s) {
    auto it = peta_.find(std::string(s));
    if (it != peta_.end()) return Value::obyek(it->second);
    // Teks intern tetap dialokasikan lewat heap agar bisa di-GC dengan benar;
    // entri tabel hanya referensi LEMAH (dibersihkan di sweep).
    return Value::mboh();  // diisi pemanggil setelah alokasi
}

Value StringTable::cari(std::string_view s) const noexcept {
    auto it = peta_.find(std::string(s));
    if (it == peta_.end()) return Value::mboh();
    return Value::obyek(it->second);
}

TeksObj* StringTable::buat(Heap& heap, std::string_view s) {
    auto* t = heap.alokasi_baru<TeksObj>();
    t->h.kind = OK::Teks;
    t->data_.assign(s);
    t->hitung_panjang();
    // Isi string dialokasikan di luar objek; hitung agar `--maks-memori` jujur.
    heap.tambah_bytes(t->data_.capacity() + sizeof(t->data_));
    return t;
}

void StringTable::daftarkan_semua(std::vector<TeksObj*>& keluar) const {
    for (const auto& kv : peta_) keluar.push_back(kv.second);
}

void StringTable::bersihkan_mati(std::vector<TeksObj*>& hidup) {
    // Buang entri yang objeknya sudah dibersihkan oleh sweep.
    for (TeksObj* t : hidup) {
        if (t->interned_) peta_.emplace(t->data_, t);
    }
}

void StringTable::reset() {
    peta_.clear();
    masuk_.clear();
}

// ===========================================================================
// TeksObj
// ===========================================================================

void TeksObj::hitung_panjang() {
    panjang_cp_ = 0;
    for (char c : data_) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++panjang_cp_;
    }
}

std::size_t TeksObj::offset_code_point(std::size_t i) const {
    std::size_t cp = 0;
    std::size_t off = 0;
    while (off < data_.size() && cp < i) {
        const unsigned char c = static_cast<unsigned char>(data_[off]);
        off += (c < 0x80) ? 1 : ((c & 0xE0) == 0xC0 ? 2 : ((c & 0xF0) == 0xE0 ? 3 : 4));
        ++cp;
    }
    return off;
}

void TeksObj::hash() {
    hash_ = std::hash<std::string>{}(data_);
    hash_valid_ = true;
}

TeksObj* TeksObj::gabung(Heap& heap, const std::string& a, const std::string& b) {
    auto* t = heap.alokasi_baru<TeksObj>();
    t->h.kind = OK::Teks;
    t->data_.reserve(a.size() + b.size());
    t->data_ = a;
    t->data_ += b;
    t->hitung_panjang();
    heap.tambah_bytes(t->data_.capacity() + sizeof(t->data_));
    return t;
}

// ===========================================================================
// ObyekObj
// ===========================================================================

void ObyekObj::init(Shape* s, std::size_t n_slot) {
    shape = s;
    jumlah_slot = n_slot;
    if (n_slot == 0) {
        slot = nullptr;
    } else {
        slot = static_cast<Value*>(::operator new(sizeof(Value) * n_slot));
        for (std::size_t i = 0; i < n_slot; ++i) slot[i] = Value::mboh();
    }
}

Properti* ObyekObj::cari(Value kunci) noexcept {
    if (shape != nullptr) {
        Properti* p = shape->find(kunci);
        if (p != nullptr) return p;
    }
    for (auto& kv : dict) {
        if (nilai_sama(kv.first, kunci)) return nullptr;  // cari nilai via get()
    }
    return nullptr;
}

bool ObyekObj::get(Value kunci, Value& keluar) const noexcept {
    if (shape != nullptr) {
        if (const Properti* p = shape->find(kunci)) {
            if ((p->attr & AttrAccessor) != 0) return false;  // perlu getter
            if (p->index >= 0 && slot != nullptr) {
                keluar = slot[p->index];
                return true;
            }
        }
    }
    for (const auto& kv : dict) {
        if (nilai_sama(kv.first, kunci)) { keluar = kv.second; return true; }
    }
    return false;
}

bool ObyekObj::set(Heap& /*heap*/, Value kunci, Value nilai) {
    if (shape != nullptr) {
        if (Properti* p = shape->find(kunci)) {
            if ((p->attr & AttrWritable) == 0) return false;
            if (p->index >= 0 && slot != nullptr) { slot[p->index] = nilai; return true; }
        }
    }
    for (auto& kv : dict) {
        if (nilai_sama(kv.first, kunci)) { kv.second = nilai; return true; }
    }
    // Properti baru: masukkan ke dict (dictionary mode).
    dict.emplace_back(kunci, nilai);
    return true;
}

bool ObyekObj::define(Heap& /*heap*/, Value kunci, Value nilai, std::uint8_t attr) {
    if (shape != nullptr && (attr & AttrAccessor) == 0) {
        if (Properti* p = shape->find(kunci)) {
            if (p->index >= 0 && slot != nullptr) slot[p->index] = nilai;
            return true;
        }
    }
    for (auto& kv : dict) {
        if (nilai_sama(kv.first, kunci)) { kv.second = nilai; return true; }
    }
    dict.emplace_back(kunci, nilai);
    return true;
}

bool ObyekObj::hapus(Value kunci) {
    if (shape != nullptr) {
        if (const Properti* p = shape->find(kunci)) {
            if ((p->attr & AttrConfigurable) == 0) return false;
        }
    }
    for (std::size_t i = 0; i < dict.size(); ++i) {
        if (dict[i].first == kunci) {
            dict.erase(dict.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

void ObyekObj::ke_dictionary(Heap& /*heap*/) {
    if (slot != nullptr) {
        // Salin slot ke dict lalu lepas.
        if (shape != nullptr) {
            for (std::size_t i = 0; i < shape->jumlah(); ++i) {
                const Properti& p = shape->at(i);
                if (p.index < 0) continue;
                bool ada = false;
                for (auto& kv : dict) {
                    if (nilai_sama(kv.first, p.kunci)) { kv.second = slot[p.index]; ada = true; break; }
                }
                if (!ada) dict.emplace_back(p.kunci, slot[p.index]);
            }
        }
        ::operator delete(slot);
        slot = nullptr;
        shape = nullptr;
    }
}

ObyekObj::Aksesor* ObyekObj::cari_aksesor(Value kunci) noexcept {
    for (Aksesor& a : aksesor) {
        if (nilai_sama(a.kunci, kunci)) return &a;
    }
    return nullptr;
}

void ObyekObj::pasang_aksesor(Value kunci, Value fn, bool getter) {
    if (Aksesor* a = cari_aksesor(kunci)) {
        if (getter) {
            a->getter = fn;
        } else {
            a->setter = fn;
        }
        return;
    }
    Aksesor a;
    a.kunci = kunci;
    if (getter) {
        a.getter = fn;
    } else {
        a.setter = fn;
    }
    aksesor.push_back(a);
}

void ObyekObj::kompak() {}

// ===========================================================================
// ArrayObj
// ===========================================================================

void ArrayObj::init(Shape* s, std::size_t kap) {
    shape = s;
    kapasitas = std::max<std::size_t>(kap, 4);
    panjang = 0;
    elemen = static_cast<Value*>(::operator new(sizeof(Value) * kapasitas));
    for (std::size_t i = 0; i < kapasitas; ++i) elemen[i] = Value::mboh();
}

void ArrayObj::ensure(std::size_t perlu, Heap* heap) {
    if (perlu <= kapasitas) return;
    std::size_t baru = std::max<std::size_t>(kapasitas * 2, perlu);
    Value* baru_ptr = static_cast<Value*>(::operator new(sizeof(Value) * baru));
    for (std::size_t i = 0; i < panjang; ++i) baru_ptr[i] = elemen[i];
    for (std::size_t i = panjang; i < baru; ++i) baru_ptr[i] = Value::mboh();
    ::operator delete(elemen);
    if (heap != nullptr) heap->tambah_bytes(sizeof(Value) * (baru - kapasitas));
    elemen = baru_ptr;
    kapasitas = baru;
}

void ArrayObj::dorong(Value v, Heap* heap) {
    ensure(panjang + 1, heap);
    elemen[panjang++] = v;
}

void ArrayObj::set(std::size_t i, Value v, Heap* heap) {
    if (i >= panjang) {
        ensure(i + 1, heap);
        for (std::size_t k = panjang; k <= i; ++k) elemen[k] = Value::mboh();
        panjang = i + 1;
    }
    elemen[i] = v;
}

void ArrayObj::perkecil(Heap* heap) {
    if (panjang == 0) return;
    if (panjang * 2 >= kapasitas) return;
    std::size_t baru = std::max<std::size_t>(panjang, 4);
    Value* baru_ptr = static_cast<Value*>(::operator new(sizeof(Value) * baru));
    for (std::size_t i = 0; i < panjang; ++i) baru_ptr[i] = elemen[i];
    ::operator delete(elemen);
    (void)heap;  // buffer diperkecil; akuntansi hanya tumbuh
    elemen = baru_ptr;
    kapasitas = baru;
}

// ===========================================================================
// PetaObj
// ===========================================================================

void PetaObj::rehash() {
    std::size_t ukuran = tabel.empty() ? 16 : tabel.size() * 2;
    if (jumlah_aktif * 2 >= ukuran) ukuran *= 2;
    tabel.assign(ukuran, static_cast<std::size_t>(-1));
    for (std::size_t i = 0; i < entri.size(); ++i) {
        if (entri[i].dihapus) continue;
        std::size_t pos = std::hash<std::uint64_t>{}(entri[i].kunci.key()) % ukuran;
        while (tabel[pos] != static_cast<std::size_t>(-1)) pos = (pos + 1) % ukuran;
        tabel[pos] = i;
    }
}

PetaObj::Entri* PetaObj::cari(Value kunci) noexcept {
    if (tabel.empty()) return nullptr;
    const std::size_t mulai = std::hash<std::uint64_t>{}(kunci.key()) % tabel.size();
    for (std::size_t probes = 0; probes < tabel.size(); ++probes) {
        const std::size_t idx = tabel[(mulai + probes) % tabel.size()];
        if (idx == static_cast<std::size_t>(-1)) return nullptr;
        if (nilai_sama(entri[idx].kunci, kunci)) return &entri[idx];
    }
    return nullptr;
}

bool PetaObj::pasang(Value kunci, Value nilai) {
    if (Entri* e = cari(kunci)) { e->nilai = nilai; return false; }
    entri.push_back(Entri{kunci, nilai, false});
    ++jumlah_aktif;
    if (tabel.empty() || jumlah_aktif * 2 >= tabel.size()) {
        rehash();
    } else {
        std::size_t pos = std::hash<std::uint64_t>{}(kunci.key()) % tabel.size();
        while (tabel[pos] != static_cast<std::size_t>(-1)) pos = (pos + 1) % tabel.size();
        tabel[pos] = entri.size() - 1;
    }
    return true;
}

bool PetaObj::hapus(Value kunci) {
    Entri* e = cari(kunci);
    if (e == nullptr) return false;
    e->dihapus = true;
    e->nilai = Value::mboh();
    e->kunci = Value::mboh();
    --jumlah_aktif;
    return true;
}

}  // namespace jawa::rt
