// module_sabi.cpp - реализация семантического ABI модуля (ELF-секция .trust.sabi).
#include "module_loader/module_sabi.hpp"

#include "module_loader/module_export.hpp"
#include "trust/version.h"
#include "utils/elf.hpp"
#include "utils/error.hpp"
#include "utils/zstd_compress.hpp"

#include <cstddef>
#include <cstring>

namespace trust {

namespace {

// -- Формат секции .trust.sabi --
//   magic[4]="TSAB" | u32 formatVersion | u32 flags(bit0=zstd) | u32 count
//   | u32 metaSize | u32 declsSize
//   body: meta (compilerVersion '\0' srcRecord '\0') | decls block (count NUL-разделённых строк)
constexpr char kSabiMagic[4] = {'T', 'S', 'A', 'B'};
constexpr uint32_t kSabiFormatVersion = 1;
constexpr uint32_t kSabiFlagCompressed = 1u << 0;
constexpr std::size_t kSabiHeaderSize = 4 + 5 * sizeof(uint32_t);

void appendU32(std::vector<unsigned char>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xffu));
    }
}

uint32_t readU32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/// Разбить блок NUL-разделённых строк; завершающий '\0' не создаёт пустую запись.
std::vector<std::string> splitNulBlock(std::span<const unsigned char> block) {
    std::vector<std::string> out;
    const char* data = reinterpret_cast<const char*>(block.data());
    std::size_t start = 0;
    for (std::size_t i = 0; i < block.size(); ++i) {
        if (data[i] == '\0') {
            out.emplace_back(data + start, i - start);
            start = i + 1;
        }
    }
    if (start < block.size()) {
        out.emplace_back(data + start, block.size() - start);
    }
    return out;
}

/// major.minor версии компилятора (для проверки совместимости). "1.2.3-x" -> "1.2".
std::string majorMinor(std::string_view version) {
    const std::size_t first = version.find('.');
    if (first == std::string_view::npos) {
        return std::string(version);
    }
    const std::size_t second = version.find('.', first + 1);
    if (second == std::string_view::npos) {
        return std::string(version);
    }
    return std::string(version.substr(0, second));
}

} // namespace

std::vector<unsigned char> buildSabiSection(const std::vector<std::string>& decls, std::string_view compilerVersion, std::string_view srcRecord,
                                            bool compress) {
    // Блок деклараций: каждая строка завершается '\0'.
    std::vector<unsigned char> body;
    for (const auto& d : decls) {
        body.insert(body.end(), d.begin(), d.end());
        body.push_back('\0');
    }
    uint32_t flags = 0;
    std::vector<unsigned char> stored = body;
    // Пустой блок не сжимаем (zstd_compress неотличим от ошибки на пустом входе).
    if (compress && !body.empty()) {
        std::vector<unsigned char> packed = detail::zstd_compress(body.data(), body.size());
        if (packed.empty()) {
            FAULT("sabi: zstd compression of the declarations block failed");
        }
        stored = std::move(packed);
        flags |= kSabiFlagCompressed;
    }

    std::vector<unsigned char> meta;
    meta.insert(meta.end(), compilerVersion.begin(), compilerVersion.end());
    meta.push_back('\0');
    meta.insert(meta.end(), srcRecord.begin(), srcRecord.end());
    meta.push_back('\0');

    std::vector<unsigned char> out;
    out.reserve(kSabiHeaderSize + meta.size() + stored.size());
    out.insert(out.end(), kSabiMagic, kSabiMagic + 4);
    appendU32(out, kSabiFormatVersion);
    appendU32(out, flags);
    appendU32(out, static_cast<uint32_t>(decls.size()));
    appendU32(out, static_cast<uint32_t>(meta.size()));
    appendU32(out, static_cast<uint32_t>(stored.size()));
    out.insert(out.end(), meta.begin(), meta.end());
    out.insert(out.end(), stored.begin(), stored.end());
    return out;
}

