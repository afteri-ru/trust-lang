// src/pipeline/pipeline_modes.cpp
// Обработчики специальных режимов Pipeline::execute(), вынесенные как отдельные зоны
// ответственности: форматирование, вывод лексем, solver export/calculate, режим компиляции,
// emit C++ и прочие emit-режимы.
#include "pipeline/pipeline.hpp"
#include "driver/cli.hpp"
#include "pipeline/makefile_build.hpp"
#include "ast/term_to_ast.hpp"
#include "attrs/attr.hpp"
#include "module_loader/module_export.hpp"
#include "syntax/lexer.h"
#include "syntax/macro.h"
#include "syntax/parser.h"
#include "syntax/term.h"
#include "ast/ast_nodes.hpp"
#include "semantic/pass_runner.hpp"
#include "semantic/diag.hpp"
#include "semantic/solver.hpp"
#include "solver/trust_to_smt.hpp"
#include "solver/smt_printer.hpp"
#include "solver/solver.hpp"
#include "transpiler/transpiler.hpp"
#include "trust/version.h"
#include "assets/asset_catalog.hpp"
#include "assets/asset_provider.hpp"

#include "utils/io.hpp"
#include "utils/file_io.hpp"
#include "formatter/format.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace trust {

// --format / --format-check: pretty-print с .trust-format и CLI-переопределениями.
int Pipeline::runFormatMode() {
        if (m_opts.input_file.empty()) {
            trust::errs() << "error: --format requires an input file\n";
            return 1;
        }
        if (!std::filesystem::exists(m_opts.input_file)) {
            trust::errs() << "error: input file not found: " << m_opts.input_file << "\n";
            return 1;
        }
        auto data = trust::utils::FileIO::read<std::vector<char>>(m_opts.input_file);
        if (!data) {
            trust::errs() << "error: cannot read file: " << m_opts.input_file << "\n";
            return 1;
        }
        std::string sourceText(data->data(), data->size());

        // Резолвим конфиг .trust-format (если задан) + CLI-переопределения.
        trust::formatter::FormatConfig cfg;
        cfg.ok = true;
        std::string cfgPath;
        if (!m_opts.format_config.empty()) {
            cfgPath = m_opts.format_config;
        } else if (!m_opts.format_no_config) {
            cfgPath = trust::formatter::findConfig(std::filesystem::path(m_opts.input_file).parent_path().string());
        }
        if (!cfgPath.empty()) {
            cfg = trust::formatter::loadConfig(cfgPath);
            if (!cfg.ok) {
                trust::errs() << "error: " << cfg.error << "\n";
                return 1;
            }
        }
        trust::formatter::FormatOptions fopts = cfg.opts;
        // Эффективные keywords (дефолт dsl + приоритет CLI > .trust-format) для форматирования.
        fopts.keywords = effectiveKeywords();
        // CLI --keyword-sigil (валидирован в parseArgs) переопределяет .trust-format.
        trust::formatter::KeywordSigil ksig;
        if (!m_opts.keyword_sigil.empty() && trust::formatter::keywordSigilFromString(m_opts.keyword_sigil, ksig)) {
            fopts.keyword_sigil = ksig;
        }

        // Форматтер подписывается на Macro::on_macro_kind, прогоняет парсинг (в т.ч. модули) и
        // собирает классификацию макросов in-stream. Диагностики парсинга подавлены.
        m_ctx.diag().setMinSeverity(trust::Severity::Fatal);
        trust::Parser parser(m_ctx);
        auto fres = trust::formatter::format(sourceText, m_opts.input_file, fopts, parser);
        if (!fres.ok) {
            trust::errs() << "error: cannot format '" << m_opts.input_file << "': " << fres.error << "\n";
            return 1;
        }
        if (m_opts.format_check) {
            if (fres.text == sourceText) {
                return 0;
            }
            trust::errs() << m_opts.input_file << ": not formatted\n";
            return 1;
        }
        trust::outs() << fres.text;
        return 0;
}

