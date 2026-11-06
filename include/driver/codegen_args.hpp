// Нормализация codegen-релевантных опций: единственный источник строки `@opts` записи кеша
// `--run` и шапки `.cppt` (`trust-options`). Владелец — модель опций (`driver`); читают
// `pipeline` (шапка `.cppt`) и `toolchain` (запись/валидация кеша) без зависимости друг от друга.
#pragma once

#include "driver/options.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace trust {

/// Нормализованная строка фактически заданных codegen-опций (сортировка + санитайз `\n`/`\t`).
[[nodiscard]] inline std::string codegenArgsRecord(const PipelineOpts& opts) {
    std::vector<std::string> args = opts.codegen_args;
    std::sort(args.begin(), args.end());
    std::string out;
    for (const auto& a : args) {
        if (!out.empty()) {
            out += ' ';
        }
        for (const char c : a) {
            out += (c == '\n' || c == '\t') ? ' ' : c;
        }
    }
    return out;
}

} // namespace trust
