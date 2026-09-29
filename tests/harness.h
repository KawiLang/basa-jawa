// Harness test Basa Jawa — tanpa dependensi eksternal.
//
// Meniru subset Catch2: TEST_CASE, SECTION, CHECK*, REQUIRE*, CHECK_THROWS.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace testing {

struct SectionState {
    std::string nama;
    bool gagal = false;
    std::vector<std::string> pesan;
};

class Context {
public:
    static Context& instance() {
        static Context c;
        return c;
    }

    void mulai_test(std::string_view nama) {
        nama_test_ = nama;
        jumlah_kecks_ = 0;
        gagal_ = false;
        bagian_ = 0;
    }

    void Report(bool ok, std::string_view ekspresi, std::string_view tambahan, const char* file, int baris) {
        ++jumlah_kecks_;
        if (ok) return;
        gagal_ = true;
        std::string msg = std::string(file) + ":" + std::to_string(baris) + ": CHECK gagal: " + std::string(ekspresi);
        if (!tambahan.empty()) msg += "\n      " + std::string(tambahan);
        if (bagian_ > 0) msg = "[bagian " + std::to_string(bagian_) + "] " + msg;
        std::fprintf(stderr, "    %s\n", msg.c_str());
        ++jumlah_gagal_;
    }

    void ReportThrow(std::string_view ekspresi, const char* file, int baris, std::string_view apa) {
        ++jumlah_kecks_;
        gagal_ = true;
        ++jumlah_gagal_;
        std::fprintf(stderr, "    %s:%d: CHECK_THROWS gagal: %s (%s)\n", file, baris,
                     std::string(ekspresi).c_str(), std::string(apa).c_str());
    }

    void ReportNoThrow(std::string_view ekspresi, const char* file, int baris, std::string_view apa) {
        ++jumlah_kecks_;
        gagal_ = true;
        ++jumlah_gagal_;
        std::fprintf(stderr, "    %s:%d: CHECK_NOTHROW gagal: %s (dilempar %s)\n", file, baris,
                     std::string(ekspresi).c_str(), std::string(apa).c_str());
    }

    [[nodiscard]] bool gagal() const noexcept { return gagal_; }
    [[nodiscard]] std::size_t jumlah_kecks() const noexcept { return jumlah_kecks_; }
    [[nodiscard]] std::size_t jumlah_gagal() const noexcept { return jumlah_gagal_; }
    [[nodiscard]] std::string_view nama_test() const noexcept { return nama_test_; }

    void tambah_bagian() { ++bagian_; }

private:
    std::string nama_test_;
    std::size_t jumlah_kecks_ = 0;
    std::size_t jumlah_gagal_ = 0;
    int bagian_ = 0;
    bool gagal_ = false;
};

using TestFn = std::function<void()>;

struct Registrar {
    Registrar(std::string_view nama, TestFn fn);
};

/// Daftarkan test; dipanggil lewat makro.
int jalankan_semua(int argc, char** argv);

/// Format nilai untuk pesan kegagalan.
template <class T>
std::string to_string(const T& v) {
    std::ostringstream os;
    os << v;
    return os.str();
}
inline std::string to_string(const std::string& v) { return "\"" + v + "\""; }
inline std::string to_string(const char* v) { return std::string("\"") + (v ? v : "(null)") + "\""; }
inline std::string to_string(bool v) { return v ? "bener" : "salah"; }
inline std::string to_string(std::nullptr_t) { return "kosong"; }

/// Registri global.
struct Registry {
    std::vector<std::pair<std::string, TestFn>> tests;
};
Registry& registry();

/// Filter baris berisi substring.
void set_filter(std::string_view f);

}  // namespace testing

// ---------------------------------------------------------------- makro
#define JAWA_TEST_CAT2(a, b) a##b
#define JAWA_TEST_CAT(a, b) JAWA_TEST_CAT2(a, b)

