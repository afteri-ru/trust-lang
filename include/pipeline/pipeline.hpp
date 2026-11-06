#pragma once

#include "pipeline/run/types.hpp"
#include "pipeline/run/phases.hpp"
// Umbrella: детали сгруппированы в internal-подкаталогах, потребитель включает ОДИН заголовок.
#include "pipeline/dsl/dsl.hpp"
#include "pipeline/modules/symbols.hpp"
#include "pipeline/paths/paths.hpp"
#include "pipeline/sourcemap/source_map.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "utils/string_interner.hpp"

namespace trust {

class Context;
class ModuleLoader;
class SemanticPassRunner;
class TypeSystem;
class TargetContract;

class Pipeline {
  public:
    Pipeline(Context& ctx, const PipelineOpts& opts);
    ~Pipeline();

    /// Компиляция/сборка главного файла вынесена в драйвер (`builddriver`): `Pipeline` производит
    /// артефакты, сборку/линковку вызывает нижестоящий слой (E20/R34).
    int emitCppToStdout(MapperFile inputFile);

    /// Граф файловых зависимостей в stdout (--emit-module-deps): правила make
    /// `<target>: <deps>` для главного `.cppt`, `.cppt` модулей и пакетов.
    int emitModuleDepsToStdout(MapperFile inputFile);

    /// Загружает DSL (если включён; уважает --dsl/--no-dsl) и возвращает эффективный список
    /// keywords (приоритет CLI --keywords > .trust-format "Keywords:" > дефолт dsl.src).
    /// Используется и для диагностики -Wsigil, и для форматирования (набор «ключевых слов»).
    std::vector<std::string> effectiveKeywords();

    /// Единственная публичная точка прогона: фронтенд (`steps` × `mode`) и запрошенные
    /// бэкенды. Артефакты бэкендов возвращаются в `PipelineResult` (presence = произведено).
    PipelineResult run(const RunRequest& req, MapperFile inputFile);

    /// Артефакты модулей: заголовок `<enc>.hpp` для каждого загруженного модуля (кроме главного)
    /// и отдельный `.cppt`, если модуль даёт out-of-line определения. Вызывается после
    /// `run`/`runCppToFile`: главный TU включает заголовки модулей (`#include "<enc>.hpp"`),
    /// поэтому заголовки должны существовать до компиляции (CLI, `--emit-cpp`, LSP).
    struct ModuleOutputs {
        std::vector<std::filesystem::path> cpptPaths;
        std::vector<std::string> packageHeaders; ///< рантайм-заголовки модульных артефактов
        std::vector<std::string> linkLibs;       ///< `-l<имя>` из атрибутов модульных артефактов
        bool ok = true;                          ///< все артефакты сгенерированы (иначе диагностика уже выдана)
    };
    ModuleOutputs generateModuleOutputs(ModuleIndex mainModuleIndex);

    /// Прогон с C++-бэкендом в build-каталог: `.cppt` + `.src_map` пишет сам Pipeline
    /// (backend::CppTarget::path). nullopt - ошибка фронтенда/записи. Атрибуты — в PipelineResult::cpp.
    /// Вход драйвера сборки (`builddriver`): артефакты производит конвейер, сборку вызывает нижестоящий слой.
    std::optional<PipelineResult> runCppToFile(MapperFile inputFile);

    /// Логическое имя модуля (read-model): нужно драйверу для entry-идентификатора
    /// (`moduleEntryIdent`) и заголовков модулей — без раскрытия загрузчика.
    [[nodiscard]] std::string moduleLogicalName(ModuleIndex idx) const;

    /// Целевой контракт прогона (read-model): целевая платформа, стандарт C++, целевые C++-факты.
    /// Единственный путь чтения значений, которые нужны сборке (`--std=`) и будущим бэкендам (R37/E20).
    [[nodiscard]] const TargetContract& targetContract() const noexcept;

  private:
    Context& m_ctx;
    /// Пул строк прогона активен на ВСЁ время жизни Pipeline (включая конструктор и загрузку DSL
    /// вне run): тексты/доки прогона принадлежат владельцу прогона, процессный пул не задействован (E9).
    StringInternerScope m_stringsScope;
    const PipelineOpts m_opts;
    /// Один Pipeline — одна компиляция: повторный run() смешал бы per-run владельцев
    /// (TargetContract, SourceMapWriter, ModuleLoader) с уже сброшенным реестром типов.
    bool m_hasRun = false;
    std::unique_ptr<ModuleLoader> m_loader; ///< Владение загрузчиком модулей (внедряется в m_ctx)
    std::shared_ptr<TypeSystem> m_types;    ///< Разделяемое владение типовой системой (внедряется в m_ctx)
    /// Владение целевым контрактом прогона (внедряется в m_ctx как порт `Context::target()`).
    std::unique_ptr<TargetContract> m_target;

