// Реализации фаз декларативного конвейера (см. include/pipeline/run/phases.hpp).
// Единственный оркестратор — `Pipeline::runPhases` (таблица PIPELINE_PHASE_LIST без условий
// реализации); гейты остановки фронтенда владеют фазы, бэкенды — контракт `runBackendStages`.
#include "pipeline/pipeline.hpp"

#include "pipeline/backend/checks.hpp"
#include "pipeline/backend/stages.hpp"
#include "pipeline/dsl/dsl.hpp"
#include "pipeline/modules/symbols.hpp"
#include "pipeline/paths/paths.hpp"
#include "pipeline/run/phases.hpp"

#include "ir/analysis_manager.hpp"
#include "ir/cfg.hpp"
#include "ir/hir_module.hpp"
#include "ir/hir_verify.hpp"
#include "module_loader/import_exports.hpp"
#include "module_loader/module_loader.hpp"
#include "package/package.hpp"
#include "semantic/analysis_api.hpp"
#include "session/context.hpp"
#include "symbols/reference_index.hpp"
#include "transpiler/transpiler.hpp"
#include "ast/declaration_ids.hpp"
#include "utils/build_record.hpp"
#include "utils/hash.hpp"
#include "utils/error.hpp"
#include "verify/verify.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace trust {

void Pipeline::runPhases(SemanticPassRunner& runner, MapperFile inputFile, PhaseRun& run) {
    struct PhaseEntry {
        PhaseKind kind;
        backend::StageOutcome (Pipeline::*fn)(SemanticPassRunner&, MapperFile, PhaseRun&);
    };
    static constexpr PhaseEntry kPhaseEntries[] = {
#define PIPELINE_PHASE_ROW(Name, label, fn) {PhaseKind::Name, &Pipeline::fn},
        PIPELINE_PHASE_LIST(PIPELINE_PHASE_ROW)
#undef PIPELINE_PHASE_ROW
    };
    static_assert(std::size(kPhaseEntries) == static_cast<std::size_t>(PhaseKind::kCount), "PIPELINE_PHASE_LIST must cover every PhaseKind");
    for (const PhaseEntry& entry : kPhaseEntries) {
        if (run.stopped) {
            break; // гейт остановки (ошибки уже диагностированы фазами фронтенда)
        }
        if ((this->*entry.fn)(runner, inputFile, run) == backend::StageOutcome::Error) {
            run.stopped = true; // ошибка стадии уже диагностирована
            break;
        }
    }
}

