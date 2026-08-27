#pragma once

// include/pipeline/analysis_options.hpp
// Единая точка применения ОПЦИЙ АНАЛИЗА (диагностики + поведенческие флаги) к
// diag::Options. Разделяет два источника опций (конвенция GCC/Clang):
//   - `-W<name>=<sev>`  - severity-диагностики (применяются через applyDiagnostics);
//   - `--solver-mode=`, `--keywords=`, `-fsolver-loop-unroll` - ПОВЕДЕНЧЕСКИЕ флаги,
//     выставляемые в diag::Options (FlagKind::SolverMode / Keywords / SolverLoopUnroll).
//
// Используется единообразно в trust (CLI compile), pipeline (CLI format),
// trust-lsp (transpileSource / handleFormatting) и html_emit (--json/--html), чтобы
// «форматирующий» и «штатный» пути применяли ОДИН набор опций анализа, а не дублировали
// логику в каждом бинарнике. Разбор произвольного списка опций (в т.ч. из шебанга файла)
// делегируется arity-aware парсеру драйвера trust (parseDriverArgs), поэтому определения
// опций не дублируются.

#include "diag/options.hpp"
#include "pipeline/cli.hpp"
#include "pipeline/pipeline.hpp"
#include "semantic/diag.hpp"

#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace trust {

/// Применяет ПОВЕДЕНЧЕСКИЕ флаги анализа из уже разобранного ParseResult к Options:
/// `--solver-mode` → FlagKind::SolverMode, `--keywords` → FlagKind::Keywords,
/// `-fsolver-loop-unroll` → FlagKind::SolverLoopUnroll.
/// Опции исполнения/линковки (--run, -o, -l, --compiler, ...) НЕ являются опциями
/// анализа и здесь игнорируются (это не тихая подмена — они разобраны в ParseResult,
/// просто не имеют смысла для diag::Options).
/// Возвращает сообщение об ошибке применения (пусто = успех), напр. невалидное значение
/// `--solver-mode=bogus` (set_flag_value возвращает false - нет тихого пропуска).
inline std::string applyBehavioralFlags(Options& opts, const ParseResult& result) {
    std::string err;
    if (!result.opts.solver_mode.empty() && !opts.set_flag_value(semantic::FlagKind::SolverMode, result.opts.solver_mode)) {
        // Имя опции берём из самой опции (flagName из реестра), а не хардкодим.
        err = "invalid value for --" + std::string(flagName(semantic::FlagKind::SolverMode)) + ": '" + result.opts.solver_mode + "'";
    }
    if (result.opts.solver_loop_unroll) {
        opts.set_enabled(semantic::FlagKind::SolverLoopUnroll, true);
    }
    if (!result.opts.keywords.empty() && !opts.set_flag_value(semantic::FlagKind::Keywords, result.opts.keywords)) {
        if (!err.empty()) {
            err += "; ";
        }
        err += "invalid value for --" + std::string(flagName(semantic::FlagKind::Keywords));
    }
    return err;
}

/// Применяет полный набор опций анализа из ParseResult: `-W`-диагностики (через
/// applyDiagnostics, может бросить std::invalid_argument на неизвестную -W-опцию) +
/// поведенческие флаги (applyBehavioralFlags). Единая точка для trust.cpp и для разбора
/// произвольного списка опций (applyAnalysisArgs). Возвращает ошибку применения поведенческих
/// флагов (пусто = успех).
inline std::string applyAnalysisOptions(Options& opts, const ParseResult& result) {
    // -W-диагностики: applyDiagnostics -> Options::parse_argv (останавливается на первом не -W).
    std::vector<std::string> diag = result.diag_args;
    std::vector<char*> wargv;
    wargv.reserve(diag.size());
    for (auto& s : diag) {
        wargv.push_back(const_cast<char*>(s.data()));
    }
    if (!wargv.empty()) {
        applyDiagnostics(opts, wargv);
    }
    return applyBehavioralFlags(opts, result);
}

/// Разбирает произвольный список опций (смесь `-W...` и поведенческих флагов, в т.ч.
/// взятых из шебанга файла) arity-aware парсером драйвера trust (parseDriverArgs) и
/// применяет к Options только опции анализа. Игнорирует опции исполнения/линковки.
/// Возвращает сообщение об ошибке разбора/применения (пусто = успех); даже при ошибке
/// применяются те опции, которые успели разобраться. Ошибки НЕ печатаются здесь - это
/// обязанность вызывающего (CLI печатает в errs(), LSP сообщает диагностикой по источнику).
inline std::string applyAnalysisArgs(Options& opts, std::span<const std::string> args) {
    if (args.empty()) {
        return {};
    }
    // parseDriverArgs ожидает полный argv (элемент [0] - имя программы и пропускается).
    // Здесь argv[0] отсутствует - подставляем фиктивное имя, чтобы первая опция списка
    // не «съедалась» как имя программы.
    std::vector<std::string> mutable_args;
    mutable_args.reserve(args.size() + 1);
    mutable_args.emplace_back("trust");
    mutable_args.insert(mutable_args.end(), args.begin(), args.end());
    ParseResult result;
    std::string err = parseDriverArgs(result, mutable_args);
    try {
        if (std::string applyErr = applyAnalysisOptions(opts, result); !applyErr.empty()) {
            if (!err.empty()) {
                err += "; ";
            }
            err += applyErr;
        }
    } catch (const std::invalid_argument& e) {
        // Неизвестная -W-опция (Options::parse_argv бросает invalid_argument).
        if (!err.empty()) {
            err += "; ";
        }
        err += e.what();
    }
    return err;
}

} // namespace trust
