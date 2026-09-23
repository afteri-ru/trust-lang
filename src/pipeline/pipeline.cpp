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
#include "semantic/behavioral_modes.hpp"
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

// Документирующие комментарии к объявлениям привязываются ГРАММАТИКОЙ к терму-идентификатора
// (term->m_docs, см. include/syntax/parser.y.in: attachLeadingDoc/attachTrailingDoc) и переносятся
// в узел объявления TermToAstConverter::convert → AstNodeBase::documentation. SymbolCollectorHook
// читает их в finalize. Отдельный AST-обход (moduleDocMap/attachDocumentation) не требуется.
// -- Ассеты стандартной библиотеки (dsl.src, iterator.src) --
// Вшиты в бинарник компилятора ЕДИНЫМ провайдером компонента stdlib
// (src/assets/asset_provider.cpp, каталог include/assets/asset_catalog.hpp, префикс "@stdlib/…").
// Локальных #embed здесь НЕТ: иначе содержимое дублировалось бы с провайдером и с LSP
// (src/lsp/builtin_catalog.cpp использует тот же аксессор).

// -----------------------------------------------------------------------------
// reportUnhandledAttributes - обход всех узлов AST (проверка «признака обработки»).
//
// Для каждого узла, несущего атрибуты (AstNodeAttr::attrs()), проверяется, что у
// атрибута установлен хотя бы один из двух «признаков обработки» - анализатором
// (analyzer) или кодогенератором (codegen), см. detail::is_handled. Атрибут без
// обоих признаков - «необработанный»: выдаётся диагностика -Wunhandled-attr на
// severity из опций (default Warning; silence -Wunhandled-attr=ignore).
// Вызывается в конвейере: после конвертации Term→AST (runPipeline без Transpile)
// и после итоговой кодогенерации C++ (runPipeline/transpileModuleBody с Transpile).
// -----------------------------------------------------------------------------
namespace {

void reportUnhandledAttributes(Context& ctx, const std::vector<AstNodePtr>& roots) {
    if (roots.empty()) {
        return;
    }
    // Диагностика semantic-уровня: если не зарегистрирована (parse-only путь без семантики) -
    // пропускаем. Severity::Ignore = "ignore" (пользователь выключил -Wunhandled-attr=ignore).
    if (!ctx.opts().is_registered(semantic::DiagId::UnhandledAttr)) {
        return;
    }
    const Severity sev = ctx.opts().get(semantic::DiagId::UnhandledAttr);
    if (sev == Severity::Ignore) {
        return;
    }
    std::vector<AstNodePtr> stack(roots.begin(), roots.end());
    while (!stack.empty()) {
        AstNodePtr node = std::move(stack.back());
        stack.pop_back();
        if (!node) {
            continue;
        }
        if (const AstNodeAttr* attrNode = node->as_attr()) {
            for (const AttrId id : attrNode->attrs()) {
                if (detail::is_handled(id)) {
                    continue;
                }
                ctx.diag().report(sev, node->range(), semantic::DiagId::UnhandledAttr,
                                  "attribute '{}' is not handled by the analyzer or the C++ code generator", ctx.attrs().get_name(id));
            }
        }
        for (const AstNodePtr& child : node->children()) {
            if (child) {
                stack.push_back(child);
            }
        }
    }
}

} // namespace

// -- Stdlib prelude: объявления типов из ассета stdlib/iterator.src --
// Парсится после dsl.src (нужны макросы, напр. @include) и добавляется ПЕРЕД главным модулем,
// чтобы ClassDecl(ы) (Iterator<T> с @[borrowed]) дошли до семантики (TypeRegistry + NativeRefHook).
// Отключается через --no-stdlib (use_stdlib=false).
static void appendStdlibPrelude(Context& ctx, bool enabled, std::vector<AstNodePtr>& out) {
    if (!enabled) {
        return;
    }
    const std::string source(embeddedAssetContent(AssetId::kStdlibIteratorSrc));
    const auto errors_before = ctx.diag().errorCount();
    Parser parser(ctx);
    TermPtr term = parser.ParseText(source, "@stdlib/iterator");
    if (!term || ctx.diag().errorCount() != errors_before) {
        return; // битый prelude не должен валить компиляцию молча - ошибки уже в diag()
    }
    convertModuleBody(ctx, term, out);
}

