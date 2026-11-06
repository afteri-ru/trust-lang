// Детерминированный JSON-сертификат `*.trustcert` (без внешних зависимостей: порядок полей
// задан кодом, экранирование — по JSON-спеке).
#include "verify/certificate.hpp"

#include "utils/strings.hpp" // utils::escapeJson — единая реализация экранирования

#include <format>
#include <string>

namespace trust {
namespace verify {

namespace {

std::string siteText(const MapperRange& range) {
    if (range.begin.isInvalid()) {
        return "<unknown>";
    }
    return std::format("file{}:{}", range.begin.fileIdx().as_index(), range.begin.offset());
}

} // namespace

std::string composeCertificate(std::string_view subjectHash, std::string_view tool, std::string_view toolVersion,
                               const std::vector<CertificateEntry>& entries) {
    std::string out;
    out += "{\n";
    out += std::format("  \"format\": \"{}\",\n", kCertificateFormat);
    out += std::format("  \"subject\": \"{}\",\n", utils::escapeJson(subjectHash));
    out += std::format("  \"solver\": {{\"name\": \"{}\", \"version\": \"{}\"}},\n", utils::escapeJson(tool), utils::escapeJson(toolVersion));
    out += "  \"obligations\": [";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const CertificateEntry& entry = entries[i];
        out += i == 0 ? "\n" : ",\n";
        out += "    {";
        out += std::format("\"kind\": \"{}\", ", obligationKindId(entry.obligation.kind));
        out += std::format("\"site\": \"{}\", ", utils::escapeJson(siteText(entry.obligation.site)));
        out += std::format("\"status\": \"{}\"", proofStatusId(entry.status));
        if (entry.obligation.envelope) {
            out += std::format(", \"envelope\": \"{}\"", utils::escapeJson(*entry.obligation.envelope));
        }
        if (entry.status == ProofStatus::Failed) {
            out += ", \"violated\": true";
        }
        out += "}";
    }
    out += entries.empty() ? "]\n" : "\n  ]\n";
    out += "}\n";
    return out;
}

} // namespace verify
} // namespace trust
