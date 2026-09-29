// Arena alokasi monotonik untuk AST dan data front-end.
//
// TANPA `new`/`delete` mentah: arena dimiliki oleh stack (RAII) dan
// membebaskan blok sekaligus. Semua objek AST dibuat lewat `ctx.alloc<T>()`.
//
// CATATAN PENTING SOAL DESTRUKTOR
// ------------------------------
// `Arena::create<T>()` melakukan placement-new, jadi bila `T` memiliki anggota
// `std::vector` / `std::string` (hampir semua node AST), buffer heap-nya harus
// dilepas oleh destructor. `reset_all()` karena itu memanggil destructor
// setiap objek yang tidak trivially-destructible dalam urutan terbalik
// (LIKW, seperti `std::vector` sendiri) sebelum membebaskan blok mentah.
// Melewatkan langkah ini membuat LeakSanitizer melaporkan kebocoran.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace jawa::support {

/// Arena bump-pointer dengan blok bertingkat.
class Arena {
public:
    explicit Arena(std::size_t first_block = 64u * 1024u) { push_block(first_block); }
    ~Arena() { reset_all(); }

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&& o) noexcept { blocks_ = std::move(o.blocks_); destructor_ = std::move(o.destructor_); o.blocks_.clear(); o.destructor_.clear(); }
    Arena& operator=(Arena&& o) noexcept {
        if (this != &o) {
            reset_all();
            blocks_ = std::move(o.blocks_);
            destructor_ = std::move(o.destructor_);
            o.blocks_.clear();
            o.destructor_.clear();
        }
        return *this;
    }

    /// Alokasikan `bytes` byte dengan perataan `align`.
    [[nodiscard]] void* allocate(std::size_t bytes, std::size_t align) noexcept {
        if (bytes == 0) bytes = 1;
        if (align < alignof(std::max_align_t)) align = alignof(std::max_align_t);
        Block& b = blocks_.back();
        const std::size_t aligned = (b.used + align - 1) & ~(align - 1);
        if (aligned + bytes > b.capacity) {
            std::size_t want = bytes + align;
            if (want < bytes * 2) want = bytes * 2;
            if (want < 1u << 16) want = 1u << 16;
            push_block(want);
            // Blok baru masih kosong: offset selalu 0 (bukan `aligned` milik
            // blok lama — memakai offset lama menulis lewat akhir blok baru).
            void* p = blocks_.back().data;
            blocks_.back().used = bytes;
            bytes_used_ += bytes;
            return p;
        }
        void* p = blocks_.back().data + aligned;
        blocks_.back().used = aligned + bytes;
        bytes_used_ += bytes;
        return p;
    }

    /// Konstruksi objek di arena dan (bila perlu) daftarkan destrutornya.
    template <class T, class... Args>
    [[nodiscard]] T* create(Args&&... args) {
        static_assert(!std::is_array_v<T>);
        void* mem = allocate(sizeof(T), alignof(T));
        T* obj = new (mem) T(std::forward<Args>(args)...);
        if constexpr (!std::is_trivially_destructible_v<T>) {
            destructor_.push_back(Destructor{obj, [](void* p) { static_cast<T*>(p)->~T(); }});
        }
        return obj;
    }

    /// Total byte yang sedang dipakai.
    [[nodiscard]] std::size_t bytes_used() const noexcept { return bytes_used_; }

    /// Jalankan destructor semua objek (urutan terbalik) lalu bebaskan blok.
    void reset_all() noexcept {
        for (std::size_t i = destructor_.size(); i > 0; --i) {
            destructor_[i - 1].fn(destructor_[i - 1].obj);
        }
        destructor_.clear();
        for (Block& b : blocks_) ::operator delete(b.data);
        blocks_.clear();
        bytes_used_ = 0;
    }

    /// Jumlah objek yang destructor-nya terdaftar (diagnostik).
    [[nodiscard]] std::size_t jumlah_objek() const noexcept { return destructor_.size() + (blocks_.empty() ? 0u : 0u); }
    [[nodiscard]] std::size_t jumlah_blok() const noexcept { return blocks_.size(); }

private:
    struct Block {
        char* data = nullptr;
        std::size_t capacity = 0;
        std::size_t used = 0;
    };

    struct Destructor {
        void* obj;
        void (*fn)(void*);
    };

    void push_block(std::size_t capacity) {
        // Semua tipe kita membutuhkan perataan paling banyak `max_align_t`.
        void* raw = ::operator new(capacity);
        blocks_.push_back(Block{static_cast<char*>(raw), capacity, 0});
    }

    std::vector<Block> blocks_;
    std::vector<Destructor> destructor_;
    std::size_t bytes_used_ = 0;
};

/// Scope arena: buat arena anak, shut down saat keluar scope.
class ArenaScope {
public:
    explicit ArenaScope(Arena& parent) noexcept : parent_(parent) {}
    ~ArenaScope() { parent_.reset_all(); }
    ArenaScope(const ArenaScope&) = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;

    [[nodiscard]] void* allocate(std::size_t bytes, std::size_t align) noexcept { return parent_.allocate(bytes, align); }
    template <class T, class... Args>
    [[nodiscard]] T* create(Args&&... args) {
        return parent_.create<T>(std::forward<Args>(args)...);
    }
    [[nodiscard]] std::size_t bytes_used() const noexcept { return parent_.bytes_used(); }

private:
    Arena& parent_;
};

}  // namespace jawa::support
