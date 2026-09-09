#pragma once

#include "pipeline/makefile_build.hpp"
#include "module_loader/module_loader.hpp"
#include "types/registry.hpp"
#include "session/context.hpp"
#include "ast/ast_nodes.hpp"
#include "transpiler/transpiler.hpp"
#include "analysis/symbol_index.hpp"
#include "solver/smt_ast.hpp"
#include "driver/options.hpp"

namespace trust {
class Macro;
} // namespace trust

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trust {


// -- Pipeline result (stateless output of runPipeline) --

struct PipelineResult {
    std::optional<std::vector<AstNodePtr>> astNodes;
    /// Собранные семантикой символы (имя→тип/диапазоны) для LSP; заполняется при
    /// semantic::FlagKind::Symbols (даже при ошибках лексера/парсера, на частичном AST).
    std::optional<SymbolIndex> symbols;

    bool isValid() const { return astNodes.has_value(); }
};

// Дособирает макросы из Context::macroDefs() в SymbolIndex (isMacro=true). Единая точка для
// обоих runPipeline и для LSP-сервера (transpileSource): имена макросов, записанные во время
// парсинга, не должны теряться даже если семантический шаг не выполнился/упал (Fatal/исключение).
inline void appendMacroSymbols(const Context& ctx, SymbolIndex& out) {
    for (const auto& md : ctx.macroDefs()) {
        SymbolInfo si;
        si.name = md.name;
        si.type = INVALID_TYPE_ID;
        si.nameRange = md.range;
        si.scopeRange = md.range;
        si.isMacro = true;
        si.documentation = md.documentation;
        out.push_back(std::move(si));
    }
}

// -- Pipeline --

class Pipeline {
  public:
    Pipeline(Context& ctx, const PipelineOpts& opts);

    // -- CLI entry point --
    int execute();

    /// Забирает владение реестром типов из Pipeline (для LSP): TypeId в SymbolInfo остаётся
    /// валидным после уничтожения Pipeline. Встроенные типы при этом разделяются через общее
    /// ядро TypeRegistry (без дублирования), пер-инстансовые - только пользовательские.
    std::unique_ptr<TypeRegistry> releaseTypes();

    /// Загружает DSL (если включён; уважает --dsl/--no-dsl) и возвращает эффективный список
    /// keywords (приоритет CLI --keywords > .trust-format "Keywords:" > дефолт dsl.src).
    /// Используется и для диагностики -Wsigil, и для форматирования (набор «ключевых слов»).
    std::vector<std::string> effectiveKeywords();

    // -- Базовый runPipeline (без Transpile) --
    // Выполняет ParseAST, Semantic.
    // Transpile без cppOut - FAULT.
    // Возвращает PipelineResult с опциональным AST.
    PipelineResult runPipeline(PipelineSteps steps, MapperFile inputFile);

    // -- runPipeline с Transpile --
    // Дополнительно выполняет CppTranspiler, записывая результат в cppOut.
    PipelineResult runPipeline(PipelineSteps steps, MapperFile inputFile, MapperFile cppOut, std::vector<ExportEntry>* out_exports = nullptr,
                               std::vector<std::string>* out_runtime_headers = nullptr, std::vector<std::string>* out_link_libs = nullptr,
                               solver::SmtScript* out_script = nullptr, std::string* out_entry_params = nullptr, bool* out_saw_entry = nullptr);

    // -- Статические методы для CLI --
    static ParseResult parseArgs(int argc, char* argv[]);
    static ParseResult parseArgs(std::span<char*> argv);
    static bool isSpecialExit(const ParseResult& r);

  private:
    Context& m_ctx;
    const PipelineOpts m_opts;
    std::unique_ptr<ModuleLoader> m_loader; ///< Владение загрузчиком модулей (внедряется в m_ctx)
    std::unique_ptr<TypeRegistry> m_types;  ///< Владение реестром типов (внедряется в m_ctx)

    static PipelineSteps determineSteps(EmitFlags flags);

    int emitOutput(const PipelineResult& result);

    // -- Обработчики специальных режимов execute() (вынесены в pipeline_modes.cpp) --
    int runFormatMode();
    int runLexemesOnlyMode(MapperFile inputFile);
    int runSolverMode(MapperFile inputFile);
    int runCompileMode(MapperFile inputFile);
    int runEmitCppMode(MapperFile inputFile);
    int runOtherEmitModes(MapperFile inputFile);

    // Loads DSL macros into m_ctx (embedded trust/dsl.src by default, --dsl <file>
    // replaces it, --no-dsl disables). Loaded once and inherited by every
    // nested Parser through Context.
    void loadDslMacros();

    // Helper: run Transpile pipeline + save .cppt + .src_map
    struct TranspileOutput {
        MapperFile outputIdx;
        std::filesystem::path cpptPath;
        std::vector<ExportEntry> exports;
        /// Рантайм-заголовки (напр. "trust/rational.hpp"), реально использованные
        /// сгенерированным кодом - pipeline извлечёт их из trust-runtime.so.
        std::vector<std::string> runtimeHeaders;
        /// Флаги линковки нативных библиотек (`-l<имя>`) из `@[link("имя")]`.
        std::vector<std::string> linkLibs;
        /// C++-типы параметров entry-функции (`<модуль>__main__`, без имён, через \", \").
        /// Пусто, если entry без параметров. Используется для генерации совпадающего extern
        /// в `_main.cppt` (обёртка main обязана объявить ту же сигнатуру, что и тело entry).
        std::string entryParams;
        /// True, если модуль содержит entry-функцию (`<module>__main__`) - отличает программу
        /// от библиотеки/скрипта (entryParams пуст и для `@main()` без параметров).
        bool hasEntry = false;
        bool valid = false;
    };
    TranspileOutput runTranspileAndSave(MapperFile inputFile);

    // Генерация отдельного .cppt для каждого загруженного исходного модуля (кроме главного).
    // Определения модулей живут в отдельных единицах трансляции и линкуются с главным файлом.
    // Возвращает пути к сгенерированным .cppt модулей. Если задан, накапливает
    // рантайм-заголовки, использованные модульными единицами.
    std::vector<std::filesystem::path> generateModuleOutputs(std::vector<std::string>* module_runtime_headers = nullptr,
                                                             std::vector<std::string>* module_link_libs = nullptr);

    // Транспиляция тела одного модуля (индекс) в отдельный .cppt (полное тело - определения).
    void transpileModuleBody(std::size_t idx, const std::filesystem::path& cpptPath, std::vector<std::string>* runtime_headers = nullptr,
                             std::vector<std::string>* link_libs = nullptr);

    std::size_t m_mainModuleIndex{0}; ///< Индекс корневого (главного) модуля
};

// -- Free functions (old parse_args wrappers) --

inline ParseResult Pipeline::parseArgs(int argc, char* argv[]) {
    return parseArgs(std::span<char*>(argv, static_cast<size_t>(argc)));
}

inline bool Pipeline::isSpecialExit(const ParseResult& r) {
    return r.opts.help_requested || r.opts.version_requested || r.exit_code != 0;
}

} // namespace trust

// -- Модули конвейера (декомпозиция pipeline.cpp) --
// Свободные функции, ранее жившие в pipeline.cpp, разнесены по модулям по зонам
// ответственности. pipeline.hpp остаётся «зонтиком» и включает их, поэтому все
// потребители `pipeline/pipeline.hpp` видят прежний набор функций без правок.
#include "pipeline/io.hpp"
#include "pipeline/runtime_locator.hpp"
#include "pipeline/build.hpp"
#include "pipeline/run.hpp"
#include "pipeline/source_map.hpp"
#include "pipeline/archive.hpp"
#include "pipeline/module_info.hpp"