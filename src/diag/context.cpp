#include "diag/context.hpp"
#include "diag/mapper.hpp"
#include "utils/error.hpp"

#include "llvm/Support/MD5.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace trust {
namespace fs = std::filesystem;

// ══════════════════════════════════════════════════════════════
//                      OutputBuffer
// ══════════════════════════════════════════════════════════════

void OutputBuffer::prepend(std::string_view text) {
    prefix.append(text);
}

// ══════════════════════════════════════════════════════════════
//                         Context
// ══════════════════════════════════════════════════════════════

// ── Конструкторы ──

Context::Context()
: m_diag(std::make_unique<DiagnosticEngine>()) {
    std::error_code ec;
    m_baseDirectory = fs::current_path(ec).generic_string();
    m_diag->setSourceManager(this);
    m_opts = Options(*m_diag);
#define OPT_REG(name, str, sev) m_opts->add_option(OptKind::name);
    OPTIONS_LIST(OPT_REG)
#undef OPT_REG
}

Context::Context(std::string_view basePath, std::string_view tempPath)
: SourceMap()
, m_diag(std::make_unique<DiagnosticEngine>())
, m_tempDirectory(tempPath) {
    if (basePath.empty()) {
        std::error_code ec;
        m_baseDirectory = fs::current_path(ec).generic_string();
    } else {
        m_baseDirectory = fs::absolute(fs::path(basePath)).generic_string();
    }
    m_diag->setSourceManager(this);
    m_opts = Options(*m_diag);
#define OPT_REG(name, str, sev) m_opts->add_option(OptKind::name);
    OPTIONS_LIST(OPT_REG)
#undef OPT_REG
}

// ── Утилиты ──

bool Context::validateSimpleName(std::string_view name) {
    if (name.empty())
        return false;
    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return true;
}

std::string Context::normalizePath(std::string_view path) const {
    if (path.empty())
        return {};
    fs::path p(path);
    if (!p.is_absolute())
        p = fs::absolute(p);
    auto base = fs::path(m_baseDirectory);
    auto rel = fs::relative(p, base);
    return rel.generic_string();
}

// ── findFileIdx ──

MapperFile Context::findFileIdx(std::string_view filePath) const {
    if (filePath.empty())
        return MapperFile{0};

    std::string norm = normalizePath(filePath);

    return SourceMap::findFileIdx(norm);
}

// ── get_prepend ──

std::string& Context::get_prepend(MapperFile idx) {
    return const_cast<std::string&>(static_cast<const Context*>(this)->get_prepend(idx));
}

const std::string& Context::get_prepend(MapperFile idx) const {
    if (!idx.isValid())
        FAULT("FileIdx is invalid (raw == 0)");
    if (!idx.isOutput())
        FAULT("FileIdx is an input file, not an output file");
    auto it = m_outputBuffers.find(idx.raw);
    if (it == m_outputBuffers.end())
        FAULT("output FileIdx has no prepend buffer");
    return it->second.prefix;
}

uint32_t Context::get_output_size(MapperFile idx) const {
    const auto& out = get_file(idx);
    return out.size();
}

// ── Входные файлы ──

MapperFile Context::add_source(std::string filename, std::string content, bool normalize) {
    if (normalize) {
        filename = normalizePath(filename);
    } else {
        if (!validateSimpleName(filename))
            FAULT("Filename '{}' not valid!", filename);
    }
    uint32_t idx = m_inputs.size();
    m_inputs.emplace_back(std::move(filename), std::move(content));
    return MapperFile::make_input(idx);
}

