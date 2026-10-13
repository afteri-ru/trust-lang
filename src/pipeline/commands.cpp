// src/pipeline/commands.cpp
// CLI/use-case команды поверх Pipeline: лексемы, solver, emit, компиляция.
#include "pipeline/commands.hpp"
#include "module_loader/module_export.hpp"
#include "module_loader/module_loader.hpp"
#include "syntax/module_header.hpp"

#include "pipeline/pipeline.hpp"
#include "session/context.hpp"
#include "toolchain/build.hpp"
#include "pipeline/dsl/dsl.hpp"
#include "pipeline/backend/emit.hpp"
#include "pipeline/paths/paths.hpp"
#include "pipeline/run/run.hpp"
#include "driver/cli.hpp"
#include "package/package.hpp"
#include "semantic/solver.hpp"
#include "solver/solver.hpp"
#include "syntax/parser.h"
#include "syntax/term/term.h"
#include "utils/error.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {
int runLexemesCommand(Context& ctx, const PipelineOpts& opts, MapperFile inputFile) {
    loadDslMacros(ctx, opts);
    // Файл уже загружен (inputFile) - парсим из реального источника.
    trust::Parser parser(ctx);
    trust::TermPtr term = parser.ParseWithSource(inputFile, /*expand_module=*/false);
    dumpTermTree(term, trust::outs());
    if (ctx.diag().errorCount() > 0) {
        return 1;
    }
    return 0;
}

int runSolverCommand(Pipeline& pipeline, Context& ctx, const PipelineOpts& opts, MapperFile inputFile) {
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
        const auto smode = semantic::solverModeFromOptions(ctx.opts());
        if (smode == analysis::SolverMode::kExport || smode == analysis::SolverMode::kCalculate) {
            // Назначение артефакта: export - файл `.smt2`, calculate - только в памяти.
            std::filesystem::path outPath;
            if (smode == analysis::SolverMode::kExport) {
                outPath = opts.output_file.empty() ? std::filesystem::path(opts.input_file) : std::filesystem::path(opts.output_file);
                if (opts.output_file.empty()) {
                    outPath.replace_extension(".smt2");
                }
            }
            RunRequest req;
            req.smt = backend::SmtTarget{outPath};
            PipelineResult result = pipeline.run(req, inputFile);
            if (ctx.diag().errorCount() > 0) {
                return 1;
            }
            // Бэкенд запрошен и семантика прошла: артефакт обязан быть произведён.
            EXPECT(result.smt.has_value() && "SMT backend requested but produced no artifact");
            if (result.smt->script.commands.empty()) {
                if (result.smt->script.outsideEnvelopeCount > 0) {
                    trust::errs() << "info: проверка не выполнялась: " << result.smt->script.outsideEnvelopeCount
                                  << " условие(й) вне области проверки (--solver-mode=" << analysis::solverModeName(*smode) << ")\n";
                } else {
                    trust::errs() << "info: контрактов для проверки нет (--solver-mode=" << analysis::solverModeName(*smode) << ")\n";
                }
                return 0;
            }
            const solver::SmtScript& script = result.smt->script;
            if (smode == analysis::SolverMode::kExport) {
                // Файлы `.smt2` и `.smt2.map` записаны Pipeline по SmtTarget::path.
                if (script.outsideEnvelopeCount > 0) {
                    trust::errs() << "info: " << script.outsideEnvelopeCount << " условие(й) вне области проверки: не проверялись\n";
                }
                return result.smt->saved ? 0 : 1;
            }
            // calculate: авто-выполнение через SolverInterface (Z3 при WITH_SOLVER, иначе stub).
            if (script.outsideEnvelopeCount > 0) {
                trust::errs() << "info: " << script.outsideEnvelopeCount << " условие(й) вне области проверки: не проверялись\n";
            }
            // Условий проверки может не остаться (всё вне области): check-sat в скрипте нет —
            // это не «решатель недоступен», а отсутствие целей.
            const bool hasGoal = std::any_of(script.commands.begin(), script.commands.end(),
                                             [](const solver::SmtCommand& command) { return command.kind == solver::SmtCommandKind::kCheckSat; });
            if (!hasGoal) {
                trust::errs() << "info: условий проверки нет (--solver-mode=" << analysis::solverModeName(*smode) << ")\n";
                return 0;
            }
            std::vector<std::pair<std::string, std::string>> model;
            auto solverPtr = solver::createSolver();
            solver::VerificationReport report;
            const solver::SolverResult sr = solver::runScript(*solverPtr, script, &model, &report);
            // Привязка нарушения к источнику: `file:line`, где построено условие проверки.
            const auto goalLocation = [&ctx](MapperRange range) -> std::string {
                if (range.begin.isInvalid()) {
                    return "<unknown>";
                }
                const auto pos = ctx.source().line_column(range.begin);
                return std::string(ctx.source().filename(range.begin.fileIdx())) + ":" + std::to_string(pos.line);
            };
            trust::errs() << "проверка контрактов: " << analysis::solverModeName(*smode);
            switch (sr) {
            case solver::SolverResult::kSat:
                trust::errs() << " -> НАЙДЕНО НАРУШЕНИЕ (sat)\n";
                for (const auto& check : report.checks) {
                    if (check.result != solver::SolverResult::kSat) {
                        continue;
                    }
                    trust::errs() << "  " << goalLocation(check.srcRange) << ": "
                                  << (check.kind.has_value() ? solver::vcObligationLabel(*check.kind) : std::string_view("условие")) << " не выполняется\n";
                    for (const auto& [nm, val] : check.model) {
                        trust::errs() << "    " << nm << " = " << fmtModelValue(val) << "\n";
                    }
                }
                return 1;
            case solver::SolverResult::kUnsat:
                trust::errs() << " -> нарушений не найдено, проверка пройдена (unsat)\n";
                return 0;
            case solver::SolverResult::kUnknown:
                trust::errs() << " -> проверить не удалось: решатель не дал ответа (unknown)\n";
                return 2;
            case solver::SolverResult::kError:
            case solver::SolverResult::kUnsupported:
            default:
                trust::errs() << " -> проверка не выполнялась: решатель недоступен (WITH_SOLVER=OFF)\n";
                return 2;
            }
        }
    }
    FAULT("runSolverCommand requires --solver-mode=export|calculate");
}

