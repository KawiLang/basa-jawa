// Posisi sumber, rentang, dan peta baris untuk stack trace presisi.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace jawa::support {

/// Lokasi di dalam satu berkas sumber.
struct SourcePos {
    std::uint32_t offset = 0;  ///< byte offset absolut dari awal berkas
    std::uint32_t baris = 1;   ///< 1-based
    std::uint32_t kolom = 1;   ///< 1-based (dalam kolom Unicode, bukan byte)
};

/// Rentang [mulai, selesai) di dalam sumber.
struct SourceRange {
    SourcePos mulai;
    SourcePos selesai;

    [[nodiscard]] bool valid() const noexcept { return mulai.offset <= selesai.offset; }
};

/// Sumber program: nama berkas + isi (UTF-8).
struct SourceFile {
    std::string_view name;      ///< path tampilan
    std::string_view code;      ///< isi berkas
    std::string_view dir;       ///< direktori untuk resolusi modul
};

/// Satu entri peta baris: offset bytecode -> baris sumber.
struct LineEntry {
    std::uint32_t offset = 0;
    std::uint32_t baris = 1;
    std::uint32_t kolom = 1;
};

/// Peta baris untuk satu chunk bytecode.
class LineMap {
public:
    void add(std::uint32_t offset, std::uint32_t baris, std::uint32_t kolom) {
        entries_.push_back(LineEntry{offset, baris, kolom});
    }

    void finalize() {
        // Urutkan stabil lalu buang entri duplikat.
        std::stable_sort(entries_.begin(), entries_.end(),
                         [](const LineEntry& a, const LineEntry& b) { return a.offset < b.offset; });
    }

    /// Baris sumber untuk offset bytecode (entri terakhir yang <= offset).
    [[nodiscard]] SourcePos lookup(std::uint32_t offset) const noexcept {
        if (entries_.empty()) return {};
        std::size_t lo = 0, hi = entries_.size();
        while (lo + 1 < hi) {
            std::size_t mid = lo + (hi - lo) / 2;
            if (entries_[mid].offset <= offset) lo = mid; else hi = mid;
        }
        return SourcePos{offset, entries_[lo].baris, entries_[lo].kolom};
    }

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    std::vector<LineEntry> entries_;
};

}  // namespace jawa::support
