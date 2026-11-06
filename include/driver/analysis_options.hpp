#pragma once

#include "diag/flag_ids.hpp"
#include "diag/options.hpp"
#include "driver/cli.hpp"
#include "driver/options.hpp"

#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace trust {

/// Применяет ПОВЕДЕНЧЕСКИЕ флаги анализа из разобранного ParseResult к Options. Опции
/// исполнения/линковки (--run, -o, -l, --compiler, ...) не являются опциями анализа и игнорируются
/// (не тихая подмена: они разобраны в ParseResult, просто не имеют смысла для diag::Options).
/// Возвращает сообщение об ошибке применения (пусто = успех); тихого пропуска нет.
inline std::string applyBehavioralFlags(Options& opts, const ParseResult& result) {
    std::string err;
    if (result.opts.error_limit) {
        opts.setErrorLimit(*result.opts.error_limit);
    }
    if (!result.opts.solver_mode.empty() && !opts.setFlagValueByName(diag::flag_id::kSolverMode, result.opts.solver_mode)) {
        // Имя опции берём из нейтрального реестра идентификаторов (diag/flag_ids.hpp).
        err = "invalid value for --" + std::string(diag::flag_id::kSolverMode) + ": '" + result.opts.solver_mode + "'";
    }
    if (result.opts.solver_loop_unroll) {
        opts.setEnabledByName(diag::flag_id::kSolverLoopUnroll, true);
    }
    opts.setEnabledByName(diag::flag_id::kComments, result.opts.comments);
    opts.setEnabledByName(diag::flag_id::kOverflowCheck, result.opts.overflow_check);
    if (!result.opts.verify.empty() && !opts.setFlagValueByName(diag::flag_id::kVerify, result.opts.verify)) {
        if (!err.empty()) {
            err += "; ";
        }
        err += "invalid value for --" + std::string(diag::flag_id::kVerify) + ": '" + result.opts.verify + "' (expected: all)";
    }
    if (!result.opts.keywords.empty() && !opts.setFlagValueByName(diag::flag_id::kKeywords, result.opts.keywords)) {
        if (!err.empty()) {
            err += "; ";
        }
        err += "invalid value for --" + std::string(diag::flag_id::kKeywords);
    }
    if (!result.opts.stack_check_mode.empty() && !opts.setFlagValueByName(diag::flag_id::kStackCheck, result.opts.stack_check_mode)) {
        if (!err.empty()) {
            err += "; ";
        }
        err += "invalid value for --" + std::string(diag::flag_id::kStackCheck) + ": '" + result.opts.stack_check_mode +
               "' (expected: off|explicit|recursion|auto)";
    }
    if (!result.opts.stack_check_reserve.empty() && !opts.setFlagValueByName(diag::flag_id::kStackCheckReserve, result.opts.stack_check_reserve)) {
        if (!err.empty()) {
            err += "; ";
        }
        err += "invalid value for --" + std::string(diag::flag_id::kStackCheckReserve) + ": '" + result.opts.stack_check_reserve +
               "' (expected: non-negative integer bytes)";
    }
    if (!result.opts.stack_check_functions.empty() && !opts.setFlagValueByName(diag::flag_id::kStackCheckFunctions, result.opts.stack_check_functions)) {
        if (!err.empty()) {
            err += "; ";
        }
        err += "invalid value for --" + std::string(diag::flag_id::kStackCheckFunctions);
    }
    return err;
}

/// Применяет полный набор опций анализа: `-W`-диагностики (applyDiagnostics, может бросить
/// std::invalid_argument на неизвестную -W-опцию) + поведенческие флаги. Возвращает ошибку
/// применения поведенческих флагов (пусто = успех).
inline std::string applyAnalysisOptions(Options& opts, const ParseResult& result) {
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

/// Разбирает произвольный список опций (в т.ч. из шебанга файла) arity-aware парсером драйвера
/// trust и применяет к Options только опции анализа. Даже при ошибке применяются успевшие
/// разобраться опции; ошибки НЕ печатаются здесь — это обязанность вызывающего.
inline std::string applyAnalysisArgs(Options& opts, std::span<const std::string> args) {
    if (args.empty()) {
        return {};
    }
    // parseDriverArgs ожидает полный argv (элемент [0] пропускается); подставляем фиктивное имя,
    // иначе первая опция списка «съедалась» бы как имя программы.
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
