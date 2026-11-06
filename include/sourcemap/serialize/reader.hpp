#pragma once

// SourceMapReader: read-only-сторона (reader space) + msgpack-сериализация.

#include "sourcemap/store/file_entry.hpp"
#include "sourcemap/serialize/msgpack_util.hpp"
#include "sourcemap/store/source_map.hpp"
#include "trust/version.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {

/// Совместимость формата карты исходников: major обязан совпадать, minor — не выше текущего.
/// Несовместимый формат — ошибка читателя, а не тихо неверная навигация.
[[nodiscard]] constexpr bool sourceMapFormatCompatible(uint32_t major, uint32_t minor) noexcept {
    return major == static_cast<uint32_t>(TRUST_VERSION_MAJOR) && minor <= static_cast<uint32_t>(TRUST_VERSION_MINOR);
}

class SourceMapReader : public SourceMap<ReaderFile> {
  public:
    SourceMapReader() = default;

    static std::unique_ptr<SourceMapReader> fromMsgpack(const unsigned char* data, size_t size);
    /// Читает embedded source map из ELF-секции .debug_trust_map
    /// Возвращает nullptr, если ELF невалиден, секция не найдена или данные повреждены.
    static std::unique_ptr<SourceMapReader> fromElf(const std::string& elfPath);

    static bool readFileArray(msgpack_object array, std::deque<FileEntry>& files, bool filenames);

    bool readFilesFromDisk(std::string_view baseDir);

    // Проверяет хеш файла с заданным индексом.
    [[nodiscard]] bool verifyHash(ReaderFile idx) const;

    std::optional<Range> mappedOutRange(Location trustLoc) const;
    std::optional<Range> mappedTrustRange(Location cppLoc) const;

    std::vector<Range> findRangesByLine(ReaderFile idx, uint32_t line, std::optional<uint32_t> column = std::nullopt) const;

    std::optional<NameMap> outNameOf(Location trustLoc, std::string_view trustName) const;
    std::optional<NameMap> trustNameOf(Location outLoc, std::string_view outName) const;

    std::vector<RangeMap> trustFileMappings(ReaderFile trustFileIdx) const;

    [[nodiscard]] const std::vector<NameMap>& nameMappings() const { return m_nameMappings; }

    [[nodiscard]] const std::map<uint32_t, RangeMap>& forwardMappings() const { return m_forward; }
    [[nodiscard]] const std::map<uint32_t, RangeMap>& backwardMappings() const { return m_backward; }

    std::optional<Range> macroDefinitionAt(Location useLoc) const;

    [[nodiscard]] std::optional<std::string> wordAt(Location loc) const;

    [[nodiscard]] Location lspToLocation(ReaderFile idx, int line, int character) const;

    [[nodiscard]] std::string rangeToFragmentString(Range range) const;

    [[nodiscard]] std::optional<RangeMap> findRangeMap(Location loc) const;

    [[nodiscard]] static bool isTrustFileExt(const std::string& path) noexcept;
    [[nodiscard]] static bool isCppFileExt(const std::string& path) noexcept;
    /// True, если имя source-map файла помечено как фиктивный (in-memory) источник
    /// префиксом '@' - такого файла нет на диске, искать его не нужно.
    [[nodiscard]] static bool isInMemoryName(std::string_view name) noexcept;
    [[nodiscard]] ReaderFile findFile(const std::string& path) const;
    [[nodiscard]] std::optional<Range> findTrustToCpp(const std::string& trustPath, int line) const;
    [[nodiscard]] std::optional<Range> findCppToTrust(const std::string& cppPath, int line) const;
    [[nodiscard]] std::optional<std::pair<std::string, int>> calcCppToTrustLine(const std::string& cppPath, int cppLine) const;

    void packRanges(MsgpackWriter& wr, const std::map<uint32_t, RangeMap>& forward) const;
    void packNames(MsgpackWriter& wr, const std::vector<NameMap>& nameMappings) const;
    void packMacros(MsgpackWriter& wr) const;

    [[nodiscard]] std::vector<unsigned char> packToMsgpack() const;

    void setFiles(std::deque<FileEntry> inputs, std::deque<FileEntry> outputs) {
        m_inputs = std::move(inputs);
        m_outputs = std::move(outputs);
    }

  private:
    bool unpackRanges(msgpack_object rangesArray);
    bool unpackNames(msgpack_object namesArray);
    bool unpackMacros(msgpack_object macrosArray);

    static bool readSingleFile(FileEntry& entry, const std::filesystem::path& basePath);

    static std::optional<Range> findRange(const std::map<uint32_t, RangeMap>& ranges, Location loc);

    template <typename NameMatcher>
    static std::optional<NameMap> findNameInMappings(const std::vector<NameMap>& nameMappings, uint32_t locPacked, NameMatcher&& nameMatcher,
                                                     Range RangeMap::* rangeMember);
};

template <typename NameMatcher>
std::optional<SourceMapReader::NameMap> SourceMapReader::findNameInMappings(const std::vector<NameMap>& nameMappings, uint32_t locPacked,
                                                                            NameMatcher&& nameMatcher, Range RangeMap::* rangeMember) {
    for (const auto& v : nameMappings) {
        const Range& r = v.rangeMap.*rangeMember;
        if (nameMatcher(v) && locPacked >= r.begin.packed && locPacked <= r.end.packed) {
            return v;
        }
    }
    return std::nullopt;
}

} // namespace trust
