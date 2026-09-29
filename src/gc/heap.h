// Heap garbage collector: mark-and-sweep presisi, non-moving.
//
// Tahap 1 (implementasi ini): stop-the-world mark & sweep yang benar.
// Tahap 2 (opsional): incremental + write barrier.
//
// Root:
//   * stack nilai VM  [stack_base, sp)
//   * frame & upvalue terbuka
//   * global/modul
//   * handle native (HandleScope RAII)
//   * antrian microtask/timer
//   * tabel intern (referensi LEMAH: dibersihkan saat sweep)
#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <functional>
#include <cstdint>
#include <vector>

#include "rt/object.h"
#include "rt/value.h"

namespace jawa::gc {

using rt::Value;
using rt::Obj;

class Heap;
struct RootVisitor;

/// Handle aman terhadap GC: membungkus Value & mendaftarkannya sebagai root.
class Handle {
public:
    Handle() = default;
    explicit Handle(Value v);
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& o) noexcept : v_(o.v_), ptr_(o.ptr_) { o.ptr_ = nullptr; }
    Handle& operator=(Handle&& o) noexcept;

    [[nodiscard]] Value get() const noexcept { return *ptr_; }
    void set(Value v) const noexcept { *ptr_ = v; }
    operator Value() const noexcept { return *ptr_; }  // NOLINT

private:
    Value v_{};
    Value* ptr_ = nullptr;
};

/// RAII: kumpulan handle native. Semua handle dibersihkan saat scope keluar.
class HandleScope {
public:
    explicit HandleScope(Heap& h);
    HandleScope(const HandleScope&) = delete;
    HandleScope& operator=(const HandleScope&) = delete;
    ~HandleScope();

    /// Alokasi slot handle yang menunjuk nilai saat ini.
    Value* slot(Value v);

private:
    Heap& heap_;
    std::size_t basis_;
};

/// Statistics GC.
struct GcStats {
    std::size_t total_alokasi = 0;
    std::size_t jumlah_objek = 0;
    std::size_t bytes_aktif = 0;
    std::size_t bytes_ditandai = 0;
    std::size_t koleksi_dipicu = 0;
    std::size_t gc_koleksi = 0;
    double ms_koleksi = 0.0;
};

class Heap {
public:
    explicit Heap(bool stress = false, std::size_t maks_mb = 0);
    ~Heap();
    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;

    // ---------------------------------------------------------- alokasi
    /// Alokasikan objek; galat memori ditangani di batas terluar.
    template <class T>
    T* alokasi() {
        void* mem = raw_allocate(sizeof(T));
        T* obj = new (mem) T();
        daftarkan(obj, sizeof(T));
        return obj;
    }

    template <class T, class... Args>
    T* alokasi_baru(Args&&... args) {
        void* mem = raw_allocate(sizeof(T));
        T* obj = new (mem) T(std::forward<Args>(args)...);
        daftarkan(obj, sizeof(T));
        return obj;
    }

    /// Tambahkan byte yang tidak tercakup `sizeof(T)` (mis. isi `std::string`
    /// milik `TeksObj`) ke akuntansi memori. Tanpa ini `--maks-memori` hanya
    /// melihat sizeof objek dan hampir tidak pernah terpicu.
    void tambah_bytes(std::size_t bytes) {
        stat_.bytes_aktif += bytes;
        periksa_batas_memori();
    }

    void* raw_allocate(std::size_t bytes);
    void raw_free(void* mem, std::size_t bytes) noexcept;

    /// Keluar dari proses bila `--maks-memori` terlampaui. Dipanggil setiap kali
    /// akuntansi memori bertambah (alokasi objek maupun pertumbuhan buffer).
    void periksa_batas_memori() const;

    // -------------------------------------------------------------- roots
    /// Daftarkan callback root (dipanggil tiap GC).
    void tambah_root_visitor(std::function<void(RootVisitor&)> fn);

    /// Tambah satu nilai sementara sebagai root (pointer native).
    void akar(Value v);

