// Chunk bytecode: satu fungsi = satu Chunk.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <cstdint>
#include <vector>

#include "rt/value.h"
#include "support/arena.h"
#include "support/source_map.h"

namespace jawa::rt {
class TeksObj;
}  // namespace jawa::rt

namespace jawa::vm {

/// Opcode (dari opcodes.def).
enum class Op : uint8_t {
#define JAWA_OP(Nama, baris_kode, tanda, grup) Nama,
#include "opcodes.def"
#undef JAWA_OP
    JumlahOp
};

/// Jumlah operand (0, 1, atau 2) untuk opcode.
[[nodiscard]] const char* opcode_nama(Op op) noexcept;
[[nodiscard]] const char* opcode_tanda(Op op) noexcept;
[[nodiscard]] const char* opcode_grup(Op op) noexcept;
[[nodiscard]] int opcode_jumlah_operand(Op op) noexcept;
[[nodiscard]] int opcode_ukuran(Op op) noexcept;

/// Satu instruksi pada waktu kompilasi.
struct Instruksi {
    Op op = Op::NOP;
    std::uint16_t a = 0;   ///< operand 1
    std::uint16_t b = 0;   ///< operand 2
    std::uint32_t baris = 0;
};

/// Rentang handler exception dalam satu chunk.
struct HandlerTable {
    struct Entri {
        std::uint32_t mulai = 0;   ///< ip awal area ter-lindungi
        std::uint32_t selesai = 0; ///< ip akhir
        std::uint32_t tangkep = 0; ///< ip handler tangkep
        std::uint32_t intriguasan = 0;  ///< ip handler intriguasan (0 = tidak ada)
        std::uint16_t kedalaman = 0;    ///< kedalaman stack saat masuk try
    };
    std::vector<Entri> entri;
};

struct Chunk;
using ChunkPtr = std::shared_ptr<Chunk>;

/// Satu fungsi terkompilasi.
struct Chunk {
    std::string_view nama;
    std::vector<Instruksi> kode;
    std::vector<rt::Value> konstanta;
    std::vector<rt::Value> nama_properti;  ///< interned, untuk GET_PROP
    std::vector<std::string_view> upvalue;  ///< nama upvalue (untuk debug)
    /// Fungsi anak (dikumpulkan `CLOSURE`): `CLOSURE i` membuat closure untuk
    /// `anak[i]`.
    std::vector<ChunkPtr> anak;
    /// Asal tiap upvalue: `>= 0` = slot lokal fungsi ini; `< 0` = upvalue
    /// `-(n+1)` dari fungsi induk. Mengikuti algoritme closure Lox.
    std::vector<std::int32_t> upvalue_sumber;
    std::vector<std::int32_t> lompat_palsu;  ///< daftar patch sementara
    std::vector<std::size_t> daftar_lompat_palsu;
    std::vector<std::uint32_t> jaring_loop;  ///< loop start untuk patching
    support::LineMap peta_baris;
    HandlerTable handler;
    std::uint8_t jumlah_param = 0;
    std::uint8_t n_argumen_tetap = 0;
    std::uint8_t jumlah_slot = 0;      ///< total local slot (termasuk temporer)
    std::uint8_t jumlah_upvalue = 0;
    std::uint8_t jumlah_temporer = 0;
    bool variadic = false;
    bool panah = false;
    bool mengko = false;
    bool generator = false;
    /// Modul ini memakai `entani` di tingkat modul (top-level await), jadi
    /// frame modul HARUS boleh menjadi akar rantai async. Tanpa tanda ini,
    /// panggilan `mengko` di tingkat modul akan ikut menunda modul (lalu
    /// `dhisik` tercetak setelah `42`), padahal `dhisik` harus lebih dulu.
    bool await_tingkat_modul = false;
    bool tail_call = false;
    bool unreachable = false;

    [[nodiscard]] std::size_t ukuran_kode() const noexcept { return kode.size(); }
    void kosongkan_jump(std::size_t di) { lompat_palsu[di] = -1; }
    void patch_jump(std::size_t di, std::uint16_t tujuan) {
        lompat_palsu[di] = static_cast<std::int32_t>(tujuan);
    }
    std::uint16_t emit(Op op, std::uint16_t a = 0, std::uint16_t b = 0, std::uint32_t baris = 0);
    std::uint16_t emit_jump(Op op, std::uint32_t baris = 0);
    std::uint16_t emit_jump_palsu(Op op, std::uint32_t baris = 0);
    void patch_di(std::size_t idx, std::uint16_t tujuan);
    std::size_t tambah_konstanta(rt::Value v);
    std::size_t tambah_nama_properti(rt::Value v);
    std::size_t tambah_nama_upvalue(std::string_view n);
};



}  // namespace jawa::vm
