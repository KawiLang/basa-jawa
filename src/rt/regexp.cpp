// Mesin regex: pencocokan backtracking dengan continuation.
//
// Struktur program adalah POHON (bukan daftar opcode), dan pencocokan memakai
// continuation: `match(node, pos, k)` berarti "cocokkan `node` mulai `pos`, lalu
// lanjutkan dengan `k` pada posisi setelahnya". Itu yang membuat tangkapan
// kelompok bisa akurat tanpa perlu pass kedua.
#include "rt/regexp.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <limits>

namespace jawa::rt {
namespace {

// ---------------------------------------------------------------------------
// Klasifikasi karakter
// ---------------------------------------------------------------------------

bool digit_(char c) { return c >= '0' && c <= '9'; }

/// Panjang satu unit UTF-8 (lead byte >= 0x80 dihitung sebagai satu karakter,
/// sehingga `.` dan `\w` tidak memecah karakter non-ASCII).
std::size_t len_utf8(char c) {
    const auto b = static_cast<unsigned char>(c);
    if (b < 0x80) return 1;
    if ((b >> 5) == 0x6) return 2;
    if ((b >> 4) == 0xE) return 3;
    if ((b >> 3) == 0x1E) return 4;
    return 1;
}

std::string utf8_encode(unsigned cp) {
    std::string keluar;
    if (cp <= 0x7F) {
        keluar.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        keluar.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        keluar.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        keluar.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        keluar.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        keluar.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return keluar;
}

/// Galat kompilasi pola: pesan pertama yang muncul yang dipakai.
struct Galat {
    std::string pesan;
    bool ada = false;
    void set(std::string p) {
        if (!ada) {
            pesan = std::move(p);
            ada = true;
        }
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Pohon program
// ---------------------------------------------------------------------------

struct RegexProgram::Node {
    enum class Jenis : uint8_t {
        Kosong,  ///< cocok string kosong
        Harf,    ///< satu byte literal
        Titik,   ///< `.`
        Kelas,   ///< `[...]` / `\d` / `\w` / `\s` (dengan negasi)
        Jangkar, ///< `^` `$` `\b` `\B`
        Rangkaian,
        Alternatif,
        Kuantifier,
        Grup,
    };

    Jenis jenis = Jenis::Kosong;
    char harf = 0;
    bool negasi_kelas = false;
    /// Anchor: 0 = `^`, 1 = `$`, 2 = `\b`, 3 = `\B`.
    int anchor = 0;
    std::vector<std::pair<unsigned char, unsigned char>> kelas;
    std::size_t kuant_min = 0;
    std::size_t kuant_maks = 0;
    bool kuant_malu = false;
    std::size_t nomor_grup = 0;  ///< 0 = non-penangkap
    std::vector<std::shared_ptr<Node>> anak;
};

namespace {

using Node = RegexProgram::Node;
using Kind = Node::Jenis;

// ---------------------------------------------------------------------------
// Kompilator pola
// ---------------------------------------------------------------------------

class Kompilator {
public:
    Kompilator(std::string_view pola, bool fold, Galat& galat) : pola_(pola), galat_(galat), fold_(fold) {}

    std::shared_ptr<Node> jalankan() {
        auto n = alternasi();
        if (galat_.ada) return nullptr;
        if (i_ != pola_.size()) {
            galat_.set("Paren ')' tidak berpasangan pada pola regex");
            return nullptr;
        }
        return n;
    }
    [[nodiscard]] std::size_t jumlah_kelompok() const { return total_kelompok_; }
    [[nodiscard]] const std::vector<std::string>& nama_kelompok() const { return nama_kelompok_; }

private:
    static std::shared_ptr<Node> buat(Kind k) {
        auto n = std::make_shared<Node>();
        n->jenis = k;
        return n;
    }
    [[nodiscard]] bool habis() const { return i_ >= pola_.size(); }
    [[nodiscard]] char peek(std::size_t n = 0) const {
        return i_ + n < pola_.size() ? pola_[i_ + n] : '\0';
    }
    void maju() { ++i_; }

    /// `\...` di luar kelas. Mengembalikan Node::Harf/Kelas/Jangkar.
    std::shared_ptr<Node> escape() {
        if (habis()) {
            galat_.set("Pola regex berakhir dengan backslash");
            return nullptr;
        }
        const char c = peek();
        maju();
        switch (c) {
            case 'd':
            case 'D':
            case 'w':
            case 'W':
            case 's':
            case 'S': {
                const char kecil = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                auto n = buat(Kind::Kelas);
                n->negasi_kelas = std::isupper(static_cast<unsigned char>(c)) != 0;
                if (kecil == 'd') {
                    n->kelas.emplace_back('0', '9');
                } else if (kecil == 'w') {
                    n->kelas.emplace_back('a', 'z');
                    n->kelas.emplace_back('A', 'Z');
                    n->kelas.emplace_back('0', '9');
                    n->kelas.emplace_back('_', '_');
                } else {
                    n->kelas.emplace_back(' ', ' ');
                    n->kelas.emplace_back('\t', '\r');
                }
                return n;
            }
            case 'b':
            case 'B': {
                auto n = buat(Kind::Jangkar);
                n->anchor = (c == 'b') ? 2 : 3;
                return n;
            }
            case 'n': return harf('\n');
            case 't': return harf('\t');
            case 'r': return harf('\r');
            case 'f': return harf('\f');
            case 'v': return harf('\v');
            case '0': return harf('\0');
            case 'x': {
                unsigned v = 0;
                if (!hex(2, v)) return nullptr;
                return harf(static_cast<char>(v));
            }
            case 'u': {
                unsigned v = 0;
                if (!hex(4, v)) return nullptr;
                auto n = buat(Kind::Rangkaian);
                for (char b : utf8_encode(v)) n->anak.push_back(harf(b));
                return n;
            }
            case 'c': {
                if (habis()) {
                    galat_.set("Pola regex berakhir dengan \\c");
                    return nullptr;
                }
                const char l = static_cast<char>(std::toupper(static_cast<unsigned char>(peek())));
                maju();
                return harf(static_cast<char>(l - 'A' + 1));
            }
            default: return harf(c);
        }
    }

    std::shared_ptr<Node> harf(char c) {
        auto n = buat(Kind::Harf);
        n->harf = c;
        return n;
    }

    bool hex(int n, unsigned& keluar) {
        unsigned v = 0;
        for (int k = 0; k < n; ++k) {
            if (habis() || !std::isxdigit(static_cast<unsigned char>(peek()))) {
                galat_.set("Escape hexadesimal tidak lengkap pada pola regex");
                return false;
            }
            const char c = peek();
            v = v * 16 + static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(c))
                                                  ? static_cast<unsigned>(c - '0')
                                                  : static_cast<unsigned>(std::tolower(c) - 'a' + 10));
            maju();
        }
        keluar = v;
        return true;
    }

    /// Klasifikasi satu karakter ke daftar rentang (dengan `fold`).
    static void tambah(std::vector<std::pair<unsigned char, unsigned char>>& kelas, unsigned char lo,
                       unsigned char hi, bool fold) {
        kelas.emplace_back(lo, hi);
        if (!fold || std::isalpha(lo) == 0) return;
        if (std::isupper(lo)) {
            kelas.emplace_back(static_cast<unsigned char>(std::tolower(lo)),
                               static_cast<unsigned char>(std::tolower(hi)));
        } else {
            kelas.emplace_back(static_cast<unsigned char>(std::toupper(lo)),
                               static_cast<unsigned char>(std::toupper(hi)));
        }
    }

    /// `[...]` -- posisi sudah melewati `[`.
    std::shared_ptr<Node> kelas_karakter() {
        auto n = buat(Kind::Kelas);
        if (!habis() && peek() == '^') {
            n->negasi_kelas = true;
            maju();
        }
        bool pertama = true;
        for (;;) {
            if (habis()) {
                galat_.set("Kelas karakter '[' tidak ditutup pada pola regex");
                return nullptr;
            }
            if (peek() == ']' && !pertama) {
                maju();
                return n;
            }
            pertama = false;
            // Ujung kiri: harf atau escape.
            std::shared_ptr<Node> e = nullptr;
            if (peek() == '\\') {
                maju();
                e = escape_kelas(n.get());
                if (e == nullptr) return nullptr;
            } else {
                e = harf(peek());
                maju();
            }
            if (e->jenis != Kind::Harf) {
                // Shorthand di dalam kelas (`[\d]`, `[\w\S]`, ...). Rentangnya
                // digabung; kalau salah satunya dinegasi (`[\D]`) kelas
                // menjadi "bukan rentang itu" -- `\D` di dalam kelas selalu
                // berarti semua yang bukan digit.
                for (const auto& r : e->kelas) tambah(n->kelas, r.first, r.second, fold_);
                if (e->negasi_kelas) n->negasi_kelas = true;
                continue;
            }
            const auto lo = static_cast<unsigned char>(e->harf);
            // Rentang? `lo-hi` dengan ujung kanan karakter tunggal.
            if (!habis() && peek() == '-' && peek(1) != ']' && peek(1) != '\0') {
                maju();
                std::shared_ptr<Node> e2 = nullptr;
                if (peek() == '\\') {
                    maju();
                    e2 = escape_kelas(n.get());
                    if (e2 == nullptr) return nullptr;
                } else {
                    e2 = harf(peek());
                    maju();
                }
                if (e2->jenis != Kind::Harf) {
                    galat_.set("Ujung rentang kelas karakter harus karakter tunggal");
                    return nullptr;
                }
                const auto hi = static_cast<unsigned char>(e2->harf);
                if (hi < lo) {
                    galat_.set("Rentang kelas karakter terbalik: " + std::string(1, static_cast<char>(lo)) +
                               "-" + std::string(1, static_cast<char>(hi)));
                    return nullptr;
                }
                tambah(n->kelas, lo, hi, fold_);
                continue;
            }
            tambah(n->kelas, lo, lo, fold_);
        }
    }

    /// Escape di dalam kelas: `\b` = backspace, sisanya sama seperti di luar.
    std::shared_ptr<Node> escape_kelas(Node* /*induk*/) {
        if (habis()) {
            galat_.set("Kelas karakter berakhir mendadak");
            return nullptr;
        }
        if (peek() == 'b') {
            maju();
            return harf('\b');
        }
        return escape();
    }

    /// `(...)` -- posisi sudah melewati `(`.
    std::shared_ptr<Node> grup() {
        auto n = buat(Kind::Grup);
        if (!habis() && peek() == '?') {
            maju();
            if (!habis() && peek() == ':') {
                maju();
            } else if (!habis() && peek() == '<') {
                // `(?<nama>...)`. `(?<=`/`(?<!` (lookbehind) ditolak.
                if (peek(1) == '=' || peek(1) == '!') {
                    galat_.set("Lookbehind '?<' belum didukung pada regex");
                    return nullptr;
                }
                maju();
                std::string nama;
                while (!habis() && peek() != '>') nama.push_back(pola_[i_++]);
                if (habis()) {
                    galat_.set("Nama kelompok tidak ditutup dengan '>'");
                    return nullptr;
                }
                maju();
                for (const std::string& s : nama_kelompok_) {
                    if (s == nama) {
                        galat_.set("Nama kelompok '" + nama + "' dipakai lebih dari sekali");
                        return nullptr;
                    }
                }
                n->nomor_grup = ++total_kelompok_;
                nama_kelompok_.push_back(nama);
            } else {
                galat_.set("Grup '(?' hanya boleh '?:' (non-penangkap) atau '?<nama>' (named)");
                return nullptr;
            }
        } else {
            n->nomor_grup = ++total_kelompok_;
        }
        n->anak.push_back(alternasi());
        if (galat_.ada) return nullptr;
        if (habis() || peek() != ')') {
            galat_.set("Grup '(' tidak ditutup pada pola regex");
            return nullptr;
        }
        maju();
        return n;
    }

    /// Baca kuantifier di posisi sekarang. `ada` = true kalau memang ada.
    bool baca_kuantifier(std::size_t& mn, std::size_t& mx, bool& malu, bool& ada) {
        ada = false;
        malu = false;
        if (habis()) return true;
        const char c = peek();
        if (c == '*') {
            mn = 0;
            mx = std::numeric_limits<std::size_t>::max();
            maju();
        } else if (c == '+') {
            mn = 1;
            mx = std::numeric_limits<std::size_t>::max();
            maju();
        } else if (c == '?') {
            mn = 0;
            mx = 1;
            maju();
        } else if (c == '{') {
            // `{` penghitung hanya kalau `{n}` / `{n,}` / `{n,m}` valid;
            // kalau tidak, perlakukan sebagai harf biasa (sesuai ECMAScript).
            std::size_t j = i_ + 1;
            unsigned a = 0;
            std::size_t digits = 0;
            while (j < pola_.size() && digit_(pola_[j])) {
                a = a * 10 + static_cast<unsigned>(pola_[j] - '0');
                ++j;
                ++digits;
                if (a > 100000) break;
            }
            if (digits == 0) return true;
            unsigned b = a;
            if (j < pola_.size() && pola_[j] == ',') {
                ++j;
                if (j < pola_.size() && digit_(pola_[j])) {
                    b = 0;
                    while (j < pola_.size() && digit_(pola_[j])) {
                        b = b * 10 + static_cast<unsigned>(pola_[j] - '0');
                        ++j;
                        if (b > 100000) break;
                    }
                } else {
                    b = kTakBesar;
                }
            }
            if (j >= pola_.size() || pola_[j] != '}') return true;
            mn = a;
            mx = b;
            i_ = j + 1;
        } else {
            return true;
        }
        if (!habis() && peek() == '?') {
            malu = true;
            maju();
        } else if (!habis() && peek() == '+') {
            galat_.set("Kuantifier possessif ('+') belum didukung pada regex");
            return false;
        }
        ada = true;
        return true;
    }

    std::shared_ptr<Node> atom() {
        if (habis()) return buat(Kind::Kosong);
        const char c = peek();
        if (c == ')') return nullptr;
        // `|` sudah ditangani oleh kondisi loop `sequentially`. `]` TIDAK:
        // menurut ECMAScript, `]` yang tidak menutup kelas karakter adalah harf
        // biasa. Versi sebelumnya mengembalikan node KOSONG tanpa memakai `]`,
        // jadi loop tidak pernah maju dan `daftar` tumbuh tanpa batas.
        if (c == '|') return buat(Kind::Kosong);
        switch (c) {
            case '(': {
                maju();
                return grup();
            }
            case '[': {
                maju();
                return kelas_karakter();
            }
            case '.': {
                maju();
                return buat(Kind::Titik);
            }
            case '^': {
                maju();
                auto n = buat(Kind::Jangkar);
                n->anchor = 0;
                return n;
            }
            case '$': {
                maju();
                auto n = buat(Kind::Jangkar);
                n->anchor = 1;
                return n;
            }
            case '*':
            case '+':
            case '?': {
                galat_.set(std::string("Kuantifier '") + c + "' tidak punya operasi di depannya");
                return nullptr;
            }
            case '\\': {
                maju();
                return escape();
            }
            default: {
                maju();
                return harf(c);
            }
        }
    }

    std::shared_ptr<Node> sequentially() {
        std::vector<std::shared_ptr<Node>> daftar;
        while (!habis() && peek() != ')' && peek() != '|') {
            const std::size_t sebelum = i_;
            std::shared_ptr<Node> a = atom();
            if (a == nullptr) {
                if (!galat_.ada) galat_.set("Pola regex tidak valid");
                return nullptr;
            }
            // Jaring pengaman: `atom()` WAJIB maju. Kalau suatu saat ada jalur
            // yang tidak, tanpa cek ini program bisa menggantung dan malloc
            // sampai kehabisan memori -- bukan gagal dengan pesan yang berguna.
            if (i_ == sebelum) {
                galat_.set("Pola regex tidak valid: karakter tak terduga di posisi " +
                           std::to_string(i_));
                return nullptr;
            }
            std::size_t mn = 0;
            std::size_t mx = 0;
            bool malu = false;
            bool ada = false;
            if (!baca_kuantifier(mn, mx, malu, ada)) return nullptr;
            if (ada) {
                if (a->jenis == Kind::Jangkar) {
                    galat_.set("Anchor tidak bisa diberi kuantifier pada pola regex");
                    return nullptr;
                }
                auto q = buat(Kind::Kuantifier);
                q->kuant_min = mn;
                q->kuant_maks = mx;
                q->kuant_malu = malu;
                q->anak.push_back(a);
                daftar.push_back(q);
            } else {
                daftar.push_back(a);
            }
        }
        if (daftar.size() == 1) return daftar[0];
        auto n = buat(Kind::Rangkaian);
        n->anak = std::move(daftar);
        return n;
    }

    std::shared_ptr<Node> alternasi() {
        std::vector<std::shared_ptr<Node>> cabang;
        cabang.push_back(sequentially());
        if (galat_.ada) return nullptr;
        while (!habis() && peek() == '|') {
            maju();
            cabang.push_back(sequentially());
            if (galat_.ada) return nullptr;
        }
        if (cabang.size() == 1) return cabang[0];
        auto n = buat(Kind::Alternatif);
        n->anak = std::move(cabang);
        return n;
    }

    static constexpr unsigned kTakBesar = 100000;
    std::string_view pola_;
    std::size_t i_ = 0;
    Galat& galat_;
    bool fold_ = false;
    std::size_t total_kelompok_ = 0;
    std::vector<std::string> nama_kelompok_;
};

// ---------------------------------------------------------------------------
// Pencocokan
// ---------------------------------------------------------------------------

/// Tangkapan kelompok. `caps[2i]` = awal, `caps[2i+1]` = akhir; `npos` = tidak
/// ikut cocok.
using Tangkapan = std::vector<std::size_t>;

class Pencocok {
public:
    /// `langkah_bersama` DIBAJARI di luar: satu pemanggilan `cari` mencoba
    /// banyak posisi mulai, dan anggaran harus berlaku untuk SELURUH
    /// pemanggilan itu -- kalau di-reset per posisi, pola ReDoS cukup lambat
    /// untuk exhausting anggaran tiap posisi tanpa pernah habis.
    Pencocok(const RegexProgram& prog, std::string_view subjek, std::size_t& langkah_bersama)
        : prog_(prog),
          subjek_(subjek),
          caps_(2 * (prog.jumlah_kelompok() + 1), std::string::npos),
          langkah_(langkah_bersama) {}

    using Lanjut = std::function<bool(std::size_t)>;

    /// Entry point publik. Penghitungan langkah ada di `match` (fungsi yang
    /// dipanggil secara rekursif), BUKAN di sini: versi sebelumnya menaruhnya di
    /// wrapper ini saja, dan karena rekursi internal memanggil `match` langsung,
    /// penghitungnya tidak pernah bertambah -- polish yang tidak berguna.
    bool cocok(const std::shared_ptr<RegexProgram::Node>& n, std::size_t pos, const Lanjut& k) {
        return match(n, pos, k);
    }

    [[nodiscard]] const Tangkapan& tangkapan() const { return caps_; }
    [[nodiscard]] bool habis_anggaran() const { return habis_anggaran_; }

private:
    static constexpr std::size_t kNpos = std::string::npos;

    bool sama(char a, char b) const {
        if (a == b) return true;
        if (!prog_.abaikan_besar_kecil()) return false;
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    }

    bool dalam_kelas(const Node& n, char c) const {
        const auto u = static_cast<unsigned char>(c);
        bool ketemu = false;
        for (const auto& [lo, hi] : n.kelas) {
            if (u >= lo && u <= hi) {
                ketemu = true;
                break;
            }
        }
        // Kelas ternegosi (`[^...]`, `\D`, `\W`, `\S`) berkebalikan: karakter
        // yang DITEMUKAN justru yang tidak cocok, dan sebaliknya.
        return n.negasi_kelas ? !ketemu : ketemu;
    }

    bool match(const std::shared_ptr<Node>& n, std::size_t pos, const Lanjut& k) {
        // Anggaran LANGKAH, bukan kedalaman. Backtracking bersifat eksponensial
        // dalam WAKTU: pola `(a+)+b` terhadap 40 huruf 'a' punya 2^40 cabang,
        // tapi kedalaman rekursinya cuma ~80. Batas kedalaman karena itu tidak
        // berguna sama sekali; yang berguna adalah menghitung berapa kali
        // fungsi ini dipanggil.
        if (langkah_ >= prog_.anggaran_langkah()) {
            habis_anggaran_ = true;
            return false;
        }
        ++langkah_;
        switch (n->jenis) {
            case Kind::Kosong:
                return k(pos);
            case Kind::Harf:
                if (pos >= subjek_.size() || !sama(n->harf, subjek_[pos])) return false;
                return k(pos + 1);
            case Kind::Titik: {
                if (pos >= subjek_.size()) return false;
                if (!prog_.titik_semu() && subjek_[pos] == '\n') return false;
                return k(pos + len_utf8(subjek_[pos]));
            }
            case Kind::Kelas:
                if (pos >= subjek_.size() || !dalam_kelas(*n, subjek_[pos])) return false;
                return k(pos + 1);
            case Kind::Jangkar:
                return jangkar(n, pos, k);
            case Kind::Rangkaian:
                return rangkaian(n, 0, pos, k);
            case Kind::Alternatif: {
                for (const auto& a : n->anak) {
                    if (match(a, pos, k)) return true;
                }
                return false;
            }
            case Kind::Grup: {
                if (n->nomor_grup == 0) return match(n->anak[0], pos, k);
                const std::size_t nomor = n->nomor_grup;
                const std::size_t awal_lama = caps_[2 * nomor];
                const std::size_t akhir_lama = caps_[2 * nomor + 1];
                Lanjut lanjut = [this, nomor, pos, &k](std::size_t p) {
                    const std::size_t a = caps_[2 * nomor];
                    const std::size_t b = caps_[2 * nomor + 1];
                    caps_[2 * nomor] = pos;
                    caps_[2 * nomor + 1] = p;
                    if (k(p)) return true;
                    caps_[2 * nomor] = a;
                    caps_[2 * nomor + 1] = b;
                    return false;
                };
                if (match(n->anak[0], pos, lanjut)) return true;
                caps_[2 * nomor] = awal_lama;
                caps_[2 * nomor + 1] = akhir_lama;
                return false;
            }
            case Kind::Kuantifier:
                return kuantifier(n, 0, pos, k);
        }
        return false;
    }

    bool jangkar(const std::shared_ptr<Node>& n, std::size_t pos, const Lanjut& k) {
        switch (n->anchor) {
            case 0:  // `^`
                if (pos == 0) return k(pos);
                if (prog_.multibaris() && subjek_[pos - 1] == '\n') return k(pos);
                return false;
            case 1:  // `$`
                if (pos == subjek_.size()) return k(pos);
                if (prog_.multibaris() && subjek_[pos] == '\n') return k(pos);
                return false;
            case 2:  // `\b`
                return di_batas(pos) ? k(pos) : false;
            default:  // `\B`
                return di_batas(pos) ? false : k(pos);
        }
    }
    bool di_batas(std::size_t pos) const {
        const bool sebelum = pos > 0 && std::isalnum(static_cast<unsigned char>(subjek_[pos - 1])) != 0;
        const bool sesudah = pos < subjek_.size() && std::isalnum(static_cast<unsigned char>(subjek_[pos])) != 0;
        return sebelum != sesudah;
    }

    bool rangkaian(const std::shared_ptr<Node>& n, std::size_t idx, std::size_t pos, const Lanjut& k) {
        if (idx == n->anak.size()) return k(pos);
        return match(n->anak[idx], pos, [this, n, idx, &k](std::size_t p) {
            return rangkaian(n, idx + 1, p, k);
        });
    }

    /// `*`, `+`, `?`, `{n,m}` -- greedy atau lazy.
    ///
    /// `dalam` membatasi kedalaman rekursi. Ini BUKAN sekadar pengaman: pola
    /// `a{100000}` membuat satu tingkat rekursi per pengulangan, dan tiap
    /// tingkat hanya memakai satu langkah -- jadi anggaran langkah tidak
    /// menghentikannya, sementara tiap tingkat memakan frame `std::function`
    /// yang cukup besar. 100000 tingkat berarti puluhan megabyte dan stack
    /// yang hampir habis, dari pola yang tampak "wajar".
    bool kuantifier(const std::shared_ptr<Node>& n, std::size_t hitung, std::size_t pos, const Lanjut& k,
                    int dalam = 0) {
        // `a{100000}` membuat satu tingkat rekursi per pengulangan; tiap tingkat
        // sudah ikut menghitung satu langkah di `match`, jadi anggaran langkah
        // ikut menghentikannya. Batas `dalam` hanya jaring pengaman kedua --
        // kalau anggaran langkah belum habis tapi kedalaman sudah terlalu besar.
        if (dalam > 10000) return k(pos);
        const std::size_t mn = n->kuant_min;
        const std::size_t mx = n->kuant_maks;
        if (!n->kuant_malu) {
            // Greedy: coba satu repetisi lagi dulu.
            if (hitung < mx) {
                // Reposisi nol (`a*` terhadap teks yang tidak bisa maju) dihentikan
                // oleh batas `hitung < mx` di bawah, jadi tidak perlu penanganan
                // khusus di sini.
                Lanjut lanjut = [this, n, hitung, dalam, &k](std::size_t p) {
                    return kuantifier(n, hitung + 1, p, k, dalam + 1);
                };
                if (match(n->anak[0], pos, lanjut)) return true;
            }
            if (hitung >= mn) return k(pos);
            return false;
        }
        // Lazy: coba turun dulu.
        if (hitung >= mn && k(pos)) return true;
        if (hitung >= mx) return false;
        const std::size_t pos_awal = pos;
        return match(n->anak[0], pos, [this, n, hitung, dalam, pos_awal, &k](std::size_t p) {
            if (p == pos_awal) return false;  // reposisi nol: hentikan
            return kuantifier(n, hitung + 1, p, k, dalam + 1);
        });
    }

    const RegexProgram& prog_;
    std::string_view subjek_;
    Tangkapan caps_;
    std::size_t& langkah_;
    bool habis_anggaran_ = false;
};

}  // namespace

std::string_view HasilRegex::kelompok(std::string_view subjek, std::size_t i) const {
    if (i >= awal.size() || awal[i] == std::string::npos) return {};
    if (akhir[i] == std::string::npos || akhir[i] > subjek.size() || akhir[i] < awal[i]) return {};
    return subjek.substr(awal[i], akhir[i] - awal[i]);
}

// ---------------------------------------------------------------------------
// RegexProgram
// ---------------------------------------------------------------------------

bool RegexProgram::cek_flag(std::string_view flag, std::string& galat_keluar) {
    for (const char c : flag) {
        if (c != 'g' && c != 'i' && c != 'm' && c != 's' && c != 'y' && c != 'u') {
            galat_keluar = std::string("Flag regex '") + c +
                           "' ora dikenal. Flag sing didukung: g i m s y u";
            return false;
        }
    }
    return true;
}

std::shared_ptr<RegexProgram> RegexProgram::kompilasi(std::string_view pola, std::string_view flag,
                                                     std::string& galat_keluar) {
    if (!cek_flag(flag, galat_keluar)) return nullptr;
    Galat gb;
    Kompilator k(pola, flag.find('i') != std::string_view::npos, gb);
    std::shared_ptr<Node> akar = k.jalankan();
    if (gb.ada || akar == nullptr) {
        galat_keluar = gb.ada ? gb.pesan : "Pola regex tidak valid: " + std::string(pola);
        return nullptr;
    }
    std::shared_ptr<RegexProgram> p(new RegexProgram());
    p->pola_ = std::string(pola);
    p->flag_ = std::string(flag);
    p->flag_global_ = flag.find('g') != std::string_view::npos;
    p->flag_i_ = flag.find('i') != std::string_view::npos;
    p->flag_m_ = flag.find('m') != std::string_view::npos;
    p->flag_s_ = flag.find('s') != std::string_view::npos;
    p->flag_y_ = flag.find('y') != std::string_view::npos;
    p->nama_kelompok_ = k.nama_kelompok();
    p->jumlah_kelompok_ = k.jumlah_kelompok();
    p->akar = akar;
    return p;
}

bool RegexProgram::cari(std::string_view subjek, std::size_t dari, HasilRegex& hasil) const {
    hasil.cocok = false;
    hasil.awal.clear();
    hasil.akhir.clear();
    batas_terlampaui_ = false;
    if (!akar) return false;
    if (dari > subjek.size()) return false;

    std::size_t langkah_bersama = 0;
    for (std::size_t p = dari;; ++p) {
        Pencocok m(*this, subjek, langkah_bersama);
        Pencocok::Lanjut berhenti = [&](std::size_t akhir) {
            hasil.cocok = true;
            const Tangkapan& c = m.tangkapan();
            hasil.awal.resize(c.size() / 2);
            hasil.akhir.resize(c.size() / 2);
            for (std::size_t i = 0; i < hasil.awal.size(); ++i) {
                hasil.awal[i] = c[2 * i];
                hasil.akhir[i] = c[2 * i + 1];
            }
            // Kelompok 0 (seluruh pencocokan) tidak dicatat oleh pencocok --
            // ia tidak punya nomor grup. Awalnya = posisi mulai pencocokan,
            // akhirnya = posisi yang diteruskan continuation.
            hasil.awal[0] = p;
            hasil.akhir[0] = akhir;
            return true;
        };
        if (m.cocok(akar, p, berhenti)) return true;
        if (m.habis_anggaran()) {
            // "Tidak tahu" != "tidak cocok".
            batas_terlampaui_ = true;
            return false;
        }
        if (p >= subjek.size()) break;
        if (flag_y_) return false;  // lengket: tidak boleh menggeser titik awal
    }
    return false;
}

bool RegexProgram::kabeh(std::string_view subjek, HasilRegex& hasil) const {
    hasil.cocok = false;
    batas_terlampaui_ = false;
    if (!akar) return false;
    std::size_t langkah_bersama = 0;
    Pencocok m(*this, subjek, langkah_bersama);
    Pencocok::Lanjut berhenti = [&](std::size_t akhir) {
        if (akhir != subjek.size()) return false;
        hasil.cocok = true;
        const Tangkapan& c = m.tangkapan();
        hasil.awal.resize(c.size() / 2);
        hasil.akhir.resize(c.size() / 2);
        for (std::size_t i = 0; i < hasil.awal.size(); ++i) {
            hasil.awal[i] = c[2 * i];
            hasil.akhir[i] = c[2 * i + 1];
        }
        hasil.awal[0] = 0;
        hasil.akhir[0] = akhir;
        return true;
    };
    if (m.cocok(akar, 0, berhenti)) return true;
    if (m.habis_anggaran()) batas_terlampaui_ = true;
    return false;
}

std::string RegexProgram::ganti(std::string_view subjek, std::string_view ganti,
                                std::size_t jumlah_maks) const {
    std::string keluar;
    std::size_t p = 0;
    std::size_t n = 0;
    while (p <= subjek.size()) {
        if (jumlah_maks != static_cast<std::size_t>(-1) && n >= jumlah_maks) break;
        HasilRegex h;
        if (!cari(subjek, p, h)) {
            if (batas_terlampaui_) break;  // hasil sebagian; lihat catatan di header
            break;
        }
        keluar.append(subjek.substr(p, h.awal[0] - p));
        for (std::size_t i = 0; i < ganti.size(); ++i) {
            if (ganti[i] != '$' || i + 1 >= ganti.size()) {
                keluar.push_back(ganti[i]);
                continue;
            }
            const char c = ganti[i + 1];
            if (c == '&') {
                keluar.append(h.kelompok(subjek, 0));
                ++i;
            } else if (c >= '0' && c <= '9') {
                keluar.append(h.kelompok(subjek, static_cast<std::size_t>(c - '0')));
                ++i;
            } else if (c == '$') {
                keluar.push_back('$');
                ++i;
            } else {
                keluar.push_back('$');
            }
        }
        const std::size_t akhir = h.awal[0] + (h.akhir[0] - h.awal[0]);
        if (akhir == h.awal[0]) {
            // Kecocokan kosong: teruskan satu karakter supaya tidak berulang.
            if (akhir < subjek.size()) keluar.push_back(subjek[akhir]);
            p = akhir + 1;
        } else {
            p = akhir;
        }
        ++n;
    }
    if (p < subjek.size()) keluar.append(subjek.substr(p));
    return keluar;
}

std::vector<HasilRegex> RegexProgram::semua(std::string_view subjek, std::size_t jumlah_maks) const {
    std::vector<HasilRegex> keluar;
    std::size_t p = 0;
    while (p <= subjek.size()) {
        if (jumlah_maks != static_cast<std::size_t>(-1) && keluar.size() >= jumlah_maks) break;
        HasilRegex h;
        if (!cari(subjek, p, h)) {
            if (batas_terlampaui_) break;
            break;
        }
        keluar.push_back(h);
        const std::size_t akhir = h.awal[0] + (h.akhir[0] - h.awal[0]);
        if (akhir == h.awal[0]) {
            if (akhir >= subjek.size()) break;
            p = akhir + 1;
        } else {
            p = akhir;
        }
    }
    return keluar;
}

}  // namespace jawa::rt
