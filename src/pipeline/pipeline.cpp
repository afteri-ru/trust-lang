#include "pipeline/pipeline.hpp"
#include "pipeline/backend/stages.hpp"
#include "syntax/module_header.hpp"
#include "pipeline/backend/checks.hpp"
#include "pipeline/dsl/dsl.hpp"
#include "pipeline/paths/paths.hpp"
#include "pipeline/run/run.hpp"
#include "pipeline/modules/symbols.hpp"
#include "toolchain/build.hpp"
#include "toolchain/hash.hpp"
#include "verify/verify.hpp"
#include "module_loader/import_exports.hpp"
#include "module_loader/module_loader.hpp"
#include "semantic/diag.hpp"
#include "semantic/analysis_api.hpp"
#include "session/context.hpp"
#include "solver/trust_to_smt.hpp"
#include "ir/analysis_manager.hpp"
#include "symbols/reference_index.hpp"
#include "ir/hir_module.hpp"
#include "ir/hir_verify.hpp"
#include "types/type_system.hpp"
#include "utils/error.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace trust {

// Документирующие комментарии к объявлениям привязываются ГРАММАТИКОЙ к терму-идентификатора
// (term->m_docs, см. include/syntax/grammar/parser.y.in: attachLeadingDoc/attachTrailingDoc) и переносятся
// в узел объявления TermToAstConverter::convert → AstNodeBase::documentation. SymbolCollectorHook
// читает их в finalize. Отдельный AST-обход (moduleDocMap/attachDocumentation) не требуется.
// -- Ассеты стандартной библиотеки (dsl.src, iterator.src) --
// Prelude-файлы читаются из пакета ресурсов `share/trust/stdlib.trust`
// (package::readResourcePackageEntry). Локальных #embed здесь НЕТ: иначе
// содержимое дублировалось бы с провайдером и с LSP (src/lsp/catalog/builtin_catalog.cpp использует
// тот же аксессор).

// -- Pipeline constructor --

Pipeline::Pipeline(Context& ctx, const PipelineOpts& opts)
: m_ctx(ctx)
, m_opts(opts) {
    m_ctx.diag().setMinSeverity(opts.quiet ? Severity::Error : Severity::Remark);
    // Pipeline владеет ModuleLoader и TypeSystem и внедряет их в Context
    // (невладеющие указатели), чтобы diag не зависел от module_loader и types.
    m_loader = std::make_unique<ModuleLoader>(m_ctx);
    m_ctx.setModuleResolver(m_loader.get());
    m_types = std::make_shared<TypeSystem>(m_ctx.diag(), m_ctx.opts());
    // Провайдер логического имени модуля: C++-проекция имени типа (модульная изоляция) —
    // из того же источника, что и кодоген/загрузчик; отдельной таблицы имён нет.
    m_types->setModuleNames(m_loader.get());
    m_ctx.setTypes(m_types.get());
}

Pipeline::~Pipeline() = default;

void Pipeline::loadDslMacros() {
    trust::loadDslMacros(m_ctx, m_opts);
}

std::vector<std::string> Pipeline::effectiveKeywords() {
    return trust::effectiveKeywords(m_ctx, m_opts);
}

PipelineResult Pipeline::run(const RunRequest& req, MapperFile inputFile) {
    // Бэкенды кодируют уже разрешённые имена/типы: без семантики они бессмысленны.
    EXPECT((!req.smt || hasStep(req.steps, FrontendSteps::Semantic)) && "SMT backend requires the Semantic step");
    EXPECT((!req.cpp || hasStep(req.steps, FrontendSteps::Semantic)) && "C++ backend requires the Semantic step");
    // Все тексты AST/HIR/символов этой компиляции интернируются в пул строк Context
    // (per-compilation), а не в процессный interner: память освобождается вместе с Context/результатом.
    StringInternerScope stringsScope(m_ctx.strings());
    // Runner живёт до конца прогона, чтобы разрешённая семантикой таблица символов (TypeId) была
    // доступна кодогенерации (проброс в CppTranspiler).
    SemanticPassRunner runner(m_ctx, m_opts.allow_semantic_on_errors);
    FrontendResult fr = runFrontend(req.steps, inputFile, req.mode, runner);
    PipelineResult result = std::move(fr.result);
    result.mainModuleIndex = fr.mainModuleIndex;
    if (!fr.stopped) {
        runBackends(result, req);
    }
    // Результат разделяет владение пулом строк: AST/HIR/SymbolIndex остаются валидны
    // после разрушения Context.
    result.strings = m_ctx.stringsPool();
    return result;
}

