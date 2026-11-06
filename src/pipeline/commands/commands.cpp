// CLI/use-case команды поверх Pipeline: лексемы, solver, emit, компиляция.
#include "pipeline/commands.hpp"
#include "module_loader/module_export.hpp"
#include "module_loader/module_loader.hpp"
#include "syntax/module_header.hpp"

#include "pipeline/pipeline.hpp"
#include "session/context.hpp"
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
    trust::ParserServices parseServices = trust::makeParserServices(ctx);
    trust::Parser parser(parseServices);
    trust::TermPtr term = parser.ParseWithSource(inputFile, /*expand_module=*/false);
    dumpTermTree(term, trust::outs());
    if (ctx.diag().errorCount() > 0) {
        return 1;
    }
    return 0;
}

int runSolverCommand(Pipeline& pipeline, Context& ctx, const PipelineOpts& opts, MapperFile inputFile) {
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
