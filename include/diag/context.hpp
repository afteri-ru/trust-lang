#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "diag/diag.hpp"
#include "diag/location.hpp"
#include "diag/mapper.hpp"
#include "diag/options.hpp"
#include "trust/version.h"

namespace trust {

// ── OutputBuffer: буфер prepend-данных выходного C++ файла ──
// Содержит только prefix, который будет добавлен к source при финализации.
struct OutputBuffer {
    std::string prefix; // prepend-данные (вставки в начало)

    void prepend(std::string_view text);
};

// Context — фасад, объединяющий Source Manager, DiagnosticEngine, SourceMapping и Options.
// Владеет диагностикой (unique_ptr) и опциями (optional).
// Поддерживает входные (trust) и выходные (cpp) файлы, маппинг между ними.
// Наследует Mapper<FileIdx> — методы маппинга (addRangeMapping, StmtBegin, StmtEnd и др.)
// доступны напрямую на Context.
class Context : public SourceMap<MapperFile> {
  public:
    using TranspileResult = std::expected<std::pair<MapperFile, MapperFile>, std::string>;

    Context();
    explicit Context(std::string_view basePath, std::string_view tempPath = "");

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // ── Входные файлы ──

    // Добавляет содержимое источника из памяти.
    // Если normalize == true: path нормализуется (приводится к относительному от baseDir).
    // Если normalize == false: filename проверяется — должен содержать только [a-zA-Z0-9_].
    // Возвращает FileIdx или {0} при ошибке.
    [[nodiscard]] MapperFile add_source(std::string filename, std::string content, bool normalize = true);

    // Читает файл с диска. path нормализуется.
    [[nodiscard]] MapperFile load_file(std::string path);

    uint32_t file_count() const { return m_inputs.size(); }

    // ── Выходные файлы ──

    // add_output: регистрирует выходной файл.
    // Если normalize == true: filename нормализуется.
    // Если normalize == false: filename проверяется — только [a-zA-Z0-9_].
    // Возвращает FileIdx (с установленным битом FILE_BITS для выходных).
    [[nodiscard]] MapperFile add_output(std::string filename, bool normalize = true);
    bool output_append(MapperFile idx, std::string_view text);
    bool output_prepend(MapperFile idx, std::string_view text);
    // filename(FileIdx idx) используется единообразно для входных и выходных файлов.
    [[nodiscard]] std::string output_result(MapperFile idx) const;
    [[nodiscard]] std::string_view output_body(MapperFile idx) const;
    // Сохраняет все выходные файлы в указанный каталог.
    // Создаёт иерархию поддиректорий из относительных имён (filename не меняется).
    // Возвращает true, если все файлы сохранены успешно.
    [[nodiscard]] bool save_output(std::string_view outputDir);
    uint32_t output_count() const { return m_outputs.size(); }

    // ── Создание и валидация Location / Range ──

    // Создание Range с проверками:
    //   - Оба Location принадлежат одному файлу
    //   - end >= begin
    // При ошибке выбрасывает FAULT.
    [[nodiscard]] MapperRange makeRange(MapperLocation begin, MapperLocation end) const;

    // ── Методы маппинга (SourceMapWriter) ──
    // Добавление маппингов
    bool addRangeMapping(MapperRange trustRange, MapperRange cppRange);
    bool addNameMapping(MapperRange trustRange, MapperRange cppRange, std::string_view trustName, std::string_view cppName);
    bool addMacroMapping(MapperRange bodyRange, MapperRange defRange);

    // ── Стек для mapStart/mapStop ──

    struct MapStartEntry {
        MapperRange inputRange;
        MapperLocation outputBegin; // Location: сохраняет и индекс файла, и позицию
    };

    // Сохраняет текущую позицию выходного файла MapperFile в стеке
    // и возвращает диапазон from_begin..from_end для входного файла в качестве ключа для mapStop()
    // Позволяет делать произвольное количество output_append в MapperFile между mapStart и mapStop
    MapperRange mapStart(MapperFile from, uint32_t from_begin, uint32_t from_end, MapperFile to);

    // Overload: принимает готовый MapperRange (from) и MapperFile (to).
    // Сохраняет текущую позицию to в стеке, возвращает from как ключ для mapStop().
    MapperRange mapStart(MapperRange from, MapperFile to);

    // Завершает отображение Range в текущей позиции выходного файла MapperFile.
    // Создаёт RangeMap (через mapRange).
    // Возвращает Range отображения в выходном файле MapperFile to из метода mapStart
    MapperRange mapStop(MapperRange from);

    // Возвращает ссылку на верхний элемент стека mapStart/mapStop (без извлечения).
    // Требует непустого стека, иначе FAULT.
    const MapStartEntry& mapStackTop() const;

    // Создание Reader (финализирует выходные файлы и кеширует результат).
    // Кеш сбрасывается (m_reader.reset()) при любом изменении входных/выходных
    // файлов или маппингов — см. add_source, load_file, add_output, addRangeMapping, и др.
    const SourceMapReader* toReader() const;

    // ── Доступ к компонентам (const и non-const перегрузки) ──
    DiagnosticEngine& diag();
    Options& opts();
    const DiagnosticEngine& diag() const;
    const Options& opts() const;

    // ── Поиск FileIdx по пути с нормализацией ──
    // Нормализует путь через baseDirectory и ищет среди входных файлов.
    // Возвращает FileIdx{0} если не найден.
    MapperFile findFileIdx(std::string_view filePath) const;

    // ── Информация об источнике (через Location) ──
    // filename/source/loc_from_line/line_column унаследованы из Mapper
    // Явно импортируем перегрузки FileIdx из Mapper, т.к. иначе они скрываются
    using SourceMap::filename;
    using SourceMap::source;
    [[nodiscard]] std::string_view filename(MapperLocation loc) const;
    [[nodiscard]] std::string_view source(MapperLocation loc) const;

    // report — convenience-метод: берёт severity из Options, вызывает DiagnosticEngine::report.
    template <typename... Args>
    void report(MapperRange range, OptKind kind, std::format_string<Args...> fmt, Args&&... args) {
        auto sev = opts().severity(kind);
        if (!sev.has_value())
            return;
        diag().report(range, *sev, std::move(fmt), std::forward<Args>(args)...);
    }

  private:
    static bool validateSimpleName(std::string_view name);
    std::string normalizePath(std::string_view path) const;

    // Вспомогательная: возвращает ссылку на prepend-буфер для выходного файла
    std::string& get_prepend(MapperFile idx);
    const std::string& get_prepend(MapperFile idx) const;

    // Получает размер данных для валидации offset'а выходного файла:
    //   если source не пуст — его размер (финализирован),
    //   иначе — размер source (аккумулируемый body в процессе построения).
    // Оба подхода дают корректный размер для offset — source всегда хранит body.
    uint32_t get_output_size(MapperFile idx) const;

    // m_inputs, m_outputs унаследованы из Mapper
    std::unordered_map<uint32_t, OutputBuffer> m_outputBuffers; // prepend-буферы, ключ = FileIdx.raw
    mutable std::unique_ptr<SourceMapReader> m_reader;
    std::unique_ptr<DiagnosticEngine> m_diag;
    std::optional<Options> m_opts;
    std::string m_baseDirectory;
    std::string m_tempDirectory;

    std::vector<MapStartEntry> m_mapStack;
};

} // namespace trust