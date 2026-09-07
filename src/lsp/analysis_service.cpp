#include "lsp/analysis_service.hpp"

#include "lsp/lsp_utils.hpp"
#include "lsp/lsp_protocol.h"

#include "diag/protocol.hpp"
#include "semantic/diag.hpp"
#include "pipeline/pipeline.hpp"
#include "pipeline/cli.hpp"
#include "pipeline/analysis_options.hpp"
#include "transpiler/transpiler.hpp"
#include "utils/file_io.hpp"
#include "utils/uri.hpp"
#include "utils/utils.hpp"
#include "utils/io.hpp"

#include <filesystem>
#include <fstream>
#include <memory>

using json = nlohmann::json;
using trust::utils::resolvePath;
using trust::utils::uriToFilePath;

namespace trust {
namespace lsp {
namespace analysis {

trust::PipelineOpts lspOptsToPipelineOpts(const LspOptions& lspOpts) {
    trust::PipelineOpts opts{};
    opts.verbose = lspOpts.trace;
    opts.quiet = false;
    opts.temp_dir = lspOpts.tempDir;
    // LSP: анализатор должен работать на частичном AST даже при ошибках лексера/парсера
    // (для сбора имён/типов и автодополнения). Транспиляция при ошибках не выполняется.
    opts.allow_semantic_on_errors = true;
    return opts;
}

} // namespace analysis

AnalysisService::AnalysisService(trust::transport::Transport& transport, LspOptions& opts, DocumentManager& documents)
: transport_(transport)
, opts_(opts)
, documents_(documents) {
}

void AnalysisService::log(const std::string& msg) const {
    lspLog(opts_, msg);
}

std::string AnalysisService::transpileSourceFile(const std::string& trustFilePath) {
    log("transpiling (in-process): " + trustFilePath);

    auto trustCodeOpt = trust::utils::FileIO::read<std::vector<char>>(trustFilePath);
    if (!trustCodeOpt) {
        std::string err = "cannot open file: " + trustFilePath;
        log(err);
        return err;
    }
    std::string trustCodeStr(trustCodeOpt->data(), trustCodeOpt->size());
    return transpileSource(trustFilePath, trustCodeStr);
}

std::string AnalysisService::transpileSource(const std::string& trustFilePath, const std::string& trustCodeStr) {
    log("transpiling (in-process): " + trustFilePath);
    std::string_view trustCode = trustCodeStr;

    auto ctx = std::make_unique<trust::Context>(opts_.projectDir.empty() ? "." : opts_.projectDir);

    std::string shebangOptionError;
    {
        const std::vector<std::string> shebang = trust::lsp::extractShebangOptions(trustCodeStr);
        trust::lsp::applyAnalysisArgsBySource(ctx->opts(), opts_.pipelineArgs, shebang, opts_.shebangMode, [&](const std::string& msg, bool fromShebang) {
            if (fromShebang) {
                shebangOptionError = msg;
            } else {
                log("warning: invalid environment analysis options: " + msg);
            }
        });
    }

    ctx->opts().set_enabled(trust::semantic::FlagKind::Symbols, true);

    auto pipelineOpts = trust::lsp::analysis::lspOptsToPipelineOpts(opts_);
    pipelineOpts.input_file = trustFilePath;

    std::filesystem::path cpptPath = trust::computeCpptPath(pipelineOpts);
    bool saveToDisk = !opts_.tempDir.empty();

    if (saveToDisk) {
        std::error_code ec;
        std::filesystem::create_directories(cpptPath.parent_path(), ec);
        if (ec) {
            log("failed to create temp dir " + cpptPath.parent_path().string() + ": " + ec.message());
        }
    }

    trust::MapperFile trustIdx = ctx->source().add_source(trustFilePath, std::string(trustCode));
    trust::MapperFile cppIdx = ctx->source().add_output(cpptPath.filename().string());

    if (!shebangOptionError.empty()) {
        ctx->diag().report(trust::Severity::Error, ctx->source().makeLoc(trustIdx, 1), "invalid analysis options in shebang: {}", shebangOptionError);
    }

    trust::SymbolIndex symbols;
    std::unique_ptr<trust::TypeRegistry> registry;
    {
        trust::Pipeline pipeline(*ctx, pipelineOpts);
        trust::PipelineResult result;
        try {
            result = pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trustIdx, cppIdx);
        } catch (const std::exception& e) {
            log("pipeline exception: " + std::string(e.what()));
            ctx->diag().report(trust::Severity::Error, ctx->source().makeLoc(trustIdx, 1), "internal analysis error: {}", e.what());
        }

        registry = pipeline.releaseTypes();

        if (result.symbols) {
            symbols = std::move(*result.symbols);
        } else {
            trust::appendMacroSymbols(*ctx, symbols);
        }
    }