backend::StageOutcome Pipeline::runReadParsePhase(SemanticPassRunner&, MapperFile inputFile, PhaseRun& run) {
    // Read source and register main file as a module.
    // parseSourceModule recursively parses the file (expand_module=true)
    // and stores the result in the registry.
    loadDslMacros();
    // Пакеты (.trust) извлекаются в build/temp-каталог (не рядом с архивом),
    // чтобы исходники пакета не оседали в дереве исходников пакета.
    loader().setPackageCacheDir(computeBuildDir(m_opts) / std::string(utils::kPackagesSubdir));
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
    modulePaths.reserve(m_opts.module_paths.size() + 1);
    for (const std::string& path : m_opts.module_paths) {
        modulePaths.emplace_back(path);
    }
    // `--stdlib` (use_stdlib) добавляет корень распакованного stdlib к путям ПОИСКА модулей: модули
    // адресуются без пакетного префикса (`\std();`, `\\builtin\string();`); `--no-stdlib` корень
    // убирает. Источник — верифицированный кэш; при его недоступности — распаковка в build-каталог.
    if (m_opts.use_stdlib) {
        std::string cache_error;
        if (const std::optional<std::filesystem::path> stdlib_root = ensureStdlibCacheEntry(cache_error)) {
            modulePaths.push_back(*stdlib_root);
        } else if (!cache_error.empty()) {
            FAULT("standard library package could not be extracted: {}", cache_error);
        } else {
            const std::optional<std::filesystem::path> archive = locateResourcePackage("stdlib");
            if (!archive) {
                FAULT("standard library package 'stdlib.trust' is not available in the toolchain resource directory");
            }
            const std::filesystem::path fallback = computeBuildDir(m_opts) / "stdlib-modules";
            std::error_code clean_ec;
            std::filesystem::remove_all(fallback, clean_ec);
            std::string extract_error;
            if (!extractAndVerifyPackage(*archive, fallback, extract_error)) {
                FAULT("standard library package could not be extracted: {}", extract_error);
            }
            modulePaths.push_back(fallback);
        }
    }
    loader().setModuleSearchPaths(std::move(modulePaths));
    std::vector<std::filesystem::path> packagePaths;
    packagePaths.reserve(m_opts.package_paths.size());
    for (const std::string& path : m_opts.package_paths) {
        packagePaths.emplace_back(path);
    }
    loader().setPackageSearchPaths(std::move(packagePaths));
    if (run.req.mode == RunMode::kProgram) {
        // Главный файл программы - от него отсчитывается имя модуля (@__MODULE_NAME__)
        // и имя entry-функции. Должен быть установлен до парсинга (parseSourceModule).
        m_ctx.source().setMainModuleFile(inputFile);
    }
    const std::string moduleName = std::string(m_ctx.source().filename(inputFile));
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
    run.mainModuleIndex = idx; // главный модуль — всегда модуль входного файла

    // Идентичность объявлений (R23) для ВСЕХ загруженных модулей (главный + импорты) ДО семантики:
    // форма record хранит `DeclId` поля как ключ связи «член AST ↔ поле формы» (R14). Идемпотентно.
    for (std::size_t i = 0; i < loader().moduleCount(); ++i) {
        const ModuleIndex midx{i};
        if (loader().isLoaded(midx)) {
            assignDeclarationIds(loader().bodyAst(midx), loader().moduleLogicalName(midx), &m_ctx.declCppFacts());
        }
    }

    // Root node of the program is a ModuleNode wrapping the module body.
    // Тело каждого загруженного модуля уже сконвертировано ОДИН раз загрузчиком
    // (сразу после парсинга), поэтому здесь конверсии нет.
    std::vector<AstNodePtr> astNodes = buildModuleRoot(idx);
    run.result.astNodes = std::move(astNodes);
    if (run.req.mode == RunMode::kProgram) {
        // Заполнить экспорт-интерфейс сайтов импорта (анализатор).
        resolveImportExports(m_ctx, loader(), *run.result.astNodes);
    } else {
        // Контроль «признака обработки» атрибутов сразу после конвертации Term->AST
        // (severity -Wunhandled-attr). В программном режиме отчёт идёт после
        // кодогенерации; в аналитическом без неё это единственная точка.
        reportUnhandledAttributes(m_ctx.runServices(), *run.result.astNodes);
    }
    return backend::StageOutcome::Done;
}

