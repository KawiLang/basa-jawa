#include "number.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <limits>

#if defined(__has_include)
#if __has_include(<charconv>)
#include <charconv>
#endif
#endif

namespace jawa::rt {
namespace {

constexpr double kMantisaMaks = 9007199254740992.0;  // 2^53

}  // namespace

// ---------------------------------------------------------------------------
// Parse
// ---------------------------------------------------------------------------

bool parse_number_strict(std::string_view s, double& keluar) noexcept {
    if (s.empty()) return false;
    // Izinkan spasi putih di sekeliling (dipanggil dari Angka.parse).
    std::size_t a = 0;
    std::size_t b = s.size();
    auto spasi = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
    while (a < b && spasi(s[a])) ++a;
    while (b > a && spasi(s[b - 1])) --b;
    if (a >= b) return false;
    const std::string_view t = s.substr(a, b - a);

    // Buang infinity/NaNForms (tidak valid untuk angka).
    std::size_t i = 0;
    bool negatif = false;
    if (t[i] == '+' || t[i] == '-') {
        negatif = (t[i] == '-');
        ++i;
    }
    if (t.substr(i) == "Infinity") {
        keluar = negatif ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
        return true;
    }
    if (t.substr(i) == "NaN") {
        keluar = std::numeric_limits<double>::quiet_NaN();
        return true;
    }
    if (i >= t.size()) return false;

    double d = 0.0;
    const char* p = t.data();
    const char* akhir = t.data() + t.size();
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    auto res = std::from_chars(p, akhir, d, std::chars_format::general);
    if (res.ec == std::errc() && res.ptr == akhir) {
        keluar = d;
        return true;
    }
    return false;
#else
    // Fallback: strtod (dengan validasi ketat tambahan).
    // from_chars tidak tersedia: pakai strtod lalu verifikasi seluruh karakter dikonsumsi.
    char buf[512];
    if (t.size() >= sizeof(buf)) {
        // sangat panjang: pakai strtod langsung pada string_view-backed buffer sementara
        std::string tmp(t);
        char* end = nullptr;
        d = std::strtod(tmp.c_str(), &end);
        if (end != tmp.c_str() + tmp.size()) return false;
        keluar = d;
        return true;
    }
    std::memcpy(buf, t.data(), t.size());
    buf[t.size()] = '\0';
    char* end = nullptr;
    errno = 0;
    d = std::strtod(buf, &end);
    if (end != buf + t.size()) return false;
    // verifikasi format ketat: hanya [+-]?digits[.digits][eE[+-]digits]
    std::size_t k = 0;
    if (k < t.size() && (t[k] == '+' || t[k] == '-')) ++k;
    bool saw_digit = false;
    while (k < t.size() && t[k] >= '0' && t[k] <= '9') { ++k; saw_digit = true; }
    if (k < t.size() && t[k] == '.') {
        ++k;
        while (k < t.size() && t[k] >= '0' && t[k] <= '9') { ++k; saw_digit = true; }
    }
    if (!saw_digit) return false;
    if (k < t.size() && (t[k] == 'e' || t[k] == 'E')) {
        ++k;
        if (k < t.size() && (t[k] == '+' || t[k] == '-')) ++k;
        bool saw_e = false;
        while (k < t.size() && t[k] >= '0' && t[k] <= '9') { ++k; saw_e = true; }
        if (!saw_e) return false;
    }
    if (k != t.size()) return false;
    keluar = d;
    return true;
#endif
}

double parse_decimal_strict(std::string_view s) noexcept {
    double d = 0.0;
    if (parse_number_strict(s, d)) return d;
    return std::numeric_limits<double>::quiet_NaN();
}

double parse_integer_radix(std::string_view digit, int radix) noexcept {
    double hasil = 0.0;
    for (char c : digit) {
        int v = -1;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        if (v < 0 || v >= radix) break;
        hasil = hasil * radix + v;
    }
    return hasil;
}

// ---------------------------------------------------------------------------
// Format
// ---------------------------------------------------------------------------

namespace {

/// Tulis digit desimal terpendek yang round-trip.
struct Shortest {
    std::string digit;  ///< digit significant tanpa titik desimal, mis. "1", "123", "1234567890123456789"
    int eksponen = 0;   ///< nilai = 0.digit * 10^eksponen
};

/// fallback portable: gunakan snprintf dengan presisi naik sampai round-trip.
Shortest shortest_via_snprintf(double d) {
    Shortest s;
    char buf[64] = {};
    for (int p = 1; p <= 17; ++p) {
        const int n = std::snprintf(buf, sizeof(buf), "%.*e", p - 1, d);
        if (n <= 0) break;
        if (std::strtod(buf, nullptr) == d) break;
    }
    // Parse "d.ddddde+XX" dengan indeks langsung (tanpa string_view) supaya
    // GCC 12 tidak melaporkan false positive -Wrestrict.
    std::string digit;
    digit.reserve(16);
    std::size_t i = 0;
    if (i < 63 && buf[i] == '-') ++i;
    int eksp = 0;
    bool saw_e = false;
    for (; i < 63; ++i) {
        const char c = buf[i];
        if (c == 'e' || c == 'E') {
            saw_e = true;
            break;
        }
        if (c >= '0' && c <= '9') digit.push_back(c);
    }
    if (saw_e && i < 63) {
        ++i;
        int tanda = 1;
        for (; i < 63 && buf[i] != '\0'; ++i) {
            if (buf[i] == '-') {
                tanda = -1;
            } else if (buf[i] == '+') {
                tanda = 1;
            } else if (buf[i] >= '0' && buf[i] <= '9') {
                eksp = eksp * 10 + (buf[i] - '0');
            }
        }
        eksp *= tanda;
    }
    // Buang digit nol di belakang (sisakan minimal satu digit).
    while (digit.size() > 1 && digit.back() == '0') digit.pop_back();
    s.digit = digit;
    s.eksponen = eksp + 1;
    return s;
}

}  // namespace

bool shortest_decimal_digits(double d, std::string& digit, int& eksponen) noexcept {
    if (!std::isfinite(d)) return false;
    if (d == 0.0) {
        // push_back (bukan operator=(const char*)) untuk menghindari false
        // positive -Wrestrict pada GCC 12.
        digit.clear();
        digit.push_back('0');
        eksponen = 1;
        return true;
    }
    const Shortest s = shortest_via_snprintf(std::abs(d));
    digit = s.digit;
    eksponen = s.eksponen;
    return true;
}

int shortest_decimal_length(double d) noexcept {
    std::string digit;
    int e = 0;
    if (!shortest_decimal_digits(d, digit, e)) return 0;
    return static_cast<int>(digit.size());
}

std::string number_to_string(double d) {
    if (std::isnan(d)) return "DuduAngka";
    if (std::isinf(d)) return d > 0 ? "Tak_Wates" : "-Tak_Wates";
    if (d == 0.0) return "0";

    std::string digit;
    int e = 0;
    (void)shortest_decimal_digits(d, digit, e);
    const int n = static_cast<int>(digit.size());
    std::string out;
    const bool negatif = std::signbit(d);

    if (e > 0 && e <= 21) {
        // Aturan ECMAScript Number::toString: `n` = posisi titik desimal relatif
        // terhadap string digit, `k` = jumlah digit.
        //   n >= k : digit + (n-k) nol            (1200 -> "1200")
        //   n <  k : n digit pertama + "." + sisa  (1.5  -> "1.5")
        if (n <= e) {
            out = digit;
            out.append(static_cast<std::size_t>(e - n), '0');
        } else {
            out = digit.substr(0, static_cast<std::size_t>(e));
            out.push_back('.');
            out += digit.substr(static_cast<std::size_t>(e));
        }
    } else if (e <= 0 && e > -6) {
        out = "0.";
        out.append(static_cast<std::size_t>(-e), '0');
        out += digit;
    } else {
        // notasi eksponensial
        out.push_back(digit[0]);
        if (n > 1) {
            out.push_back('.');
            out += digit.substr(1);
        }
        out.push_back('e');
        out.push_back(e - 1 >= 0 ? '+' : '-');
        const int ae = std::abs(e - 1);
        out += std::to_string(ae);
    }
    return negatif ? "-" + out : out;
}

std::string number_to_radix(double d, int radix) {
    if (radix < 2 || radix > 36) radix = 10;
    if (std::isnan(d)) return "DuduAngka";
    if (std::isinf(d)) return d > 0 ? "Tak_Wates" : "-Tak_Wates";
    if (d == 0.0) return "0";

    const bool negatif = d < 0;
    if (negatif) d = -d;

    // bagian bulat
    double bulat = std::floor(d);
    double frac = d - bulat;

    std::string hasil;
    if (bulat == 0.0) {
        hasil = "0";
    } else {
        // konversi bagian bulat dengan pembagian (dbl mantissa bisa > 2^53)
        std::array<char, 1100> tmp{};
        std::size_t idx = 0;
        while (bulat >= 1.0 && idx < tmp.size()) {
            const double q = std::floor(bulat / radix);
            const int r = static_cast<int>(bulat - q * radix);
            tmp[idx++] = static_cast<char>(r < 10 ? ('0' + r) : ('a' + r - 10));
            bulat = q;
        }
        for (std::size_t k = idx; k > 0; --k) hasil.push_back(tmp[k - 1]);
    }

    if (frac > 0.0) {
        hasil.push_back('.');
        int guard = 0;
        while (frac > 0.0 && guard < 1100) {
            frac *= radix;
            const int digit = static_cast<int>(std::floor(frac));
            hasil.push_back(static_cast<char>(digit < 10 ? ('0' + digit) : ('a' + digit - 10)));
            frac -= digit;
            ++guard;
        }
    }
    return negatif ? "-" + hasil : hasil;
}

std::string number_to_fixed(double d, int digit) {
    if (std::isnan(d)) return "DuduAngka";
    if (std::isinf(d)) return d > 0 ? "Tak_Wates" : "-Tak_Wates";
    if (digit < 0) digit = 0;
    if (digit > 100) digit = 100;
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%.*f", digit, d);
    return std::string(buf);
}

std::string number_to_precision(double d, int digit) {
    if (std::isnan(d)) return "DuduAngka";
    if (std::isinf(d)) return d > 0 ? "Tak_Wates" : "-Tak_Wates";
    if (digit < 1) digit = 1;
    if (digit > 100) digit = 100;
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%.*g", digit, d);
    return std::string(buf);
}

std::string number_to_exponential(double d, int digit) {
    if (std::isnan(d)) return "DuduAngka";
    if (std::isinf(d)) return d > 0 ? "Tak_Wates" : "-Tak_Wates";
    if (digit < 0) digit = 0;
    if (digit > 100) digit = 100;
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%.*e", digit, d);
    std::string s(buf);
    // snprintf menghasilkan "1.500e+02"; JS menghasilkan "1.5e+2"
    // normalisasi: buang nol di belakang pada mantisa
    std::size_t epos = s.find('e');
    if (epos == std::string::npos) return s;
    std::string mant = s.substr(0, epos);
    std::string eks = s.substr(epos + 1);
    if (mant.find('.') != std::string::npos) {
        while (!mant.empty() && mant.back() == '0') mant.pop_back();
        if (!mant.empty() && mant.back() == '.') mant.pop_back();
    }
    char sign = eks[0];
    std::size_t z = 0;
    while (z + 1 < eks.size() && eks[z + 1] == '0') ++z;
    return mant + "e" + std::string(1, sign) + eks.substr(z + 1);
}

NumberKind classify_number(double d) noexcept {
    if (std::isnan(d)) return NumberKind::Nan;
    if (std::isinf(d)) return NumberKind::Infinite;
    if (d == 0.0) return NumberKind::MinusZero;
    return NumberKind::Finite;
}

bool exactly_int32(double d) noexcept {
    if (!std::isfinite(d)) return false;
    if (d != std::floor(d)) return false;
    return d >= -2147483648.0 && d <= 2147483647.0;
}

}  // namespace jawa::rt
