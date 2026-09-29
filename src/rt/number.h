// Konversi angka <-> teks presisi penuh (setara Number.prototype.toString JS).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace jawa::rt {

/// Parse desimal (dengan titik & eksponen) secara ketat. Leading whitespace
/// tidak diterima (panggil `Angka.parse` yang melakukan trim).
[[nodiscard]] double parse_decimal_strict(std::string_view s) noexcept;

/// Parse bilangan bulat dengan radix (2/8/16). Separator `_` sudah dibuang.
[[nodiscard]] double parse_integer_radix(std::string_view digit, int radix) noexcept;

/// Parse string teks menjadi double secara ketat (RFC-like). Kembalikan true bila berhasil.
[[nodiscard]] bool parse_number_strict(std::string_view s, double& keluar) noexcept;

/// Format angka dengan format terpendek yang round-trip (ekuivalen `String(n)`).
[[nodiscard]] std::string number_to_string(double d);

/// Format ke basis tertentu (2..36), gaya ECMAScript `toString(radix)`.
[[nodiscard]] std::string number_to_radix(double d, int radix);

/// `toFixed(digit)`.
[[nodiscard]] std::string number_to_fixed(double d, int digit);

/// `toPrecision(digit)`.
[[nodiscard]] std::string number_to_precision(double d, int digit);

/// `toExponential(digit)`.
[[nodiscard]] std::string number_to_exponential(double d, int digit);

/// Hitung panjang representasi desimal terpendek (untuk algoritma Grisu).
[[nodiscard]] int shortest_decimal_length(double d) noexcept;

/// digits + eksponen desimal terpendek. Mengembalikan false bila bukan finite.
[[nodiscard]] bool shortest_decimal_digits(double d, std::string& digits, int& eksponen) noexcept;

/// Bandingkan dua teks angka (untuk klasifikasi).
enum class NumberKind : uint8_t { Finite, Infinite, Nan, MinusZero };

[[nodiscard]] NumberKind classify_number(double d) noexcept;

/// Apakah `d` bulat dan bisa direpresentasikan persis sebagai int32?
[[nodiscard]] bool exactly_int32(double d) noexcept;

}  // namespace jawa::rt