MapperFile Context::load_file(std::string path) {
    if (m_inputs.empty()) {
        // Первый вызов — загружаем главный файл
        fs::path p = fs::absolute(fs::path(path));
        m_baseDirectory = p.parent_path().generic_string();
        std::string norm = p.filename().generic_string();

        std::ifstream ifs(p, std::ios::in | std::ios::binary);
        if (!ifs)
            FAULT("Main file '{}' not found!", p.generic_string());
        std::ostringstream ss;
        ss << ifs.rdbuf();
        auto content = ss.str();
        m_inputs.emplace_back(std::move(norm), std::move(content));
        m_reader.reset();
        return MapperFile::make_input(0);
    }

    // Последующие вызовы — загружаем модуль
    std::string norm = normalizePath(path);
    for (uint32_t i = 0; i < m_inputs.size(); ++i) {
        if (m_inputs[i].getFilename() == norm)
            FAULT("Module file {} already loaded as index {}!", m_inputs[i].getFilename(), i);
    }
    std::ifstream ifs(norm, std::ios::in | std::ios::binary);
    if (!ifs) {
        ifs.open(path, std::ios::in | std::ios::binary);
        if (!ifs)
            FAULT("Module file '{}' not found!", norm);
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    auto content = ss.str();
    m_inputs.emplace_back(std::move(norm), std::move(content));
    m_reader.reset();
    return MapperFile::make_input(static_cast<uint32_t>(m_inputs.size()) - 1u);
}

// ── Выходные файлы ──

MapperFile Context::add_output(std::string filename, bool normalize) {
    if (normalize) {
        filename = normalizePath(filename);
    } else {
        if (!validateSimpleName(filename))
            FAULT("Filename '{}' not valid!", filename);
    }
    uint32_t idx = m_outputs.size();
    m_outputs.emplace_back(std::move(filename));
    auto ret = MapperFile::make_output(idx);
    m_outputBuffers[ret.raw] = OutputBuffer{};
    return ret;
}

bool Context::output_append(MapperFile idx, std::string_view text) {
    if (!idx.isOutput())
        FAULT("FileIdx is an input file, not an output file");
    auto& out = get_file(idx);
    out.appendSource(text);
    m_reader.reset();
    return true;
}

bool Context::output_prepend(MapperFile idx, std::string_view text) {
    auto& pre = get_prepend(idx);
    std::string t(text);
    if (t.empty() || t.back() != '\n')
        t.push_back('\n');
    pre.append(t);
    return true;
}

std::string Context::output_result(MapperFile idx) const {
    const auto& pre = get_prepend(idx);
    const auto& out = get_file(idx);
    std::string r;
    r.reserve(pre.size() + out.size());
    r.append(pre);
    r.append(out.getSource());
    return r;
}

std::string_view Context::output_body(MapperFile idx) const {
    if (!idx.isOutput())
        FAULT("FileIdx is an input file, not an output file");
    return get_file(idx).getSource();
}

bool Context::save_output(std::string_view outputDir) {
    if (outputDir.empty()) {
        FAULT("save_output: outputDir must not be empty");
        return false;
    }

    if (m_outputs.empty()) {
        return true; // нечего сохранять
    }

    bool allOk = true;
    for (size_t i = 0; i < m_outputs.size(); ++i) {
        FileEntry& fe = m_outputs[i];
        if (fe.getFilename().empty()) {
            FAULT("save_output: output file index {} has no filename", i);
            allOk = false;
            continue;
        }

        // filename всегда относительный — резолвим относительно outputDir
        // При этом filename может содержать поддиректории (напр. "sub/a.cpp")
        fs::path outPath = fs::path(outputDir) / fe.getFilename();
        std::error_code ec;

        // Создаём родительские директории (иерархия из относительного имени)
        fs::path parentDir = outPath.parent_path();
        if (!parentDir.empty()) {
            fs::create_directories(parentDir, ec);
            if (ec) {
                FAULT("save_output: cannot create directory '{}': {}", parentDir.generic_string(), ec.message());
                allOk = false;
                continue;
            }
        }

        // Собираем содержимое (prepend + source)
        std::string content = output_result(MapperFile::make_output(static_cast<uint32_t>(i)));

        // Пишем файл
        {
            std::ofstream ofs(outPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!ofs) {
                FAULT("save_output: cannot open output file '{}' for writing", outPath.generic_string());
                allOk = false;
                continue;
            }
            ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        }
    }

    return allOk;
}

// ── Создание и валидация Location / Range ──

MapperRange Context::makeRange(MapperLocation begin, MapperLocation end) const {
    if (!begin.isValid() || !end.isValid())
        FAULT("Location is invalid (packed == 0)");
    if (begin.fileIdx() != end.fileIdx())
        FAULT("begin and end belong to different files");
    if (end.offset() < begin.offset())
        FAULT("end offset {} is less than begin offset {}", end.offset(), begin.offset());
    return MapperRange{begin, end};
}

std::string_view Context::filename(MapperLocation loc) const {
    return SourceMap::filename(loc.fileIdx());
}

std::string_view Context::source(MapperLocation loc) const {
    return SourceMap::source(loc.fileIdx());
}

// ── Доступ к компонентам ──

DiagnosticEngine& Context::diag() {
    return *m_diag;
}
Options& Context::opts() {
    return *m_opts;
}
AttrPool& Context::attrs() {
    if (!m_attr_pool) {
        m_attr_pool = std::make_unique<AttrPool>();
        register_builtin_attrs(*m_attr_pool);
    }
    return *m_attr_pool;
}
const DiagnosticEngine& Context::diag() const {
    return *m_diag;
}
const Options& Context::opts() const {
    return *m_opts;
}
const AttrPool& Context::attrs() const {
    EXPECT(m_attr_pool != nullptr);
    return *m_attr_pool;
}

// ══════════════════════════════════════════════════════════════
//              SourceMapWriter methods (on Context)
// ══════════════════════════════════════════════════════════════

bool Context::addRangeMapping(MapperRange trustRange, MapperRange cppRange) {
    EXPECT(trustRange.begin.isValid());
    EXPECT(trustRange.end.isValid());
    EXPECT(cppRange.begin.isValid());
    EXPECT(cppRange.end.isValid());

    uint32_t trustKey = trustRange.begin.packed;
    uint32_t cppKey = cppRange.begin.packed;

    EXPECT(m_forward.find(trustKey) == m_forward.end());
    EXPECT(m_backward.find(cppKey) == m_backward.end());

    m_forward[trustKey] = RangeMap{trustRange, cppRange};
    m_backward[cppKey] = RangeMap{cppRange, trustRange};
    m_reader.reset();
    return true;
}

bool Context::addNameMapping(MapperRange trustRange, MapperRange cppRange, std::string_view trustName, std::string_view cppName) {
    EXPECT(trustRange.begin.isValid());
    EXPECT(trustRange.end.isValid());
    EXPECT(cppRange.begin.isValid());
    EXPECT(cppRange.end.isValid());

    NameMap info;
    info.rangeMap = RangeMap{trustRange, cppRange};
    info.fromName = trustName;
    info.toName = cppName;
    m_nameMappings.push_back(info);
    m_nameBackward.emplace(std::string(cppName), std::string(trustName));
    m_reader.reset();
    return true;
}

bool Context::addMacroMapping(MapperRange bodyRange, MapperRange defRange) {
    EXPECT(bodyRange.begin.isValid());
    EXPECT(bodyRange.end.isValid());
    EXPECT(defRange.begin.isValid());
    EXPECT(defRange.end.isValid());
    if (bodyRange.begin.isOutput())
        FAULT("bodyRange must be an input file");
    if (defRange.begin.isOutput())
        FAULT("defRange must be an input file");

    uint32_t key = bodyRange.begin.packed;
    // Пропускаем дубликаты — они возникают при рекурсивном раскрытии макросов,
    // когда вложенный макрос уже зарегистрировал маппинг для того же токена.
    if (m_macroForward.find(key) != m_macroForward.end())
        return true;

    m_macroForward[key] = RangeMap{bodyRange, defRange};
    m_reader.reset();
    return true;
}

const SourceMapReader* Context::toReader() const {
    if (m_reader)
        return m_reader.get();

    auto reader = std::make_unique<SourceMapReader>();

    std::vector<FileEntry> finalizedInputs = m_inputs;
    std::vector<FileEntry> finalizedOutputs;
    finalizedOutputs.reserve(m_outputs.size());
    std::vector<uint64_t> prependSizes(m_outputs.size(), 0);

    for (size_t i = 0; i < m_outputs.size(); ++i) {
        MapperFile idx = MapperFile::make_output(static_cast<uint32_t>(i));
        uint32_t raw = idx.raw;
        auto it = m_outputBuffers.find(raw);
        std::string fullContent;
        if (it != m_outputBuffers.end() && !it->second.prefix.empty()) {
            prependSizes[i] = static_cast<uint64_t>(it->second.prefix.size());
            fullContent.reserve(it->second.prefix.size() + m_outputs[i].size());
            fullContent.append(it->second.prefix);
            fullContent.append(m_outputs[i].getSource());
        } else {
            fullContent = std::string(m_outputs[i].getSource());
        }
        // filename всегда относительный — не меняем его
        finalizedOutputs.emplace_back(m_outputs[i].getFilename(), std::move(fullContent));
    }

    // Шаблонная лямбда для конверсии map-контейнера (std::map<uint32_t, RangeMap>)
    auto convertMap = [](auto& srcMap, auto& dstMap) {
        for (auto& [key, entry] : srcMap) {
            ReaderRange readerFrom{static_cast<ReaderLocation>(entry.from.begin), static_cast<ReaderLocation>(entry.from.end)};
            ReaderRange readerTo{static_cast<ReaderLocation>(entry.to.begin), static_cast<ReaderLocation>(entry.to.end)};
            dstMap[key] = SourceMapReader::RangeMap{readerFrom, readerTo};
        }
    };
    convertMap(m_forward, reader->m_forward);
    convertMap(m_macroForward, reader->m_macroForward);
    convertMap(m_backward, reader->m_backward);

    // Конверсия nameMappings (вектор, а не map)
    for (auto& entry : m_nameMappings) {
        ReaderRange readerFrom{static_cast<ReaderLocation>(entry.rangeMap.from.begin), static_cast<ReaderLocation>(entry.rangeMap.from.end)};
        ReaderRange readerTo{static_cast<ReaderLocation>(entry.rangeMap.to.begin), static_cast<ReaderLocation>(entry.rangeMap.to.end)};
        SourceMapReader::RangeMap readerRange{readerFrom, readerTo};
        reader->m_nameMappings.emplace_back(std::move(readerRange), std::move(entry.fromName), std::move(entry.toName));
    }
    reader->m_nameBackward = m_nameBackward;

    auto offsetPrepends = [&prependSizes](auto& entries, bool cppIsFrom) {
        for (auto& [key, entry] : entries) {
            (void)key;
            auto& cppRange = cppIsFrom ? entry.from : entry.to;
            auto cppIdx = cppRange.begin.fileIdx();
            if (!cppIdx.isOutput())
                continue;
            uint32_t outIdx = cppIdx.as_index();
            uint64_t prependSize = prependSizes[outIdx];
            if (prependSize == 0)
                continue;
            uint32_t beginOff = cppRange.begin.offset();
            using LocType = decltype(cppRange.begin);
            cppRange.begin = LocType::makeLoc(cppIdx, beginOff + static_cast<uint32_t>(prependSize));
            if (cppRange.end.isValid()) {
                uint32_t endOff = cppRange.end.offset();
                cppRange.end = LocType::makeLoc(cppIdx, endOff + static_cast<uint32_t>(prependSize));
            }
        }
    };
    offsetPrepends(reader->m_backward, true);
    offsetPrepends(reader->m_forward, false);

    reader->m_inputs = std::move(finalizedInputs);
    reader->m_outputs = std::move(finalizedOutputs);

    m_reader = std::move(reader);
    return m_reader.get();
}

} // namespace trust