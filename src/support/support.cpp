#include "support/diagnostics.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>

namespace jawa::support {
namespace {

const std::array<CatalogMessage, 200>& katalog() {
    static const std::array<CatalogMessage, 200> k = [] {
        std::array<CatalogMessage, 200> a{};
        std::size_t n = 0;
#define JAWA_MSG(kode, jenis, jawa, indonesia, saran) a[n++] = CatalogMessage{kode, jawa, indonesia, saran};
#include "messages.def"
#undef JAWA_MSG
        return a;
    }();
    return k;
}

/// Jumlah entri katalog (dihitung dengan probe).
struct KatalogInfo {
    static std::size_t size() {
        const auto& k = katalog();
        std::size_t n = 0;
        while (n < k.size() && k[n].id_code != nullptr) ++n;
        return n;
    }
};

/// Cari pesan via perfect index berdasarkan huruf pertama + 3 digit kode.
const CatalogMessage* cari(std::string_view kode) noexcept {
    if (kode.size() != 4) return nullptr;
    const auto& k = katalog();
    for (std::size_t i = 0; i < KatalogInfo::size(); ++i) {
        if (kode == k[i].id_code) return &k[i];
    }
    return nullptr;
}

}  // namespace

const CatalogMessage* find_message(const char* code) noexcept {
    if (code == nullptr) return nullptr;
    return cari(code);
}

std::string format_message(const CatalogMessage& m, MsgLang lang) {
    switch (lang) {
        case MsgLang::Jawa: return m.jawa;
        case MsgLang::Indonesia: return m.indonesia;
        case MsgLang::Dwi: break;
    }
    // dwi: "Jawa — Indonesia" (dipakai dwibahasa agar tetap ringkas)
    std::string out(m.jawa);
    if (m.jawa[0] != '\0' && m.indonesia[0] != '\0' && std::string_view(m.jawa) != std::string_view(m.indonesia)) {
        out += " / ";
        out += m.indonesia;
    }
    return out;
}

std::string Diagnostic::format() const {
    std::string out;
    const char* prefix = "";
    switch (kind) {
        case DiagKind::Lexer: prefix = "KleruToken"; break;
        case DiagKind::Sintaks: prefix = "KleruSintaks"; break;
        case DiagKind::Scope: prefix = "KleruCakupan"; break;
        case DiagKind::Tipe: prefix = "KleruJinisTipe"; break;
        case DiagKind::Runtime: prefix = "Kleru"; break;
        case DiagKind::Internal: prefix = "Bug ing interpreter"; break;
        case DiagKind::CLI: prefix = "KleruCLI"; break;
    }
    out += prefix;
    out += " [";
    out += code;
    out += "] (";
    out += std::string(berkas);
    out += ":";
    out += std::to_string(pos.baris);
    out += ":";
    out += std::to_string(pos.kolom);
    out += ")\n";
    out += "  ";
    out += pesan;
    out += "\n";
    if (!potongan.empty()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%5u | ", pos.baris);
        out += buf;
        out += potongan;
        out += "\n";
        out += "      | ";
        const std::size_t kolom = pos.kolom > 0 ? static_cast<std::size_t>(pos.kolom) - 1 : 0;
        for (std::size_t i = 0; i < kolom; ++i) {
            out += (i < kolom && potongan[i] == '\t') ? '\t' : ' ';
        }
        out += '^';
        for (std::uint32_t i = 1; i < panjang_kolom; ++i) out += '~';
        out += "\n";
    }
    if (!saran.empty()) {
        out += "  Saran: ";
        out += saran;
        out += "\n";
    }
    if (!jejak.empty()) {
        out += "Jejak (paling anyar ing ngisor):\n";
        for (const std::string& j : jejak) {
            out += "  ";
            out += j;
            out += "\n";
        }
    }
    return out;
}

void isi_potongan(Diagnostic& d, std::string_view kode) {
    if (d.pos.offset >= kode.size()) return;
    std::size_t awal = d.pos.offset;
    while (awal > 0 && kode[awal - 1] != '\n') --awal;
    std::size_t akhir = d.pos.offset;
    while (akhir < kode.size() && kode[akhir] != '\n') ++akhir;
    if (akhir > awal && kode[akhir - 1] == '\r') --akhir;
    d.potongan = std::string(kode.substr(awal, akhir - awal));

    // Panjang sorotan dalam kolom: hitung dari offset ke akhir baris.
    std::size_t lebar_bita = 0;
    for (std::size_t i = d.pos.offset; i < akhir; ++i) {
        if ((static_cast<unsigned char>(kode[i]) & 0xC0) != 0x80) ++lebar_bita;
    }
    if (lebar_bita == 0) lebar_bita = 1;
    if (d.panjang_kolom == 0) d.panjang_kolom = 1;
    d.panjang_kolom = static_cast<std::uint32_t>(std::min<std::size_t>(lebar_bita, 40));
}

void cetak_diagnostik(std::FILE* keluar, const DiagnosticBag& bag) {
    for (const Diagnostic& d : bag.peringatan()) {
        const std::string s = d.format();
        std::fwrite(s.data(), 1, s.size(), keluar);
    }
    for (const Diagnostic& d : bag.galat()) {
        const std::string s = d.format();
        std::fwrite(s.data(), 1, s.size(), keluar);
    }
}

}  // namespace jawa::support
