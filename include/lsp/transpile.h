#ifndef TRUST_LSP_TRANSPILE_H
#define TRUST_LSP_TRANSPILE_H

#include "debug/trust_source.h"

#include <string>
#include <string_view>
#include <vector>

namespace trust {
namespace lsp {

// ── Результат транспиляции ──
struct TranspileResult {
    std::vector<std::string> cppLines;          // сгенерированные строки C++
    std::unique_ptr<TrustSource> sourceMap;     // mapping trust → cpp
    std::vector<std::string> errors;            // ошибки транспиляции (если есть)
};

// Транспилирует Trust-код в C++ и строит source map in-process.
// trustCode — исходный код на Trust.
// trustFileName — имя файла для source map (может быть пустым).
// basePath — базовый путь для нормализации (может быть пустым).
TranspileResult transpileTrustSource(std::string_view trustCode,
                                     std::string_view trustFileName,
                                     std::string_view basePath = "");

} // namespace lsp
} // namespace trust

#endif // TRUST_LSP_TRANSPILE_H