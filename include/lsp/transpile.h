#ifndef TRUST_LSP_TRANSPILE_H
#define TRUST_LSP_TRANSPILE_H

#include "diag/context.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace trust {
namespace lsp {

// Потоковый транспилятор Trust → C++ с построением source map.
// Обрабатывает trustCode строка за строкой, без предварительного разбиения.
// trustFileName/cppFileName используются для регистрации source/output в ctx.
// Ошибки сообщаются через ctx.report() / ctx.diag().
// Возвращает пару (FileIdx входного файла, FileIdx выходного файла).
std::pair<MapperFile, MapperFile> transpile(std::string_view trustCode, std::string_view trustFileName, std::string_view cppFileName, Context& ctx);

} // namespace lsp
} // namespace trust

#endif // TRUST_LSP_TRANSPILE_H