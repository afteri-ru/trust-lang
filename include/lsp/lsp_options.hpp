#pragma once

// include/lsp/lsp_options.hpp
// LSP-специфичная обработка опций анализа: строка шебанга файла и применение опций ПО ИСТОЧНИКУ
// (шебанг vs окружение) с атрибуцией ошибок.
//
// Принцип разделения (см. MEMORY): общие опции проекта (--solver-mode, --keywords,
// -fsolver-loop-unroll, -W...) обрабатываются централизованно (pipeline/analysis_options.hpp,
// единый источник commonAnalysisOptions); специфичные для файла/приложения (шебанг) - только в
// LSP-слое. Здесь - только LSP-специфичная логика поверх общего applyAnalysisArgs.

#include "lsp/lsp_protocol.h"
#include "pipeline/analysis_options.hpp"

#include <functional>
#include <string>
#include <vector>

namespace trust {
namespace lsp {

/// Извлекает опции компилятора из строки шебанга файла (`#!<interpreter> [options]`):
/// возвращает токены ПОСЛЕ пути интерпретатора (сам путь пропускается). Если первая строка
/// не является шебангом (`#!`) - возвращает пустой список.
/// Семантика: для trust строка `#!` - это комментарий для лексера; опции доходят до компилятора
/// через argv ОС при запуске скрипта. LSP, открывающий файл как текст, сам извлекает эти опции,
/// чтобы анализ соответствовал реальному запуску.
inline std::vector<std::string> extractShebangOptions(const std::string& text) {
    std::vector<std::string> out;
    if (text.size() < 2 || text[0] != '#' || text[1] != '!') {
        return out;
    }
    // Первая строка до '\n' (без учёта возможного '\r' в конце).
    std::size_t nl = text.find('\n');
    std::string line = (nl == std::string::npos) ? text : text.substr(0, nl);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    std::istringstream ss(line.substr(2));
    std::string tok;
    bool first = true; // первый токен - путь интерпретатора, пропускаем
    while (ss >> tok) {
        if (first) {
            first = false;
            continue;
        }
        out.push_back(std::move(tok));
    }
    return out;
}

/// Применяет опции анализа (окружение `env` + шебанг `shebang`) к `opts` в порядке, заданном
/// `mode`, отчитываясь об ошибках ПО ИСТОЧНИКУ через `onError(msg, fromShebang)`. Ошибки
/// `applyAnalysisArgs` возвращает строкой; здесь они передаются наружу, чтобы LSP мог показать
/// ошибку шебанга диагностикой на строке шебанга, а ошибку окружения - в лог.
///
/// Последовательное применение: каждая следующая опция переопределяет предыдущую (как в argv
/// trust), поэтому порядок задаёт приоритет:
///   EnvAfterShebang  = shebang затем env  (env сильнее);
///   EnvBeforeShebang = env затем shebang   (shebang сильнее).
inline void applyAnalysisArgsBySource(trust::Options& opts, const std::vector<std::string>& env, const std::vector<std::string>& shebang, ShebangMode mode,
                                      const std::function<void(const std::string& msg, bool fromShebang)>& onError) {
    auto applyOne = [&](const std::vector<std::string>& args, bool fromShebang) {
        const std::string err = trust::applyAnalysisArgs(opts, args);
        if (!err.empty()) {
            onError(err, fromShebang);
        }
    };
    switch (mode) {
    case ShebangMode::Ignore:
        applyOne(env, false);
        break;
    case ShebangMode::ShebangOnly:
        applyOne(shebang, true);
        break;
    case ShebangMode::EnvAfterShebang:
        applyOne(shebang, true);
        applyOne(env, false);
        break;
    case ShebangMode::EnvBeforeShebang:
        applyOne(env, false);
        applyOne(shebang, true);
        break;
    }
}

} // namespace lsp
} // namespace trust