    /// Единственный владелец загрузчика модулей (верхний слой); `Context` получает только
    /// невладеющий `ModuleResolver*` для парсера.
    [[nodiscard]] ModuleLoader& loader() { return *m_loader; }
    [[nodiscard]] const ModuleLoader& loader() const { return *m_loader; }

    /// Корневой узел модуля (ModuleNode с телом, конвертированным загрузчиком).
    std::vector<AstNodePtr> buildModuleRoot(ModuleIndex idx);

    /// Экспортированные generic-функции без определения в компиляции (declaration-only форма
    /// не выражает шаблоны: C++ требует тело). Проверяются ТОЛЬКО модули (не главный юнит).
    void checkExportedGenericDeclarations(ModuleIndex mainModuleIndex);

    /// Коллизии одноимённых ОПРЕДЕЛЕНИЙ разных модулей: C++-имя модуль-независимо (ABI прозрачен
    /// относительно C++), поэтому два модуля, определяющие одно и то же имя в одной области,
    /// дают multiple definition. Диагностируем раньше линкера; изоляция — область имён `ns::`.
    void checkCrossModuleCollisions();

    /// Единственный оркестратор прогона: исполняет запрошенные фазы в порядке таблицы
    /// PIPELINE_PHASE_LIST. Условий реализации не содержит: фазы сами решают NoWork/остановку.
    void runPhases(SemanticPassRunner& runner, MapperFile inputFile, PhaseRun& run);

    /// HIR из корней семантики (buildHir + verifyHir-гейт). ЕДИНСТВЕННАЯ точка построения HIR:
    /// LowerHir-фаза и модульные юниты (`lowerModuleUnit`) используют одну реализацию.
    /// nullopt — гейт verifyHir нарушен (ошибка уже диагностирована, ICE-политика).
    std::optional<HirModule> buildLoweredHir(const std::vector<AstNodePtr>& roots, bool verify);

    // Реализации фаз: единая сигнатура (runner/inputFile фаза игнорирует, если не нужны);
    // порядок исполнения — таблица PIPELINE_PHASE_LIST, не эти объявления.
    backend::StageOutcome runReadParsePhase(SemanticPassRunner& runner, MapperFile inputFile, PhaseRun& run);
    backend::StageOutcome runSemanticPhase(SemanticPassRunner& runner, MapperFile inputFile, PhaseRun& run);
    backend::StageOutcome runLowerHirPhase(SemanticPassRunner& runner, MapperFile inputFile, PhaseRun& run);
    backend::StageOutcome runBuildMirPhase(SemanticPassRunner& runner, MapperFile inputFile, PhaseRun& run);
    backend::StageOutcome runFlowAnalysesPhase(SemanticPassRunner& runner, MapperFile inputFile, PhaseRun& run);
    backend::StageOutcome runFinalizePhase(SemanticPassRunner& runner, MapperFile inputFile, PhaseRun& run);
    backend::StageOutcome runBackendsPhase(SemanticPassRunner& runner, MapperFile inputFile, PhaseRun& run);

    /// Понижение тела загруженного модуля: корневой `ModuleNode` + экспорт-интерфейс импортов →
    /// HIR (verify, без flow). ЕДИНСТВЕННАЯ точка, из которой строятся артефакты модуля:
    /// `.cppt` и `.hpp` используют один lowering.
    struct ModuleUnitLowering {
        std::vector<AstNodePtr> astNodes;
        std::optional<HirModule> hir; ///< nullopt - ошибка (диагностика уже выдана)
    };
    ModuleUnitLowering lowerModuleUnit(ModuleIndex moduleIdx);

    /// Запись заголовка модуля (`<enc>.hpp`) по `path`: интерфейсная проекция экспорт-набора
    /// модуля (`CppTranspiler::generateHeaderToFile`). nullopt - ошибка кодогенерации/записи
    /// (диагностика уже выдана).
    std::optional<backend::HeaderArtifact> emitModuleHeader(const HirModule& hir, ModuleIndex moduleIdx, const std::filesystem::path& path);

    // Loads DSL macros into m_ctx (stdlib/dsl.src from the stdlib package by default, --dsl <file>
    // replaces it, --no-dsl disables). Loaded once and inherited by every
    // nested Parser through Context.
    void loadDslMacros();

    /// Seed целевых C++-фактов встроенных интринсиков из статической таблицы `target::builtinCppSeed()`
    /// (привязка «trust-имя → TypeId»). Единственный источник C++-имени/include этих типов (R1/R37).
    void seedIntrinsicCppFacts();
};

} // namespace trust
