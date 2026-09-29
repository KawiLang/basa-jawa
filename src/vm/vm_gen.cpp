// ===========================================================================
// Generator LAZY (`gawe*` + `metokake`)
//
// Generator memakai mechanism continuation yang SAMA dengan rantai `async`
// (lihat D-023): `metokake` menyalin frame generator beserta nilai stack-nya
// ke `Lanjutan`, lalu memangkas `frames_`/`stack_` supaya pemanggil melanjutkan
// dari instruksi setelah `CALL`. Pemulihan menyalin balik dan menjalankan loop
// bytecode lagi.
//
// Bedanya dengan async: pemanggilan `gawe* f()` mengembalikan objek Generator
// dan body-nya baru berjalan sampai `metokake` PERTAMA. Tinggi call stack tidak
// bertambah, jadi generator tak berhingga tetap bisa dipakai:
//
//     gawe* tak_henti() { ana i = 0; nalika (bener) { metokake i; i = i + 1; } }
//     kanggo (ana x saka tak_henti()) { ... }   // berhenti kapan saja
//
// D-019 (generator mode-eager) digantikan oleh D-028. Batas 2^20 hasil yang
// dulu dipakai sebagai pengaman tidak diperlukan lagi: pemanggilan tidak lagi
// menjalankan body sampai selesai.
// ===========================================================================

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "rt/object.h"
#include "rt/string.h"
#include "vm/vm.h"