// -- determineSteps: EmitFlags → PipelineSteps --
// Transpile включён в битмаску для Cpp-режима.

PipelineSteps Pipeline::determineSteps(EmitFlags flags) {
    if ((flags & EmitFlags::Cpp) != EmitFlags::None) {
        return PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile;
    }
    if ((flags & EmitFlags::AST) != EmitFlags::None) {
        return PipelineSteps::ParseAST;
    }
    if ((flags & EmitFlags::Tokens) != EmitFlags::None) {
        return PipelineSteps::ParseAST;
    }
    if ((flags & EmitFlags::Macros) != EmitFlags::None) {
        return PipelineSteps::ParseAST;
    }
    return PipelineSteps::None;
}

// -- Pipeline constructor --

Pipeline::Pipeline(Context& ctx, const PipelineOpts& opts)
: m_ctx(ctx)
, m_opts(opts) {
    m_ctx.diag().setMinSeverity(opts.quiet ? Severity::Error : Severity::Remark);
    // Pipeline владеет ModuleLoader и TypeRegistry и внедряет их в Context
    // (невладеющие указатели), чтобы diag не зависел от module_loader и types.
    m_loader = std::make_unique<ModuleLoader>(m_ctx);
    m_ctx.setLoader(m_loader.get());
    m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
    m_ctx.setTypes(m_types.get());
}

// -- Pipeline::loadDslMacros --
// By default the embedded trust/dsl.src is loaded into m_ctx. --dsl <file>
// replaces it, --no-dsl disables loading entirely. The Macro is unique by
// m_ctx and inherited by every (nested) Parser via Context.

void Pipeline::loadDslMacros() {
    if (m_opts.no_dsl || m_ctx.macro()) {
        return;
    }

    // Снимок CLI-значения keywords ДО загрузки dsl: value-флаг может быть установлен через
    // `--keywords=` (и, как побочный эффект value-флага, через `-Wkeywords=`). Это значение
    // побеждает дефолт из dsl.src (и .trust-format). Выполняется только один раз (первый реальный
    // загруз dsl); повторные вызовы упираются в guard выше и настройки CLI не переопределяют.
    std::string cliKw;
    if (auto v = m_ctx.opts().flagValueByName("keywords"); v && !v->empty()) {
        cliKw = std::string(*v);
    }

    std::string source;
    if (!m_opts.dsl_file.empty()) {
        auto content = trust::utils::FileIO::read<std::string>(m_opts.dsl_file);
        if (!content) {
            FAULT("Failed to open DSL file '{}'", m_opts.dsl_file);
        }
        source = std::move(*content);
    } else {
        source = std::string(embeddedAssetContent(AssetId::kStdlibDslSrc));
    }

    auto macro = std::make_shared<Macro>(m_ctx);
    m_ctx.setMacro(macro);
    Parser parser(m_ctx);
    // Встроенный DSL - «фиктивный» in-memory источник под именем "@stdlib/dsl"
    // (префикс '@' = файла на диске нет, readFilesFromDisk его пропускает).
    // Содержимое "@stdlib/dsl" - это stdlib/dsl.src; LSP сохраняет его на диск как
    // <tempDir>/stdlib/dsl.src, чтобы ссылки на определения макросов были
    // навигируемы (см. lsp/NAVIGATION.md).
    TermPtr term = parser.ParseText(source, "@stdlib/dsl");
    if (!term || m_ctx.diag().errorCount() > 0) {
        FAULT("Failed to parse DSL source");
    }

    // Приоритет keywords: CLI (снимок) > .trust-format "Keywords:" > дефолт dsl.src.
    if (!cliKw.empty()) {
        // CLI побеждает всё (в т.ч. дефолт dsl и .trust-format).
        m_ctx.opts().setFlagValueByName("keywords", cliKw);
    } else {
        // CLI не задан: .trust-format переопределяет дефолт dsl.
        const std::string cfgPath = trust::formatter::findConfig(std::filesystem::path(m_opts.input_file).parent_path().string());
        if (!cfgPath.empty()) {
            auto cfg = trust::formatter::loadConfig(cfgPath);
            if (cfg.ok && !cfg.opts.keywords.empty()) {
                std::string kwList;
                for (const auto& k : cfg.opts.keywords) {
                    if (!kwList.empty()) {
                        kwList += ",";
                    }
                    kwList += k;
                }
                m_ctx.opts().setFlagValueByName("keywords", kwList);
            }
        }
    }
}

