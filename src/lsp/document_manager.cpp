#include "lsp/document_manager.hpp"

#include "diag/mapper.hpp"
#include "utils/file_io.hpp"
#include "utils/uri.hpp"

#include <vector>

using namespace trust;

namespace trust {
namespace lsp {

namespace {

// LSP position → offset в строке (строки разделены '\n').
size_t positionToOffset(const std::string& text, int line, int character) {
    size_t pos = 0;
    for (int i = 0; i < line; ++i) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            break;
        }
        pos = nl + 1;
    }
    if (pos + static_cast<size_t>(character) > text.size()) {
        return text.size();
    }
    return pos + static_cast<size_t>(character);
}

} // namespace

void DocumentManager::didOpen(const nlohmann::json& req) {
    nlohmann::json params = req.value("params", nlohmann::json());
    std::string uri = params.value("textDocument", nlohmann::json()).value("uri", "");
    std::string filePath = trust::utils::uriToFilePath(uri);

    log("didOpen: " + filePath);

    if (!trust::SourceMapReader::isTrustFileExt(filePath)) {
        log("  skipped (not a trust source file)");
        return;
    }

    if (cppToTrustCache_.find(filePath) != cppToTrustCache_.end()) {
        log("  cpp file already cached via reverse cache");
        return;
    }

    pendingTranspile_.erase(filePath);

    std::string text = params.value("textDocument", nlohmann::json()).value("text", "");
    if (text.empty()) {
        log("  no buffer text in didOpen, reading from disk");
        auto code = trust::utils::FileIO::read<std::vector<char>>(filePath);
        if (code) {
            transpile_(filePath, std::string(code->data(), code->size()));
        } else {
            transpile_(filePath, "");
        }
        publish_(uri);
    } else {
        openDocuments_[filePath] = text;
        transpile_(filePath, text);
        publish_(uri);
    }

    log("didOpen completed for " + filePath);
}

void DocumentManager::didClose(const nlohmann::json& req) {
    nlohmann::json params = req.value("params", nlohmann::json());
    std::string uri = params.value("textDocument", nlohmann::json()).value("uri", "");
    std::string filePath = trust::utils::uriToFilePath(uri);

    // Не удаляем sourceCache_ или cppToTrustCache_ при didClose - это ломало
    // hover/definition/documentLink при переключении вкладок и для C++ файлов.
    // Кэш чистится только при didOpen (если хеш изменился) или при shutdown.
    log("didClose ignored (cache preserved)");
}

void DocumentManager::didChange(const nlohmann::json& req) {
    nlohmann::json params = req.value("params", nlohmann::json());
    std::string uri = params.value("textDocument", nlohmann::json()).value("uri", "");
    std::string filePath = trust::utils::uriToFilePath(uri);

    if (!trust::SourceMapReader::isTrustFileExt(filePath)) {
        log("  skipped (not a trust source file)");
        return;
    }

    std::string newText = applyContentChanges(filePath, params.value("contentChanges", nlohmann::json::array()));
    openDocuments_[filePath] = newText;

    pendingTranspile_[filePath] = std::chrono::steady_clock::now();
    log("didChange: buffer updated for " + filePath + " (transpile deferred)");
}

std::string DocumentManager::applyContentChanges(const std::string& filePath, const nlohmann::json& contentChanges) {
    auto it = openDocuments_.find(filePath);
    std::string text;
    if (it != openDocuments_.end()) {
        text = it->second;
    } else {
        auto code = trust::utils::FileIO::read<std::vector<char>>(filePath);
        if (code) {
            text.assign(code->data(), code->size());
        }
    }

    if (!contentChanges.is_array()) {
        return text;
    }

    for (const auto& ch : contentChanges) {
        if (ch.contains("range")) {
            const auto& range = ch["range"];
            size_t s =
                positionToOffset(text, range.value("start", nlohmann::json()).value("line", 0), range.value("start", nlohmann::json()).value("character", 0));
            size_t e =
                positionToOffset(text, range.value("end", nlohmann::json()).value("line", 0), range.value("end", nlohmann::json()).value("character", 0));
            if (e < s) {
                e = s;
            }
            text.replace(s, e - s, ch.value("text", ""));
        } else {
            text = ch.value("text", "");
        }
    }
    return text;
}