// --emit-lexemes: быстрый путь только через legacy лексер.
int Pipeline::runLexemesOnlyMode(MapperFile inputFile) {
        loadDslMacros();
        // Файл уже загружен (inputFile) - парсим из реального источника.
        trust::Parser parser(m_ctx);
        trust::TermPtr term = parser.ParseWithSource(inputFile, /*expand_module=*/false);
        if (term) {
            // Walk the tree and print leaf terms
            std::function<void(const trust::TermPtr&)> dumpTerm = [&](const trust::TermPtr& t) {
                if (!t || t->getTermID() == trust::TermID::END) {
                    return;
                }
                trust::outs() << t->getText() << "\t" << trust::toString(t->getTermID()) << "\n";
                for (const auto& child : t->m_sequence) {
                    dumpTerm(child);
                }
                if (t->m_args) {
                    for (const auto& [_, arg] : *t->m_args) {
                        dumpTerm(arg);
                    }
                }
                if (t->m_left) {
                    dumpTerm(t->m_left);
                }
                if (t->m_right) {
                    dumpTerm(t->m_right);
                }
            };
            dumpTerm(term);
        }
        return 0;
}

// solver-mode=export|calculate: генерация SMT-LIB 2 / авто-выполнение через решатель.
int Pipeline::runSolverMode(MapperFile inputFile) {
    // -- 4a. Solver: export/calculate (генерация SMT-LIB 2 для z3) --
    // Конвертация значения из модели z3 в читаемый вид: `#x<hex>` (BitVec) → знаковое десятичное
    // (2's complement по ширине hex-строки); прочее (true/false, массивы) - как есть.
    const auto fmtModelValue = [](std::string_view v) -> std::string {
        if (v.size() >= 2 && v[0] == '#' && v[1] == 'x') {
            uint64_t bits = 0;
            for (std::size_t i = 2; i < v.size(); ++i) {
                const char c = v[i];
                const unsigned d = (c >= '0' && c <= '9')   ? static_cast<unsigned>(c - '0')
                                   : (c >= 'a' && c <= 'f') ? static_cast<unsigned>(c - 'a' + 10)
                                   : (c >= 'A' && c <= 'F') ? static_cast<unsigned>(c - 'A' + 10)
                                                            : 0u;
                bits = (bits << 4) | d;
            }
            const unsigned width = static_cast<unsigned>((v.size() - 2) * 4);
            if (width > 0 && width < 64 && (bits >> (width - 1)) & 1u) {
                // Отрицательное знаковое (2's complement).
                const uint64_t mask = (width == 64) ? ~0ULL : ((1ULL << width) - 1);
                const uint64_t mag = ((~bits & mask) + 1) & mask;
                return "-" + std::to_string(mag);
            }
            return std::to_string(bits);
        }
        return std::string(v);
    };
    {
        const auto smode = semantic::solverModeFromOptions(m_ctx.opts());
        if (smode == semantic::SolverMode::kExport || smode == semantic::SolverMode::kCalculate) {
            MapperFile noCpp; // Solver шаг без Transpile - cppOut не требуется
            solver::SmtScript script;
            auto steps = PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Solver;
            runPipeline(steps, inputFile, noCpp, nullptr, nullptr, nullptr, &script);
            if (m_ctx.diag().errorCount() > 0) {
                return 1;
            }
            if (script.commands.empty()) {
                trust::errs() << "info: no trust contracts to verify (--solver-mode=" << semantic::solverModeName(*smode) << ")\n";
                return 0;
            }
            if (smode == semantic::SolverMode::kExport) {
                std::filesystem::path outPath =
                    m_opts.output_file.empty() ? std::filesystem::path(m_opts.input_file) : std::filesystem::path(m_opts.output_file);
                if (m_opts.output_file.empty()) {
                    outPath.replace_extension(".smt2");
                }
                const std::string smt2 = solver::SmtPrinter::printScript(script);
                std::ofstream ofs(outPath);
                if (!ofs) {
                    trust::errs() << "error: cannot write solver file: " << outPath << "\n";
                    return 1;
                }
                ofs << smt2;
                trust::errs() << "info: wrote SMT-LIB 2 file: " << outPath << "\n";
                // Файл отображения .smt2.map: SMT-символы/assert → trust-источник (для LSP/отладки).
                std::filesystem::path mapPath = outPath;
                mapPath += ".map";
                std::ofstream map_ofs(mapPath);
                if (!map_ofs) {
                    trust::errs() << "error: cannot write solver map file: " << mapPath << "\n";
                    return 1;
                }
                map_ofs << solver::SmtPrinter::buildSmt2Map(m_ctx, script, smt2);
                trust::errs() << "info: wrote SMT-LIB 2 map: " << mapPath << "\n";
                return 0;
            }
            // calculate: авто-выполнение через SolverInterface (Z3 при WITH_SOLVER, иначе stub).
            std::vector<std::pair<std::string, std::string>> model;
            auto solverPtr = solver::createSolver();
            const solver::SolverResult sr = solver::runScript(*solverPtr, script, &model);
            trust::errs() << "solver: " << semantic::solverModeName(*smode);
            switch (sr) {
            case solver::SolverResult::kSat:
                trust::errs() << " -> SAT (найден контрпример - есть нарушение контракта)\n";
                if (!model.empty()) {
                    trust::errs() << "  counterexample:\n";
                    for (const auto& [nm, val] : model) {
                        trust::errs() << "    " << nm << " = " << fmtModelValue(val) << "\n";
                    }
                }
                return 1;
            case solver::SolverResult::kUnsat:
                trust::errs() << " -> UNSAT (все контракты выполняются)\n";
                return 0;
            case solver::SolverResult::kUnknown:
                trust::errs() << " -> UNKNOWN (решатель не смог решить)\n";
                return 2;
            case solver::SolverResult::kError:
            case solver::SolverResult::kUnsupported:
            default:
                trust::errs() << " -> недоступно (WITH_SOLVER=OFF; включите для Z3)\n";
                return 2;
            }
        }
    }
    // Недостижимо: метод вызывается только при solver-mode=export|calculate.
    return 0;
}

