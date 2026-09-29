// Cetak AST Basa Jawa (untuk `jawa ast`).
#pragma once

#include <string>

#include "parse/ast.h"

namespace jawa::parse {

/// Kembalikan representasi AST ter-indent untuk ditampilkan pengguna.
[[nodiscard]] std::string cetak_ast(const ast::Program* p);

}  // namespace jawa::parse
