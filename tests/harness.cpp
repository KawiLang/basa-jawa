#include "harness.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace testing {

Registry& registry() {
    static Registry r;
    return r;
}

Registrar::Registrar(std::string_view nama, TestFn fn) {
    registry().tests.emplace_back(std::string(nama), std::move(fn));
}

namespace {
std::string& filter_storage() {
    static std::string f;
    return f;
}
}  // namespace

void set_filter(std::string_view f) { filter_storage() = std::string(f); }

int jalankan_semua(int argc, char** argv) {
    std::string filter;
    bool daftar = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--filter" && i + 1 < argc) {
            filter = argv[++i];
        } else if (a == "--list" || a == "-l") {
            daftar = true;
        } else if (a.rfind("--filter=", 0) == 0) {
            filter = a.substr(9);
        }
    }

    if (daftar) {
        for (const auto& [nama, fn] : registry().tests) (void)fn, std::printf("%s\n", nama.c_str());
        return 0;
    }

    std::size_t jumlah_lulus = 0;
    std::size_t jumlah_gagal = 0;
    std::vector<std::string> gagal_nama;
    std::size_t total_kecks = 0;
    std::size_t total_gagal_kecks = 0;

    const auto mula = std::chrono::steady_clock::now();

    for (const auto& [nama, fn] : registry().tests) {
        if (!filter.empty() && nama.find(filter) == std::string::npos) continue;
        Context::instance().mulai_test(nama);
        std::fprintf(stderr, "[ RUN  ] %s\n", nama.c_str());
        try {
            fn();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "    exception: %s\n", e.what());
            Context::instance().Report(false, "test", e.what(), "<exception>", 0);
        } catch (...) {
            std::fprintf(stderr, "    exception: unknown\n");
            Context::instance().Report(false, "test", "unknown", "<exception>", 0);
        }
        total_kecks += Context::instance().jumlah_kecks();
        total_gagal_kecks += Context::instance().jumlah_gagal();
        if (Context::instance().gagal()) {
            ++jumlah_gagal;
            gagal_nama.push_back(nama);
            std::fprintf(stderr, "[ FAIL ] %s\n", nama.c_str());
        } else {
            ++jumlah_lulus;
            std::fprintf(stderr, "[  OK  ] %s (%zu cek)\n", nama.c_str(), Context::instance().jumlah_kecks());
        }
    }

    const auto selesai = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(selesai - mula).count();

    std::fprintf(stderr, "\n=== Ringkasan: %zu lulus, %zu gagal dari %zu test | %zu cek, %zu cek gagal | %.1f ms ===\n",
                 jumlah_lulus, jumlah_gagal, jumlah_lulus + jumlah_gagal, total_kecks, total_gagal_kecks, ms);
    if (!gagal_nama.empty()) {
        std::fprintf(stderr, "Gagal:\n");
        for (const std::string& n : gagal_nama) std::fprintf(stderr, "  - %s\n", n.c_str());
    }
    return jumlah_gagal == 0 ? 0 : 1;
}

}  // namespace testing

int main(int argc, char** argv) { return testing::jalankan_semua(argc, argv); }
