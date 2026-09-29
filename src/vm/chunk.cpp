#include "chunk.h"

#include <array>

namespace jawa::vm {
namespace {

struct OpInfo {
    const char* nama;
    const char* tanda;
    const char* grup;
    int operand;
};

const std::array<OpInfo, static_cast<std::size_t>(Op::JumlahOp)>& tabel() {
    static const std::array<OpInfo, static_cast<std::size_t>(Op::JumlahOp)> t = [] {
        std::array<OpInfo, static_cast<std::size_t>(Op::JumlahOp)> a{};
        std::size_t k = 0;
#define JAWA_OP(Nama, baris_kode, tanda, grup) a[k++] = OpInfo{#Nama, tanda, grup, baris_kode};
#include "opcodes.def"
#undef JAWA_OP
        return a;
    }();
    return t;
}

}  // namespace

const char* opcode_nama(Op op) noexcept {
    const auto i = static_cast<std::size_t>(op);
    return i < tabel().size() ? tabel()[i].nama : "?";
}

const char* opcode_tanda(Op op) noexcept {
    const auto i = static_cast<std::size_t>(op);
    return i < tabel().size() ? tabel()[i].tanda : "?";
}

const char* opcode_grup(Op op) noexcept {
    const auto i = static_cast<std::size_t>(op);
    return i < tabel().size() ? tabel()[i].grup : "?";
}

int opcode_jumlah_operand(Op op) noexcept {
    const auto i = static_cast<std::size_t>(op);
    return i < tabel().size() ? tabel()[i].operand : 0;
}

int opcode_ukuran(Op op) noexcept { return 1 + 2 * opcode_jumlah_operand(op); }

// ---------------------------------------------------------------------------
// Chunk
// ---------------------------------------------------------------------------

std::uint16_t Chunk::emit(Op op, std::uint16_t a, std::uint16_t b, std::uint32_t baris) {
    Instruksi ins;
    ins.op = op;
    ins.a = a;
    ins.b = b;
    ins.baris = baris;
    kode.push_back(ins);
    if (baris != 0) peta_baris.add(static_cast<std::uint32_t>(kode.size() - 1), baris, 1);
    return static_cast<std::uint16_t>(kode.size() - 1);
}

std::uint16_t Chunk::emit_jump(Op op, std::uint32_t baris) { return emit(op, 0, 0, baris); }

std::uint16_t Chunk::emit_jump_palsu(Op op, std::uint32_t baris) {
    const std::uint16_t di = emit(op, 0, 0, baris);
    daftar_lompat_palsu.push_back(lompat_palsu.size());
    lompat_palsu.push_back(-1);
    return di;
}

void Chunk::patch_di(std::size_t idx, std::uint16_t tujuan) {
    if (idx >= lompat_palsu.size()) return;
    lompat_palsu[idx] = static_cast<std::int32_t>(tujuan);
}

std::size_t Chunk::tambah_konstanta(rt::Value v) {
    for (std::size_t i = 0; i < konstanta.size(); ++i) {
        if (konstanta[i] == v) return i;
    }
    konstanta.push_back(v);
    return konstanta.size() - 1;
}

std::size_t Chunk::tambah_nama_properti(rt::Value v) {
    for (std::size_t i = 0; i < nama_properti.size(); ++i) {
        if (nama_properti[i] == v) return i;
    }
    nama_properti.push_back(v);
    return nama_properti.size() - 1;
}

std::size_t Chunk::tambah_nama_upvalue(std::string_view n) {
    for (std::size_t i = 0; i < upvalue.size(); ++i) {
        if (upvalue[i] == n) return i;
    }
    upvalue.push_back(n);
    return upvalue.size() - 1;
}

}  // namespace jawa::vm
