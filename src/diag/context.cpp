#include "diag/context.hpp"
#include "utils/error.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace trust {
namespace fs = std::filesystem;

// ══════════════════════════════════════════════════════════════
//                      OutputBuffer
// ══════════════════════════════════════════════════════════════

void OutputBuffer::append(std::string_view text) {
    body.append(text);
}

void OutputBuffer::prepend(std::string_view text) {
    prefix.insert(0, text);
}

std::string OutputBuffer::result() const {
    std::string r;
    r.reserve(prefix.size() + body.size());
    r.append(prefix);
    r.append(body);
    return r;
}

// ══════════════════════════════════════════════════════════════
//                         Context
// ══════════════════════════════════════════════════════════════

// ── Конструкторы ──

Context::Context()
    : m_mapping(std::make_unique<SourceMapping>()),
      m_diag(std::make_unique<DiagnosticEngine>())
{
    std::error_code ec;
    m_baseDirectory = fs::current_path(ec).generic_string();
    m_diag->setSourceManager(this);
    m_opts = Options(*m_diag);
#define OPT_REG(name, str, sev) m_opts->add_option(OptKind::name);
    OPTIONS_LIST(OPT_REG)
#undef OPT_REG
}

Context::Context(std::string_view basePath, std::string_view tempPath)
    : m_mapping(std::make_unique<SourceMapping>()),
      m_diag(std::make_unique<DiagnosticEngine>()),
      m_tempDirectory(tempPath)
{
    // Нормализация basePath: если пусто — берём cwd
    if (basePath.empty()) {
        std::error_code ec;
        m_baseDirectory = fs::current_path(ec).generic_string();
    } else {
        m_baseDirectory = fs::absolute(
            fs::path(basePath)).generic_string();
    }

    m_diag->setSourceManager(this);
    m_opts = Options(*m_diag);
#define OPT_REG(name, str, sev) m_opts->add_option(OptKind::name);
    OPTIONS_LIST(OPT_REG)
#undef OPT_REG
}

// ── Утилиты ──

bool Context::validateSimpleName(std::string_view name) {
    if (name.empty()) return false;
    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_'))
            return false;
    }
    return true;
}

std::string Context::normalizePath(std::string_view path) const {
    if (path.empty()) return {};

    fs::path p(path);
    if (!p.is_absolute()) {
        p = fs::absolute(p);
    }
    // Относительность относительно baseDirectory
    auto base = fs::path(m_baseDirectory);
    auto rel = fs::relative(p, base);
    return rel.generic_string();
}

// fileIdxToArrayIndex: преобразует FileIdx в 0-based индекс массива.
// Для входных: raw без OUTPUT_FLAG = index + 1  →  index = raw - 1
// Для выходных: raw с OUTPUT_FLAG = (index + 1) | OUTPUT_FLAG  →  index = (raw & ~OUTPUT_FLAG) - 1
// Возвращает -1 если индекс некорректен.
int Context::fileIdxToArrayIndex(FileIdx idx, bool& isOutput) const {
    isOutput = (idx.raw & (1u << FileIdx::FILEIDX_BITS)) != 0u;
    uint32_t base = idx.raw & ((1u << FileIdx::FILEIDX_BITS) - 1u); // младшие FILEIDX_BITS бит (без флага)
    if (base < 1) return -1; // 0 — невалидный
    return static_cast<int>(base - 1); // 1-based → 0-based
}

// ── Входные файлы ──

FileIdx Context::add_source(std::string filename, std::string content, bool normalize) {
    if (normalize) {
        filename = normalizePath(filename);
    } else {
        if (!validateSimpleName(filename)) {
            return FileIdx{0};
        }
    }
    int idx = static_cast<int>(m_files.size());
    m_files.push_back({std::move(filename), std::move(content)});
    // 1-based: index + 1
    return FileIdx{static_cast<uint32_t>(idx + 1)};
}

