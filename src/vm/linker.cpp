// ===========================================================================
// Linker modul ES
//
// `impor { a, b } saka "./modul.jw"` dan `ekspor` butuh modul nyata, bukan
// objek kosong. Linker di sini:
//   1. menyelesaikan spesifikasi (path relatif terhadap direktori pengimpor),
//   2. membaca & mengompilasi berkas (dengan cache per path kanonik),
//   3. mengevaluasi body modul di frame-nya sendiri dengan cakupan global
//      terpisah,
//   4. mengembalikan objek ekspor (nama -> nilai).
//
// Modul dievaluasi di tengah loop bytecode yang sedang berjalan (modul A
// mengimpor B sementara frame A masih hidup), jadi ini memakai frame datar
// yang sama -- bukan loop bersarang, bukan fiber.
// ===========================================================================

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include "compile/compiler.h"
#include "lex/lexer.h"
#include "parse/parser.h"
#include "rt/object.h"
#include "rt/string.h"
#include "stdlib/stdlib.h"
#include "vm/vm.h"

namespace jawa::vm {

using rt::ClosureObj;
using rt::FungsiObj;
using rt::OK;
using rt::Value;

namespace {

/// Direktori dari sebuah path ("/a/b/c.jw" -> "/a/b", "c.jw" -> "").
std::string direktori_dari(const std::string& path) {
    const std::size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return {};
    return path.substr(0, pos);
}

/// Normalisasi sederhana: buang "./", rangkap "/", dan sediakan "." untuk
/// path relatif. Tidak menyentuh symlink atau path absolut sistem -- cukup
/// untuk membuat kunci cache konsisten antar platform yang kita dukung.
std::string normalkan(std::string p) {
    std::string hasil;
    hasil.reserve(p.size());
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i] == '/' && !hasil.empty() && hasil.back() == '/') continue;
        hasil.push_back(p[i]);
        if (p[i] == '/' && i + 2 < p.size() && p[i + 1] == '.' && p[i + 2] == '/') ++i;
    }
    return hasil;
}

}  // namespace

// ===========================================================================
// Resolusi path
// ===========================================================================

std::string VM::selesaikan_path(const std::string_view spesifikasi, const std::string_view dari_dir) {
    std::string s(spesifikasi);
    if (s.empty()) return normalkan(std::string(dari_dir));
    if (s.front() == '/' || s.front() == '\\') return normalkan(s);
    if (s.rfind("./", 0) == 0 || s.rfind("../", 0) == 0) {
        return normalkan(std::string(dari_dir) + "/" + s);
    }
    return normalkan(std::string(dari_dir) + "/" + s);
}

// ===========================================================================
// Kompilasi
// ===========================================================================

ClosureObj* VM::kompilasi_modul(const std::string_view sumber, const std::string_view nama_berkas,
                                const std::string_view dir) {
    support::DiagnosticBag bag{50};
    lex::TokenList token;
    lex::Lexer lx(sumber, nama_berkas, dir, lex::LexOptions{opt_.strict_titik_koma, opt_.strict_krama});
    lx.lex_semua(token);
    bag.gabung(lx.bag());
    if (bag.ada_galat()) {
        for (const support::Diagnostic& d : bag.galat()) {
            const std::string s = d.format();
            std::fwrite(s.data(), 1, s.size(), stderr);
        }
        return nullptr;
    }

    parse::Parser parser(token, arena_scratch_, bag, nama_berkas);
    const ast::NodePtr prog = parser.parse_program();
    if (bag.ada_galat()) {
        for (const support::Diagnostic& d : bag.galat()) {
            const std::string s = d.format();
            std::fwrite(s.data(), 1, s.size(), stderr);
        }
        return nullptr;
    }

    compile::Compiler kompiler(heap_, bag, nama_berkas);
    const compile::HasilKompilasi hasil = kompiler.compile(static_cast<const ast::Program*>(prog));
    if (bag.ada_galat() || hasil.modul == nullptr) {
        for (const support::Diagnostic& d : bag.galat()) {
            const std::string s = d.format();
            std::fwrite(s.data(), 1, s.size(), stderr);
        }
        return nullptr;
    }

    // `FungsiObj::nama` dan `Chunk::nama` adalah `string_view`, jadi nama
    // modul harus punya pemilik yang bertahan. `singsahan_teks_` adalah deque
    // (alamat elemen stabil) dan hanya dibersihkan bersama VM.
    singsahan_teks_.push_back(std::string(nama_berkas));
    const std::string_view nama_tetap = singsahan_teks_.back();

    auto* fn = heap_.alokasi<FungsiObj>();
    fn->h.kind = OK::Fungsi;
    fn->kode = hasil.modul;
    fn->nama = nama_tetap;

    auto* clo = heap_.alokasi<ClosureObj>();
    clo->h.kind = OK::Closure;
    clo->fungsi = fn;
    // Nama closure = path modul, supaya jejak stack galat menyebut berkas yang
    // benar (bukan `<modul>` untuk semua modul).
    clo->nama = nama_tetap;
    return clo;
}

// ===========================================================================
// Evaluasi
// ===========================================================================

