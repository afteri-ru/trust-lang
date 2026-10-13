#pragma once

// include/sourcemap/serialize/reader.hpp
// SourceMapReader: read-only-сторона (reader space) + msgpack-сериализация.

#include "sourcemap/store/file_entry.hpp"
#include "sourcemap/serialize/msgpack_util.hpp"
#include "sourcemap/store/source_map.hpp"

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

// ══════════════════════════════════════════════════════════════
//  SourceMapReader: read-only (reader space)
// ══════════════════════════════════════════════════════════════

class SourceMapReader : public SourceMap<ReaderFile> {
  public:
    SourceMapReader() = default;

    // -- Factory methods --
    static std::unique_ptr<SourceMapReader> fromMsgpack(const unsigned char* data, size_t size);
    /// Читает embedded source map из ELF-секции .debug_trust_map
    /// Возвращает nullptr, если ELF невалиден, секция не найдена или данные повреждены.
    static std::unique_ptr<SourceMapReader> fromElf(const std::string& elfPath);

    // -- Чтение файлов с диска после десериализации --
    static bool readFileArray(msgpack_object array, std::deque<FileEntry>& files, bool filenames);

    bool readFilesFromDisk(std::string_view baseDir);

    // Проверяет хеш файла с заданным индексом.
    [[nodiscard]] bool verifyHash(ReaderFile idx) const;

    // -- Поиск диапазона по позиции --
    std::optional<Range> getMapTrustToCpp(Location trustLoc) const;
    std::optional<Range> getMapCppToTrust(Location cppLoc) const;

    // -- Поиск всех диапазонов по строке/колонке --
    std::vector<Range> findRangesByLine(ReaderFile idx, uint32_t line, std::optional<uint32_t> column = std::nullopt) const;

    // -- Поиск имени по позиции --
    std::optional<NameMap> getCppName(Location trustLoc, std::string_view trustName) const;
    std::optional<NameMap> getTrustName(Location cppLoc, std::string_view cppName) const;

    // -- Итерация маппингов по файлу --
    std::vector<RangeMap> getTrustFileMappings(ReaderFile trustFileIdx) const;

    // -- Доступ к nameMappings --
    [[nodiscard]] const std::vector<NameMap>& getNameMappings() const { return m_nameMappings; }

    // -- Доступ к маппингам --
    [[nodiscard]] const std::map<uint32_t, RangeMap>& getForwardMappings() const { return m_forward; }
    [[nodiscard]] const std::map<uint32_t, RangeMap>& getBackwardMappings() const { return m_backward; }

    // -- Поиск макросов --
    std::optional<Range> getMacroDefRange(Location bodyLoc) const;

    // -- Извлечение слова под курсором --
    [[nodiscard]] std::optional<std::string> getWordAt(Location loc) const;

    // -- LSP convenience --
    [[nodiscard]] Location lspToLocation(ReaderFile idx, int line, int character) const;

    // -- Фрагмент URL --
    [[nodiscard]] std::string rangeToFragmentString(Range range) const;

    // -- Поиск полного RangeMap по позиции --
    [[nodiscard]] std::optional<RangeMap> findRangeMap(Location loc) const;

    // ══════════════════════════════════════════════════════════════
    //  High-level convenience методы
    // ══════════════════════════════════════════════════════════════

    [[nodiscard]] static bool isTrustFileExt(const std::string& path) noexcept;
    [[nodiscard]] static bool isCppFileExt(const std::string& path) noexcept;
    /// True, если имя source-map файла помечено как фиктивный (in-memory) источник
    /// префиксом '@' - такого файла нет на диске, искать его не нужно.
    [[nodiscard]] static bool isInMemoryName(std::string_view name) noexcept;
    [[nodiscard]] ReaderFile findFile(const std::string& path) const;
    [[nodiscard]] std::optional<Range> findTrustToCpp(const std::string& trustPath, int line) const;
    [[nodiscard]] std::optional<Range> findCppToTrust(const std::string& cppPath, int line) const;
    [[nodiscard]] std::optional<std::pair<std::string, int>> calcCppToTrustLine(const std::string& cppPath, int cppLine) const;

    // -- Сериализация (msgpack) --
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

// -- SourceMapReader template methods --

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