// Возвращает эффективный список keywords (для -Wsigil и для форматирования).
std::vector<std::string> Pipeline::effectiveKeywords() {
    loadDslMacros();
    std::vector<std::string> out;
    if (auto v = m_ctx.opts().flagValueByName("keywords"); v && !v->empty()) {
        out = trust::formatter::splitKeywordList(*v);
    }
    return out;
}
// -- runPipeline (без Transpile) --

PipelineResult Pipeline::runPipeline(PipelineSteps steps, MapperFile inputFile) {
    PipelineResult result;
    // Stdlib prelude участвует ТОЛЬКО в семантике (регистрация типа Iterator + @[borrowed]),
    // но НЕ в кодогенерации (иначе добавляет лишнюю строку в вывод).
    std::vector<AstNodePtr> preludeNodes;

    if (hasStep(steps, PipelineSteps::ParseAST)) {
        // Read source and register main file as a module.
        // parseSourceModule recursively parses the file (expand_module=true)
        // and stores the result in the registry.
        loadDslMacros();
        std::string moduleName = std::string(m_ctx.source().filename(inputFile));
        std::size_t idx = m_ctx.loader().parseSourceModule(moduleName, inputFile);

        // Root node of the program is a ModuleNode wrapping the module body.
        auto modTerm = Term::Create(TermID::MODULE, moduleName);
        auto mn = std::make_shared<ModuleNode>(idx, std::move(modTerm));
        convertModuleBody(m_ctx, m_ctx.loader().body(idx), mn->m_body);
        std::vector<AstNodePtr> astNodes;
        appendStdlibPrelude(m_ctx, m_opts.use_stdlib, preludeNodes);
        astNodes.push_back(std::move(mn));
        result.astNodes = std::move(astNodes);
        // Контроль «признака обработки» атрибутов сразу после конвертации Term→AST
        // (severity -Wunhandled-attr; в этом режиме без Transpile это единственная точка).
        reportUnhandledAttributes(m_ctx, *result.astNodes);
    }

    // Если конвертация Term→AST дала ошибки (например нереализованная конструкция:
    // await/yield/when/filling) - семантический анализ и транспиляция на неполном/повреждённом
    // AST не запускаются (могут упасть на незаполненных детях). astNodes структурно построены;
    // факт ошибки виден вызывающему по diag().errorCount()>0.
    if (hasStep(steps, PipelineSteps::ParseAST) && m_ctx.diag().errorCount() > 0 && !m_opts.allow_semantic_on_errors) {
        return result;
    }

    if (hasStep(steps, PipelineSteps::Semantic)) {
        EXPECT(result.astNodes.has_value() && "runAst must produce astNodes");
        SemanticPassRunner runner(m_ctx);
        // Prelude идёт ПЕРВЫМ, чтобы @[borrowed]-классы были помечены до анализа пользовательского кода.
        std::vector<AstNodePtr> semanticRoots;
        semanticRoots.reserve(preludeNodes.size() + result.astNodes->size());
        semanticRoots.insert(semanticRoots.end(), preludeNodes.begin(), preludeNodes.end());
        semanticRoots.insert(semanticRoots.end(), result.astNodes->begin(), result.astNodes->end());
        bool ok = runner.run(semanticRoots);
        // Сбор символов для LSP - выполняется даже при ошибках (частичный AST).
        if (m_ctx.opts().is_enabled(semantic::FlagKind::Symbols)) {
            result.symbols = runner.takeSymbolIndex();
            // Макроопределения, записанные во время парсинга (не теряются после PopScope модуля).
            appendMacroSymbols(m_ctx, *result.symbols);
        }
        if (!ok) {
            return result;
        }
    }

    // Transpile без cppOut - FAULT
    if (hasStep(steps, PipelineSteps::Transpile)) {
        FAULT("runPipeline without cppOut called with Transpile step");
    }

    return result;
}

// -- runPipeline (с Transpile) --