Value VM::evaluasi_modul(ModuleRecord* rec) {
    if (rec == nullptr) return Value::mboh();
    if (rec->dievaluasi) return rec->ekspor;

    ObyekObj* ekspor = buat_obyek();
    rec->ekspor = Value::obyek(ekspor);
    rec->sedang = true;
    modul_tumpukan_.push_back(rec);
    modul_aktif = rec;
    // Simpan/smulih posisi sumber supaya pesan galat & jejak stack menunjuk
    // modul yang sedang berjalan, bukan modul pemanggil.
    const std::string pos_lama = std::move(pos_berkas_);
    const SourcePos sumber_lama = pos_sumber_;
    pos_berkas_ = rec->path;

    // Frame modul: `this` di stack 0, lalu seluruh slot lokal modul.
    const std::size_t dasar = stack_.size();
    // Ambang loop BERBASI FRAME, bukan indeks stack: `dasar` tidak boleh
    // dipakai di sini, karena loop berhenti saat `frames_.size()` turun ke
    // jumlah frame sebelum modul ini didorong.
    const std::size_t frame_awal = frames_.size();
    const std::size_t jumlah_slot = rec->entri->fungsi->kode->jumlah_slot;
    dorong(Value::mboh());
    dorong(Value::obyek(rec->entri));
    while (stack_.size() < dasar + std::max<std::size_t>(jumlah_slot, 2u)) {
        dorong(Value::mboh());
    }

    Frame f;
    f.closure = rec->entri;
    f.chunk = rec->entri->fungsi->kode.get();
    f.ip = 0;
    f.slot_base = dasar;
    f.n_argumen = 0;
    f.this_val = Value::mboh();
    if (f.chunk->await_tingkat_modul) {
        f.akar_async = true;
        dasar_async_ = frames_.size();
    }
    frames_.push_back(std::move(f));

    const std::size_t langkah_awal = langkah_;
    langkah_ = 0;
    const Status s = jalankan_loop(frame_awal + 1);
    langkah_ = langkah_awal;

    modul_tumpukan_.pop_back();
    modul_aktif = modul_tumpukan_.empty() ? nullptr : modul_tumpukan_.back();
    pos_berkas_ = std::move(pos_lama);
    pos_sumber_ = sumber_lama;
    rec->sedang = false;

    if (s != Status::Selesai) {
        // Galat di dalam modul: hapus supaya importer berikutnya tidak memakai
        // modul separuh jadi, lalu laporkan sebagai galat program.
        rec->dievaluasi = false;
        rec->ekspor = Value::mboh();
        stack_.resize(dasar);
        return Value::mboh();
    }
    stack_.resize(dasar);
    rec->dievaluasi = true;
    return rec->ekspor;
}

// ===========================================================================
// Pemuatan
// ===========================================================================

Value VM::muat_modul(const std::string_view spesifikasi, const std::string_view dari_dir) {
    if (spesifikasi.rfind("std:", 0) == 0) {
        // Modul bawaan pustaka standar: objek global sebagai namespace.
        const std::string_view nama = spesifikasi.substr(4);
        const Value g = stdlib::ambil_global(*this, nama);
        if (g.is_obyek()) return g;
        lempar(buat_kleru("KleruModul",
                         "Modul bawaan \"" + std::string(nama) + "\" ora ana ing pustaka standar."));
        return Value::mboh();
    }

    const std::string path = selesaikan_path(spesifikasi, dari_dir);
    if (ModuleRecord* ada = cari_modul(path); ada != nullptr) {
        if (ada->dievaluasi) return ada->ekspor;
        if (ada->sedang) {
            // Impor siklik: kembalikan objek ekspor yang sedang terisi supaya
            // rantai tidak menggantung. Isi finalnya baru lengkap setelah
            // modul selesai dievaluasi.
            ada->impor_siklik = true;
            if (ada->ekspor.is_obyek()) return ada->ekspor;
            ObyekObj* o = buat_obyek();
            ada->ekspor = Value::obyek(o);
            return ada->ekspor;
        }
    }

    std::string sumber;
    if (opt_.baca_berkas) {
        if (!opt_.baca_berkas(path, sumber)) {
            lempar(buat_kleru("KleruModul", "Ora bisa maca modul \"" + path + "\"."));
            return Value::mboh();
        }
    } else {
        std::ifstream f(path, std::ios::binary);
        if (!f) {
            lempar(buat_kleru("KleruModul", "Ora bisa maca modul \"" + path + "\"."));
            return Value::mboh();
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        sumber = ss.str();
    }

    const std::string nama_berkas = path;
    ClosureObj* clo = kompilasi_modul(sumber, nama_berkas, direktori_dari(path));
    if (clo == nullptr) {
        lempar(buat_kleru("KleruModul", "Gagal kompilasi modul \"" + path + "\"."));
        return Value::mboh();
    }

    // Daftarkan ke cache SEBELUM evaluasi supaya impor siklik menemukan
    // entri yang sama (dan bisa memakai objek ekspor yang sedang terisi).
    daftarkan_modul(path, path, sumber);
    ModuleRecord* rec = cari_modul(path);
    if (rec == nullptr) {
        lempar(buat_kleru("KleruModul", "Gagal mendaftarkan modul \"" + path + "\"."));
        return Value::mboh();
    }
    rec->entri = clo;
    return evaluasi_modul(rec);
}

}  // namespace jawa::vm