int Pipeline::compileProgram(MapperFile inputFile) {
    // --run: кеш по md5 исходника - если файл(ы) не менялись и exe существует,
    // НЕ перекомпилировать, а сразу запустить (md5 встроен в exe - ELF-секция .debug_trust_hash).
    if (auto cached_rc = tryRunCached(m_opts); cached_rc.has_value()) {
        return *cached_rc;
    }

    auto result = runCppToFile(inputFile);
    if (!result.has_value()) {
        return 1;
    }
    const std::filesystem::path& cpptPath = result->cpp->path;
    // Исходные модули компилируются отдельными единицами и линкуются с главным файлом.
    ModuleOutputs modules = generateModuleOutputs(result->mainModuleIndex);
    if (!modules.ok) {
        return 1;
    }
    // Рантайм-заголовки главного файла + модулей - только реально использованные.
    std::vector<std::string> package_headers = result->cpp->packageHeaders;
    package_headers.insert(package_headers.end(), modules.packageHeaders.begin(), modules.packageHeaders.end());
    // Флаги линковки нативных библиотек главного файла + модулей.
    std::vector<std::string> link_libs = result->cpp->linkLibs;
    link_libs.insert(link_libs.end(), modules.linkLibs.begin(), modules.linkLibs.end());
    // Полное имя entry-функции строит DSL-макрос `main` из @__MODULE_NAME__ — единственная
    // формула `moduleEntryIdent` (та же, что для implicit-entry в emitCppArtifact).
    std::string entry_func_name = moduleEntryIdent(loader().moduleLogicalName(result->mainModuleIndex));
    toolchain::RuntimeHeaders runtime_headers{package_headers, result->cpp->entryParams};

    // Однофайловый режим: встроить main-обёртку в тот же .cppt модуля (без отдельного
    // _main.cppt). Заголовки для обёртки (trust/args.hpp и т.п.) извлекает writeBuildFiles
    // ниже по entry_params; здесь только дописываем текст main в конец модуля.
    if (m_opts.single_file) {
        const bool usesStackCheck = runtime_headers.usesStackCheck();
        const bool usesSync = runtime_headers.usesSync();
        // Entry-функция может быть задана и НЕ DSL-объявлением @main (result->cpp->sawEntry), а C++-embed
        // `{% int <модуль>__main__() {...} %}` (тесты run_cache и т.п.). Тогда в .cppt реально есть
        // `<модуль>__main__`. Если определения entry нет вовсе (чистая библиотека / «модуль-скрипт»
        // без main) - режим module-as-script не реализован: явная диагностика без fallback.
        if (!result->cpp->sawEntry) {
            std::ifstream in(cpptPath);
            if (!in) {
                trust::errs() << "error: cannot read generated file: " << cpptPath << "\n";
                return 1;
            }
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
        std::ofstream main_ofs(cpptPath, std::ios::app);
        if (!main_ofs) {
            trust::errs() << "error: failed to inline main wrapper into " << cpptPath << "\n";
            return 1;
        }
        main_ofs << "\n" << toolchain::buildEntryMainSource(m_opts, usesStackCheck, usesSync, entry_func_name, result->cpp->entryParams);
        if (m_opts.verbose) {
            trust::errs() << "info: inlined main wrapper into " << cpptPath << "\n";
        }
    }

    // --emit-build-dir: только генерируем build-каталог (единый переносимый build.conf),
    // БЕЗ компиляции/линковки. Используется trust-lsp для скачиваемого архива.
    const toolchain::BuildRequest spec{m_opts, cpptPath, modules.cpptPaths, runtime_headers, link_libs, entry_func_name};
    if (m_opts.emit_build_dir_only) {
        toolchain::BuildInputs inputs;
        if (!toolchain::writeBuildFiles(spec, inputs)) {
            return 1;
        }
        return 0;
    }
    if (!toolchain::compileAndLink(spec)) {
        return 1;
    }
    // --run: запустить собранный исполняемый файл (md5 исходника встроен в srcHash).
    if (m_opts.run && m_opts.compile_mode == CompileMode::Executable) {
        return runBuiltExecutable(m_opts, cpptPath);
    }
    return 0;
}

int Pipeline::emitCppToStdout(MapperFile inputFile) {
    auto result = runCppToFile(inputFile);
    if (!result.has_value()) {
        return 1;
    }
    // Текст TU ссылается на заголовки импортируемых модулей (`#include "<enc>.hpp"`):
    // материализуем их в build-каталог, чтобы вывод `--emit-cpp` был компилируемым.
    ModuleOutputs modules = generateModuleOutputs(result->mainModuleIndex);
    if (!modules.ok) {
        return 1;
    }
    trust::outs() << m_ctx.source().output_result(result->cpp->index);
    return 0;
}

namespace {

/// Путь для вывода правил: относительный от cwd (стабильный и переносимый вид).
std::string depPath(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path rel = std::filesystem::relative(path, std::filesystem::current_path(), ec);
    return ec ? path.string() : rel.generic_string();
}

/// Одна строка правила make: `target: dep...` (зависимости уникальны и отсортированы).
void printMakeRule(const std::string& target, std::vector<std::string> deps) {
    std::sort(deps.begin(), deps.end());
    deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
    trust::outs() << target << ":";
    for (const std::string& dep : deps) {
        trust::outs() << ' ' << dep;
    }
    trust::outs() << "\n";
}

} // namespace

int Pipeline::emitModuleDepsToStdout(MapperFile inputFile) {
    // Граф строится по загруженным файлам: нужен только разбор (импорты резолвит парсер).
    RunRequest req = RunRequest::analysis();
    req.steps = FrontendSteps::ParseAST;
    PipelineResult result = run(req, inputFile);
    if (!result.isValid() || m_ctx.diag().errorCount() > 0) {
        return 1;
    }
    const std::filesystem::path buildDir = computeBuildDir(m_opts);
    const ModuleIndex mainIdx = result.mainModuleIndex;

    // Заголовок `.hpp` получает КАЖДЫЙ загруженный модуль (в т.ч. declaration-only: его интерфейс
    // самостоятелен). `.cppt` — только модули с out-of-line определениями (см. generateModuleOutputs).
    // Зависимости транзитивны по построению: заголовок модуля подключает заголовки своих импортов,
    // поэтому артефакт зависит от ВСЕХ источников/заголовков компиляции и архивов пакетов (та же
    // грубая модель, что у главного `.cppt`) — иначе изменение интерфейса глубокого модуля даёт
    // stale build.
    std::vector<ModuleIndex> loadedModules;
    std::vector<std::string> allSources;
    std::vector<std::string> allHeaders;
    for (std::size_t i = 0; i < loader().moduleCount(); ++i) {
        const ModuleIndex idx{i};
        if (idx == mainIdx || !loader().isLoaded(idx)) {
            continue;
        }
        loadedModules.push_back(idx);
        allSources.push_back(depPath(std::filesystem::path(loader().modulePath(idx))));
        allHeaders.push_back(depPath(moduleHeaderPath(buildDir, loader().moduleLogicalName(idx))));
    }
    std::vector<std::string> packageDeps;
    for (const auto& archive : programDependencyArchives(loader(), m_opts)) {
        packageDeps.push_back(depPath(archive));
    }
    // Общая часть зависимостей: все источники + архивы пакетов.
    std::vector<std::string> baseDeps = allSources;
    baseDeps.insert(baseDeps.end(), packageDeps.begin(), packageDeps.end());

    for (const ModuleIndex idx : loadedModules) {
        const std::string ownHeader = depPath(moduleHeaderPath(buildDir, loader().moduleLogicalName(idx)));
        // Заголовки транзитивных импортов: собственный заголовок целью самого себя быть не может.
        std::vector<std::string> headerDeps = baseDeps;
        for (const std::string& header : allHeaders) {
            if (header != ownHeader) {
                headerDeps.push_back(header);
            }
        }
        printMakeRule(ownHeader, std::move(headerDeps));
        if (!loader().interfaceFacts(idx).hasOutOfLineDefinitions) {
            continue;
        }
        // `.cppt` модуля начинается с `#include` собственного заголовка.
        std::vector<std::string> cppDeps = baseDeps;
        cppDeps.insert(cppDeps.end(), allHeaders.begin(), allHeaders.end());
        printMakeRule(depPath(moduleCppPath(buildDir, loader().modulePath(idx))), std::move(cppDeps));
    }

    // Главный `.cppt` зависит от входного файла, всех загруженных модулей (исходники + заголовки)
    // и пакетов.
    std::vector<std::string> mainDeps{depPath(m_opts.input_file)};
    mainDeps.insert(mainDeps.end(), baseDeps.begin(), baseDeps.end());
    mainDeps.insert(mainDeps.end(), allHeaders.begin(), allHeaders.end());
    printMakeRule(depPath(computeCpptPath(m_opts)), std::move(mainDeps));
    return 0;
}

int runOtherEmitCommand(Pipeline& pipeline, Context& ctx, const PipelineOpts& opts, MapperFile inputFile) {
    RunRequest req = RunRequest::analysis();
    req.steps = FrontendSteps::ParseAST;
    PipelineResult result = pipeline.run(req, inputFile);
    if (!result.isValid()) {
        return 1;
    }
    // Диагностические emit-режимы завершаются ошибкой при наличии ошибок парсинга
    // (аналог -fsyntax-only в clang): выходной поток может быть неполным, а тесты
    // `%not %trust --emit-...` должны видеть ненулевой код возврата.
    if (ctx.diag().errorCount() > 0) {
        return 1;
    }
    return printEmitOutput(opts, ctx, result);
}

} // namespace trust