namespace {

/// Рекурсивно заполняет экспорт-интерфейс всех сайтов импорта (`ModuleNode::isImport()`):
/// связывает индекс модуля через loader, сохраняет «полный» экспорт в реестре и кладёт
/// отфильтрованный (по маскам `\module(mod, masks)`) список экспортов в узел. Это шаг
/// «анализатора», выполняемый в конвейере после построения AST.
void resolveImportExports(Context& ctx, const std::vector<AstNodePtr>& astNodes) {
    for (const auto& node : astNodes) {
        if (!node) {
            continue;
        }
        if (node->kind() == ParserToken::Kind::ModuleDecl) {
            const auto& mn = static_cast<const ModuleNode&>(*node);
            if (mn.isImport()) {
                if (auto idx = ctx.loader().indexOf(mn.moduleId()); idx) {
                    const_cast<ModuleNode&>(mn).setModuleIndex(*idx);
                    const auto& body = mn.m_body;
                    std::vector<TermPtr> full = collectExportedDecls(body, "");
                    ctx.loader().setInterface(*idx, full);
                    const_cast<ModuleNode&>(mn).setExports(collectExportedDecls(body, mn.importMasks()));
                } else {
                    ctx.diag().report(Severity::Error, mn.range(), "Module '{}' is not loaded", mn.moduleId());
                }
            }
        }
        // Обход детей (в т.ч. тела импортированного модуля - там могут быть вложенные импорты).
        for (const auto& child : node->children()) {
            if (child) {
                std::vector<AstNodePtr> one{child};
                resolveImportExports(ctx, one);
            }
        }
    }
}

} // namespace
PipelineResult Pipeline::runPipeline(PipelineSteps steps, MapperFile inputFile, MapperFile cppOut, std::vector<ExportEntry>* out_exports,
                                     std::vector<std::string>* out_runtime_headers, std::vector<std::string>* out_link_libs, solver::SmtScript* out_script,
                                     std::string* out_entry_params, bool* out_saw_entry) {
    PipelineResult result;
    // Stdlib prelude участвует ТОЛЬКО в семантике (регистрация типа Iterator + @[borrowed]),
    // но НЕ в кодогенерации (иначе добавляет лишнюю строку в вывод).
    std::vector<AstNodePtr> preludeNodes;

    if (hasStep(steps, PipelineSteps::ParseAST)) {
        // Read source and register main file as a module.
        // parseSourceModule recursively parses the file (expand_module=true)
        // and stores the result in the registry.
        loadDslMacros();
        std::string moduleName = std::string(m_ctx.source().filename(inputFile));
        // Главный файл программы - от него отсчитывается имя модуля (@__MODULE_NAME__)
        // и имя entry-функции. Должен быть установлен до парсинга (parseSourceModule).
        m_ctx.source().setMainModuleFile(inputFile);
        std::size_t idx = m_ctx.loader().parseSourceModule(moduleName, inputFile);
        m_mainModuleIndex = idx;

        // Root node of the program is a ModuleNode wrapping the module body.
        auto modTerm = Term::Create(TermID::MODULE, moduleName);
        auto mn = std::make_shared<ModuleNode>(idx, std::move(modTerm));
        convertModuleBody(m_ctx, m_ctx.loader().body(idx), mn->m_body);
        std::vector<AstNodePtr> astNodes;
        appendStdlibPrelude(m_ctx, m_opts.use_stdlib, preludeNodes);
        astNodes.push_back(std::move(mn));
        result.astNodes = std::move(astNodes);

        // Заполнить экспорт-интерфейс сайтов импорта (анализатор).
        resolveImportExports(m_ctx, *result.astNodes);
    }

    // Если конвертация Term→AST дала ошибки (например нереализованная конструкция:
    // await/yield/when/filling) - семантический анализ и транспиляция на неполном/повреждённом
    // AST не запускаются (могут упасть на незаполненных детях). astNodes структурно построены;
    // факт ошибки виден вызывающему по diag().errorCount()>0.
    if (hasStep(steps, PipelineSteps::ParseAST) && m_ctx.diag().errorCount() > 0 && !m_opts.allow_semantic_on_errors) {
        return result;
    }

    // Семантический анализ. Runner живёт до конца функции, чтобы разрешённая семантикой
    // таблица символов (TypeId) была доступна кодогенерации (проброс в CppTranspiler).
    SemanticPassRunner runner(m_ctx);
    if (hasStep(steps, PipelineSteps::Semantic)) {
        EXPECT(result.astNodes.has_value() && "runAst must produce astNodes");
        // Prelude идёт ПЕРВЫМ, чтобы @[borrowed]-классы были помечены до анализа пользовательского кода.
        std::vector<AstNodePtr> semanticRoots;
        semanticRoots.reserve(preludeNodes.size() + result.astNodes->size());
        semanticRoots.insert(semanticRoots.end(), preludeNodes.begin(), preludeNodes.end());
        semanticRoots.insert(semanticRoots.end(), result.astNodes->begin(), result.astNodes->end());
        bool ok = runner.run(semanticRoots);
        // Сбор символов для LSP - выполняется даже при ошибках (частичный AST).
        if (m_ctx.opts().is_enabled(semantic::FlagKind::Symbols)) {
            result.symbols = runner.takeSymbolIndex();
            // Макроопределения, записанные во время парсинга (не теряются после PopScope модуля).
            appendMacroSymbols(m_ctx, *result.symbols);
        }
        if (!ok) {
            return result;
        }
    }

    // Шаг Solver: генерация SMT-LIB 2 (--solver-mode=export/calculate). После семантики имена/типы
    // разрешены; TrustToSmt переводит контракты функций в VCs (SmtScript), SmtPrinter печатает
    // его в файл (export) или SolverInterface исполняет (calculate) на стороне execute().
    if (hasStep(steps, PipelineSteps::Solver)) {
        EXPECT(result.astNodes.has_value() && "runAst must produce astNodes");
        solver::TrustToSmt bridge(m_ctx);
        auto script = bridge.generate(*result.astNodes);
        if (!script) {
            return result; // диагностика уже выдана (неподдерживаемая конструкция)
        }
        if (out_script) {
            *out_script = std::move(*script);
        }
    }

    if (hasStep(steps, PipelineSteps::Transpile)) {
        EXPECT(result.astNodes.has_value() && "runAst must produce astNodes");
        EXPECT(!cppOut.isInvalid() && "cppOut must be a valid output file");
        // Проброс разрешённых типов из семантики в кодогенерацию (единый TypeId с анализом).
        CppTranspiler transpiler(m_ctx, &runner.analysis().symbols(), semantic::behavioralModesFromOptions(m_ctx.opts()));
        // Однофайловый режим (-fsingle-file): активен для корневого модуля главного файла;
        // имя синтезируемого implicit-entry совпадает с ожидаемым entry_func_name обёртки.
        if (m_opts.single_file) {
            transpiler.configureSingleFile(true, m_ctx.source().moduleName(inputFile) + "__main__");
        }
        transpiler.generateToFile(*result.astNodes, cppOut);
        // Финальная сеть после итоговой генерации C++: не должно остаться ни одного
        // «необработанного» атрибута (без признака обработки анализатором/кодогенератором)
        // ни на одном узле AST. Severity -Wunhandled-attr (default Warning).
        reportUnhandledAttributes(m_ctx, *result.astNodes);
        if (out_exports) {
            *out_exports = transpiler.exports();
        }
        if (out_runtime_headers) {
            const auto& hdrs = transpiler.runtimeHeaders();
            out_runtime_headers->assign(hdrs.begin(), hdrs.end());
        }
        if (out_link_libs) {
            const auto& libs = transpiler.linkLibs();
            out_link_libs->assign(libs.begin(), libs.end());
        }
        if (out_entry_params) {
            *out_entry_params = transpiler.entryParams();
        }
        if (out_saw_entry) {
            *out_saw_entry = transpiler.sawEntry();
        }
    }

    return result;
}

