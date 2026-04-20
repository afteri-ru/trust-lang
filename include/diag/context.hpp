#pragma once

#include <format>
#include <memory>
#include <string>
#include <vector>

#include "diag/diag.hpp"
#include "diag/location.hpp"
#include "diag/options.hpp"

namespace trust {

// Context — фасад, объединяющий Source Manager, DiagnosticEngine и Options.
// Владееет диагностикой (unique_ptr) и опциями (optional).
class Context {
public:
    Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    struct FileEntry {
        std::string filename;
        std::string source;
    };

    // Добавляет содержимое источника из памяти / Читает файл с диска.
    SourceIdx add_source(std::string filename, std::string content);
    SourceIdx load_file(std::string path);

    // Доступ к компонентам (const и non-const перегрузки).
    DiagnosticEngine& diag();
    Options& opts();
    const DiagnosticEngine& diag() const;
    const Options& opts() const;

    // Информация об источнике (через SourceIdx / SourceLoc).
    std::string_view filename(SourceIdx idx) const;
    std::string_view source(SourceIdx idx) const;
    int file_count() const { return static_cast<int>(m_files.size()); }

    std::string_view filename(SourceLoc loc) const;
    std::string_view source(SourceLoc loc) const;

    // loc_from_line — O(N) поиск начала строки по номеру.
    SourceLoc loc_from_line(SourceIdx idx, int line) const;

    // line_column — O(N) конвертация offset → line:column с кэшем последнего запроса.
    struct LineColumn {
        int line{1};
        int column{1};
    };
    LineColumn line_column(SourceLoc loc) const;

    int line(SourceLoc loc) const { return line_column(loc).line; }
    int column(SourceLoc loc) const { return line_column(loc).column; }

    // report — convenience-метод: берёт severity из Options, вызывает DiagnosticEngine::report.
    // Если severity == nullopt, сообщение не генерируется.
    template <typename... Args>
    void report(SourceRange range, OptKind kind,
                std::format_string<Args...> fmt, Args&&... args) {
        auto sev = opts().severity(kind);
        if (!sev.has_value()) return;
        diag().report(range, *sev, std::move(fmt), std::forward<Args>(args)...);
    }

private:
    int _to_int(SourceIdx idx) const;

    std::vector<FileEntry> m_files;
    std::unique_ptr<DiagnosticEngine> m_diag;
    std::optional<Options> m_opts;

    // Кеш на CACHE_SIZE элементов для line_column().
    static constexpr int CACHE_SIZE = 4;
    mutable SourceLoc m_cache_loc[CACHE_SIZE];
    mutable LineColumn m_cache_lc[CACHE_SIZE];
    mutable int m_cache_next = 0;
};

} // namespace trust