// Режим компиляции: transpile + сборка/линковка (или --emit-build-dir).
int Pipeline::runCompileMode(MapperFile inputFile) {
        // --run: кеш по md5 исходника - если файл(ы) не менялись и exe существует,
        // НЕ перекомпилировать, а сразу запустить (md5 встроен в exe: __trust_exports.srcHash).
        if (auto cached_rc = tryRunCached(m_opts); cached_rc.has_value()) {
            return *cached_rc;
        }

        auto out = runTranspileAndSave(inputFile);
        if (!out.valid) {
            return 1;
        }
        // Исходные модули компилируются отдельными единицами и линкуются с главным файлом.
        std::vector<std::string> module_runtime_headers;
        std::vector<std::string> module_link_libs;
        auto moduleCppts = generateModuleOutputs(&module_runtime_headers, &module_link_libs);
        // Рантайм-заголовки главного файла + модулей - только реально использованные.
        std::vector<std::string> runtime_headers = out.runtimeHeaders;
        runtime_headers.insert(runtime_headers.end(), module_runtime_headers.begin(), module_runtime_headers.end());
        // Флаги линковки нативных библиотек главного файла + модулей.
        std::vector<std::string> link_libs = out.linkLibs;
        link_libs.insert(link_libs.end(), module_link_libs.begin(), module_link_libs.end());
        // Имя entry-функции совпадает с DSL-макросом `main`: <имя_модуля>__main__.
        std::string entry_func_name = m_ctx.source().moduleName(inputFile) + "__main__";

        // Однофайловый режим: встроить main-обёртку в тот же .cppt модуля (без отдельного
        // _main.cppt). Заголовки для обёртки (trust/args.hpp и т.п.) извлекает writeBuildFiles
        // ниже по entry_params; здесь только дописываем текст main в конец модуля.
        if (m_opts.single_file) {
            const bool usesStackCheck = std::find(runtime_headers.begin(), runtime_headers.end(), "trust/stack_check.hpp") != runtime_headers.end();
            const bool usesSync = std::find(runtime_headers.begin(), runtime_headers.end(), "trust/trusted-cpp-sync.hpp") != runtime_headers.end();
            // Entry-функция может быть задана и НЕ DSL-объявлением @main (out.hasEntry), а C++-embed
            // `{% int <модуль>__main__() {...} %}` (тесты run_cache и т.п.). Тогда в .cppt реально есть
            // `<модуль>__main__`. Если определения entry нет вовсе (чистая библиотека / «модуль-скрипт»
            // без main) - режим module-as-script не реализован: явная диагностика без fallback.
            if (!out.hasEntry) {
                std::ifstream in(out.cpptPath);
                bool hasMain = false;
                std::string line;
                while (std::getline(in, line)) {
                    if (line.find(entry_func_name) != std::string::npos) {
                        hasMain = true;
                        break;
                    }
                }
                if (!hasMain) {
                    trust::errs() << "error: single-file mode (-fsingle-file, default for --run) requires a main "
                                     "entry point (@main or {% ... __main__() ... %}); 'module-as-script' "
                                     "(top-level code without main) is not supported yet. Build in multi-file "
                                     "mode with -fno-single-file.\n";
                    return 1;
                }
            }
            std::ofstream main_ofs(out.cpptPath, std::ios::app);
            if (!main_ofs) {
                trust::errs() << "error: failed to inline main wrapper into " << out.cpptPath << "\n";
                return 1;
            }
            main_ofs << "\n" << buildEntryMainSource(m_opts, usesStackCheck, usesSync, entry_func_name, out.entryParams);
            if (m_opts.verbose) {
                trust::errs() << "info: inlined main wrapper into " << out.cpptPath << "\n";
            }
        }

        // --emit-build-dir: только генерируем build-каталог (единый переносимый build.conf),
        // БЕЗ компиляции/линковки. Используется trust-lsp для скачиваемого архива.
        if (m_opts.emit_build_dir_only) {
            if (!writeBuildFiles(m_opts, out.cpptPath, moduleCppts, runtime_headers, link_libs, entry_func_name, out.entryParams)) {
                return 1;
            }
            return 0;
        }
        if (!compileAndLink(m_opts, out.cpptPath, moduleCppts, runtime_headers, link_libs, entry_func_name, out.entryParams)) {
            return 1;
        }
        // --run: запустить собранный исполняемый файл (md5 исходника встроен в srcHash).
        if (m_opts.run && m_opts.compile_mode == CompileMode::Executable) {
            return runBuiltExecutable(m_opts, out.cpptPath);
        }
        return 0;
}

// --emit-cpp: transpile и вывод результата в stdout.
int Pipeline::runEmitCppMode(MapperFile inputFile) {
        auto out = runTranspileAndSave(inputFile);
        if (!out.valid) {
            return 1;
        }
        trust::outs() << m_ctx.source().output_result(out.outputIdx);
        return 0;
}

// Прочие emit-режимы (tokens/ast/macros).
int Pipeline::runOtherEmitModes(MapperFile inputFile) {
        auto steps = determineSteps(m_opts.emit_flags);
        auto result = runPipeline(steps, inputFile);
        if (!result.isValid() && steps != PipelineSteps::None) {
            return 1;
        }
        // Диагностические emit-режимы завершаются ошибкой при наличии ошибок парсинга
        // (аналог -fsyntax-only в clang): выходной поток может быть неполным, а тесты
        // `%not %trust --emit-...` должны видеть ненулевой код возврата.
        if (m_ctx.diag().errorCount() > 0) {
            return 1;
        }
        return emitOutput(result);
}

} // namespace trust
