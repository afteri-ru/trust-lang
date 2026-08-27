#include "lsp/navigation_service.hpp"

#include "lsp/lsp_utils.hpp"
#include "lsp/lsp_protocol.h"
#include "utils/uri.hpp"

using json = nlohmann::json;
using trust::utils::uriToFilePath;

namespace trust {
namespace lsp {
namespace navigation {

void handleDefinition(trust::transport::Transport& transport, DocumentManager& documents, LspOptions& opts, const json& req) {
    json id = req.value("id", json());

    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");
    json position = params.value("position", json());
    int line = position.value("line", 0);
    int character = position.value("character", 0);

    std::string filePath = uriToFilePath(uri);
    lspLog(opts, "handleDefinition: " + filePath + " line=" + std::to_string(line) + " col=" + std::to_string(character));

    std::string err;
    auto cr = documents.getCachedReader(filePath, err);
    if (!cr.reader) {
        if (!err.empty()) {
            lspLog(opts, "  " + err);
        }
        sendLspResponse(transport, id, json());
        return;
    }

    const auto& reader = *cr.reader;
    trust::ReaderFile queryIdx = cr.isCppRequest ? cr.cppReaderIdx : cr.trustReaderIdx;
    trust::SourceMapReader::Location loc = reader.lspToLocation(queryIdx, line, character);

    auto maybeMap = reader.findRangeMap(loc);
    if (!maybeMap.has_value()) {
        lspLog(opts, "  no mapping found for definition");
        sendLspResponse(transport, id, json());
        return;
    }

    const auto& rangeMap = *maybeMap;
    // Для definition нужна целевая сторона (куда перейти)
    const auto& targetRange = rangeMap.to;

    // Определяем целевой файл
    trust::ReaderFile targetFile = targetRange.begin.fileIdx();
    std::string targetPath;
    if (targetFile.isOutput()) {
        targetPath = documents.sourceCache().at(cr.isCppRequest ? documents.cppToTrustCache().at(filePath) : filePath).cppFilePath;
    } else {
        targetPath = cr.isCppRequest ? documents.cppToTrustCache().at(filePath) : filePath;
    }

    std::string targetUri = makeFragmentUri(reader, targetPath, targetRange);

    json lspLocation = {{"uri", targetUri}, {"range", rangeToLspRange(reader, targetRange)}};
    sendLspResponse(transport, id, lspLocation);
    lspLog(opts, "  definition: from " + formatRange(reader, rangeMap.from, filePath) + "  ->  " + formatRange(reader, rangeMap.to, targetPath));
}

void handleDocumentLink(trust::transport::Transport& transport, DocumentManager& documents, LspOptions& opts, const json& req) {
    json id = req.value("id", json());

    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");

    std::string filePath = uriToFilePath(uri);
    lspLog(opts, "handleDocumentLink: " + filePath);

    std::string err;
    auto cr = documents.getCachedReader(filePath, err);
    if (!cr.reader) {
        if (!err.empty()) {
            lspLog(opts, "  " + err);
        }
        sendLspResponse(transport, id, json::array());
        return;
    }

    const auto& reader = *cr.reader;

    std::string trustFilePath = cr.isCppRequest ? documents.cppToTrustCache().at(filePath) : filePath;
    const CachedSource& cs = documents.sourceCache().at(trustFilePath);

    json links = json::array();

    if (cr.isCppRequest) {
        // -- C++ → Trust: link на trust-файл по cpp-диапазону --
        auto backward = reader.getBackwardMappings();
        lspLog(opts,
               "  [cpp→trust] backward-mappings total=" + std::to_string(backward.size()) + " cppReaderIdx=" + std::to_string(cr.cppReaderIdx.as_index()));
        for (const auto& [key, entry] : backward) {
            (void)key;
            const auto& cppRange = entry.from;
            if (cppRange.begin.fileIdx() != cr.cppReaderIdx) {
                continue;
            }
            const auto& trustRange = entry.to;
            std::string targetUri = makeFragmentUri(reader, trustFilePath, trustRange);
            json link = {{"range", rangeToLspRange(reader, cppRange)}, {"target", targetUri}};
            links.push_back(std::move(link));
            lspLog(opts, "    cpp link: " + formatRange(reader, cppRange, filePath) + "  ->  " + formatRange(reader, trustRange, trustFilePath));
        }

        for (const auto& nameMap : reader.getNameMappings()) {
            const auto& toRange = nameMap.rangeMap.to;
            if (toRange.begin.fileIdx() != cr.cppReaderIdx) {
                continue;
            }
            std::string targetUri = makeFragmentUri(reader, trustFilePath, nameMap.rangeMap.from);
            json link = {{"range", rangeToLspRange(reader, toRange)}, {"target", targetUri}};
            links.push_back(std::move(link));
            lspLog(opts, "    cpp name-link: " + formatRange(reader, toRange, filePath) + "  ->  " + formatRange(reader, nameMap.rangeMap.from, trustFilePath));
        }
    } else {
        // -- Trust → C++: link на C++ файл --
        auto mappings = reader.getTrustFileMappings(cr.trustReaderIdx);
        lspLog(opts,
               "  [trust→cpp] forward-mappings total=" + std::to_string(mappings.size()) + " trustReaderIdx=" + std::to_string(cr.trustReaderIdx.as_index()));
        for (const auto& mapping : mappings) {
            const auto& trustRange = mapping.from;
            const auto& cppRange = mapping.to;
            if (cppRange.begin.fileIdx().isOutput()) {
                std::string targetUri = makeFragmentUri(reader, cs.cppFilePath, cppRange);
                json link = {{"range", rangeToLspRange(reader, trustRange)}, {"target", targetUri}};
                links.push_back(std::move(link));
                lspLog(opts, "    trust link: " + formatRange(reader, trustRange, filePath) + "  ->  " + formatRange(reader, cppRange, cs.cppFilePath));
            }
        }

        for (const auto& nameMap : reader.getNameMappings()) {
            const auto& fromRange = nameMap.rangeMap.from;
            if (fromRange.begin.fileIdx() != cr.trustReaderIdx) {
                continue;
            }
            std::string targetUri = makeFragmentUri(reader, cs.cppFilePath, nameMap.rangeMap.to);
            json link = {{"range", rangeToLspRange(reader, fromRange)}, {"target", targetUri}};
            links.push_back(std::move(link));
            lspLog(opts,
                   "    trust name-link: " + formatRange(reader, fromRange, filePath) + "  ->  " + formatRange(reader, nameMap.rangeMap.to, cs.cppFilePath));
        }
    }

    // Убираем ссылки, чей диапазон - строгое надмножество другого диапазона
    if (links.size() > 1) {
        auto posLE = [](const json& a, const json& b) -> bool {
            return a["line"].get<int>() < b["line"].get<int>() ||
                   (a["line"].get<int>() == b["line"].get<int>() && a["character"].get<int>() <= b["character"].get<int>());
        };
        auto contains = [&](const json& outer, const json& inner) -> bool {
            return posLE(outer["start"], inner["start"]) && posLE(inner["end"], outer["end"]);
        };
        json filtered = json::array();
        for (const auto& link : links) {
            bool isSuperset = false;
            for (const auto& other : links) {
                if (&link == &other) {
                    continue;
                }
                if (contains(link["range"], other["range"]) && !contains(other["range"], link["range"])) {
                    isSuperset = true;
                    break;
                }
            }
            if (!isSuperset) {
                filtered.push_back(link);
            }
        }
        links = std::move(filtered);
    }

    json result = links;
    sendLspResponse(transport, id, result);
    lspLog(opts, "  generated " + std::to_string(links.size()) + " document link(s)");
}

} // namespace navigation
} // namespace lsp
} // namespace trust
