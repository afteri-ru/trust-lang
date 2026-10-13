// src/sourcemap/reader.cpp
// SourceMapReader/FileEntry: file I/O, FileEntry indexing and ELF loading.

#include "location/location.hpp"
#include "sourcemap/mapper.hpp"
#include "utils/utils.hpp"
#include "utils/error.hpp"

#include "llvm/Support/MD5.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>


namespace trust {

FileEntry::FileEntry(std::string filename, std::string source)
: m_filename(std::move(filename))
, m_source(std::move(source))
, m_hash(std::nullopt) {}

uint64_t FileEntry::getHash() const noexcept {
    if (!m_hash.has_value()) {
        m_hash = llvm::MD5Hash(std::string_view(m_source));
    }
    return *m_hash;
}

void FileEntry::setFilename(std::string f) {
    m_filename = std::move(f);
    EXPECT(m_source.empty());
    invalidateCache();
}

void FileEntry::appendSource(std::string_view text) {
    m_source.append(text);
    // Максимальный размер для входных файлов - более консервативная оценка
    EXPECT(m_source.size() <= LocationPack::MAX_OFFSET_INPUT);
    invalidateCache();
}

void FileEntry::setSource(std::string s) {
    m_source = std::move(s);
    // Максимальный размер для входных файлов - более консервативная оценка
    EXPECT(m_source.size() <= LocationPack::MAX_OFFSET_INPUT);
    invalidateCache();
}

void FileEntry::ensureCache() const {
    if (m_sparseCache.empty()) {
        m_sparseCache.build(m_source);
    }
}

// -- Вычисление строки/колонки по 1-based offset --
FileEntry::LineColumn FileEntry::calc_column(size_t offset) const {
    ensureCache();

    EXPECT(offset > 0);

    size_t target = offset - 1; // переводим в 0-based

    // Если кеш пуст (пустой файл) - всё в первой строке
    if (m_sparseCache.empty()) {
        return LineColumn{1, offset};
    }

    // Бинарный поиск по кешу
    const SparseCache::Entry* anchor = m_sparseCache.find_by_offset(target);
    if (!static_cast<bool>(anchor)) {
        return LineColumn{1, offset};
    }

    size_t startOff = anchor->offset;
    size_t line = anchor->line;
    size_t column = 1;

    // Линейный скан от startOff до target
    size_t fileSize = m_source.size();
    for (size_t i = startOff; i < target && i < fileSize; ++i) {
        const unsigned char c = static_cast<unsigned char>(m_source[i]);
        if (c == '\n') {
            ++line;
            column = 1;
        } else if ((c & 0xC0) != 0x80) {
            // Колонка считается в СИМВОЛАХ UTF-8: continuation-байты (0x80..0xBF)
            // многобайтового символа не инкрементируют колонку, иначе при кириллице
            // указатель диагностики съезжает вправо (байтовая vs символьная колонка).
            ++column;
        }
    }

    return {line, column};
}

// -- Вычисление offset (1-based) по номеру строки (1-based) --
size_t FileEntry::calc_line_offset(size_t line) const {
    ensureCache();

    // Если кеш пуст - пустой файл
    if (m_sparseCache.empty()) {
        return 1;
    }

    // Бинарный поиск по кешу
    const SparseCache::Entry* anchor = m_sparseCache.find_by_line(line);
    if (!static_cast<bool>(anchor)) {
        return 1;
    }

    size_t startOff = anchor->offset;
    size_t currentLine = anchor->line;
    size_t fileSize = m_source.size();

    // Сканируем от startOff до поиска нужной строки
    for (size_t i = startOff; i < fileSize; ++i) {
        if (currentLine == line) {
            return i + 1;
        }
        if (m_source[i] == '\n') {
            ++currentLine;
        }
    }

    // Если строка не найдена - возвращаем конец файла
    return fileSize + 1;
}

bool SourceMapReader::readFilesFromDisk(std::string_view baseDir) {
    namespace fs = std::filesystem;
    fs::path basePath;
    if (!baseDir.empty()) {
        basePath = fs::absolute(fs::path(baseDir));
    } else {
        std::error_code ec;
        basePath = fs::current_path(ec);
    }

    bool allOk = true;
    for (auto& entry : m_inputs) {
        // Фиктивные (in-memory) источники помечены префиксом '@' - файла на диске
        // нет, пытаться читать их не нужно (иначе ложно «файл не найден»).
        if (isInMemoryName(entry.getFilename())) {
            continue;
        }
        if (!readSingleFile(entry, basePath)) {
            allOk = false;
        }
    }

    for (auto& entry : m_outputs) {
        if (isInMemoryName(entry.getFilename())) {
            continue;
        }
        if (!readSingleFile(entry, basePath)) {
            allOk = false;
        }
    }

    return allOk;
}

bool SourceMapReader::verifyHash(ReaderFile idx) const {
    const auto& entry = get_file(idx);
    if (!entry.m_hash_original.has_value()) {
        return true;
    }
    return entry.getHash() == *entry.m_hash_original;
}

bool SourceMapReader::readSingleFile(FileEntry& entry, const std::filesystem::path& basePath) {
    const std::string& fname = entry.getFilename();
    if (fname.empty()) {
        entry.setSource("");
        return false;
    }

    namespace fs = std::filesystem;
    fs::path fullPath(fname);
    if (!fullPath.is_absolute()) {
        fullPath = basePath / fullPath;
    }

    auto content = utils::FileIO::read<std::vector<char>>(fullPath.generic_string());
    if (!content) {
        entry.setSource("");
        return false;
    }

    entry.setSource(std::string(content->data(), content->size()));
    return true;
}

bool SourceMapReader::readFileArray(msgpack_object array, std::deque<FileEntry>& files, bool filenames) {
    if (array.type != MSGPACK_OBJECT_ARRAY) {
        return false;
    }

    if (filenames) {
        for (uint32_t i = 0; i < array.via.array.size; ++i) {
            msgpack_object elem = array.via.array.ptr[i];
            if (elem.type != MSGPACK_OBJECT_STR) {
                return false;
            }
            files.emplace_back();
            files.back().setFilename(std::string(elem.via.str.ptr, elem.via.str.size));
        }
    } else {
        for (uint32_t i = 0; i < array.via.array.size; ++i) {
            msgpack_object h = array.via.array.ptr[i];
            if (h.type != MSGPACK_OBJECT_POSITIVE_INTEGER) {
                return false;
            }
            if (i < files.size()) {
                files[i].setHashOriginal(h.via.u64);
            }
        }
    }
    return true;
}

std::unique_ptr<SourceMapReader> SourceMapReader::fromElf(const std::string& elfPath) {
    auto sectionData = utils::readElfSection(elfPath, ".debug_trust_map");
    if (!sectionData) {
        return nullptr;
    }

    return fromMsgpack(sectionData->data(), sectionData->size());
}

} // namespace trust