    /// Akar sementara untuk objek yang dibuat DI LUAR frame VM — misalnya
    /// konstanta string hasil kompilasi, yang belum punya frame untuk menopausekannya.
    /// Dibersihkan dengan `bersihkan_akar_sementara()` setelah objek yang memegangnya
    /// (mis. `FungsiObj`) sudah jadi root.
    void akar_sementara(Value v) { akar_sementara_.push_back(v); }
    void bersihkan_akar_sementara() { akar_sementara_.clear(); }

    /// Slot handle RAII; alamat stabil karena memakai `std::deque`.
    Value* tambah_handle(Value v);

    /// Dipakai HandleScope.
    void buka_scope(HandleScope* s);
    void tutup_scope(HandleScope* s);

    // ----------------------------------------------------------- koleksi
    void mungkin_koleksi();
    void koleksi_full();
    void gc_stress_aktif(bool aktif) noexcept { stress_ = aktif; }
    [[nodiscard]] bool gc_stress() const noexcept { return stress_; }

    [[nodiscard]] const GcStats& statistik() const noexcept { return stat_; }
    [[nodiscard]] std::size_t maks_bytes() const noexcept { return maks_bytes_; }
    [[nodiscard]] std::size_t bytes_aktif() const noexcept { return stat_.bytes_aktif; }
    [[nodiscard]] std::size_t ambang_bytes() const noexcept { return std::max<std::size_t>(amb_kb_ * 1024u, 64u * 1024u); }
    void set_ambang(std::size_t bytes) noexcept { amb_kb_ = bytes / 1024u; }
    [[nodiscard]] bool log_gc() const noexcept { return log_gc_; }
    void set_log_gc(bool v) noexcept { log_gc_ = v; }

private:
    friend class ::jawa::gc::Handle;
    friend class ::jawa::gc::HandleScope;

    struct Worklist;

    void daftarkan(Obj* o, std::size_t bytes);
    void tandai_roots();
    void tandai_objek(Obj* o);
    void tandai_objek_objek(Value v);
    void tandai_anak(Obj* o, Worklist& wl);
    void sweep();
    void hitung_ambang();

    /// Root tambahan: nilai sementara dari kode native.
    struct RootExtra {
        Value* ptr = nullptr;
        Value cadangan{};
    };
    std::deque<Value> akar_sementara_;

    std::vector<Obj*> semua_;
    std::vector<Obj*> abu_;  // untuk incremental (Fase 11)
    std::vector<std::function<void(RootVisitor&)>> root_visitor_;
    std::vector<RootExtra> akar_;
    /// Arena handle: `std::deque` supaya alamat stabil saat `push_back`.
    std::deque<Value> handle_arena_;
    /// Karantina alokasi: objek yang BARU dialokasikan belum sempat di-root
    /// pemanggil (mis. `alokasi_baru<TeksObj>()` lalu mengisi field-nya). Kalau
    /// `sweep()` langsung mengambilnya, terjadi use-after-free saat
    /// `--gc-stress` aktif. Solusinya: N alokasi terakhir tidak pernah disapu
    /// sampai-aged-N.
    static constexpr std::size_t kKarantina = 64;
    std::deque<Obj*> karantina_;

    bool stress_ = false;
    bool log_gc_ = false;
    std::size_t maks_bytes_ = 0;
    std::size_t amb_kb_ = 256;
    GcStats stat_{};
};

/// Visitor root internal (dipakai VM & modul).
struct RootVisitor {
    Heap* heap = nullptr;
    /// Dipasang `Heap::tandai_roots`; `rooted(v)` akan menandai objek di `v`.
    std::function<void(Value)> visit;

    void rooted(Value v) const {
        if (visit) visit(v);
    }
    void rooted_range(Value* mulai, Value* akhir) const {
        if (visit == nullptr) return;
        for (Value* p = mulai; p < akhir; ++p) visit(*p);
    }
};

}  // namespace jawa::gc