std::unique_ptr<TypeRegistry> Pipeline::releaseTypes() {
    return std::move(m_types);
}

// -- emitOutput: вывод для emit-режимов --

int Pipeline::emitOutput(const PipelineResult& result) {
    auto flags = m_opts.emit_flags;

    if ((flags & EmitFlags::Tokens) != EmitFlags::None) {
        EXPECT(result.astNodes.has_value() && "runAst must produce astNodes");
        for (const auto& nodePtr : *result.astNodes) {
            if (nodePtr) {
                trust::outs() << nodePtr->text() << "\t" << ParserToken::name(nodePtr->kind()) << "\n";
            }
        }
        return 0;
    }

    if ((flags & EmitFlags::AST) != EmitFlags::None) {
        EXPECT(result.astNodes.has_value() && "runAst must produce astNodes");
        for (const auto& nodePtr : *result.astNodes) {
            if (nodePtr) {
                trust::outs() << nodePtr->dump() << "\n";
            }
        }
        return 0;
    }

    if ((flags & EmitFlags::Macros) != EmitFlags::None) {
        // Дамп макроопределений (для отладки/диагностики макропроцессора).
        for (const auto& md : m_ctx.macroDefs()) {
            trust::outs() << md.name << "\n";
        }
        return 0;
    }

    FAULT("unreachable: emitOutput called for emit-flags without matching handler");
    return 1;
}

