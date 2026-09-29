// ===========================================================================
// Async: Janji (promise), suspensi & resume rantai `entani`
//
// Rantai `async` TIDAK memakai fiber atau stack C++ terpisah. Karena frame
// bytecode sudah berupa list datar (`VM::frames_`) dan nilai berada di stack
// datar, "menunda" sebuah rantai cukup:
//
//   1. menyalin frame Async..atas beserta nilai stack-nya ke `Lanjutan`,
//   2. menutup upvalue yang menunjuk ke rentang stack itu (supaya tidak ada
//      pointer menggantung setelah stack dipakai ulang),
//   3. memangkas `frames_` & `stack_` sehingga frame pemanggil (kode sinkron)
//      melanjutkan dari instruksi setelah `CALL`.
//
// Pemulihan melakukan kebalikannya lalu menjalankan loop bytecode lagi. Indeks
// lompatan (`Frame::ip`) sudah tersimpan, jadi eksekusi melanjutkan tepat di
// instruksi setelah `AWAIT`.
//
// Urutan loop acara meniru ECMAScript: seluruh mikrotugas didahulukan, baru
// satu timer; setiap timer yang dinyalakan boleh menjadwalkan mikrotugas baru
// yang akan habis sebelum timer berikutnya.
// ===========================================================================

#include <algorithm>
#include <utility>

#include "rt/object.h"
#include "rt/string.h"
#include "vm/vm.h"

