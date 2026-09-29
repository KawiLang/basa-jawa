// Katalog pesan & diagnostik.
//
// Semua pesan pengguna (Jawa/Indonesia) berasal dari `messages.def` agar mudah
// ditinjau dan disatukan. `Diagnostic` membawa kode galat, lokasi, sumber,
// penanda kolom, saran perbaikan, dan jejak panggil.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "support/result.h"
#include "support/source_map.h"

namespace jawa::support {

/// Bahasa pesan.
enum class MsgLang : uint8_t { Jawa = 0, Indonesia = 1, Dwi = 2 };

/// Level diagnostik.
enum class DiagLevel : uint8_t { Catatan, Peringatan, Galat, GalatInternal };

/// Tingkat keparahan galat: L=lexer, S=sintaks/scope, T=tipe, R=runtime, I=internal.
enum class DiagKind : uint8_t { Lexer, Sintaks, Scope, Tipe, Runtime, Internal, CLI };

/// Satu pesan dari katalog `messages.def`.
struct CatalogMessage {
    const char* id_code;    ///< contoh "S012"
    const char* jawa;       ///< pesan bahasa Jawa
    const char* indonesia;  ///< pesan bahasa Indonesia
    const char* saran;      ///< saran perbaikan (boleh kosong)
};

/// Cari pesan di katalog berdasarkan kode ("S012"); nullptr bila tidak ada.
const CatalogMessage* find_message(const char* code) noexcept;

/// Format pesan sesuai bahasa yang dipilih (dwi = "Jawa — Indonesia").
[[nodiscard]] std::string format_message(const CatalogMessage& m, MsgLang lang);

/// Respons diagnostik tunggal.
struct Diagnostic {
    DiagKind kind = DiagKind::Sintaks;
    DiagLevel level = DiagLevel::Galat;
    std::string code;                 ///< "S012", "T003", ...
    std::string_view berkas;          ///< nama berkas
    SourcePos pos;                    ///< lokasi
    std::string pesan;                ///< teks utama
    std::string saran;                ///< saran perbaikan
    std::string potongan;             ///< baris sumber
    std::uint32_t panjang_kolom = 1;  ///< panjang sorotan (dalam kolom)
    std::vector<std::string> jejak;   ///< jejak panggilan (opsional)

    /// Isi ulang `pesan` & `saran` dari katalog sesuai bahasa.
    void terapkan_bahasa(MsgLang lang) {
        const CatalogMessage* m = find_message(code.c_str());
        if (m == nullptr) return;
        pesan = format_message(*m, lang);
        if (m->saran != nullptr && m->saran[0] != '\0') saran = m->saran;
    }

    /// Bentuk teks lengkap multi-baris (dipakai printer diagnostik).
    [[nodiscard]] std::string format() const;
};

/// Kolektor diagnostik dengan batas jumlah galat.
class DiagnosticBag {
public:
    explicit DiagnosticBag(std::size_t maks_galat = 50) : maks_(maks_galat) {}

    void add(Diagnostic d) {
        ++total_;
        if (d.level == DiagLevel::Peringatan) {
            peringatan_.push_back(std::move(d));
        } else if (galat_.size() < maks_) {
            galat_.push_back(std::move(d));
        } else {
            ++dipotong_;
        }
    }

    void add_galat(Diagnostic d) {
        d.level = DiagLevel::Galat;
        add(std::move(d));
    }
    void add_peringatan(Diagnostic d) {
        d.level = DiagLevel::Peringatan;
        add(std::move(d));
    }

    [[nodiscard]] bool ada_galat() const noexcept { return !galat_.empty(); }
    [[nodiscard]] std::size_t jumlah_galat() const noexcept { return galat_.size(); }
    [[nodiscard]] std::size_t jumlah_peringatan() const noexcept { return peringatan_.size(); }
    [[nodiscard]] std::size_t jumlah_total() const noexcept { return total_; }
    [[nodiscard]] std::size_t jumlah_dipotong() const noexcept { return dipotong_; }
    [[nodiscard]] bool penuh() const noexcept { return galat_.size() >= maks_; }

    [[nodiscard]] const std::vector<Diagnostic>& galat() const noexcept { return galat_; }
    [[nodiscard]] const std::vector<Diagnostic>& peringatan() const noexcept { return peringatan_; }

    void terapkan_bahasa(MsgLang lang) {
        for (Diagnostic& d : galat_) d.terapkan_bahasa(lang);
        for (Diagnostic& d : peringatan_) d.terapkan_bahasa(lang);
    }

    void gabung(const DiagnosticBag& lain) {
        for (const Diagnostic& d : lain.peringatan_) add(Diagnostic{d});
        for (const Diagnostic& d : lain.galat_) add(Diagnostic{d});
    }

    void reset() {
        galat_.clear();
        peringatan_.clear();
        total_ = 0;
        dipotong_ = 0;
    }

private:
    std::vector<Diagnostic> galat_;
    std::vector<Diagnostic> peringatan_;
    std::size_t maks_;
    std::size_t total_ = 0;
    std::size_t dipotong_ = 0;
};

using DiagResult = Result<void, Diagnostic>;

/// Isi `potongan` (baris sumber) & `panjang_kolom` dari isi berkas mentah.
void isi_potongan(Diagnostic& d, std::string_view kode);

}  // namespace jawa::support