std::optional<SabiData> parseSabiSection(std::span<const unsigned char> bytes, std::string& error) {
    if (bytes.size() < kSabiHeaderSize) {
        error = "section is truncated (header)";
        return std::nullopt;
    }
    if (std::memcmp(bytes.data(), kSabiMagic, 4) != 0) {
        error = "bad magic (not a .trust.sabi section)";
        return std::nullopt;
    }
    const unsigned char* p = bytes.data();
    const uint32_t formatVersion = readU32(p + 4);
    const uint32_t flags = readU32(p + 8);
    const uint32_t count = readU32(p + 12);
    const uint32_t metaSize = readU32(p + 16);
    const uint32_t declsSize = readU32(p + 20);
    if (formatVersion != kSabiFormatVersion) {
        error = "unsupported format version " + std::to_string(formatVersion) + " (expected " + std::to_string(kSabiFormatVersion) + ")";
        return std::nullopt;
    }
    if ((flags & ~kSabiFlagCompressed) != 0) {
        error = "unknown flags 0x" + std::to_string(flags);
        return std::nullopt;
    }
    const std::size_t bodyOff = kSabiHeaderSize;
    if (bodyOff > bytes.size() || metaSize > bytes.size() - bodyOff || declsSize > bytes.size() - bodyOff - metaSize) {
        error = "section is truncated (body)";
        return std::nullopt;
    }

    SabiData data;
    {
        std::span<const unsigned char> meta(bytes.data() + bodyOff, metaSize);
        const std::vector<std::string> fields = splitNulBlock(meta);
        if (fields.size() != 2 || fields[0].empty() || fields[1].empty()) {
            error = "section has malformed metadata (compiler version / source record)";
            return std::nullopt;
        }
        data.compilerVersion = fields[0];
        data.srcRecord = fields[1];
    }

    std::span<const unsigned char> declBlock(bytes.data() + bodyOff + metaSize, declsSize);
    std::vector<unsigned char> decoded(declBlock.begin(), declBlock.end());
    if ((flags & kSabiFlagCompressed) != 0) {
        decoded = detail::zstd_decompress(declBlock.data(), declBlock.size());
        if (decoded.empty() && declsSize != 0) {
            error = "failed to decompress declarations block";
            return std::nullopt;
        }
    }
    data.decls = splitNulBlock(decoded);
    if (data.decls.size() != count) {
        error = "declaration count mismatch (" + std::to_string(data.decls.size()) + " != " + std::to_string(count) + ")";
        return std::nullopt;
    }
    return data;
}

std::optional<SabiData> readModuleSabi(const std::string& path, std::string& error) {
    std::optional<std::vector<unsigned char>> section = utils::readElfSection(path, ".trust.sabi");
    if (!section) {
        error = "'.trust.sabi' section not found in '" + path + "'";
        return std::nullopt;
    }
    std::optional<SabiData> data = parseSabiSection(*section, error);
    if (!data) {
        error = "invalid '.trust.sabi' section in '" + path + "': " + error;
        return std::nullopt;
    }
    // Совместимость версии компилятора: major.minor должны совпадать.
    if (majorMinor(data->compilerVersion) != majorMinor(TRUST_VERSION_SHORT)) {
        error = "module built by incompatible compiler " + data->compilerVersion + " (expected " TRUST_VERSION_SHORT ")";
        return std::nullopt;
    }
    return data;
}

std::string_view declName(std::string_view declText) {
    if (!declText.empty() && declText.front() == '`') {
        const std::size_t close = declText.find('`', 1);
        return (close == std::string_view::npos) ? declText : declText.substr(0, close + 1);
    }
    if (!declText.empty() && declText.front() == '%') {
        declText.remove_prefix(1);
    }
    const std::size_t end = declText.find_first_of("(: =");
    return (end == std::string_view::npos) ? declText : declText.substr(0, end);
}

std::vector<std::string> filterModuleDecls(const std::vector<std::string>& decls, std::string_view commaMasks) {
    std::vector<std::string> out;
    for (const auto& d : decls) {
        if (matchesAnyMask(commaMasks, declName(d))) {
            out.push_back(d);
        }
    }
    return out;
}

} // namespace trust