#define TEST_CASE(nama)                                                          \
    static void JAWA_TEST_CAT(jawa_test_fn_, __LINE__)();                         \
    static ::testing::Registrar JAWA_TEST_CAT(jawa_test_reg_, __LINE__){           \
        nama, &JAWA_TEST_CAT(jawa_test_fn_, __LINE__)};                          \
    static void JAWA_TEST_CAT(jawa_test_fn_, __LINE__)()

/// SECTION: dijalankan sekali per test (bukan multi-run) untuk kesederhanaan & kecepatan.
#define SECTION(nama) if (true)

#define CHECK(expr)                                                              \
    do {                                                                         \
        bool _ok = false;                                                        \
        try { _ok = static_cast<bool>(expr); } catch (const std::exception& e) {   \
            _ok = false;                                                         \
            ::testing::Context::instance().Report(false, #expr, e.what(), __FILE__, __LINE__); \
            break;                                                               \
        }                                                                        \
        ::testing::Context::instance().Report(_ok, #expr, "", __FILE__, __LINE__);\
    } while (false)

#define CHECK_MSG(expr, msg)                                                     \
    do {                                                                         \
        bool _ok = false;                                                        \
        try { _ok = static_cast<bool>(expr); } catch (const std::exception& e) {  \
            ::testing::Context::instance().Report(false, #expr, e.what(), __FILE__, __LINE__); \
            break;                                                               \
        }                                                                        \
        ::testing::Context::instance().Report(_ok, #expr, (msg), __FILE__, __LINE__);\
    } while (false)

#define REQUIRE(expr)                                                            \
    do {                                                                         \
        bool _ok = false;                                                        \
        try { _ok = static_cast<bool>(expr); } catch (...) { _ok = false; }      \
        if (!_ok) {                                                              \
            ::testing::Context::instance().Report(false, #expr, "REQUIRE", __FILE__, __LINE__); \
            return;                                                              \
        }                                                                        \
    } while (false)

#define CHECK_EQ(a, b) CHECK(::testing::cmp_eq((a), (b)))
#define CHECK_NE(a, b) CHECK(!(::testing::cmp_eq((a), (b))))
#define CHECK_LT(a, b) CHECK((a) < (b))
#define CHECK_LE(a, b) CHECK((a) <= (b))
#define CHECK_GT(a, b) CHECK((a) > (b))
#define CHECK_GE(a, b) CHECK((a) >= (b))

#define CHECK_THROWS_AS(expr, ExcT)                                              \
    do {                                                                         \
        bool _lempar = false;                                                    \
        try { (void)(expr); } catch (const ExcT&) { _lempar = true; }            \
        catch (...) { _lempar = false; }                                         \
        ::testing::Context::instance().Report(_lempar, #expr " melempar " #ExcT, "", __FILE__, __LINE__);\
    } while (false)

#define CHECK_THROWS(expr)                                                       \
    do {                                                                         \
        bool _lempar = false;                                                    \
        try { (void)(expr); } catch (...) { _lempar = true; }                     \
        ::testing::Context::instance().Report(_lempar, #expr, "", __FILE__, __LINE__);\
    } while (false)

#define CHECK_NOTHROW(expr)                                                      \
    do {                                                                         \
        bool _aman = true;                                                       \
        std::string _apa;                                                        \
        try { (void)(expr); } catch (const std::exception& e) { _aman = false; _apa = e.what(); } \
        catch (...) { _aman = false; _apa = "unknown"; }                         \
        ::testing::Context::instance().ReportNoThrow(#expr, __FILE__, __LINE__, _apa); \
    } while (false)

namespace testing {

/// Perbandingan yang menghasilkan pesan selisih bila gagal.
template <class A, class B>
bool cmp_eq(const A& a, const B& b) {
    if (a == b) return true;
    std::string m = "nilai: " + to_string(a) + " != " + to_string(b);
    Context::instance().Report(false, "==", m, "<cmp>", 0);
    return false;
}

}  // namespace testing
