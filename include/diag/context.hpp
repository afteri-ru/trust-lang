#pragma once

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "diag/diag.hpp"
#include "diag/location.hpp"
#include "diag/mapping.hpp"
#include "diag/options.hpp"

namespace trust {

// ── OutputBuffer: буфер выходного C++ файла ──
struct OutputBuffer {
    std::string prefix;  // prepend-данные (вставки в начало)
    std::string body;    // последовательно добавляемые данные

    void append(std::string_view text);
    void prepend(std::string_view text);
    std::string result() const;  // prefix + body
};

// Context — фасад, объединяющий Source Manager, DiagnosticEngine, SourceMapping и Options.
// Владееет диагностикой (unique_ptr) и опциями (optional).
// Поддерживает входные (trust) и выходные (cpp) файлы, маппинг между ними.
class Context {
public:
    Context();
    explicit Context(std::string_view basePath, std::string_view tempPath = "");

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    struct FileEntry {
        std::string filename;
        std::string source;
    };

    // ── Входные файлы ──

    // Добавляет содержимое источника из памяти.
    // Если normalize == true: path нормализуется (приводится к относительному от baseDir).
    // Если normalize == false: filename проверяется — должен содержать только [a-zA-Z0-9_].
    // Возвращает FileIdx или {0} при ошибке.
    [[nodiscard]] FileIdx add_source(std::string filename, std::string content, bool normalize = true);

    // Читает файл с диска. path нормализуется.
    [[nodiscard]] FileIdx load_file(std::string path);

    [[nodiscard]] std::string_view filename(FileIdx idx) const;
    [[nodiscard]] std::string_view source(FileIdx idx) const;

    int file_count() const { return static_cast<int>(m_files.size()); }

    // ── Выходные файлы ──

    // add_output: регистрирует выходной файл.
    // Если normalize == true: filename нормализуется.
    // Если normalize == false: filename проверяется — только [a-zA-Z0-9_].
    // Возвращает FileIdx (с установленным битом OUTPUT_FLAG).
    FileIdx add_output(std::string filename, bool normalize = true);
    bool output_append(FileIdx idx, std::string_view text);
    bool output_prepend(FileIdx idx, std::string_view text);
    // filename(FileIdx idx) используется единообразно для входных и выходных файлов.
    std::string output_result(FileIdx idx) const;
    int output_count() const { return static_cast<int>(m_outputs.size()); }

    // ── Создание и валидация SourceLoc / SourceRange ──

    // Валидация SourceLoc — проверяет, что позиция корректна:
    //   - SourceLoc не является invalid
    //   - FileIdx соответствует существующему файлу (входному или выходному)
    //   - offset не превышает размер данных файла
    // При ошибке выбрасывает FAULT с описанием причины.
    void validateLoc(SourceLoc loc) const;

    // Создание SourceLoc с проверками:
    //   - FileIdx не равен 0
    //   - offset в пределах размера данных файла
    //   - (флаг output определяется по старшему биту FileIdx)
    // При ошибке выбрасывает FAULT.
    SourceLoc makeLoc(FileIdx idx, int offset) const;

    // Создание SourceRange с проверками:
    //   - Оба SourceLoc проходят validateLoc
    //   - Оба SourceLoc принадлежат одному файлу
    //   - end.offset() >= begin.offset()
    // При ошибке выбрасывает FAULT.
    SourceRange makeRange(SourceLoc begin, SourceLoc end) const;

    // ── Маппинг (делегирован SourceMapping) ──
    bool addRangeMapping(SourceRange trustRange, SourceRange cppRange);
    bool addNameMapping(SourceRange trustRange, SourceRange cppRange,
                        std::string_view trustName, std::string_view cppName);
    std::optional<SourceRange> mapTrustToCpp(SourceLoc trustLoc) const;
    std::optional<SourceRange> mapCppToTrust(SourceLoc cppLoc) const;
    std::optional<NameRangeInfo> getCppName(SourceLoc trustLoc, std::string_view trustName) const;
    std::optional<NameRangeInfo> getTrustName(SourceLoc cppLoc, std::string_view cppName) const;

    // ── Сериализация маппинга ──
    [[nodiscard]] std::vector<unsigned char> packMapping() const;
    [[nodiscard]] bool unpackMapping(const unsigned char* data, size_t size);

    // ── Доступ к компонентам (const и non-const перегрузки) ──
    DiagnosticEngine& diag();
    Options& opts();
    const DiagnosticEngine& diag() const;
    const Options& opts() const;

    // ── Информация об источнике (через SourceLoc) ──
    [[nodiscard]] std::string_view filename(SourceLoc loc) const;
    [[nodiscard]] std::string_view source(SourceLoc loc) const;

    // loc_from_line — O(N) поиск начала строки по номеру.
    SourceLoc loc_from_line(FileIdx idx, int line) const;

    // line_column — O(N) конвертация offset → line:column с кэшем последнего запроса.
    struct LineColumn {
        int line{1};
        int column{1};
    };
    LineColumn line_column(SourceLoc loc) const;

    int line(SourceLoc loc) const { return line_column(loc).line; }
    int column(SourceLoc loc) const { return line_column(loc).column; }

    // report — convenience-метод: берёт severity из Options, вызывает DiagnosticEngine::report.
    template <typename... Args>
    void report(SourceRange range, OptKind kind,
                std::format_string<Args...> fmt, Args&&... args) {
        auto sev = opts().severity(kind);
        if (!sev.has_value()) return;
        diag().report(range, *sev, std::move(fmt), std::forward<Args>(args)...);
    }

private:
    struct OutputFile {
        std::string filename;
        OutputBuffer buffer;
    };

    static bool validateSimpleName(std::string_view name);
    std::string normalizePath(std::string_view path) const;
    int fileIdxToArrayIndex(FileIdx idx, bool& isOutput) const;

    std::vector<FileEntry> m_files;
    std::vector<OutputFile> m_outputs;
    std::unique_ptr<SourceMapping> m_mapping;
    std::unique_ptr<DiagnosticEngine> m_diag;
    std::optional<Options> m_opts;
    std::string m_baseDirectory;
    std::string m_tempDirectory;

    // Кеш на CACHE_SIZE элементов для line_column().
    static constexpr int CACHE_SIZE = 4;
    mutable SourceLoc m_cache_loc[CACHE_SIZE];
    mutable LineColumn m_cache_lc[CACHE_SIZE];
    mutable int m_cache_next = 0;
};

} // namespace trust