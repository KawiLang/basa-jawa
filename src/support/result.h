// Result<T, E> — tipe kembalian front-end tanpa exception C++ yang melintasi
// batas VM.
//
// Menyediakan antarmuka yang sama dengan `std::expected` (bila tersedia) atau
// sebuah polyfill mandiri bila kompilator belum punya C++23. Semua kode
// front-end memakai tipe ini, sehingga tidak ada exception yang keluar dari
// lexer/parser/compiler/optimizer.
#pragma once

#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

namespace jawa::support {

/// Nilai atau galat. Mirip `std::expected` tetapi hanya butuh C++20.
template <class T, class E>
class [[nodiscard]] Result {
public:
    using value_type = T;
    using error_type = E;

    Result(const T& v) : storage_(std::in_place_index<0>, v) {}       // NOLINT
    Result(T&& v) : storage_(std::in_place_index<0>, std::move(v)) {}  // NOLINT
    Result(const E& e) : storage_(std::in_place_index<1>, e) {}       // NOLINT
    Result(E&& e) : storage_(std::in_place_index<1>, std::move(e)) {}  // NOLINT

    [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
    [[nodiscard]] bool has_error() const noexcept { return storage_.index() == 1; }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] T& value() & noexcept { return std::get<0>(storage_); }
    [[nodiscard]] const T& value() const& noexcept { return std::get<0>(storage_); }
    [[nodiscard]] T&& value() && noexcept { return std::get<0>(std::move(storage_)); }

    [[nodiscard]] E& error() & noexcept { return std::get<1>(storage_); }
    [[nodiscard]] const E& error() const& noexcept { return std::get<1>(storage_); }
    [[nodiscard]] E&& error() && noexcept { return std::get<1>(std::move(storage_)); }

    /// Nilai bila ada, atau `alt` bila galat.
    [[nodiscard]] T value_or(T alt) const { return has_value() ? value() : std::move(alt); }

    /// Ubah nilai sukses T menjadi U; galat diteruskan apa adanya.
    template <class U, class F>
    [[nodiscard]] Result<U, E> map(F&& fn) const& {
        if (has_value()) return Result<U, E>(std::invoke(std::forward<F>(fn), value()));
        return Result<U, E>(error());
    }

private:
    std::variant<T, E> storage_;
};

/// Result<void, E> — sukses tanpa nilai.
template <class E>
class [[nodiscard]] Result<void, E> {
public:
    using value_type = void;
    using error_type = E;

    Result() : err_{} {}
    explicit Result(const E& e) : err_{e}, has_error_{true} {}         // NOLINT
    explicit Result(E&& e) : err_{std::move(e)}, has_error_{true} {}   // NOLINT

    [[nodiscard]] bool has_value() const noexcept { return !has_error_; }
    [[nodiscard]] bool has_error() const noexcept { return has_error_; }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] const E& error() const noexcept { return err_; }

private:
    E err_{};
    bool has_error_ = false;
};

/// Bantu pembuat: `bali_galat(e)` & `bali_nilai(v)`.
template <class E>
[[nodiscard]] inline auto gagal(E e) {
    return Result<void, E>(std::move(e));
}

}  // namespace jawa::support