    trust::ReaderFile trustReaderIdx = trust::ReaderFile::from(trustIdx);
    trust::ReaderFile cppReaderIdx = trust::ReaderFile::from(cppIdx);

    if (saveToDisk) {
        try {
            const auto* reader = ctx->source().toReader();
            if (reader) {
                trust::ReaderFile dslIdx = reader->findFile("@trust/dsl");
                if (!dslIdx.isInvalid()) {
                    std::string_view dslSource = reader->source(dslIdx);
                    std::filesystem::path dslPath = cpptPath.parent_path() / "trust" / "dsl.src";
                    std::error_code ec;
                    std::filesystem::create_directories(dslPath.parent_path(), ec);
                    std::ofstream ofs(dslPath, std::ios::binary);
                    if (ofs) {
                        ofs.write(dslSource.data(), static_cast<std::streamsize>(dslSource.size()));
                        log("  saved dsl.src to: " + std::filesystem::absolute(dslPath).string());
                    }
                }
            }
        } catch (const std::exception& e) {
            log("  dsl.src save failed: " + std::string(e.what()));
        }
    }

    if (saveToDisk) {
        if (trust::saveCppAndEmbedSourceMap(*ctx, cppIdx, cpptPath, opts_.trace)) {
            log("  saved cpp to: " + cpptPath.string());
        } else {
            log("warning: could not write to " + cpptPath.string());
        }
    }

    std::string cppFilePath;
    if (saveToDisk) {
        cppFilePath = std::filesystem::absolute(cpptPath).string();
    } else {
        cppFilePath = std::filesystem::absolute(resolvePath(cpptPath.filename().string(), opts_.projectDir)).string();
    }

    bool hasErrors = ctx->diag().errorCount() > 0;
    std::string errMsg;
    if (hasErrors) {
        errMsg = "transpilation completed with " + std::to_string(ctx->diag().errorCount()) + " error(s)";
        log(errMsg);
    }

    // -- Трассировка: дамп всех маппингов из source map (до перемещения ctx в кеш) --
    if (opts_.trace) {
        try {
            const auto* reader = ctx->source().toReader();
            if (reader) {
                log("  === mapping dump for " + trustFilePath + " ===");
                log("  trustReaderIdx=" + std::to_string(trustReaderIdx.as_index()) + " cppReaderIdx=" + std::to_string(cppReaderIdx.as_index()) +
                    " cppFilePath=" + cppFilePath);
                for (const auto& [key, entry] : reader->getForwardMappings()) {
                    (void)key;
                    log("    forward: " + trust::lsp::formatRange(*reader, entry.from, trustFilePath) +
                        " [in=" + std::to_string(entry.from.begin.fileIdx().as_index()) + "@" + std::to_string(entry.from.begin.offset()) + "-" +
                        std::to_string(entry.from.end.offset()) + "]  ->  " + trust::lsp::formatRange(*reader, entry.to, cppFilePath) +
                        " [out=" + std::to_string(entry.to.begin.fileIdx().as_index()) + "@" + std::to_string(entry.to.begin.offset()) + "-" +
                        std::to_string(entry.to.end.offset()) + "]");
                }
                for (const auto& [key, entry] : reader->getBackwardMappings()) {
                    (void)key;
                    log("    backward: " + trust::lsp::formatRange(*reader, entry.from, cppFilePath) + "  ->  " +
                        trust::lsp::formatRange(*reader, entry.to, trustFilePath));
                }
                for (const auto& nm : reader->getNameMappings()) {
                    log("    name(" + nm.fromName + " -> " + nm.toName + "): " + trust::lsp::formatRange(*reader, nm.rangeMap.from, trustFilePath) +
                        " [in=" + std::to_string(nm.rangeMap.from.begin.fileIdx().as_index()) + "@" + std::to_string(nm.rangeMap.from.begin.offset()) + "-" +
                        std::to_string(nm.rangeMap.from.end.offset()) + "]  ->  " + trust::lsp::formatRange(*reader, nm.rangeMap.to, cppFilePath) +
                        " [out=" + std::to_string(nm.rangeMap.to.begin.fileIdx().as_index()) + "@" + std::to_string(nm.rangeMap.to.begin.offset()) + "-" +
                        std::to_string(nm.rangeMap.to.end.offset()) + "]");
                }
                log("  === end mapping dump ===");
            }
        } catch (const std::exception& e) {
            log("  mapping dump failed: " + std::string(e.what()));
        }
    }

