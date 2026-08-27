#include "pipeline/pipeline.hpp"
#include "pipeline/cli.hpp"
#include "pipeline/makefile_build.hpp"
#include "ast/term_to_ast.hpp"
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

#include "utils/io.hpp"
#include "utils/file_io.hpp"
#include "formatter/format.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace trust {

// Документирующие комментарии к объявлениям привязываются ГРАММАТИКОЙ к терму-идентификатора
// (term->m_docs, см. include/syntax/parser.y.in: attachLeadingDoc/attachTrailingDoc) и переносятся
// в узел объявления TermToAstConverter::convert → AstNodeBase::documentation. SymbolCollectorHook
// читает их в finalize. Отдельный AST-обход (moduleDocMap/attachDocumentation) не требуется.
// -- Встроенный trust/dsl.src: компилируется в бинарник через #embed --
// Относительный путь от каталога исходника (src/pipeline/ → include/trust/).

static constexpr char kEmbeddedDslSrc[] = {
#embed "../../include/trust/dsl.src"
    , 0};

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
// replaces it, --no-dsl disables loading entirely. The Macro is owned by
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
        source.assign(kEmbeddedDslSrc, sizeof(kEmbeddedDslSrc) - 1);
    }

    auto macro = std::make_shared<Macro>(m_ctx);
    m_ctx.setMacro(macro);
    Parser parser(m_ctx);
    // Встроенный DSL - «фиктивный» in-memory источник под именем "@trust/dsl"
    // (префикс '@' = файла на диске нет, readFilesFromDisk его пропускает).
    // Содержимое "@trust/dsl" - это trust/dsl.src; LSP сохраняет его на диск как
    // <tempDir>/trust/dsl.src, чтобы ссылки на определения макросов были
    // навигируемы (см. lsp/NAVIGATION.md).
    TermPtr term = parser.ParseText(source, "@trust/dsl");
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
        astNodes.push_back(std::move(mn));
        result.astNodes = std::move(astNodes);
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
        bool ok = runner.run(*result.astNodes);
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
                                     std::vector<std::string>* out_runtime_headers, std::vector<std::string>* out_link_libs, solver::SmtScript* out_script) {
    PipelineResult result;

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
        bool ok = runner.run(*result.astNodes);
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
        CppTranspiler transpiler(m_ctx, &runner.analysis().symbols());
        transpiler.generateToFile(*result.astNodes, cppOut);
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
    auto result = runPipeline(steps, inputFile, out.outputIdx, &out.exports, &out.runtimeHeaders, &out.linkLibs);
    if (!result.isValid() || m_ctx.diag().errorCount() > 0) {
        return out;
    }

    if (!saveCppAndEmbedSourceMap(m_ctx, out.outputIdx, out.cpptPath, m_opts.verbose, out.exports, /*embed_export_table=*/true,
                                  buildProgramRecord(std::filesystem::path(m_opts.input_file), m_ctx, m_mainModuleIndex))) {
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
    CppTranspiler transpiler(m_ctx, &runner.analysis().symbols());
    transpiler.generateToFile(astNodes, outputIdx);
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
    saveCppAndEmbedSourceMap(m_ctx, outputIdx, cpptPath, m_opts.verbose, transpiler.exports(), /*embed_export_table=*/false);
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

    // -- 4. Compile mode --
    if (m_opts.should_compile()) {
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
        // --emit-build-dir: только генерируем build-каталог (единый переносимый build.conf),
        // БЕЗ компиляции/линковки. Используется trust-lsp для скачиваемого архива.
        if (m_opts.emit_build_dir_only) {
            if (!writeBuildFiles(m_opts, out.cpptPath, moduleCppts, runtime_headers, link_libs, entry_func_name)) {
                return 1;
            }
            return 0;
        }
        if (!compileAndLink(m_opts, out.cpptPath, moduleCppts, runtime_headers, link_libs, entry_func_name)) {
            return 1;
        }
        // --run: запустить собранный исполняемый файл (md5 исходника встроен в srcHash).
        if (m_opts.run && m_opts.compile_mode == CompileMode::Executable) {
            return runBuiltExecutable(m_opts, out.cpptPath);
        }
        return 0;
    }

    // -- 5. Emit: Cpp mode (transpile + stdout) --
    if ((m_opts.emit_flags & EmitFlags::Cpp) != EmitFlags::None) {
        auto out = runTranspileAndSave(inputFile);
        if (!out.valid) {
            return 1;
        }
        trust::outs() << m_ctx.source().output_result(out.outputIdx);
        return 0;
    }

    // -- 6. Other emit modes (Tokens, AST, Macros) --
    {
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
}

} // namespace trust