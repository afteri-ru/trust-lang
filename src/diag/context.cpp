#include "diag/context.hpp"

#include <fstream>
#include <sstream>

namespace trust {

// При создании: DiagnosticEngine и Options связаны. diag получает указатель на Context
// для доступа к исходному коду при выводе location.
Context::Context() : m_diag(std::make_unique<DiagnosticEngine>()) {
    m_diag->setSourceManager(this);
    m_opts = Options(*m_diag);
#define OPT_REG(name, str, sev) m_opts->add_option(OptKind::name);
    OPTIONS_LIST(OPT_REG)
#undef OPT_REG
}

int Context::_to_int(SourceIdx idx) const {
    return idx.raw;
}

SourceIdx Context::add_source(std::string filename, std::string content) {
    int idx = static_cast<int>(m_files.size());
    m_files.push_back({std::move(filename), std::move(content)});
    return SourceIdx::make(idx);
}

// load_file читает весь файл в память. Для больших файлов — O(filesize) I/O.
SourceIdx Context::load_file(std::string path) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs)
        throw std::runtime_error("Cannot open file: " + path);
    std::ostringstream ss;
    ss << ifs.rdbuf();
    int idx = static_cast<int>(m_files.size());
    m_files.push_back({std::move(path), ss.str()});
    return SourceIdx::make(idx);
}

// Доступоры. m_opts инициализируется в конструкторе, поэтому безопасен.
DiagnosticEngine& Context::diag() { return *m_diag; }
Options& Context::opts() { return *m_opts; }
const DiagnosticEngine& Context::diag() const { return *m_diag; }
const Options& Context::opts() const { return *m_opts; }

std::string_view Context::filename(SourceIdx idx) const {
    if (!idx.isValid()) return {};
    int i = idx.raw;
    if (i < 0 || i >= static_cast<int>(m_files.size()))
        return {};
    return m_files[i].filename;
}

std::string_view Context::source(SourceIdx idx) const {
    if (!idx.isValid()) return {};
    int i = idx.raw;
    if (i < 0 || i >= static_cast<int>(m_files.size()))
        return {};
    return m_files[i].source;
}

std::string_view Context::filename(SourceLoc loc) const {
    if (!loc.isValid()) return {};
    return filename(loc.source_idx());
}

std::string_view Context::source(SourceLoc loc) const {
    if (!loc.isValid()) return {};
    return source(loc.source_idx());
}

// loc_from_line — линейный поиск начала строки. Для N строк работает за O(content_size).
SourceLoc Context::loc_from_line(SourceIdx idx, int line) const {
    if (!idx.isValid()) return SourceLoc::invalid();

    int i = idx.raw;
    if (i < 0 || i >= static_cast<int>(m_files.size()))
        return SourceLoc::invalid();

    const auto& src = m_files[i].source;
    int current_line = 1;
    for (int off = 0; off < static_cast<int>(src.size()); ++off) {
        if (current_line == line) {
            return SourceLoc::make(idx, off + 1);
        }
        if (src[off] == '\n') {
            ++current_line;
        }
    }
    // Если строка больше последней — возвращаем позицию в конце
    return SourceLoc::make(idx, static_cast<int>(src.size()) + 1);
}

Context::LineColumn Context::line_column(SourceLoc loc) const {
    // Поиск в кеше
    for (int i = 0; i < CACHE_SIZE; ++i) {
        if (m_cache_loc[i].packed == loc.packed)
            return m_cache_lc[i];
    }

    if (!loc.isValid()) return LineColumn{1, 1};

    int src_idx = loc.source_idx().raw;
    if (src_idx < 0 || src_idx >= static_cast<int>(m_files.size()))
        return LineColumn{1, 1};

    const auto& src = m_files[src_idx].source;
    const int target = loc.offset();

    int line = 1;
    int column = 1;
    for (int i = 0; i < target - 1 && i < static_cast<int>(src.size()); ++i) {
        if (src[i] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }

    // Сохраняем в кеш (циклический буфер)
    int idx = m_cache_next;
    m_cache_loc[idx] = loc;
    m_cache_lc[idx] = LineColumn{line, column};
    m_cache_next = (idx + 1) % CACHE_SIZE;

    return LineColumn{line, column};
}

} // namespace trust