backend::StageOutcome Pipeline::runSemanticPhase(SemanticPassRunner& runner, MapperFile inputFile, PhaseRun& run) {
    if (!hasStep(run.req.steps, FrontendSteps::Semantic)) {
        return backend::StageOutcome::NoWork; // ParseAST-only прогон (--emit-module-deps, emit-режимы)
    }
    // При errorCount>0 после ParseAST semantic/transpile НЕ запускаются (AST — восстановленный
    // префикс после parser error recovery, а не только обрезанный). LSP-recover
    // (allow_semantic_on_errors) допускает частичный AST.
    if (hasStep(run.req.steps, FrontendSteps::ParseAST) && m_ctx.diag().errorCount() > 0 && !m_opts.allow_semantic_on_errors) {
        run.stopped = true;
        return backend::StageOutcome::Done;
    }
    EXPECT(run.result.astNodes.has_value() && "runAst must produce astNodes");
    // Вход семантики — узлы программы (реализации stdlib приходят явными импортами модулей).
    run.semanticOk = runner.run(*run.result.astNodes);
    // Provider-гейт: реализации builtin-типов нужны ТОЛЬКО C++-кодогену. Прогон без C++-бэкенда
    // (semantic-only, SMT/контракты, LSP) работает с интерфейсами и реализаций не требует.
    if (run.semanticOk && run.req.cpp && !verifySemanticProviders(m_ctx, *run.result.astNodes)) {
        run.semanticOk = false;
    }
    // Семантическая модель — продукт фазы, а не LSP-опция: таблица символов (persistent),
    // видимость импорта и ссылки собираются всегда, в т.ч. на частичном AST после ошибок.
    // Макросы дособираются до передачи контекста анализа в модель (дальше она неизменяема).
    appendMacroSymbols(m_ctx, runner.symbolIndex());
    ModuleProjection modules;
    modules.mainModuleIndex = run.mainModuleIndex;
    modules.moduleNames.reserve(loader().moduleCount());
    for (std::size_t i = 0; i < loader().moduleCount(); ++i) {
        modules.moduleNames.emplace_back(loader().moduleLogicalName(ModuleIndex{i}));
    }
    // Ссылки имя→объявление текущего файла (переход/hover по ссылке):
    // один обход AST по материализованным семантикой фактам.
    ReferenceIndex references;
    for (const AstNodePtr& root : *run.result.astNodes) {
        if (root != nullptr) {
            collectReferences(*root, inputFile, m_ctx.types(), m_ctx.source(), references);
        }
    }
    run.result.semantics.emplace(runner.takeAnalysis(), m_ctx.stringsPool(), m_types, std::move(modules), std::move(references));
    return backend::StageOutcome::Done;
}

std::optional<HirModule> Pipeline::buildLoweredHir(const std::vector<AstNodePtr>& roots, bool verify) {
    // HIR — производная форма (глубокий клон AST + lowering): единственный структурный вход
    // бэкендов и flow-анализов. Строится ПОСЛЕ семантики и из неизменённой AST.
    HirModule hir = buildHir(roots, m_ctx, *m_types);
    // Гейт контракта J2: структурная корректность и полнота материализованных фактов
    // проверяются ДО потребителей (flow-анализы/кодоген). Нарушение — ICE-политика.
    if (verify) {
        if (const auto violation = verifyHir(hir)) {
            reportInternalError(m_ctx.diag(), m_opts.allow_semantic_on_errors, *violation);
            // Потребители HIR не запускаются на непроверенном представлении: гейт errorCount
            // остановит конвейер (Finalize) до установки hirNodes.
            return std::nullopt;
        }
    }
    return hir;
}

backend::StageOutcome Pipeline::runLowerHirPhase(SemanticPassRunner& runner, MapperFile, PhaseRun& run) {
    if (!hasStep(run.req.steps, FrontendSteps::Semantic)) {
        return backend::StageOutcome::NoWork; // ParseAST-only прогон: HIR не строится
    }
    // После ICE представление недостоверно - HIR не строим.
    if (runner.crashed()) {
        run.hir.reset();
        return backend::StageOutcome::NoWork;
    }
    run.hir = buildLoweredHir(*run.result.astNodes, run.semanticOk);
    return backend::StageOutcome::Done;
}

backend::StageOutcome Pipeline::runBuildMirPhase(SemanticPassRunner&, MapperFile, PhaseRun& run) {
    if (!run.hir) {
        return backend::StageOutcome::NoWork;
    }
    // MIR — единственный вход flow-анализов и верификатора: CFG строятся ОДИН раз (buildMir).
    flow::MirModule mir = flow::buildMir(run.hir->nodes(), m_ctx.attrs());
    if (run.semanticOk) {
        if (const auto violation = flow::verifyFlowModule(mir)) {
            reportInternalError(m_ctx.diag(), m_opts.allow_semantic_on_errors, *violation);
            // MIR не устанавливаем: FlowAnalyses получит NoWork, потребители не запускаются
            // на недостоверном представлении.
            return backend::StageOutcome::Done;
        }
    }
    run.mir = std::move(mir);
    return backend::StageOutcome::Done;
}

