#pragma once

#include "pipeline/run/types.hpp"
// Umbrella: детали сгруппированы в internal-подкаталогах, потребитель включает ОДИН заголовок.
#include "pipeline/archive/archive.hpp"
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

namespace trust {

class Context;
class ModuleLoader;
class SemanticPassRunner;
class TypeSystem;

// -- Pipeline --

class Pipeline {
  public:
    Pipeline(Context& ctx, const PipelineOpts& opts);
    ~Pipeline();

    /// Компиляция/сборка главного файла (--run, -c/-a/--shared-lib, --emit-build-dir).
    /// CLI-диспетчеризация и выбор режима — вне Pipeline (см. pipeline/commands.hpp).
    int compileProgram(MapperFile inputFile);

    /// Транспиляция в build-каталог и вывод .cppt в stdout (--emit-cpp).
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

  private:
    Context& m_ctx;
    const PipelineOpts m_opts;
    std::unique_ptr<ModuleLoader> m_loader; ///< Владение загрузчиком модулей (внедряется в m_ctx)
    std::shared_ptr<TypeSystem> m_types;  ///< Разделяемое владение типовой системой (внедряется в m_ctx)

    /// Результат общего фронтенда: ParseAST → AST → error-gate → Semantic → symbols.
    struct FrontendResult {
        PipelineResult result;                ///< astNodes + symbols
        std::vector<AstNodePtr> preludeNodes; ///< stdlib prelude (участвует только в семантике)
        ModuleIndex mainModuleIndex{};        ///< индекс главного модуля (модуль входного файла)
        bool stopped = false;                 ///< фронтенд остановлен на ошибках (бэкенды не запускаются)
    };

    /// Единственный владелец загрузчика модулей (верхний слой); `Context` получает только
    /// невладеющий `ModuleResolver*` для парсера.
    [[nodiscard]] ModuleLoader& loader() { return *m_loader; }
    [[nodiscard]] const ModuleLoader& loader() const { return *m_loader; }

    /// Корневой узел модуля (ModuleNode с телом, конвертированным загрузчиком).
    std::vector<AstNodePtr> buildModuleRoot(const std::string& moduleName, ModuleIndex idx);

    /// S4c: exported generic-функции без определения в компиляции (declaration-only форма
    /// не выражает шаблоны: C++ требует тело). Проверяются ТОЛЬКО модули (не главный юнит).
    /// `roots` - корни семантики (prelude + главный юнит): определения ищутся везде.
    void checkExportedGenericDeclarations(ModuleIndex mainModuleIndex);

    /// Единственная реализация фронтенда прогона. `runner` передаётся снаружи: он обязан жить
    /// до конца прогона, если после семантики запрошена кодогенерация (TypeId из анализа).
    FrontendResult runFrontend(FrontendSteps steps, MapperFile inputFile, RunMode mode, SemanticPassRunner& runner);

    /// Фаза производного представления: HIR (клон AST + lowering) → verify → MIR → verify →
    /// flow-анализы (DA → borrow). Вход — AST после семантики (включая prelude главного юнита);
    /// AST не изменяется. Единственная точка, где строится HIR/MIR и запускаются их потребители.
    /// `verify` — гейт верификаторов: включается только на успешном прогоне семантики
    /// (частичный AST в LSP даёт законные «дыры», это не ICE). `mir` отсутствует, если гейт
    /// нарушен (потребители не запускаются на недостоверном представлении).
    struct LoweredUnit {
        HirModule hir;
        std::optional<flow::MirModule> mir;
    };
    LoweredUnit lowerAndAnalyze(const std::vector<AstNodePtr>& roots, bool verify, bool runFlow = true);

    /// Запускает запрошенные бэкенды после фронтенда (SMT → C++). Ошибка SMT-генерации
    /// останавливает прогон до кодогенерации (диагностика уже выдана).
    void runBackends(PipelineResult& result, const RunRequest& req);

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

    // Прогон с C++-бэкендом в build-каталог: `.cppt` + `.src_map` пишет сам Pipeline
    // (backend::CppTarget::path). nullopt - ошибка фронтенда/записи. Атрибуты — в PipelineResult::cpp.
    std::optional<PipelineResult> runCppToFile(MapperFile inputFile);
};

} // namespace trust
