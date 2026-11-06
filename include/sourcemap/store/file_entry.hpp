#pragma once

// FileEntry: запись о входном/выходном файле (имя, содержимое, хэш, кеш line/column).

#include "location/location.hpp"
#include "utils/utils.hpp"
#include "utils/error.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace trust {

class FileEntry {
  public:
    struct LineColumn {
        size_t line{1};
        size_t column{1};
    };

    FileEntry(std::string filename, std::string source = "");
    FileEntry() = default;

    [[nodiscard]] const std::string& getFilename() const noexcept { return m_filename; }
    [[nodiscard]] const std::string& getSource() const noexcept { return m_source; }
    [[nodiscard]] LocationPack::RawType size() const {
        ASSERT(m_source.size() <= LocationPack::MAX_OFFSET_INPUT); // Should be checked when adding data
        return static_cast<LocationPack::RawType>(m_source.size());
    }
    [[nodiscard]] uint64_t getHash() const noexcept;

    void setFilename(std::string f);
    void setSource(std::string s);
    void appendSource(std::string_view text);
    void setHashOriginal(std::optional<uint64_t> h) noexcept { m_hash_original = h; }

  protected:
    // Вычислениея строк и колонок по смещению в файле и наоборот
    // Используется SparseCache + линейный поиск
    // Предназначены для исолпьзования только в классе SourceMap
    // у которого есть дополнительный LruCache с O(1) поиском

    template <typename F>
    friend struct SourceMap;
    friend class SourceMapReader;

    [[nodiscard]] LineColumn calc_column(size_t offset) const;

    [[nodiscard]] size_t calc_line_offset(size_t line) const;

    [[nodiscard]] size_t calc_line_count() const { return std::count(m_source.begin(), m_source.end(), '\n') + 1; }

  private:
    std::string m_filename;
    std::string m_source;
    mutable std::optional<uint64_t> m_hash;

    std::optional<uint64_t> m_hash_original{std::nullopt};
    mutable SparseCache m_sparseCache;

    void ensureCache() const;
    void invalidateCache() noexcept {
        m_sparseCache.invalidate();
        m_hash.reset();
    }
};

} // namespace trust