backend::StageOutcome Pipeline::runFlowAnalysesPhase(SemanticPassRunner&, MapperFile, PhaseRun& run) {
    if (!run.mir) {
        return backend::StageOutcome::NoWork;
    }
    // Анализы запускаются менеджером в порядке зависимостей (DA → borrow).
    flow::AnalysisManager manager;
    manager.add(std::make_unique<semantic::DefiniteAssignmentPass>(m_ctx));
    manager.add(std::make_unique<semantic::BorrowFlowPass>(m_ctx));
    manager.run(*run.mir);
    return backend::StageOutcome::Done;
}

backend::StageOutcome Pipeline::runFinalizePhase(SemanticPassRunner&, MapperFile, PhaseRun& run) {
    if (!hasStep(run.req.steps, FrontendSteps::Semantic)) {
        return backend::StageOutcome::NoWork;
    }
    if (!run.semanticOk) {
        run.stopped = true; // блокирующие ошибки семантики — backends не запускаются
        return backend::StageOutcome::Done;
    }
    // Экспортированный generic без определения в компиляции (declaration-only форма не
    // выражает шаблоны). Диагностика добавляет ошибки - error-gate ниже её учитывает.
    checkExportedGenericDeclarations(run.mainModuleIndex);
    // Коллизии одноимённых определений разных модулей: C++-ABI модуль-независим -> диагностируем
    // раньше линкера (см. `checkCrossModuleCollisions`).
    checkCrossModuleCollisions();
    // Flow-анализы и верификатор выполняются ДО Finalize, поэтому их ошибки (uninit,
    // borrow, ICE-политика) обязаны так же останавливать бэкенды, как и ошибки семантики.
    if (m_ctx.diag().errorCount() > 0) {
        run.stopped = true;
        return backend::StageOutcome::Done;
    }
    EXPECT(run.hir.has_value() && "successful semantic run must produce HIR");
    run.result.hirNodes = std::move(*run.hir);
    run.result.mir = std::move(run.mir);
    return backend::StageOutcome::Done;
}

backend::StageOutcome Pipeline::runBackendsPhase(SemanticPassRunner&, MapperFile, PhaseRun& run) {
    if (!run.req.cpp && !run.req.smt) {
        return backend::StageOutcome::NoWork;
    }
    // Режимы разрешаются ОДИН раз (единственная точка) и передаются стадиям данными.
    const analysis::BehavioralModes modes = codegenModes(semantic::behavioralModesFromOptions(m_ctx.opts()), m_ctx.opts());
    backend::BackendRequest back_req{run.result.hirNodes ? &*run.result.hirNodes : nullptr,
                                     run.result.mir ? &*run.result.mir : nullptr,
                                     modes,
                                     run.mainModuleIndex,
                                     run.req.cpp,
                                     run.req.smt};
    // Целевой контракт — данные стадии (R37/E20): невладеющая ссылка на per-run владельца значений.
    back_req.target = &m_ctx.target();
    back_req.noop = m_opts.noop_backend;
    // Гейт доказательств: режим — данные стадии; сертификат пишется рядом с входом.
    if (verify::verifyModeFromOptions(m_ctx.opts()).has_value()) {
        back_req.verifyAll = true;
        if (!m_opts.input_file.empty()) {
            // Сертификат — артефакт сборки: <build_dir>/<stem>.trustcert (тот же каталог, что .cppt).
            const std::filesystem::path build_dir = computeBuildDir(m_opts);
            std::filesystem::path cert = build_dir / std::filesystem::path(m_opts.input_file).stem();
            cert += ".trustcert";
            back_req.certificatePath = cert;
            const auto inputHash = utils::md5HexFile(m_opts.input_file);
            EXPECT(inputHash.has_value() && "certificate input hash requires a readable input file");
            back_req.inputHash = *inputHash;
        }
    }
    backend::BackendServices services{m_ctx.runServices(), m_opts, loader()};
    if (!backend::runBackendStages(backend::kBackendStages, services, back_req, run.result)) {
        return backend::StageOutcome::Error; // ошибка стадии уже диагностирована
    }
    return backend::StageOutcome::Done;
}

} // namespace trust