FileIdx Context::load_file(std::string path) {
    std::string norm = normalizePath(path);
    // Сначала ищем среди уже загруженных
    for (int i = 0; i < static_cast<int>(m_files.size()); ++i) {
        if (m_files[i].filename == norm) {
            return FileIdx{static_cast<uint32_t>(i + 1)};
        }
    }

    std::ifstream ifs(norm, std::ios::in | std::ios::binary);
    if (!ifs) {
        // Пробуем оригинальный path
        ifs.open(path, std::ios::in | std::ios::binary);
        if (!ifs) {
            return FileIdx{0};
        }
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    int idx = static_cast<int>(m_files.size());
    m_files.push_back({std::move(norm), ss.str()});
    return FileIdx{static_cast<uint32_t>(idx + 1)};
}

std::string_view Context::filename(FileIdx idx) const {
    bool isOutput;
    int i = fileIdxToArrayIndex(idx, isOutput);
    if (i < 0) return {};
    if (!isOutput) {
        if (i >= static_cast<int>(m_files.size()))
            return {};
        return m_files[i].filename;
    }
    if (i >= static_cast<int>(m_outputs.size()))
        return {};
    return m_outputs[i].filename;
}

std::string_view Context::source(FileIdx idx) const {
    bool isOutput;
    int i = fileIdxToArrayIndex(idx, isOutput);
    if (i < 0 || isOutput) return {};
    if (i >= static_cast<int>(m_files.size()))
        return {};
    return m_files[i].source;
}

// ── Выходные файлы ──

FileIdx Context::add_output(std::string filename, bool normalize) {
    if (normalize) {
        filename = normalizePath(filename);
    } else {
        if (!validateSimpleName(filename)) {
            return FileIdx{0};
        }
    }
    int idx = static_cast<int>(m_outputs.size());
    m_outputs.push_back({std::move(filename), OutputBuffer{}});
    // 1-based + OUTPUT_FLAG: (index + 1) | (1 << FILEIDX_BITS)
    return FileIdx{static_cast<uint32_t>((idx + 1) | (1 << FileIdx::FILEIDX_BITS))};
}

bool Context::output_append(FileIdx idx, std::string_view text) {
    bool isOutput;
    int i = fileIdxToArrayIndex(idx, isOutput);
    if (i < 0 || !isOutput) return false;
    if (i >= static_cast<int>(m_outputs.size()))
        return false;
    m_outputs[i].buffer.append(text);
    return true;
}

bool Context::output_prepend(FileIdx idx, std::string_view text) {
    bool isOutput;
    int i = fileIdxToArrayIndex(idx, isOutput);
    if (i < 0 || !isOutput) return false;
    if (i >= static_cast<int>(m_outputs.size()))
        return false;
    m_outputs[i].buffer.prepend(text);
    return true;
}

std::string Context::output_result(FileIdx idx) const {
    bool isOutput;
    int i = fileIdxToArrayIndex(idx, isOutput);
    if (i < 0 || !isOutput) return {};
    if (i >= static_cast<int>(m_outputs.size()))
        return {};
    return m_outputs[i].buffer.result();
}

// ── Создание и валидация SourceLoc / SourceRange ──

void Context::validateLoc(SourceLoc loc) const {
    if (!loc.isValid())
        FAULT("SourceLoc is invalid (packed == 0)");

    FileIdx idx = loc.fileIdx();
    if (idx.raw == 0)
        FAULT("SourceLoc has invalid FileIdx (raw == 0)");

    bool isOutput;
    int arrIdx = fileIdxToArrayIndex(idx, isOutput);
    if (arrIdx < 0)
        FAULT("SourceLoc has invalid FileIdx raw value {}", idx.raw);

    int fileSize = 0;
    if (isOutput) {
        if (arrIdx >= static_cast<int>(m_outputs.size()))
            FAULT("output FileIdx {} out of range (max {})", idx.raw, m_outputs.size());
        fileSize = static_cast<int>(m_outputs[arrIdx].buffer.result().size());
    } else {
        if (arrIdx >= static_cast<int>(m_files.size()))
            FAULT("input FileIdx {} out of range (max {})", idx.raw, m_files.size());
        fileSize = static_cast<int>(m_files[arrIdx].source.size());
    }

    int off = loc.offset();
    if (off < 1 || off > fileSize + 1)
        FAULT("offset {} out of range [1, {}] for {} file", off, fileSize + 1, isOutput ? "output" : "input");
}

SourceLoc Context::makeLoc(FileIdx idx, int offset) const {
    if (idx.raw == 0)
        FAULT("FileIdx is invalid (raw == 0)");

    bool isOutput;
    int arrIdx = fileIdxToArrayIndex(idx, isOutput);
    if (arrIdx < 0)
        FAULT("FileIdx raw {} is invalid", idx.raw);

    int fileSize = 0;
    if (isOutput) {
        if (arrIdx >= static_cast<int>(m_outputs.size()))
            FAULT("output FileIdx {} out of range (max {})", idx.raw, m_outputs.size());
        fileSize = static_cast<int>(m_outputs[arrIdx].buffer.result().size());
    } else {
        if (arrIdx >= static_cast<int>(m_files.size()))
            FAULT("input FileIdx {} out of range (max {})", idx.raw, m_files.size());
        fileSize = static_cast<int>(m_files[arrIdx].source.size());
    }

    if (offset < 1 || offset > fileSize + 1)
        FAULT("offset {} out of range [1, {}] for {} file", offset, fileSize + 1, isOutput ? "output" : "input");

    uint32_t base = idx.raw & ((1u << FileIdx::FILEIDX_BITS) - 1u); // 1-based без флага
    uint32_t flag = isOutput ? SourceLoc::OUTPUT_FLAG : 0u;
    return SourceLoc{flag | (base << SourceLoc::FILEIDX_SHIFT) | static_cast<uint32_t>(offset)};
}

SourceRange Context::makeRange(SourceLoc begin, SourceLoc end) const {
    validateLoc(begin);
    validateLoc(end);

    // Проверка, что оба принадлежат одному файлу
    if (begin.fileIdx().raw != end.fileIdx().raw)
        FAULT("begin and end belong to different files (begin FileIdx={}, end FileIdx={})",
              begin.fileIdx().raw, end.fileIdx().raw);

    if (end.offset() < begin.offset())
        FAULT("end offset {} is less than begin offset {}", end.offset(), begin.offset());

    return SourceRange{begin, end};
}

// ── filename/source через SourceLoc ──

std::string_view Context::filename(SourceLoc loc) const {
    if (!loc.isValid()) return {};
    return filename(loc.fileIdx());
}

std::string_view Context::source(SourceLoc loc) const {
    if (!loc.isValid()) return {};
    // Для выходных файлов source() не имеет смысла — это буфер, не строка.
    if (loc.isOutput()) return {};
    return source(loc.fileIdx());
}

// ── loc_from_line ──

SourceLoc Context::loc_from_line(FileIdx idx, int line) const {
    bool isOutput;
    int i = fileIdxToArrayIndex(idx, isOutput);
    if (i < 0 || isOutput) return SourceLoc::invalid();

    if (i >= static_cast<int>(m_files.size()))
        return SourceLoc::invalid();

    const auto& src = m_files[i].source;
    int current_line = 1;
    for (int off = 0; off < static_cast<int>(src.size()); ++off) {
        if (current_line == line) {
            return makeLoc(idx, off + 1);
        }
        if (src[off] == '\n') {
            ++current_line;
        }
    }
    return makeLoc(idx, static_cast<int>(src.size()) + 1);
}

// ── line_column ──

Context::LineColumn Context::line_column(SourceLoc loc) const {
    // Поиск в кеше
    for (int i = 0; i < CACHE_SIZE; ++i) {
        if (m_cache_loc[i].packed == loc.packed)
            return m_cache_lc[i];
    }

    if (!loc.isValid()) return LineColumn{1, 1};

    FileIdx idx = loc.fileIdx();
    bool isOutput;
    int src_idx = fileIdxToArrayIndex(idx, isOutput);
    if (src_idx < 0 || isOutput || src_idx >= static_cast<int>(m_files.size()))
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
    int cache_idx = m_cache_next;
    m_cache_loc[cache_idx] = loc;
    m_cache_lc[cache_idx] = LineColumn{line, column};
    m_cache_next = (cache_idx + 1) % CACHE_SIZE;

    return LineColumn{line, column};
}

// ── Доступ к компонентам ──

DiagnosticEngine& Context::diag() { return *m_diag; }
Options& Context::opts() { return *m_opts; }
const DiagnosticEngine& Context::diag() const { return *m_diag; }
const Options& Context::opts() const { return *m_opts; }

// ══════════════════════════════════════════════════════════════
//                   Маппинг (делегирование)
// ══════════════════════════════════════════════════════════════

bool Context::addRangeMapping(SourceRange trustRange, SourceRange cppRange) {
    return m_mapping->addRangeMapping(trustRange, cppRange);
}

bool Context::addNameMapping(SourceRange trustRange, SourceRange cppRange,
                             std::string_view trustName, std::string_view cppName) {
    return m_mapping->addNameMapping(trustRange, cppRange, trustName, cppName);
}

std::optional<SourceRange> Context::mapTrustToCpp(SourceLoc trustLoc) const {
    return m_mapping->getMapTrustToCpp(trustLoc);
}

std::optional<SourceRange> Context::mapCppToTrust(SourceLoc cppLoc) const {
    return m_mapping->getMapCppToTrust(cppLoc);
}

std::optional<NameRangeInfo> Context::getCppName(
    SourceLoc trustLoc, std::string_view trustName) const {
    return m_mapping->getCppName(trustLoc, trustName);
}

std::optional<NameRangeInfo> Context::getTrustName(
    SourceLoc cppLoc, std::string_view cppName) const {
    return m_mapping->getTrustName(cppLoc, cppName);
}

std::vector<unsigned char> Context::packMapping() const {
    return m_mapping->pack();
}

bool Context::unpackMapping(const unsigned char* data, size_t size) {
    return m_mapping->unpack(data, size);
}

} // namespace trust