void DocumentManager::flushDocument(const std::string& filePath) {
    pendingTranspile_.erase(filePath);
    auto it = openDocuments_.find(filePath);
    if (it == openDocuments_.end()) {
        return;
    }

    std::string transpileErr = transpile_(filePath, it->second);
    if (!transpileErr.empty()) {
        log("transpilation failed for " + filePath + ": " + transpileErr + ", using previous cache");
    }
    publish_(trust::utils::filePathToUri(filePath));
}

void DocumentManager::flushPendingTranspile() {
    if (pendingTranspile_.empty()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::string> due;
    for (const auto& [path, t] : pendingTranspile_) {
        if (now - t >= std::chrono::milliseconds(kDebounceMs)) {
            due.push_back(path);
        }
    }
    for (const auto& path : due) {
        flushDocument(path);
    }
}

DocumentManager::CachedReader DocumentManager::getCachedReader(const std::string& filePath, std::string& outError) {
    bool isCpp = false;
    std::string trustFilePath;

    auto cppIt = cppToTrustCache_.find(filePath);
    if (cppIt != cppToTrustCache_.end()) {
        trustFilePath = cppIt->second;
        isCpp = true;
        log("  getCachedReader: reverse-cache HIT  cppPath=" + filePath + " -> trustPath=" + trustFilePath);
    } else if (trust::SourceMapReader::isCppFileExt(filePath)) {
        log("  getCachedReader: cpp file NOT in reverse-cache (miss): " + filePath);
        outError = "";
        return {nullptr, trust::ReaderFile{0}, trust::ReaderFile{0}, false};
    } else {
        trustFilePath = filePath;
        log("  getCachedReader: trust file request: " + filePath);
    }

    if (trust::SourceMapReader::isInMemoryName(trustFilePath)) {
        outError = "in-memory source (no file on disk): " + trustFilePath;
        return {nullptr, trust::ReaderFile{0}, trust::ReaderFile{0}, false};
    }

    if (pendingTranspile_.find(trustFilePath) != pendingTranspile_.end()) {
        flushDocument(trustFilePath);
    }

    auto it = sourceCache_.find(trustFilePath);
    if (it == sourceCache_.end()) {
        if (!isCpp && !trust::SourceMapReader::isCppFileExt(trustFilePath) && trustFilePath.find(".src") != std::string::npos) {
            std::string transpileErr;
            auto docIt = openDocuments_.find(trustFilePath);
            if (docIt != openDocuments_.end()) {
                transpileErr = transpile_(trustFilePath, docIt->second);
            } else {
                auto code = trust::utils::FileIO::read<std::vector<char>>(trustFilePath);
                transpileErr = code ? transpile_(trustFilePath, std::string(code->data(), code->size())) : transpile_(trustFilePath, "");
            }
            if (!transpileErr.empty()) {
                log("  auto-transpile on cache miss failed: " + transpileErr);
            }
            it = sourceCache_.find(trustFilePath);
        }
    }
    if (it == sourceCache_.end()) {
        outError = "File not cached: " + filePath;
        return {nullptr, trust::ReaderFile{0}, trust::ReaderFile{0}, false};
    }

    const trust::SourceMapReader* reader = it->second.sourceMap->source().toReader();
    if (!reader) {
        outError = "Failed to get SourceMapReader";
        return {nullptr, trust::ReaderFile{0}, trust::ReaderFile{0}, false};
    }

    CachedReader result;
    result.reader = reader;
    result.trustReaderIdx = it->second.trustReaderIdx;
    result.cppReaderIdx = it->second.cppReaderIdx;
    result.isCppRequest = isCpp;
    log("  getCachedReader: OK trustReaderIdx=" + std::to_string(it->second.trustReaderIdx.as_index()) +
        " cppReaderIdx=" + std::to_string(it->second.cppReaderIdx.as_index()) + " isCpp=" + (isCpp ? "1" : "0"));
    return result;
}

} // namespace lsp
} // namespace trust