namespace jawa::vm {

// ===========================================================================
// Janji
// ===========================================================================

rt::JanjiObj* VM::buat_janji() {
    auto* j = heap_.alokasi<rt::JanjiObj>();
    j->h.kind = rt::OK::Janji;
    j->status = rt::JanjiStatus::Menunggu;
    return j;
}

void VM::jadwal_timer(const std::uint64_t tunda_ms, rt::JanjiObj* j, Value nilai) {
    Timer t;
    t.tunda_ms = tunda_ms;
    t.urutan = urutan_timer_++;
    t.janji = j;
    t.nilai = nilai;
    pekerja_.push_back(t);
}

void VM::jadwal_mikrotugas(rt::JanjiObj* janji, Value nilai, bool ditolak, Value fungsi, Value turunan) {
    Mikrotugas t;
    t.jenis = Mikrotugas::Jenis::HandlerThen;
    t.janji = janji;
    t.nilai = nilai;  // handler menerima nilai hasil (atau alasan penolakan)
    t.ditolak = ditolak;
    t.fungsi = fungsi;
    t.turunan = turunan;
    antrean_mikrotugas_.push_back(std::move(t));
}

void VM::selesaikan_janji(rt::JanjiObj* j, Value nilai, bool ditolak) {
    if (j == nullptr || j->status != rt::JanjiStatus::Menunggu) return;
    j->hasil = nilai;
    j->status = ditolak ? rt::JanjiStatus::Gagal : rt::JanjiStatus::Slamet;

    // Rantai `async` yang menunggu Janji ini dijadwalkan sebagai mikrotugas,
    // sehingga berjalan SETELAH seluruh kode sinkron selesai.
    if (!j->lanjutan_vm.empty()) {
        Mikrotugas t;
        t.jenis = Mikrotugas::Jenis::LanjutAsync;
        t.janji = j;
        t.nilai = nilai;
        t.ditolak = ditolak;
        t.lanjutan.assign(j->lanjutan_vm.begin(), j->lanjutan_vm.end());
        j->lanjutan_vm.clear();
        antrean_mikrotugas_.push_back(std::move(t));
    }
    // Handler `.then`/`.tangkep` juga dijadwalkan sebagai mikrotugas.
    for (const rt::JanjiObj::Then& th : j->then_daftar) {
        const Value f = ditolak ? th.on_tolak : th.on_slamet;
        if (!f.is_obyek()) continue;
        Mikrotugas t;
        t.jenis = Mikrotugas::Jenis::HandlerThen;
        t.janji = j;
        t.nilai = nilai;  // handler menerima nilai hasil (atau alasan penolakan)
        t.ditolak = ditolak;
        t.fungsi = f;
        t.turunan = th.asli;
        antrean_mikrotugas_.push_back(std::move(t));
    }
    j->then_daftar.clear();
    j->tangkap_daftar.clear();
    j->intriguasan_daftar.clear();
}

// ===========================================================================
// Pemanggilan fungsi `mengko`
// ===========================================================================

Value VM::panggil_async(ClosureObj* fn, Value this_val, std::vector<Value>& args) {
    rt::JanjiObj* j = buat_janji();
    const Value jv = rt::Value::obyek(j);
    // Janji didorong TERLEBIH DAHULU supaya menjadi hasil pemanggilan pada
    // indeks yang sama seperti frame biasa. `kBuangHasil` membuat `RETURN`
    // memangkas stack ke `slot_base` tanpa mendorong nilai balik, jadi Janji ini
    // tetap menjadi hasil yang dilihat pemanggil.
    dorong(jv);
    const std::size_t sebelum = frames_.size();
    mulai_frame(fn, this_val, args, Frame::kBuangHasil);
    if (frames_.size() == sebelum) {
        stack_.pop_back();  // `mulai_frame` gagal (tumpukan penuh)
        dorong(Value::mboh());
        return Value::mboh();
    }
    Frame& f = frames_.back();
    f.janji_async = jv;
    if (dasar_async_ == kTanpaAsync) {
        f.akar_async = true;
        dasar_async_ = frames_.size() - 1;
    }
    return jv;
}

// ===========================================================================
// Suspend
// ===========================================================================

bool VM::suspensi_async(rt::JanjiObj* j) {
    if (dasar_async_ == kTanpaAsync || dasar_async_ >= frames_.size()) return false;
    const std::size_t k = dasar_async_;

    auto lan = std::make_unique<Lanjutan>();
    lan->slot_base = frames_[k].slot_base;
    lan->frame.assign(frames_.begin() + static_cast<std::ptrdiff_t>(k), frames_.end());
    lan->janji = rt::Value::obyek(j);

    // Tutup upvalue yang menunjuk ke rentang stack yang akan disalin. Setelah
    // ini sel menyimpan salinan nilainya sendiri, jadi aman ketika stack VM
    // dipakai ulang oleh kode lain.
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
    dasar_async_ = kTanpaAsync;

    Lanjutan* ptr = lan.get();
    lanjutian_.push_back(std::move(lan));
    j->lanjutan_vm.push_back(ptr);
    return true;
}

// ===========================================================================
// Resume
// ===========================================================================

void VM::lanjutkan_async(Lanjutan* lan, Value hasil, bool /*ditolak*/) {
    if (lan == nullptr || lan->dipakai) return;
    lan->dipakai = true;
    // Pulihkan di ATAS stack saat ini, bukan memaksa ukuran stack ke
    // `lan->slot_base`. Selama rantai tertunda, pemanggil sudah melanjutkan dan
    // bisa saja memakan slot yang tadinya menyimpan Janji; `resize` ke
    // `lan->slot_base` akan mengisi slot itu dengan nilai sampah. Jadi geser
    // indeks slot semua frame sebesar selisihnya. Upvalue ke rentang itu sudah
    // ditutup saat ditunda, jadi tidak ada pointer yang perlu ikut bergeser.
    const std::ptrdiff_t geser =
        static_cast<std::ptrdiff_t>(stack_.size()) - static_cast<std::ptrdiff_t>(lan->slot_base);
    for (const Value& v : lan->stack) stack_.push_back(v);
    for (Frame& fr : lan->frame) {
        fr.slot_base =
            static_cast<std::size_t>(static_cast<std::ptrdiff_t>(fr.slot_base) + geser);
        if (fr.target_balas != Frame::kTanpaTarget && fr.target_balas != Frame::kBuangHasil) {
            fr.target_balas =
                static_cast<std::size_t>(static_cast<std::ptrdiff_t>(fr.target_balas) + geser);
        }
    }
    // Nilai hasil `entani` diletakkan di puncak stack; opcode setelah `AWAIT`
    // mengerapinya sebagai hasil ekspresi. `ditolak` tidak dipakai di sini:
    // jalur penolakan ditangani `unwind_galat` pada frame pemanggil.
    dorong(hasil);
    frames_.insert(frames_.end(), lan->frame.begin(), lan->frame.end());
    const std::size_t k = frames_.size() - lan->frame.size();
    if (k < frames_.size()) {
        dasar_async_ = frames_[k].akar_async ? k : kTanpaAsync;
        for (std::size_t i = k + 1; i < frames_.size(); ++i) {
            if (frames_[i].akar_async) {
                dasar_async_ = i;
                break;
            }
        }
    }
    // Ambang loop = indeks frame AKAR rantai async (`k`), bukan jumlah frame
    // setelah pemulihan. Rantai async boleh memuat frame yang BUKAN pemanggil
    // -- saat modul ikut menjadi akar (top-level `enteni`), frame modul ikut
    // dipulihkan dan harus ikut berjalan sampai selesai. Dengan ambang
    // `frames_.size()`, loop berhenti begitu frame di atas akar hilang, dan
    // sisa rantai (termasuk frame modul) tidak pernah dijalankan lagi.
    // Kalau `k == 0`, loop berhenti saat `frames_` kosong.
    const std::size_t ambang = k;
    (void)jalankan_loop(ambang);
}

// ===========================================================================
// Mikrotugas
// ===========================================================================

bool VM::jalankan_mikrotugas() {
    if (antrean_mikrotugas_.empty()) return false;
    while (!antrean_mikrotugas_.empty()) {
        Mikrotugas t = std::move(antrean_mikrotugas_.front());
        antrean_mikrotugas_.pop_front();
        if (t.jenis == Mikrotugas::Jenis::LanjutAsync) {
            for (Lanjutan* lan : t.lanjutan) lanjutkan_async(lan, t.nilai, t.ditolak);
            if (galat_.ada) return false;
            continue;
        }
        if (!t.fungsi.is_obyek()) continue;
        std::vector<Value> args{t.nilai};
        const Value hasil = panggil(t.fungsi, rt::Value::mboh(), args);
        if (galat_.ada) return false;
        if (t.turunan.is_obyek()) {
            if (auto* tur = static_cast<rt::JanjiObj*>(t.turunan.mutable_pointer());
                tur != nullptr && tur->status == rt::JanjiStatus::Menunggu) {
                selesaikan_janji(tur, hasil, false);
            }
        }
    }
    return true;
}

bool VM::jalankan_loop_acara() {
    bool ada_karya = false;
    for (;;) {
        if (jalankan_mikrotugas()) {
            ada_karya = true;
            continue;
        }
        if (pekerja_.empty()) return ada_karya;
        std::stable_sort(pekerja_.begin(), pekerja_.end(), [](const Timer& a, const Timer& b) {
            return a.tunda_ms != b.tunda_ms ? a.tunda_ms < b.tunda_ms : a.urutan < b.urutan;
        });
        const Timer t = pekerja_.front();
        pekerja_.erase(pekerja_.begin());
        selesaikan_janji(t.janji, t.nilai, false);
        ada_karya = true;
    }
}

}  // namespace jawa::vm
