#include "lsp/services/analysis_service.hpp"
#include "lsp/catalog/lsp_utils.hpp"
#include "lsp/lsp_protocol.h"

#include "diag/diagnostic_location.hpp"
#include "sourcemap/protocol.hpp"
#include "session/context.hpp"
#include "pipeline/pipeline.hpp"
#include "driver/cli.hpp"
#include "driver/analysis_options.hpp"
#include "transpiler/transpiler.hpp"
#include "utils/utils.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <utility>

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

    // Семантическая модель (символы/видимость/ссылки) строится всегда — флаг не нужен.

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

    std::optional<trust::SemanticModel> semantics;
    {
        trust::Pipeline pipeline(*ctx, pipelineOpts);
        trust::PipelineResult result;
        try {
            result = pipeline.run(trust::RunRequest::withCpp(cppIdx), trustIdx);
        } catch (const std::exception& e) {
            log("pipeline exception: " + std::string(e.what()));
            ctx->diag().report(trust::Severity::Error, ctx->source().makeLoc(trustIdx, 1), "internal analysis error: {}", e.what());
        }

        // Клиентский TU включает заголовки импортируемых модулей (`#include "<enc>.hpp"`):
        // материализуем их рядом с `.cppt`, иначе clangd не найдёт файлы. Диагностики — в Pipeline.
        if (saveToDisk && result.isValid()) {
            pipeline.generateModuleOutputs(result.mainModuleIndex);
        }

        // Модель семантики — часть результата прогона (строится всегда при шаге Semantic).
        semantics = std::move(result.semantics);
    }

    trust::ReaderFile trustReaderIdx = trust::ReaderFile::from(trustIdx);
    trust::ReaderFile cppReaderIdx = trust::ReaderFile::from(cppIdx);

    if (saveToDisk) {
        try {
            const auto* reader = ctx->source().toReader();
            if (reader) {
                // Встроенные псевдо-файлы stdlib (dsl.src, iterator.src) сохраняются
                // в <каталог cppt>/stdlib/, чтобы ссылки на определения (LSP navigation) были
                // навигируемы. Путь читает hover_service.cpp.
                auto saveEmbedded = [&](std::string_view fileTag, std::string_view fileName) {
                    trust::ReaderFile idx = reader->findFile(std::string(fileTag));
                    if (idx.isInvalid()) {
                        return;
                    }
                    std::string_view source = reader->source(idx);
                    std::filesystem::path path = cpptPath.parent_path() / "stdlib" / std::string(fileName);
                    std::error_code ec;
                    std::filesystem::create_directories(path.parent_path(), ec);
                    std::ofstream ofs(path, std::ios::binary);
                    if (ofs) {
                        ofs.write(source.data(), static_cast<std::streamsize>(source.size()));
                        log("  saved " + std::string(fileName) + " to: " + std::filesystem::absolute(path).string());
                    }
                };
                saveEmbedded("@stdlib/dsl", "dsl.src");
                saveEmbedded("@stdlib/iterator", "iterator.src");
                saveEmbedded("@stdlib/async", "async.src");
            }
        } catch (const std::exception& e) {
            log("  dsl.src save failed: " + std::string(e.what()));
        }
    }

    if (saveToDisk) {
        if (trust::saveCppAndEmbedSourceMap(ctx->runServices(), cppIdx, cpptPath, opts_.trace)) {
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

    if (opts_.trace) {
        try {
            const auto* reader = ctx->source().toReader();
            if (reader) {
                log("  === mapping dump for " + trustFilePath + " ===");
                log("  trustReaderIdx=" + std::to_string(trustReaderIdx.as_index()) + " cppReaderIdx=" + std::to_string(cppReaderIdx.as_index()) +
                    " cppFilePath=" + cppFilePath);
                for (const auto& [key, entry] : reader->forwardMappings()) {
                    (void)key;
                    log("    forward: " + trust::lsp::formatRange(*reader, entry.from, trustFilePath) +
                        " [in=" + std::to_string(entry.from.begin.fileIdx().as_index()) + "@" + std::to_string(entry.from.begin.offset()) + "-" +
                        std::to_string(entry.from.end.offset()) + "]  ->  " + trust::lsp::formatRange(*reader, entry.to, cppFilePath) +
                        " [out=" + std::to_string(entry.to.begin.fileIdx().as_index()) + "@" + std::to_string(entry.to.begin.offset()) + "-" +
                        std::to_string(entry.to.end.offset()) + "]");
                }
                for (const auto& [key, entry] : reader->backwardMappings()) {
                    (void)key;
                    log("    backward: " + trust::lsp::formatRange(*reader, entry.from, cppFilePath) + "  ->  " +
                        trust::lsp::formatRange(*reader, entry.to, trustFilePath));
                }
                for (const auto& nm : reader->nameMappings()) {
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
    cs.model = std::move(semantics);
    documents_.sourceCache()[trustFilePath] = std::move(cs);

    if (!hasErrors) {
        documents_.cppToTrustCache()[cppFilePath] = trustFilePath;
        log("transpilation completed successfully");
    }

    return errMsg;
}

namespace {

/// 0-based диапазон LSP из ЕДИНОЙ декодированной локации (diag::decodeRange/diagnosticLocation).
/// Общий шейпинг для диагностики и её fixits: второй копии конверсии в файле нет.
json protocolDiagnosticRange(const trust::DiagnosticLocation& loc) {
    if (!loc.valid()) {
        return {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 0}}}};
    }
    return {{"start", {{"line", static_cast<int>(loc.begin.line) - 1}, {"character", static_cast<int>(loc.begin.column) - 1}}},
            {"end", {{"line", static_cast<int>(loc.end.line) - 1}, {"character", static_cast<int>(loc.end.column) - 1}}}};
}

} // namespace

void AnalysisService::publishDiagnostics(const std::string& uri) {
    std::string filePath = uriToFilePath(uri);
    // Публикация — по URI файла диапазона каждой записи (кросс-файловый Note не теряется).
    // Non-navigable источники (output, `@`-in-memory) пропускаются; записи без локации не публикуются.
    std::map<std::string, json> published;

    auto it = documents_.sourceCache().find(filePath);
    if (it != documents_.sourceCache().end()) {
        const auto& diag = it->second.sourceMap->diag();
        const auto* ctx = it->second.sourceMap.get();
        const auto& entries = diag.diagnostics();
        for (const auto& entry : entries) {
            if (entry.range.begin.isInvalid() || entry.range.begin.fileIdx().isOutput()) {
                continue;
            }
            std::string targetUri = uri;
            if (entry.range.begin.fileIdx().as_index() != it->second.trustReaderIdx.as_index()) {
                const std::string_view name = ctx->fileName(entry.range.begin);
                if (name.empty() || name.front() == '@') {
                    continue;
                }
                std::filesystem::path path(name);
                if (path.is_relative()) {
                    path = std::filesystem::path(std::string(ctx->source().baseDirectory())) / path;
                }
                targetUri = trust::utils::filePathToUri(path.string());
            }
            // Локация резолвится ЕДИНЫМ декодером (diagnosticLocation); LSP-специфика —
            // только 0-based {line-1, column-1} и единый severityToLsp.
            const trust::DiagnosticLocation loc = trust::diagnosticLocation(entry, ctx);
            const json range = protocolDiagnosticRange(loc);

            log("  diagnostic: " + targetUri + " range=" + range.dump() + " sev=" + std::to_string(static_cast<int>(entry.severity)) + " msg=" + entry.message);

            const int lspSeverity = trust::severityToLsp(entry.severity);

            json diagObj = {{"range", range}, {"severity", lspSeverity}, {"source", "trust-lsp"}, {"message", entry.message}};

            if (!entry.fixits.empty() && ctx) {
                json fixits = json::array();
                for (const auto& f : entry.fixits) {
                    const trust::DiagnosticLocation fl = trust::decodeRange(f.range, ctx);
                    fixits.push_back(json{{"range", protocolDiagnosticRange(fl)}, {"replacement", f.replacement}});
                }
                diagObj["data"] = json{{"fixits", std::move(fixits)}};
            }

            if (!published[targetUri].is_array()) {
                published[targetUri] = json::array();
            }
            published[targetUri].push_back(std::move(diagObj));
        }
    }
    if (!published[uri].is_array()) {
        published[uri] = json::array();
    }

    std::set<std::string> currentUris;
    for (const auto& [targetUri, diagnostics] : published) {
        (void)diagnostics;
        currentUris.insert(targetUri);
    }
    currentUris.insert(uri);
    for (const std::string& targetUri : currentUris) {
        if (targetUri != uri) {
            uriOwners_[targetUri] = uri;
        }
    }
    const auto previous = publishedUrisByDocument_.find(uri);
    if (previous != publishedUrisByDocument_.end()) {
        for (const std::string& staleUri : previous->second) {
            if (currentUris.contains(staleUri)) {
                continue;
            }
            const auto owner = uriOwners_.find(staleUri);
            if (owner == uriOwners_.end() || owner->second != uri) {
                continue;
            }
            json params = {{"uri", staleUri}, {"diagnostics", json::array()}};
            sendLspNotification(transport_, "textDocument/publishDiagnostics", params);
            log("cleared diagnostics for " + staleUri);
            uriOwners_.erase(owner);
        }
    }
    publishedUrisByDocument_[uri] = currentUris;

    // Запрошенный файл — последним: последнее уведомление описывает запрошенный документ.
    for (const auto& [targetUri, diagnostics] : published) {
        if (targetUri == uri) {
            continue;
        }
        json params = {{"uri", targetUri}, {"diagnostics", diagnostics}};
        sendLspNotification(transport_, "textDocument/publishDiagnostics", params);
        log("published " + std::to_string(diagnostics.size()) + " diagnostic(s) for " + targetUri);
    }
    {
        json params = {{"uri", uri}, {"diagnostics", published[uri]}};
        sendLspNotification(transport_, "textDocument/publishDiagnostics", params);
        log("published " + std::to_string(published[uri].size()) + " diagnostic(s) for " + uri);
    }
}

} // namespace lsp
} // namespace trust