// -- lowerAndAnalyze: HIR + flow-анализы (потребители производного представления) --

Pipeline::LoweredUnit Pipeline::lowerAndAnalyze(const std::vector<AstNodePtr>& roots, bool verify, bool runFlow) {
    // HIR — производная форма (глубокий клон AST + lowering): единственный структурный вход
    // бэкендов и flow-анализов. Строится ПОСЛЕ семантики и из неизменённой AST.
    HirModule hir = buildHir(roots, m_ctx);
    // Гейт контракта J2: структурная корректность и полнота материализованных фактов
    // проверяются ДО потребителей (flow-анализы/кодоген). Нарушение — ICE-политика.
    if (verify) {
        if (const auto violation = verifyHir(hir)) {
            reportInternalError(m_ctx, m_opts.allow_semantic_on_errors, *violation);
            // Потребители HIR не запускаются на непроверенном представлении: в recover-режиме
            // (LSP) возвращаем HIR без flow-анализов, чтобы не каскадировать ICE.
            return LoweredUnit{std::move(hir), std::nullopt};
        }
    }
    if (!runFlow) {
        // Модульный юнит: семантика уже выполнена над этими же (разделяемыми) узлами AST
        // в главном юните. Повторный semantic/flow-прогон не нужен; HIR строится из
        // материализованных фактов, verifyHir остаётся гейтом контракта.
        return LoweredUnit{std::move(hir), std::nullopt};
    }
    // MIR — единственный вход flow-анализов и верификатора: CFG строятся ОДИН раз (buildMir),
    // анализы запускаются менеджером в порядке зависимостей (DA → borrow). HIR остаётся входом
    // кодогена, а MIR отдаётся в PipelineResult как единая производная форма.
    flow::MirModule mir = flow::buildMir(hir.nodes(), m_ctx.attrs());
    if (verify) {
        if (const auto violation = flow::verifyFlowModule(mir)) {
            reportInternalError(m_ctx, m_opts.allow_semantic_on_errors, *violation);
            return LoweredUnit{std::move(hir), std::nullopt};
        }
    }
    flow::AnalysisManager manager;
    manager.add(std::make_unique<semantic::DefiniteAssignmentPass>(m_ctx));
    manager.add(std::make_unique<semantic::BorrowFlowPass>(m_ctx));
    manager.run(mir);
    return LoweredUnit{std::move(hir), std::move(mir)};
}