// -- runTranspileAndSave: общий helper для compile и emit-cpp --

Pipeline::TranspileOutput Pipeline::runTranspileAndSave(MapperFile inputFile) {
    TranspileOutput out;
    out.cpptPath = computeCpptPath(m_opts);
    // Use the real .cppt basename so the source map's output filename can be
    // resolved back to the generated file (findFile / findCppToTrust).
    out.outputIdx = m_ctx.source().add_output(out.cpptPath.filename().string());
    if (out.outputIdx.isInvalid()) {
        trust::errs() << "error: failed to create output file entry\n";
        return out;
    }

    auto steps = PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile;
    auto result = runPipeline(steps, inputFile, out.outputIdx, &out.exports, &out.runtimeHeaders, &out.linkLibs, nullptr, &out.entryParams, &out.hasEntry);
    if (!result.isValid() || m_ctx.diag().errorCount() > 0) {
        return out;
    }

    if (!saveCppAndEmbedSourceMap(m_ctx, out.outputIdx, out.cpptPath, m_opts.verbose, out.exports, /*embed_export_table=*/m_opts.embed_source_map,
                                  buildProgramRecord(std::filesystem::path(m_opts.input_file), m_ctx, m_mainModuleIndex, m_opts),
                                  /*embed_source_map=*/m_opts.embed_source_map, codegenArgsRecord(m_opts))) {
        return out;
    }

    if (m_opts.verbose) {
        trust::errs() << "info: generated " << out.cpptPath << "\n";
    }

    out.valid = true;
    return out;
}

// -- generateModuleOutputs / transpileModuleBody: отдельные .cppt исходных модулей --

std::vector<std::filesystem::path> Pipeline::generateModuleOutputs(std::vector<std::string>* module_runtime_headers,
                                                                   std::vector<std::string>* module_link_libs) {
    namespace fs = std::filesystem;
    std::vector<fs::path> paths;
    const fs::path build_dir = computeBuildDir(m_opts);
    for (std::size_t idx = 0; idx < m_ctx.loader().moduleCount(); ++idx) {
        if (idx == m_mainModuleIndex) {
            continue; // главный файл генерируется как основной .cppt
        }
        if (!m_ctx.loader().isLoaded(idx)) {
            continue; // незагруженный (бинарный/заглушка) модуль не транслируем
        }
        const fs::path stem = fs::path(m_ctx.loader().moduleName(idx)).stem();
        const fs::path modPath = build_dir / (stem.string() + ".cppt");
        transpileModuleBody(idx, modPath, module_runtime_headers, module_link_libs);
        paths.push_back(std::move(modPath));
    }
    return paths;
}