namespace jawa::vm {

using rt::GeneratorObj;
using rt::GeneratorStatus;
using rt::OK;

// ===========================================================================
// Pemanggilan
// ===========================================================================

void VM::panggil_generator(ClosureObj* fn, Value this_val, std::vector<Value>& args) {
    auto* g = heap_.alokasi<GeneratorObj>();
    g->h.kind = OK::Generator;
    g->status = GeneratorStatus::Jalan;
    g->fungsi = Value::obyek(fn);

    // Objek Generator didorong lebih dulu supaya menjadi hasil pemanggilan
    // pada indeks yang sama seperti frame biasa; `kBuangHasil` membuat
    // `RETURN` memangkas stack tanpa mendorong nilai balik (lihat
    // `VM::panggil_async` untuk argumen yang sama).
    dorong(Value::obyek(g));
    const std::size_t sebelum = frames_.size();
    mulai_frame(fn, this_val, args, Frame::kBuangHasil);
    if (frames_.size() == sebelum) {
        stack_.pop_back();  // `mulai_frame` gagal (tumpukan penuh)
        dorong(Value::mboh());
        return;
    }
    Frame& f = frames_.back();
    f.agen = g;
    f.akar_agen = true;
}

// ===========================================================================
// Suspend (metokake)
// ===========================================================================

bool VM::suspensi_generator(GeneratorObj* g, Value hasil) {
    if (g == nullptr) return false;
    // Akar generator = frame paling bawah yang ditandai `agen == g`.
    std::size_t k = frames_.size();
    for (std::size_t i = frames_.size(); i > 0; --i) {
        if (frames_[i - 1].agen != g) continue;
        k = i - 1;
        if (frames_[i - 1].akar_agen) break;  // frame akar: berhenti di sini
    }
    if (k >= frames_.size()) return false;

    auto lan = std::make_unique<Lanjutan>();
    lan->slot_base = frames_[k].slot_base;
    lan->frame.assign(frames_.begin() + static_cast<std::ptrdiff_t>(k), frames_.end());
    lan->agen = g;
    lan->dasar_async = dasar_async_;
    g->nilai = hasil;
    g->dimulai = true;

    // Tutup upvalue yang menunjuk ke rentang stack yang akan disalin (lihat
    // `VM::suspensi_async` untuk penjelasan kenapa ini wajib).
    if (!stack_.empty()) {
        const Value* dasar = &stack_[0];
        for (Upvalue* u : open_upvalues_) {
            if (u == nullptr || u->lokasi == nullptr) continue;
            const std::size_t idx = static_cast<std::size_t>(u->lokasi - dasar);
            if (idx >= lan->slot_base) u->close();
        }
    }
    if (stack_.size() > lan->slot_base) {
        lan->stack.assign(stack_.begin() + static_cast<std::ptrdiff_t>(lan->slot_base), stack_.end());
    }

    stack_.resize(lan->slot_base);
    frames_.resize(k);

    Lanjutan* ptr = lan.get();
    lanjutian_.push_back(std::move(lan));
    g->lanjutan = ptr;
    return true;
}

// ===========================================================================
// Resume
// ===========================================================================

void VM::lanjutkan_generator(GeneratorObj* g, Value kirim, bool pakai_kirim) {
    if (g == nullptr) return;
    g->kirim = kirim;
    g->kirim_pakai = pakai_kirim;

    if (g->lanjutan == nullptr) {
        // Generator sudah selesai (atau belum pernah dimulai).
        return;
    }
    Lanjutan* lan = g->lanjutan;
    g->lanjutan = nullptr;
    if (lan->dipakai) return;
    lan->dipakai = true;

    // Pulihkan di ATAS stack saat ini, bukan memaksa ukuran stack ke
    // `lan->slot_base`. Selama generator tertunda, pemanggil sudah melanjutkan
    // dan bisa saja memakan slot yang tadinya menyimpan hasil pemanggilan;
    // `resize` ke `lan->slot_base` akan mengisi slot yang sudah dibuang
    // pemanggil dengan nilai sampah. Jadi geser indeks slot semua frame
    // sebesar selisihnya. Upvalue ke rentang itu sudah ditutup saat
    // ditunda, jadi tidak ada pointer yang perlu ikut bergeser.
    const std::ptrdiff_t geser = static_cast<std::ptrdiff_t>(stack_.size()) -
                                 static_cast<std::ptrdiff_t>(lan->slot_base);
    for (const Value& v : lan->stack) stack_.push_back(v);
    for (Frame& fr : lan->frame) {
        fr.slot_base =
            static_cast<std::size_t>(static_cast<std::ptrdiff_t>(fr.slot_base) + geser);
        if (fr.target_balas != Frame::kTanpaTarget && fr.target_balas != Frame::kBuangHasil) {
            fr.target_balas =
                static_cast<std::size_t>(static_cast<std::ptrdiff_t>(fr.target_balas) + geser);
        }
    }
    // Nilai `metokake` berikutnya (atau `mboh` kalau body langsung selesai)
    // diletakkan di puncak stack; opcode setelah `YIELD` mengerapinya.
    dorong(g->nilai);
    frames_.insert(frames_.end(), lan->frame.begin(), lan->frame.end());
    dasar_async_ = lan->dasar_async;

    // Ambang loop = jumlah frame SETELAH continuation dipulihkan. Loop berhenti
    // begitu frame generator hilang -- baik karena `metokake` lagi (frame
    // dipangkas) maupun karena body selesai (frame di-pop). Penting: frame
    // pemanggil TIDAK ikut dipulihkan, dan kalau ambangnya `0` loop akan
    // melanjutkan eksekusi frame pemanggil di dalam loop bersarang ini.
    const std::size_t ambang = frames_.size();
    if (std::getenv("JAWA_DBG") != nullptr) {
        std::fprintf(stderr, "[T] lanjut: geser=%td lan=%p stack=%zu frames=%zu\n", geser,
                     static_cast<void*>(lan), stack_.size(), frames_.size());
    }
    (void)jalankan_loop(ambang);
    if (std::getenv("JAWA_DBG") != nullptr) {
        std::fprintf(stderr, "[T] lanjut selesai: status=%d nilai=%s\n", static_cast<int>(g->status),
                     std::string(rt::nilai_ke_teks(*this, g->nilai)).c_str());
    }
    dasar_async_ = lan->dasar_async;
}

// ===========================================================================
// Satu langkah: `{ nilai, selesai }`
// ===========================================================================

ObyekObj* VM::langkah_generator(GeneratorObj* g, Value kirim, bool pakai_kirim) {
    if (g == nullptr) return nullptr;
    // Dua tahap, supaya satu panggilan `langkah` = satu `metokake`:
    //   - nilai saat ini sudah dilaporkan di panggilan sebelumnya -> lanjutkan,
    //   - kalau tidak, nilai yang ada sekarang inilah jawabannya.
    if (g->lanjutan != nullptr && g->sudah_dibaca) {
        g->nilai = Value::mboh();
        lanjutkan_generator(g, kirim, pakai_kirim);
        if (galat_.ada) {
            g->status = GeneratorStatus::Gagal;
            g->galat = galat_.nilai;
            return nullptr;
        }
    }
    g->sudah_dibaca = true;
    const bool selesai = g->status == GeneratorStatus::Selesai;
    if (std::getenv("JAWA_DBG") != nullptr) {
        std::fprintf(stderr, "[T] lap: lan=%p dibaca=%d status=%d nilai=%s selesai=%d\n",
                     static_cast<void*>(g->lanjutan), static_cast<int>(g->sudah_dibaca),
                     static_cast<int>(g->status), std::string(rt::nilai_ke_teks(*this, g->nilai)).c_str(),
                     static_cast<int>(selesai));
    }
    ObyekObj* hasil = buat_obyek();
    // Saat selesai, `nilai` adalah nilai `bali` generator. `next()` melaporkan
    // `mboh` (sifat JavaScript); nilai balik tersedia lewat `g.bali`.
    hasil->set(heap_, Value::obyek(rt::buat_teks(heap_, "nilai")),
               selesai ? Value::mboh() : g->nilai);
    hasil->set(heap_, Value::obyek(rt::buat_teks(heap_, "selesai")), Value::boolean(selesai));
    return hasil;
}

}  // namespace jawa::vm