Pipeline::FrontendResult Pipeline::runFrontend(FrontendSteps steps, MapperFile inputFile, RunMode mode, SemanticPassRunner& runner) {
    FrontendResult fr;
    // Stdlib prelude участвует ТОЛЬКО в семантике (регистрация типа Iterator + @[borrowed]),
    // но НЕ в кодогенерации (иначе добавляет лишнюю строку в вывод).
    if (hasStep(steps, FrontendSteps::ParseAST)) {
        // Read source and register main file as a module.
        // parseSourceModule recursively parses the file (expand_module=true)
        // and stores the result in the registry.
        loadDslMacros();
        // Пакеты (.trust) извлекаются в build/temp-каталог (не рядом с архивом),
        // чтобы исходники пакета не оседали в дереве исходников пакета.
        loader().setPackageCacheDir(computeBuildDir(m_opts) / std::string(toolchain::kPackagesSubdir));
        // Scope root: явный --module-root или каталог входного файла; пути модулей/пакетов — из CLI.
        std::filesystem::path moduleRoot =
            m_opts.module_root.empty() ? std::filesystem::path(m_opts.input_file).parent_path() : std::filesystem::path(m_opts.module_root);
        if (moduleRoot.empty()) {
            moduleRoot = std::filesystem::current_path();
        }
        std::error_code root_ec;
        const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(moduleRoot, root_ec);
        if (!root_ec) {
            moduleRoot = canonicalRoot;
        }
        std::vector<std::filesystem::path> modulePaths;
        modulePaths.reserve(m_opts.module_paths.size());
        for (const std::string& path : m_opts.module_paths) {
            modulePaths.emplace_back(path);
        }
        loader().setModuleSearchPaths(std::move(modulePaths));
        std::vector<std::filesystem::path> packagePaths;
        packagePaths.reserve(m_opts.package_paths.size());
        for (const std::string& path : m_opts.package_paths) {
            packagePaths.emplace_back(path);
        }
        loader().setPackageSearchPaths(std::move(packagePaths));
        if (mode == RunMode::kProgram) {
            // Главный файл программы - от него отсчитывается имя модуля (@__MODULE_NAME__)
            // и имя entry-функции. Должен быть установлен до парсинга (parseSourceModule).
            m_ctx.source().setMainModuleFile(inputFile);
        }
        std::string moduleName = std::string(m_ctx.source().filename(inputFile));
        // Идентичность модуля — канонический путь файла (как у импортируемых модулей); source-map
        // имя относительно baseDirectory оставляем только как display-имя корневого ModuleNode.
        std::error_code input_path_ec;
        std::filesystem::path canonicalInput = std::filesystem::weakly_canonical(std::filesystem::path(m_opts.input_file), input_path_ec);
        if (input_path_ec) {
            canonicalInput = std::filesystem::absolute(std::filesystem::path(m_opts.input_file), input_path_ec);
        }
        const std::string moduleId = input_path_ec ? std::string(m_opts.input_file) : canonicalInput.generic_string();
        std::filesystem::path inputDir = std::filesystem::path(m_opts.input_file).parent_path();
        if (inputDir.empty()) {
            inputDir = std::filesystem::current_path();
        }
        std::error_code input_ec;
        const std::filesystem::path canonicalInputDir = std::filesystem::weakly_canonical(inputDir, input_ec);
        if (!input_ec) {
            inputDir = canonicalInputDir;
        }
        const bool rootProgram = (inputDir == moduleRoot);
        const bool allowNoHeader = rootProgram;
        const ModuleIndex idx = loader().parseSourceModule(moduleId, inputFile, {}, moduleRoot, {}, allowNoHeader);
        fr.mainModuleIndex = idx; // главный модуль — всегда модуль входного файла

        // Root node of the program is a ModuleNode wrapping the module body.
        // Тело каждого загруженного модуля уже сконвертировано ОДИН раз загрузчиком
        // (сразу после парсинга), поэтому здесь конверсии нет.
        std::vector<AstNodePtr> astNodes = buildModuleRoot(moduleName, idx);
        appendStdlibPrelude(m_ctx, m_opts.use_stdlib, fr.preludeNodes);
        fr.result.astNodes = std::move(astNodes);
        if (mode == RunMode::kProgram) {
            // Заполнить экспорт-интерфейс сайтов импорта (анализатор).
            resolveImportExports(m_ctx, loader(), *fr.result.astNodes);
        } else {
            // Контроль «признака обработки» атрибутов сразу после конвертации Term->AST
            // (severity -Wunhandled-attr). В программном режиме отчёт идёт после
            // кодогенерации; в аналитическом без неё это единственная точка.
            reportUnhandledAttributes(m_ctx, *fr.result.astNodes);
        }
    }

    // Если конвертация Term->AST дала ошибки (например нереализованная конструкция:
    // await/yield/when/filling) - семантический анализ и транспиляция на неполном/повреждённом
    // AST не запускаются (могут упасть на незаполненных детях). astNodes структурно построены;
    // факт ошибки виден вызывающему по diag().errorCount()>0.
    if (hasStep(steps, FrontendSteps::ParseAST) && m_ctx.diag().errorCount() > 0 && !m_opts.allow_semantic_on_errors) {
        fr.stopped = true;
        return fr;
    }

    if (hasStep(steps, FrontendSteps::Semantic)) {
        EXPECT(fr.result.astNodes.has_value() && "runAst must produce astNodes");
        // Prelude идёт ПЕРВЫМ, чтобы @[borrowed]-классы были помечены до анализа пользовательского кода.
        std::vector<AstNodePtr> semanticRoots;
        semanticRoots.reserve(fr.preludeNodes.size() + fr.result.astNodes->size());
        semanticRoots.insert(semanticRoots.end(), fr.preludeNodes.begin(), fr.preludeNodes.end());
        semanticRoots.insert(semanticRoots.end(), fr.result.astNodes->begin(), fr.result.astNodes->end());
        const bool ok = runner.run(semanticRoots);
        // Семантическая модель — продукт фазы, а не LSP-опция: таблица символов (persistent),
        // видимость импорта и ссылки собираются всегда, в т.ч. на частичном AST после ошибок.
        // Макросы дособираются до передачи контекста анализа в модель (дальше она неизменяема).
        appendMacroSymbols(m_ctx, runner.symbolIndex());
        ModuleProjection modules;
        modules.mainModuleIndex = fr.mainModuleIndex;
        modules.moduleNames.reserve(loader().moduleCount());
        for (std::size_t i = 0; i < loader().moduleCount(); ++i) {
            modules.moduleNames.emplace_back(loader().moduleLogicalName(ModuleIndex{i}));
        }
        modules.moduleAliases = loader().moduleAliases(fr.mainModuleIndex);
        // Ссылки имя→объявление текущего файла (переход/hover по ссылке):
        // один обход AST по материализованным семантикой фактам.
        ReferenceIndex references;
        for (const AstNodePtr& root : *fr.result.astNodes) {
            if (root != nullptr) {
                collectReferences(*root, inputFile, m_ctx.types(), m_ctx.source(), references);
            }
        }
        fr.result.semantics.emplace(runner.takeAnalysis(), m_ctx.stringsPool(), m_types, std::move(modules), std::move(references));
        // HIR-фаза выполняется и на частичном AST (LSP recover): flow-диагностики сохраняются.
        // После ICE представление недостоверно - HIR не строим.
        std::optional<LoweredUnit> lowered;
        if (!runner.crashed()) {
            lowered.emplace(lowerAndAnalyze(semanticRoots, /*verify=*/ok));
        }
        if (!ok) {
            fr.stopped = true;
            return fr;
        }
        // S4c: exported generic без определения в компиляции (declaration-only форма не
        // выражает шаблоны). Диагностика добавляет ошибки - общий error-gate ниже её учитывает.
        checkExportedGenericDeclarations(fr.mainModuleIndex);
        // Flow-анализы и верификатор выполняются ПОСЛЕ `run()`, поэтому их ошибки (uninit,
        // borrow, ICE-политика) обязаны так же останавливать бэкенды, как и ошибки семантики.
        if (m_ctx.diag().errorCount() > 0) {
            fr.stopped = true;
            return fr;
        }
        EXPECT(lowered.has_value() && "successful semantic run must produce HIR");
        // HIR главного модуля отдаётся без prelude-части (последняя участвует только в семантике).
        const std::size_t preludeCount = fr.preludeNodes.size();
        EXPECT(lowered->hir.size() >= preludeCount && "buildHir must preserve the number of roots (prelude + module)");
        fr.result.hirNodes = lowered->hir.tailFrom(preludeCount);
        fr.result.mir = std::move(lowered->mir);
    }

    return fr;
}