void Pipeline::transpileModuleBody(std::size_t idx, const std::filesystem::path& cpptPath, std::vector<std::string>* runtime_headers,
                                   std::vector<std::string>* link_libs) {
    namespace fs = std::filesystem;
    MapperFile outputIdx = m_ctx.source().add_output(cpptPath.filename().string());
    if (outputIdx.isInvalid()) {
        return;
    }

    // Корневой узел модуля с полным телом (определения) - отдельная единица трансляции.
    auto modTerm = Term::Create(TermID::MODULE, m_ctx.loader().moduleName(idx));
    auto mn = std::make_shared<ModuleNode>(idx, std::move(modTerm));
    convertModuleBody(m_ctx, m_ctx.loader().body(idx), mn->m_body);
    std::vector<AstNodePtr> astNodes;
    astNodes.push_back(std::move(mn));
    resolveImportExports(m_ctx, astNodes); // вложенные импорты внутри модуля

    if (m_ctx.diag().errorCount() > 0) {
        return;
    }

    // Семантика + кодогенерация тела модуля (аналогично главному файлу).
    SemanticPassRunner runner(m_ctx);
    if (!runner.run(astNodes)) {
        return;
    }
    CppTranspiler transpiler(m_ctx, &runner.analysis().symbols(), semantic::behavioralModesFromOptions(m_ctx.opts()));
    transpiler.generateToFile(astNodes, outputIdx);
    // Финальная сеть (как в runPipeline): после кодогенерации тела модуля не должно
    // остаться «необработанных» атрибутов (severity -Wunhandled-attr).
    reportUnhandledAttributes(m_ctx, astNodes);
    if (runtime_headers) {
        const auto& hdrs = transpiler.runtimeHeaders();
        runtime_headers->insert(runtime_headers->end(), hdrs.begin(), hdrs.end());
    }
    if (link_libs) {
        const auto& libs = transpiler.linkLibs();
        link_libs->insert(link_libs->end(), libs.begin(), libs.end());
    }
    if (m_ctx.diag().errorCount() > 0) {
        return;
    }
    // Модуль-исходник линкуется в программу: экспорт-таблица принадлежит главному файлу.
    // Встраивание source map управляется флагом -fsourcemap (по умолчанию встраивается).
    saveCppAndEmbedSourceMap(m_ctx, outputIdx, cpptPath, m_opts.verbose, transpiler.exports(), /*embed_export_table=*/false, {},
                             /*embed_source_map=*/m_opts.embed_source_map, codegenArgsRecord(m_opts));
}

// -- Execute: полный цикл для CLI --

int Pipeline::execute() {
    // -- 1. Special modes (module-info) --
    if (m_opts.module_info_requested) {
        return showModuleInfo(m_opts.input_file, m_opts.verbose);
    }

    // -- 1a. --format-dump-config: печать настроек форматирования с дефолтами/комментариями --
    if (m_opts.format_dump_config) {
        trust::outs() << trust::formatter::dumpConfig({});
        return 0;
    }

    // -- 1a'. --complete-options: имена опций и -W-диагностик для shell-completion --
    if (m_opts.complete_options) {
        for (const std::string& t : trust::driverOptionTokens()) {
            trust::outs() << t << "\n";
        }
        for (const std::string& w : m_ctx.opts().allWNames()) {
            trust::outs() << w << "\n";
        }
        return 0;
    }

    // -- 1a''. --complete-files: опции со значением-файлом/путём для shell-completion --
    if (m_opts.complete_files) {
        for (const std::string& t : trust::driverFileValueTokens()) {
            trust::outs() << t << "\n";
        }
        return 0;
    }


    // -- 1b. Форматирование (pretty-print): --format / --format-check --
    if (m_opts.format_requested) {
        return runFormatMode();
    }

    // -- 2. Load input file --
    if (!std::filesystem::exists(m_opts.input_file)) {
        trust::errs() << "error: input file not found: " << m_opts.input_file << "\n";
        return 1;
    }

    MapperFile inputFile = m_ctx.source().load_file(m_opts.input_file);
    if (m_opts.verbose) {
        trust::errs() << "info: loaded " << m_opts.input_file << "\n";
    }

    // -- 3. LexemesOnly - быстрый путь: только legacy лексер --
    // Модули здесь не раскрываются: это режим вывода лексем, а не загрузки AST.
    if ((m_opts.emit_flags & EmitFlags::LexemesOnly) != EmitFlags::None) {
        return runLexemesOnlyMode(inputFile);
    }

    // -- 4a. Solver: export/calculate (генерация SMT-LIB 2 для z3) --
    {
        const auto smode = semantic::solverModeFromOptions(m_ctx.opts());
        if (smode == semantic::SolverMode::kExport || smode == semantic::SolverMode::kCalculate) {
            return runSolverMode(inputFile);
        }
    }

    // -- 4. Compile mode --
    if (m_opts.should_compile()) {
        return runCompileMode(inputFile);
    }

    // -- 5. Emit: Cpp mode (transpile + stdout) --
    if ((m_opts.emit_flags & EmitFlags::Cpp) != EmitFlags::None) {
        return runEmitCppMode(inputFile);
    }

    // -- 6. Other emit modes (Tokens, AST, Macros) --
    return runOtherEmitModes(inputFile);
}

} // namespace trust