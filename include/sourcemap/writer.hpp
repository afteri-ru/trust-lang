#pragma once

// include/sourcemap/writer.hpp
// SourceMapWriter: mutable-сторона (builder space): файлы, выходные буферы, маппинги.

#include "sourcemap/store/file_entry.hpp"
#include "sourcemap/store/output_buffer.hpp"
#include "sourcemap/store/source_map.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trust {

class SourceMapReader;

// ══════════════════════════════════════════════════════════════
//  SourceMapWriter: mutable (writer space) - интеграция всего
//  функционала из бывшего Context, связанного с SourceMap.
// ══════════════════════════════════════════════════════════════

class SourceMapWriter : public SourceMap<MapperFile> {
  public:
    SourceMapWriter();
    explicit SourceMapWriter(std::string_view basePath, std::string_view tempPath = "");

    SourceMapWriter(const SourceMapWriter&) = delete;
    SourceMapWriter& operator=(const SourceMapWriter&) = delete;

    /// Определён в writer_to_reader.cpp: владеет неполным SourceMapReader (unique_ptr).
    ~SourceMapWriter();

    // -- Входные файлы --

    // Добавляет содержимое источника из памяти.
    // Если normalize == true: path нормализуется (приводится к относительному от baseDir).
    // Если normalize == false: filename проверяется - должен содержать только [a-zA-Z0-9_].
    // Возвращает MapperFile или {0} при ошибке.
    [[nodiscard]] MapperFile add_source(std::string filename, std::string content, bool normalize = true);

    // Загружает файл с диска.
    [[nodiscard]] MapperFile load_file(std::string path);

    uint32_t file_count() const { return static_cast<uint32_t>(m_inputs.size()); }

    // -- Выходные файлы --

    // add_output: регистрирует выходной файл.
    [[nodiscard]] MapperFile add_output(std::string filename, bool normalize = true);
    bool output_append(MapperFile idx, std::string_view text);
    /// Удаляет последний временный output (используется для рендера выражения в строку).
    bool output_drop(MapperFile idx);
    bool output_prepend(MapperFile idx, std::string_view text, std::string_view ns = "");
    bool output_prepend_leading(MapperFile idx, std::string_view text);
    [[nodiscard]] std::string output_result(MapperFile idx) const;
    [[nodiscard]] std::string_view output_body(MapperFile idx) const;
    [[nodiscard]] bool save_output(std::string_view outputDir);
    uint32_t output_count() const { return static_cast<uint32_t>(m_outputs.size()); }

    // -- Создание и валидация Location / Range --
    [[nodiscard]] MapperRange makeRange(MapperLocation begin, MapperLocation end) const;
    [[nodiscard]] bool isValid(MapperLocation loc) const;
    [[nodiscard]] bool isValid(MapperRange range) const;

    // -- Методы маппинга (SourceMapWriter) --
    bool addRangeMapping(MapperRange trustRange, MapperRange cppRange);
    bool addNameMapping(MapperRange trustRange, MapperRange cppRange, std::string_view trustName, std::string_view cppName);
    bool addMacroMapping(MapperRange bodyRange, MapperRange defRange);

    // -- Стек для mapStart/mapStop --
    struct MapStartEntry {
        MapperRange inputRange;
        MapperLocation outputBegin;
    };

    MapperRange mapStart(MapperFile from, uint32_t from_begin, uint32_t from_end, MapperFile to);
    MapperRange mapStart(MapperRange from, MapperFile to);
    MapperRange mapStop(MapperRange from);
    const MapStartEntry& mapStackTop() const;

    // -- toReader --
    const SourceMapReader* toReader() const;

    // -- Информация об источнике --
    [[nodiscard]] std::string_view filename(MapperLocation loc) const;
    [[nodiscard]] std::string_view source(MapperLocation loc) const;
    using SourceMap::column;
    using SourceMap::filename;
    using SourceMap::findFileIdx;
    using SourceMap::get_file;
    using SourceMap::getFileHash;
    using SourceMap::getText;
    using SourceMap::line;
    using SourceMap::line_column;
    using SourceMap::lineCount;
    using SourceMap::loc_from_line;
    using SourceMap::makeLoc;
    using SourceMap::source;

    // -- Поиск FileIdx по пути с нормализацией --
    MapperFile findFileIdx(std::string_view filePath) const;

    void setBaseDirectory(std::string_view path);
    [[nodiscard]] const std::string& baseDirectory() const noexcept { return m_baseDirectory; }

    // -- Главный (корневой) файл модуля --
    /// Устанавливает путь главного файла программы/модуля. Имя модуля любого файла
    /// (см. moduleName) вычисляется относительно каталога главного файла.
    void setMainModuleFile(MapperFile mainFile);
    /// Имя модуля по FileIdx: путь текущего файла относительно каталога главного файла,
    /// без расширения; разделители каталога ('/' и '\\') заменяются на '_'.
    /// Если главный файл не задан - используется baseDirectory.
    [[nodiscard]] std::string moduleName(MapperFile idx) const;

    // -- Подавление маппинга (для синтетических узлов, напр. forward-decl на сайте импорта) --
    // Пока счётчик > 0, addRangeMapping/addNameMapping/mapStart/mapStop/addMacroMapping - no-op.
    void suppressMapping() { ++m_mappingSuppressed; }
    void resumeMapping() {
        EXPECT(m_mappingSuppressed > 0);
        --m_mappingSuppressed;
    }
    [[nodiscard]] bool mappingSuppressed() const noexcept { return m_mappingSuppressed > 0; }
    /// True, если mapStart уже выполнен (стек маппинга не пуст) - т.е. можно безопасно
    /// вызывать mapDeclaredName (иначе mapStackTop FAULT). Используется для опционального
    /// маппинга синтетических узлов без исходного range.
    [[nodiscard]] bool mappingActive() const noexcept { return !m_mapStack.empty(); }

  private:
    static bool validateSimpleName(std::string_view name);
    std::string normalizePath(std::string_view path) const;

    std::string get_prepend(MapperFile idx, unsigned indentSize = 4) const;
    uint32_t get_output_size(MapperFile idx) const;

    // Буферы prepend-данных, ключ = FileIdx.raw
    std::unordered_map<uint32_t, OutputBuffer> m_outputBuffers;
    mutable std::unique_ptr<SourceMapReader> m_reader;

    std::string m_baseDirectory;
    std::string m_tempDirectory;
    MapperFile m_mainModuleFile{}; ///< FileIdx главного файла программы (см. moduleName).

    std::vector<MapStartEntry> m_mapStack;
    int m_mappingSuppressed = 0;
};

} // namespace trust