// -- runBackends: упорядоченный backend-конвейер (solver → cpp) --

void Pipeline::runBackends(PipelineResult& result, const RunRequest& req) {
    if (!req.cpp && !req.smt) {
        return;
    }
    // Режимы разрешаются ОДИН раз (единственная точка) и передаются стадиям данными.
    const analysis::BehavioralModes modes = semantic::behavioralModesFromOptions(m_ctx.opts());
    backend::BackendRequest back_req{
        result.hirNodes ? &*result.hirNodes : nullptr, result.mir ? &*result.mir : nullptr, modes, result.mainModuleIndex, req.cpp, req.smt};
    // Гейт доказательств: режим — данные стадии; сертификат пишется рядом с входом.
    if (verify::verifyModeFromOptions(m_ctx.opts()).has_value()) {
        back_req.verifyAll = true;
        if (!m_opts.input_file.empty()) {
            // Сертификат — артефакт сборки: <build_dir>/<stem>.trustcert (тот же каталог, что .cppt).
            const std::filesystem::path build_dir = computeBuildDir(m_opts);
            std::filesystem::path cert = build_dir / std::filesystem::path(m_opts.input_file).stem();
            cert += ".trustcert";
            back_req.certificatePath = cert;
            back_req.inputHash = toolchain::fileHash(m_opts.input_file);
        }
    }
    backend::BackendServices services{m_ctx, m_opts, loader()};
    backend::runBackendStages(backend::kBackendStages, services, back_req, result);
}


} // namespace trust