    // Кешируем результат (даже при ошибках диагностики - для частичного source map)
    CachedSource cs;
    cs.sourceMap = std::move(ctx);
    cs.cppFilePath = cppFilePath;
    cs.trustReaderIdx = trustReaderIdx;
    cs.cppReaderIdx = cppReaderIdx;
    cs.symbols = std::move(symbols);
    cs.types = std::move(registry);
    documents_.sourceCache()[trustFilePath] = std::move(cs);

    if (!hasErrors) {
        documents_.cppToTrustCache()[cppFilePath] = trustFilePath;
        log("transpilation completed successfully");
    }

    return errMsg;
}

void AnalysisService::publishDiagnostics(const std::string& uri) {
    std::string filePath = uriToFilePath(uri);
    json diagnostics = json::array();

    auto it = documents_.sourceCache().find(filePath);
    if (it != documents_.sourceCache().end()) {
        const auto& diag = it->second.sourceMap->diag();
        const auto* ctx = it->second.sourceMap.get();
        const auto& entries = diag.diagnostics();
        for (const auto& entry : entries) {
            // Публикуем только диагностики ТЕКУЩЕГО trust-файла.
            if (entry.range.begin.isInvalid() || entry.range.begin.fileIdx().isOutput() ||
                entry.range.begin.fileIdx().as_index() != it->second.trustReaderIdx.as_index()) {
                continue;
            }
            json range;
            if (ctx) {
                const auto pr = trust::mapperRangeToProtocol(ctx->source(), entry.range);
                range = {{"start", {{"line", pr.start.line}, {"character", pr.start.character}}},
                         {"end", {{"line", pr.end.line}, {"character", pr.end.character}}}};
            } else {
                range = {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}};
            }

            log("  diagnostic: " + filePath + " range=" + range.dump() + " sev=" + std::to_string(static_cast<int>(entry.severity)) + " msg=" + entry.message);

            const int lspSeverity = trust::severityToLsp(entry.severity);

            json diagObj = {{"range", range}, {"severity", lspSeverity}, {"source", "trust-lsp"}, {"message", entry.message}};

            if (!entry.fixits.empty() && ctx) {
                json fixits = json::array();
                for (const auto& f : entry.fixits) {
                    const auto pr = trust::mapperRangeToProtocol(ctx->source(), f.range);
                    fixits.push_back({{"range",
                                       {{"start", {{"line", pr.start.line}, {"character", pr.start.character}}},
                                        {"end", {{"line", pr.end.line}, {"character", pr.end.character}}}}},
                                      {"replacement", f.replacement}});
                }
                diagObj["data"] = json{{"fixits", std::move(fixits)}};
            }

            diagnostics.push_back(std::move(diagObj));
        }
    }

    json params = {{"uri", uri}, {"diagnostics", diagnostics}};
    sendLspNotification(transport_, "textDocument/publishDiagnostics", params);
    log("published " + std::to_string(diagnostics.size()) + " diagnostic(s) for " + uri);
}

} // namespace lsp
} // namespace trust
