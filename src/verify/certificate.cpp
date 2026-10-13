// src/verify/certificate.cpp
// Детерминированный JSON-сертификат `*.trustcert` (без внешних зависимостей: порядок полей
// задан кодом, экранирование — по JSON-спеке).
#include "verify/certificate.hpp"

#include <format>
#include <string>

namespace trust {
namespace verify {

namespace {

/// Экранирование строки в JSON (\" \\ и управляющие символы).
std::string jsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
}

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
    out += std::format("  \"subject\": \"{}\",\n", jsonEscape(subjectHash));
    out += std::format("  \"solver\": {{\"name\": \"{}\", \"version\": \"{}\"}},\n", jsonEscape(tool), jsonEscape(toolVersion));
    out += "  \"obligations\": [";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const CertificateEntry& entry = entries[i];
        out += i == 0 ? "\n" : ",\n";
        out += "    {";
        out += std::format("\"kind\": \"{}\", ", obligationKindId(entry.obligation.kind));
        out += std::format("\"site\": \"{}\", ", jsonEscape(siteText(entry.obligation.site)));
        out += std::format("\"status\": \"{}\"", proofStatusId(entry.status));
        if (entry.obligation.envelope) {
            out += std::format(", \"envelope\": \"{}\"", jsonEscape(*entry.obligation.envelope));
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
