#include "cli/repl.h"

#include <algorithm>
#include <cctype>
#include <ostream>

#include "lex/lexer.h"
#include "support/diagnostics.h"

namespace jawa::cli {

using lex::Tok;

namespace {

/// Kode diagnostik yang menandakan "menunggu, pengguna belum selesai mengetik".
bool kode_menunggu_closure(const support::Diagnostic& d) {
    // L002 string, L003 template, L006 komentar blok.
    return d.code == "L002" || d.code == "L003" || d.code == "L006";
}

/// Hitung imbangan kurung. `true` kalau tokennya menutup kurung.
bool kurung_menutup(std::size_t& kedalaman, Tok t) {
    switch (t) {
        case Tok::LBrace:
        case Tok::LBracket:
        case Tok::LParen:
            ++kedalaman;
            return false;
        case Tok::RBrace:
        case Tok::RBracket:
        case Tok::RParen:
            if (kedalaman > 0) --kedalaman;
            return true;
        default:
            return false;
    }
}

}  // namespace

Kelengkapan cek_kelengkapan(std::string_view sumber) {
    Kelengkapan h;
    // String yang belum ditutup dicek lebih dulu: kalau memang belum ketutup,
    // sisa baris berikutnya masih bagian dari string itu, jadi kurung di
    // dalamnya tidak boleh dihitung.
    lex::LexOptions opt;
    lex::TokenList daftar;
    lex::Lexer lx(sumber, "<repl>", ".", opt);
    lx.lex_semua(daftar);
    for (const support::Diagnostic& d : lx.bag().galat()) {
        if (!kode_menunggu_closure(d)) continue;
        h.lengkap = false;
        h.alasan = "masih ada " + std::string(d.saran.empty() ? d.pesan : d.saran);
        return h;
    }
    // Kurung kurawal/kurung siku/kurung kurung. Template literal tidak dihitung
    // berganda: `${...}` di dalamnya sudah dihitung sebagai token biasa oleh
    // lexer, jadi kurung di dalamnya terhitung tepat sekali.
    std::size_t kedalaman = 0;
    for (const lex::Token& t : daftar.token) {
        if (t.jenis == Tok::Eof) break;
        kurung_menutup(kedalaman, t.jenis);
    }
    if (kedalaman > 0) {
        h.lengkap = false;
        h.alasan = "kurung utawa kurung kurawal durung ketutup";
    }
    return h;
}

void cetak_bantuan_repl(std::ostream& keluar) {
    keluar << "Perintah REPL:\n"
           << "  :q, :keluar        keluar dari REPL\n"
           << "  :bantuan, :?        tampilkan bantuan ini\n"
           << "  :nilai <nama>       cetak nilai global bernama <nama>\n"
           << "  :sampah             statistik pengumpulan sampah\n"
           << "  :reset              kosongkan seluruh state (semua nama hilang)\n";
}

int jalankan_repl(std::function<std::optional<std::string>()> pemasok_baris, bool interaktif,
                  std::ostream& keluar, std::ostream& galat, const Evaluator& evaluasi) {
    std::string akumulasi;
    int gagal = 0;
    // `galat` dipakai untuk memberi tahu kenapa masukan dianggap belum selesai,
    // supaya pengguna di mode interaktif tahu dia tinggal mengetik kelanjutan.
    const auto beri_tahu = [&](const std::string& alasan) {
        if (interaktif) galat << "... " << alasan << "\n";
    };

    for (;;) {
        if (interaktif) keluar << (akumulasi.empty() ? "jawa> " : "  ...> ") << std::flush;
        const std::optional<std::string> baris = pemasok_baris();
        if (!baris.has_value()) break;
        if (interaktif) keluar << '\n';

        // Perintah hanya berlaku di luar kelanjutan: di tengah statement yang
        // belum selesai, `:` adalah karakter biasa.
        if (akumulasi.empty()) {
            std::string_view p(*baris);
            while (!p.empty() && std::isspace(static_cast<unsigned char>(p.back()))) p.remove_suffix(1);
            if (!p.empty() && p.front() == ':') {
                const std::size_t spasi = p.find_first_of(" \t");
                const std::string_view kata = p.substr(0, spasi);
                if (kata == ":q" || kata == ":keluar" || kata == ":quit") return gagal;
                if (kata == ":bantuan" || kata == ":?" || kata == ":help") {
                    cetak_bantuan_repl(keluar);
                    continue;
                }
                const HasilRepl h = evaluasi(p);
                if (h.keluar) return gagal;
                if (h.ada_galat) ++gagal;
                continue;
            }
        }

        if (akumulasi.empty()) {
            akumulasi = *baris;
        } else {
            akumulasi += '\n';
            akumulasi += *baris;
        }

        const Kelengkapan lengkap = cek_kelengkapan(akumulasi);
        if (!lengkap.lengkap) {
            // Terus kumpulkan baris. Kalau masukan habis dalam keadaan seperti
            // ini, sisa evaluasinya yang dievaluasi di bawah, bukan dibuang
            // diam-diam.
            beri_tahu(lengkap.alasan);
            continue;
        }

        std::string untuk_dijalankan = akumulasi;
        akumulasi.clear();
        const HasilRepl h = evaluasi(untuk_dijalankan);
        if (h.keluar) return gagal;
        if (h.ada_galat) ++gagal;
    }

    if (!akumulasi.empty()) {
        // Masukan berakhir di tengah statement: jalankan apa adanya supaya
        // ketikan pengguna tidak hilang tanpa jejak.
        const HasilRepl h = evaluasi(akumulasi);
        if (h.ada_galat) ++gagal;
    }
    return gagal;
}

}  // namespace jawa::cli
