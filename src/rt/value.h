// Value Basa Jawa — nilai 8-byte dengan NaN-boxing.
//
// ---------------------------------------------------------------------------
// DESAIN (lihat docs/object-model.md)
// ---------------------------------------------------------------------------
// Semua nilai bahasa disimpan dalam 64 bit. Angka `double` occupy seluruh 64
// bit. Nilai selain double memakai *pola bit* di dalam domain NaN:
//
//   bit 63     : 1        (penanda: seluruh nilai boxed bertanda negatif)
//   bit 62..52 : 0x7FF    (eksponen maksimum IEEE-754)
//   bit 51..48 : TAG      (jenis nilai boxed, 4 bit = 16 jenis)
//   bit 47..0  : muatan   (nilai int32 / pointer 48-bit / 0)
//
// Kenapa memakai bit 63 sebagai penanda? Karena hanya dengan begitu blok tag
// tidak bertabrakan dengan `double`: suatu `double` dianggap boxed HANYA bila
// bit tanda = 1 DAN eksponennya 0x7FF, yaitu persis pola "NaN negatif".
// Semua `double` lain (termasuk +Inf dan NaN positif) aman. Agar tidak ada
// tabrakan, `Value::number()` menormalkan SETIAP NaN (apa pun bit tandanya)
// menjadi tag `NaNAngka`, sehingga runtime tidak pernah menyimpan NaN mentah.
// Empat tag sisanya (8..15) dibiarkan untuk pengembangan berikutnya.
//
// Catatan LA57: muatan 48 bit hanya aman bila alamat user-space < 2^48. Pada
// kernel dengan LA57 aktif, definisikan JAWA_NO_NAN_BOX (mode 16 byte).
//
// ---------------------------------------------------------------------------
// MODE CADANGAN
// ---------------------------------------------------------------------------
// Bila platform tidak mendukung asumsi pointer 48-bit, definisikan
// JAWA_NO_NAN_BOX: API Value tetap sama persis tetapi lebarnya 16 byte
// (tagged union). Seluruh project dikompilasi dengan satu mode yang sama.
#pragma once

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace jawa::rt {

/// Tipe nilai Basa Jawa.
enum class Tag : uint8_t {
    Mboh = 0,    ///< undefined
    Kosong = 1,  ///< null
    Boole = 2,   ///< boolean (muatan 0/1)
    Obyek = 3,   ///< pointer objek
    BigInt = 4,  ///< pointer objek BigInt
    Simbol = 5,  ///< pointer objek Simbol
    /// NaN sebagai nilai angka (bukan sekadar "mboh"). Dipakai `DuduAngka`.
    /// Tanpa tag khusus ini, NaN akan nabrak pola `Mboh` (bit 51..48 = 0).
    NaNAngka = 6,
    // Tag 7 dipakai untuk "angka bulat 32-bit" pada mode boxed.
    Int32 = 7,
};

/// Pointee tag untuk bigint/simbol juga berupa objek pada heap yang sama.
inline constexpr int kPointerBits = 48;

#ifndef JAWA_NO_NAN_BOX

class Value {
public:
    using raw_t = std::uint64_t;

    /// Bit tanda (penanda nilai boxed).
    static constexpr raw_t kSignBit = 0x8000'0000'0000'0000ull;
    /// Eksponen maksimum tanpa bit tanda.
    static constexpr raw_t kExpMaks = 0x7FF0'0000'0000'0000ull;
    /// Basis nilai boxed: bit tanda + eksponen maksimum.
    static constexpr raw_t kBoxBase = kSignBit | kExpMaks;
    /// Medan tag: bit 51..48 (4 bit = 16 jenis nilai boxed).
    static constexpr raw_t kTagMask = 0x000F'0000'0000'0000ull;
    /// Muatan: bit 47..0 (48 bit; cukup untuk pointer user-space x86-64).
    static constexpr raw_t kPayloadMask = 0x0000'FFFF'FFFF'FFFFull;

    static constexpr Value from_bits(raw_t b) noexcept { return Value{b, PrivTag{}}; }
    [[nodiscard]] constexpr raw_t raw() const noexcept { return bits_; }

    // ------------------------------------------------------- konstruksi
    /// Nilai default adalah `mboh` (NaN bawaan dengan muatan 0).
    constexpr Value() noexcept : bits_(kBoxBase) {}  // = boxed(Mboh, 0)

    static constexpr Value mboh() noexcept { return boxed(Tag::Mboh, 0); }
    static constexpr Value kosong() noexcept { return boxed(Tag::Kosong, 0); }
    static constexpr Value boolean(bool b) noexcept { return boxed(Tag::Boole, b ? 1u : 0u); }
    static constexpr Value special(Tag t) noexcept { return boxed(t, 0); }
    static constexpr Value angka_int32(std::int32_t i) noexcept { return boxed(Tag::Int32, raw_t(std::uint32_t(i))); }
    /// Double biasa. NaN dinormalkan ke tag `NaNAngka` supaya tidak membaca
    /// diri sendiri sebagai `Mboh` (pola boxed dengan tag 0).
    static Value number(double d) noexcept {
        if (std::isnan(d)) return boxed(Tag::NaNAngka, 0);
        return from_bits(bits_of_double(d));
    }
    static Value bigint(const void* p) noexcept { return boxed_ptr(Tag::BigInt, p); }
    static Value simbol(const void* p) noexcept { return boxed_ptr(Tag::Simbol, p); }
    static Value obyek(const void* p) noexcept { return boxed_ptr(Tag::Obyek, p); }

    static constexpr raw_t bits_of_double(double d) noexcept {
        raw_t b;
        std::memcpy(&b, &d, sizeof(b));
        return b;
    }
    static constexpr double double_of_bits(raw_t b) noexcept {
        double d;
        std::memcpy(&d, &b, sizeof(d));
        return d;
    }

    // ---------------------------------------------------------- predicate
    /// Tag yang tersirat untuk pola boxed (bit 50..48). Tidak valid untuk double.
    [[nodiscard]] constexpr Tag tag() const noexcept { return Tag((bits_ & kTagMask) >> 48); }

    /// NaN sebagai nilai angka.
    [[nodiscard]] bool is_nan_angka() const noexcept { return is_tag(Tag::NaNAngka); }

    /// True bila ini `double` biasa. Nilai boxed dikenali dari bit tanda 1 +
    /// eksponen 0x7FF; `number()` menormalkan NaN ke `NaNAngka` sehingga pola
    /// ini tidak pernah aberta oleh NaN hardware.
    [[nodiscard]] constexpr bool is_plain_double() const noexcept {
        return (bits_ & kSignBit) == 0 || (bits_ & kExpMaks) != kExpMaks;
    }

    /// True bila nilainya adalah pola boxed (bukan double biasa).
    [[nodiscard]] constexpr bool is_boxed() const noexcept { return !is_plain_double(); }

    [[nodiscard]] constexpr bool is_tag(Tag t) const noexcept {
        return !is_plain_double() && (bits_ & kTagMask) == (raw_t(t) << 48);
    }

    [[nodiscard]] bool is_mboh() const noexcept { return is_tag(Tag::Mboh); }
    [[nodiscard]] bool is_kosong() const noexcept { return is_tag(Tag::Kosong); }
    [[nodiscard]] bool is_boole() const noexcept { return is_tag(Tag::Boole); }
    [[nodiscard]] bool is_int32() const noexcept { return is_tag(Tag::Int32); }
    [[nodiscard]] bool is_obyek() const noexcept { return is_tag(Tag::Obyek); }
    [[nodiscard]] bool is_bigint() const noexcept { return is_tag(Tag::BigInt); }
    [[nodiscard]] bool is_simbol() const noexcept { return is_tag(Tag::Simbol); }
    /// "Angka" dalam pengertian bahasa: double (termasuk int32 ter-promote).
    [[nodiscard]] bool is_number() const noexcept { return is_plain_double(); }
    [[nodiscard]] bool is_angka() const noexcept { return is_plain_double() || is_int32() || is_nan_angka(); }

    [[nodiscard]] bool is_bener() const noexcept { return is_boole() && (bits_ & 1u) != 0u; }
    [[nodiscard]] bool bool_value() const noexcept { return is_boole() && (bits_ & 1u) != 0u; }

    // ------------------------------------------------------------ accessor
    /// Double "apa adanya" (tanpa konversi int32).
    [[nodiscard]] double as_double_pure() const noexcept { return double_of_bits(bits_); }
    /// Double sebagai nilai angka (int32 di-promote).
    [[nodiscard]] double as_number() const noexcept {
        if (is_int32()) return static_cast<double>(std::int32_t(bits_ & 0xFFFF'FFFFu));
        if (is_nan_angka()) return std::numeric_limits<double>::quiet_NaN();
        return double_of_bits(bits_);
    }
    [[nodiscard]] double as_double() const noexcept { return as_number(); }
    [[nodiscard]] std::int32_t as_i32() const noexcept { return std::int32_t(bits_ & 0xFFFF'FFFFu); }

    /// Pointer untuk Obyek / BigInt / Simbol.
    [[nodiscard]] const void* pointer() const noexcept { return reinterpret_cast<const void*>(std::uintptr_t(bits_ & kPayloadMask)); }
    [[nodiscard]] void* mutable_pointer() const noexcept {
        return reinterpret_cast<void*>(std::uintptr_t(bits_ & kPayloadMask));
    }
    template <class T>
    [[nodiscard]] const T* as() const noexcept { return static_cast<const T*>(pointer()); }
    template <class T>
    [[nodiscard]] T* as_mut() const noexcept { return static_cast<T*>(const_cast<void*>(pointer())); }

    // ------------------------------------------------------------- operator
    friend constexpr bool operator==(Value a, Value b) noexcept { return a.bits_ == b.bits_; }
    friend constexpr bool operator!=(Value a, Value b) noexcept { return a.bits_ != b.bits_; }

    /// Kunci stabil untuk unordered_map: semua NaN dinormalkan menjadi satu nilai.
    /// Kunci stabil untuk tabel hash: seluruh NaN dinormalkan ke satu nilai.
    [[nodiscard]] raw_t key() const noexcept {
        if (is_plain_double() && std::isnan(double_of_bits(bits_))) {
            return kBoxBase | (raw_t(Tag::NaNAngka) << 48);
        }
        return bits_;
    }

    /// Nilai "kosong" untuk inisialisasi array/vektor.
    static constexpr Value nol() noexcept { return boxed(Tag::Mboh, 0); }

private:
    struct PrivTag {};
    constexpr Value(raw_t b, PrivTag) noexcept : bits_(b) {}
    static constexpr Value boxed(Tag t, raw_t payload) noexcept {
        return Value{kBoxBase | (raw_t(t) << 48) | (payload & kPayloadMask), PrivTag{}};
    }
    static Value boxed_ptr(Tag t, const void* p) noexcept {
        const std::uintptr_t up = reinterpret_cast<std::uintptr_t>(p);
        // Pointer harus muat 48 bit pada mode NaN-boxing (lihat catatan LA57).
        return Value{kBoxBase | (raw_t(t) << 48) | (raw_t(up) & kPayloadMask), PrivTag{}};
    }
    /// Helper agar `is_boxed()` mudah dibaca.
    raw_t bits_;
};

static_assert(sizeof(Value) == 8, "Value harus berukuran 8 byte");
static_assert(std::is_trivially_copyable_v<Value>, "Value harus trivially copyable");

#else  // ======================= MODE TAG-UNION 16 BYTE =======================

class Value {
public:
    enum class Kind : uint8_t { Mboh, Kosong, Boole, Number, Int32, Obyek, BigInt, Simbol, NaNAngka };

    Value() noexcept : kind_(Kind::Mboh), num_(0.0) {}
    static Value mboh() noexcept { return Value(); }
    static Value kosong() noexcept { Value v; v.kind_ = Kind::Kosong; return v; }
    static Value boolean(bool b) noexcept { Value v; v.kind_ = Kind::Boole; v.num_ = b ? 1.0 : 0.0; return v; }
    static Value number(double d) noexcept {
        // NaN disimpan sebagai Kind::NaNAngka supaya tidak tertukar dengan
        // `Mboh` (lihat catatan NaN-boxing di bagian atas).
        if (std::isnan(d)) { Value v; v.kind_ = Kind::NaNAngka; v.num_ = 0.0; return v; }
        Value v; v.kind_ = Kind::Number; v.num_ = d; return v;
    }
    static Value angka_int32(std::int32_t i) noexcept { Value v; v.kind_ = Kind::Int32; v.num_ = double(i); return v; }
    static Value obyek(const void* p) noexcept { Value v; v.kind_ = Kind::Obyek; v.ptr_ = p; return v; }
    static Value bigint(const void* p) noexcept { Value v; v.kind_ = Kind::BigInt; v.ptr_ = p; return v; }
    static Value simbol(const void* p) noexcept { Value v; v.kind_ = Kind::Simbol; v.ptr_ = p; return v; }
    static Value special(Tag t) noexcept { Value v; v.kind_ = Kind::NaNAngka; v.num_ = double(t); return v; }

    [[nodiscard]] bool is_mboh() const noexcept { return kind_ == Kind::Mboh; }
    [[nodiscard]] bool is_kosong() const noexcept { return kind_ == Kind::Kosong; }
    [[nodiscard]] bool is_boole() const noexcept { return kind_ == Kind::Boole; }
    [[nodiscard]] bool is_int32() const noexcept { return kind_ == Kind::Int32; }
    [[nodiscard]] bool is_obyek() const noexcept { return kind_ == Kind::Obyek; }
    [[nodiscard]] bool is_bigint() const noexcept { return kind_ == Kind::BigInt; }
    [[nodiscard]] bool is_simbol() const noexcept { return kind_ == Kind::Simbol; }
    [[nodiscard]] bool is_nan_angka() const noexcept { return kind_ == Kind::NaNAngka; }
    [[nodiscard]] bool is_number() const noexcept { return kind_ == Kind::Number; }
    [[nodiscard]] bool is_angka() const noexcept {
        return kind_ == Kind::Number || kind_ == Kind::Int32 || kind_ == Kind::NaNAngka;
    }
    [[nodiscard]] bool is_bener() const noexcept { return kind_ == Kind::Boole && num_ != 0.0; }
    [[nodiscard]] bool bool_value() const noexcept { return is_bener(); }
    [[nodiscard]] double as_number() const noexcept {
        return kind_ == Kind::NaNAngka ? std::numeric_limits<double>::quiet_NaN() : num_;
    }
    [[nodiscard]] double as_double() const noexcept { return as_number(); }
    [[nodiscard]] double as_double_pure() const noexcept { return num_; }
    [[nodiscard]] std::int32_t as_i32() const noexcept { return std::int32_t(num_); }
    [[nodiscard]] const void* pointer() const noexcept { return ptr_; }
    [[nodiscard]] void* mutable_pointer() const noexcept { return const_cast<void*>(ptr_); }
    template <class T>
    [[nodiscard]] const T* as() const noexcept { return static_cast<const T*>(ptr_); }
    template <class T>
    [[nodiscard]] T* as_mut() const noexcept { return static_cast<T*>(const_cast<void*>(ptr_)); }
    /// Kunci hash 64-bit. BUKAN representasi bit: tag dicampur dengan
    /// perkalian Fibonacci supaya tidak pernah bertabrakan dengan bit muatan
    /// (pada mode boxed, muatan berupa `double` yang bit atasnya sudah terpakai).
    /// Kesamaan nilai ditentukan `sama()`, bukan perbandingan `key()`.
    [[nodiscard]] std::uint64_t key() const noexcept {
        if (kind_ == Kind::NaNAngka) return 0x9E37'79B9'7F4A'7C15ull;  // kanonik
        std::uint64_t muatan = muatan_bit();
        return (static_cast<std::uint64_t>(kind_) * 0x9E37'79B9'7F4A'7C15ull) ^
               (muatan * 0xC2B2'AE3D'27D4'EB4Full);
    }
    /// Bentuk yang sama dengan mode NaN-boxing (dipakai sebagai kunci hash).
    [[nodiscard]] std::uint64_t raw() const noexcept { return key(); }
    [[nodiscard]] std::uintptr_t raw_pointer() const noexcept {
        return reinterpret_cast<std::uintptr_t>(ptr_);
    }

    // ------------------------------------------------------------ perbandingan
    // Dua nilai sama bila tag sama DAN muatan sama. Setara dengan mode
    // NaN-boxing: `NaN` dinormalkan ke `NaNAngka` sehingga `NaN === NaN` benar,
    // sedangkan `NaN !== NaN` dijaga `OP::SEQ` (lihat `vm_loop.cpp`).
    [[nodiscard]] bool sama(const Value& o) const noexcept {
        if (kind_ != o.kind_) return false;
        if (kind_ == Kind::Obyek || kind_ == Kind::BigInt || kind_ == Kind::Simbol) return ptr_ == o.ptr_;
        if (kind_ == Kind::NaNAngka) return true;
        return std::bit_cast<std::uint64_t>(num_) == std::bit_cast<std::uint64_t>(o.num_);
    }
    [[nodiscard]] bool operator==(const Value& o) const noexcept { return sama(o); }
    [[nodiscard]] bool operator!=(const Value& o) const noexcept { return !sama(o); }

    template <class T>
    [[nodiscard]] bool operator==(const T& o) const noexcept {
        return sama(Value(o));
    }
    template <class T>
    [[nodiscard]] bool operator!=(const T& o) const noexcept {
        return !sama(Value(o));
    }

private:
    /// Bit muatan aktif sesuai tag: pointer untuk tag pointer, selain itu bit `double`.
    [[nodiscard]] std::uint64_t muatan_bit() const noexcept {
        if (kind_ == Kind::Obyek || kind_ == Kind::BigInt || kind_ == Kind::Simbol) {
            return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(ptr_));
        }
        return std::bit_cast<std::uint64_t>(num_);
    }

    Kind kind_;
    union { double num_; const void* ptr_; };
};

static_assert(sizeof(Value) == 16, "Value mode union harus 16 byte");
static_assert(std::is_trivially_copyable_v<Value>, "Value harus trivially copyable");

#endif

/// Nilai placeholder netral (undefined) untuk inisialisasi vektor.
inline Value value_nol() noexcept { return Value::mboh(); }

/// Kebenaran nilai (Bagian 3.5): `salah`, `kosong`, `mboh`, `0`, `-0`, `NaN`,
/// `0n`, dan `""` adalah falsy; objek/dhaptar/fungsi selalu truthy.
[[nodiscard]] inline bool benar(Value v) noexcept {
    if (v.is_mboh() || v.is_kosong()) return false;
    if (v.is_boole()) return v.bool_value();
    if (v.is_int32()) return v.as_i32() != 0;
    // NaN adalah falsy (punya tag tersendiri supaya tidak tertukar dengan `mboh`).
    if (v.is_nan_angka()) return false;
    if (v.is_number()) return v.as_double_pure() != 0.0 && !std::isnan(v.as_double_pure());
    if (v.is_obyek() || v.is_bigint() || v.is_simbol()) return true;
    return true;
}

}  // namespace jawa::rt
