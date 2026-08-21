#include "diag/mapper.hpp"
#include "utils/error.hpp"
#include "utils/file_io.hpp"

#include <filesystem>
#include <system_error>

namespace trust {
namespace fs = std::filesystem;

// ══════════════════════════════════════════════════════════════
//                      OutputBuffer
// ══════════════════════════════════════════════════════════════

void OutputBuffer::prepend(std::string_view text, std::string_view ns) {
    std::string key(ns);
    m_prefixes[key].emplace(text);
}

void OutputBuffer::prependLeading(std::string_view text) {
    m_leading.append(text);
}

std::string OutputBuffer::build(unsigned indentSize) const {
    std::string result = m_leading;
    for (const auto& [ns, lines] : m_prefixes) {
        if (ns.empty()) {
            for (const auto& line : lines) {
                result.append(line);
                if (line.empty() || line.back() != '\n') {
                    result.push_back('\n');
                }
            }
        } else {
            result.append("namespace ");
            result.append(ns);
            result.append(" {\n");
            std::string indent(indentSize, ' ');
            for (const auto& line : lines) {
                result.append(indent);
                result.append(line);
                if (line.empty() || line.back() != '\n') {
                    result.push_back('\n');
                }
            }
            result.append("}\n");
        }
    }
    return result;
}

// ══════════════════════════════════════════════════════════════
//                      SourceMapWriter
// ══════════════════════════════════════════════════════════════

// -- Конструкторы --

SourceMapWriter::SourceMapWriter()
: SourceMap() {
    std::error_code ec;
    m_baseDirectory = fs::current_path(ec).generic_string();
}

SourceMapWriter::SourceMapWriter(std::string_view basePath, std::string_view tempPath)
: SourceMap()
, m_tempDirectory(tempPath) {
    if (basePath.empty()) {
        std::error_code ec;
        m_baseDirectory = fs::current_path(ec).generic_string();
    } else {
        m_baseDirectory = fs::absolute(fs::path(basePath)).generic_string();
    }
}

// -- Главный (корневой) файл модуля --

void SourceMapWriter::setMainModuleFile(MapperFile mainFile) {
    m_mainModuleFile = mainFile;
}

std::string SourceMapWriter::moduleName(MapperFile idx) const {
    std::error_code ec;
    // filename(idx) - путь, нормализованный относительно baseDirectory (может быть относительным).
    fs::path file = fs::path(filename(idx));
    if (!file.is_absolute()) {
        file = fs::path(m_baseDirectory) / file;
    }
    file = file.lexically_normal();

    // База отсчёта: каталог главного файла (если задан), иначе baseDirectory.
    // filename(...) возвращает путь относительно baseDirectory - приводим базу к absolute,
    // чтобы fs::relative сравнивал пути одинаковой природы.
    fs::path baseDir;
    if (!m_mainModuleFile.isInvalid()) {
        baseDir = fs::path(filename(m_mainModuleFile)).parent_path();
    }
    if (baseDir.empty()) {
        baseDir = fs::path(m_baseDirectory);
    }
    if (!baseDir.is_absolute()) {
        baseDir = fs::path(m_baseDirectory) / baseDir;
    }
    baseDir = baseDir.lexically_normal();

    fs::path rel;
    if (baseDir.empty()) {
        rel = file;
    } else {
        rel = fs::relative(file, baseDir, ec);
        if (ec) {
            rel = file.filename();
        }
    }

    std::string name = rel.replace_extension("").generic_string();
    for (char& ch : name) {
        if (ch == '/' || ch == '\\') {
            ch = '_';
        }
    }
    return name;
}

// -- Утилиты --

bool SourceMapWriter::validateSimpleName(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

std::string SourceMapWriter::normalizePath(std::string_view path) const {
    if (path.empty()) {
        return {};
    }
    // Resolve relative paths against the base directory, not the current
    // working directory. Otherwise the stored filename depends on the process
    // CWD and becomes unusable when the map is read back from another location.
    fs::path base = fs::path(m_baseDirectory);
    fs::path p(path);
    if (!p.is_absolute()) {
        p = base / p;
    }
    auto rel = fs::relative(p, base);
    return rel.generic_string();
}

// -- findFileIdx --

MapperFile SourceMapWriter::findFileIdx(std::string_view filePath) const {
    if (filePath.empty()) {
        return MapperFile{0};
    }

    std::string norm = normalizePath(filePath);

    return SourceMap::findFileIdx(norm);
}

// -- get_prepend --

std::string SourceMapWriter::get_prepend(MapperFile idx, unsigned indentSize) const {
    if (idx.isInvalid()) {
        FAULT("FileIdx is invalid (raw == 0)");
    }
    if (!idx.isOutput()) {
        FAULT("FileIdx is an input file, not an output file");
    }
    auto it = m_outputBuffers.find(idx.raw);
    if (it == m_outputBuffers.end()) {
        FAULT("output FileIdx has no prepend buffer");
    }
    return it->second.build(indentSize);
}

uint32_t SourceMapWriter::get_output_size(MapperFile idx) const {
    const auto& out = get_file(idx);
    return out.size();
}

// -- Входные файлы --

MapperFile SourceMapWriter::add_source(std::string filename, std::string content, bool normalize) {
    if (normalize) {
        filename = normalizePath(filename);
    } else {
        if (!validateSimpleName(filename)) {
            FAULT("Filename '{}' not valid!", filename);
        }
    }
    uint32_t idx = m_inputs.size();
    m_inputs.emplace_back(std::move(filename), std::move(content));
    return MapperFile::make_input(idx);
}

MapperFile SourceMapWriter::load_file(std::string path) {
    std::string norm = normalizePath(path);
    if (norm.empty()) {
        fs::path p = fs::absolute(fs::path(path));
        norm = p.generic_string();
    }

    // Проверка на дубликат
    for (uint32_t i = 0; i < m_inputs.size(); ++i) {
        if (m_inputs[i].getFilename() == norm) {
            FAULT("Module file {} already loaded as index {}!", m_inputs[i].getFilename(), i);
        }
    }

    auto content = utils::FileIO::read<std::vector<char>>(norm);
    if (!content) {
        content = utils::FileIO::read<std::vector<char>>(path);
        if (!content) {
            FAULT("Module file '{}' not found!", norm);
        }
    }
    m_inputs.emplace_back(std::move(norm), std::string(content->data(), content->size()));
    m_reader.reset();
    return MapperFile::make_input(static_cast<uint32_t>(m_inputs.size()) - 1u);
}

// -- Выходные файлы --

MapperFile SourceMapWriter::add_output(std::string filename, bool normalize) {
    if (normalize) {
        filename = normalizePath(filename);
    } else {
        if (!validateSimpleName(filename)) {
            FAULT("Filename '{}' not valid!", filename);
        }
    }
    uint32_t idx = m_outputs.size();
    m_outputs.emplace_back(std::move(filename));
    auto ret = MapperFile::make_output(idx);
    m_outputBuffers[ret.raw] = OutputBuffer{};
    return ret;
}

bool SourceMapWriter::output_append(MapperFile idx, std::string_view text) {
    if (!idx.isOutput()) {
        FAULT("FileIdx is an input file, not an output file");
    }
    auto& out = get_file(idx);
    out.appendSource(text);
    m_reader.reset();
    return true;
}

bool SourceMapWriter::output_prepend(MapperFile idx, std::string_view text, std::string_view ns) {
    if (!idx.isOutput()) {
        FAULT("FileIdx is an input file, not an output file");
    }
    auto it = m_outputBuffers.find(idx.raw);
    if (it == m_outputBuffers.end()) {
        FAULT("output FileIdx has no prepend buffer");
    }
    it->second.prepend(text, ns);
    m_reader.reset();
    return true;
}

bool SourceMapWriter::output_prepend_leading(MapperFile idx, std::string_view text) {
    if (!idx.isOutput()) {
        FAULT("FileIdx is an input file, not an output file");
    }
    auto it = m_outputBuffers.find(idx.raw);
    if (it == m_outputBuffers.end()) {
        FAULT("output FileIdx has no prepend buffer");
    }
    it->second.prependLeading(text);
    m_reader.reset();
    return true;
}

std::string SourceMapWriter::output_result(MapperFile idx) const {
    std::string pre = get_prepend(idx);
    const auto& out = get_file(idx);
    std::string r;
    r.reserve(pre.size() + out.size());
    r.append(pre);
    r.append(out.getSource());
    return r;
}

std::string_view SourceMapWriter::output_body(MapperFile idx) const {
    if (!idx.isOutput()) {
        FAULT("FileIdx is an input file, not an output file");
    }
    return get_file(idx).getSource();
}

bool SourceMapWriter::save_output(std::string_view outputDir) {
    if (outputDir.empty()) {
        FAULT("save_output: outputDir must not be empty");
        return false;
    }

    if (m_outputs.empty()) {
        return true;
    }

    bool allOk = true;
    for (size_t i = 0; i < m_outputs.size(); ++i) {
        FileEntry& fe = m_outputs[i];
        if (fe.getFilename().empty()) {
            FAULT("save_output: output file index {} has no filename", i);
            allOk = false;
            continue;
        }

        fs::path outPath = fs::path(outputDir) / fe.getFilename();
        std::error_code ec;

        fs::path parentDir = outPath.parent_path();
        if (!parentDir.empty()) {
            fs::create_directories(parentDir, ec);
            if (ec) {
                FAULT("save_output: cannot create directory '{}': {}", parentDir.generic_string(), ec.message());
                allOk = false;
                continue;
            }
        }

        std::string content = output_result(MapperFile::make_output(static_cast<uint32_t>(i)));

        {
            if (!utils::FileIO::write(outPath.generic_string(), content)) {
                FAULT("save_output: cannot open output file '{}' for writing", outPath.generic_string());
                allOk = false;
                continue;
            }
        }
    }

    return allOk;
}

// -- Создание и валидация Location / Range --

MapperRange SourceMapWriter::makeRange(MapperLocation begin, MapperLocation end) const {
    if (begin.isInvalid() || end.isInvalid()) {
        FAULT("Location is invalid (packed == 0)");
    }
    if (begin.fileIdx() != end.fileIdx()) {
        FAULT("begin and end belong to different files");
    }
    if (end.offset() < begin.offset()) {
        FAULT("end offset {} is less than begin offset {}", end.offset(), begin.offset());
    }
    return MapperRange{begin, end};
}

bool SourceMapWriter::isValid(MapperLocation loc) const {
    if (loc.isInvalid()) {
        return false;
    }
    return loc.offset() <= get_file(loc.fileIdx()).size();
}

bool SourceMapWriter::isValid(MapperRange range) const {
    if (!isValid(range.begin) || !isValid(range.end)) {
        return false;
    }
    return range.begin.fileIdx() == range.end.fileIdx() && range.begin <= range.end;
}

std::string_view SourceMapWriter::filename(MapperLocation loc) const {
    return SourceMap::filename(loc.fileIdx());
}

std::string_view SourceMapWriter::source(MapperLocation loc) const {
    return SourceMap::source(loc.fileIdx());
}

// ══════════════════════════════════════════════════════════════
//              SourceMapWriter маппинг-методы
// ══════════════════════════════════════════════════════════════

bool SourceMapWriter::addRangeMapping(MapperRange trustRange, MapperRange cppRange) {
    if (mappingSuppressed()) {
        return true; // подавлено: синтетические узлы (forward-decl на сайте импорта)
    }
    EXPECT(!trustRange.begin.isInvalid());
    EXPECT(!trustRange.end.isInvalid());
    EXPECT(!cppRange.begin.isInvalid());
    EXPECT(!cppRange.end.isInvalid());

    uint32_t trustKey = trustRange.begin.packed;
    uint32_t cppKey = cppRange.begin.packed;

    auto trustIt = m_forward.find(trustKey);
    if (trustIt != m_forward.end()) {
        FAULT("addRangeMapping: trustKey already mapped: "
              "key={:#x} new=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}] "
              "existing=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}]",
              trustKey, trustRange.begin.fileIdx().as_index(), trustRange.begin.offset(), trustRange.end.fileIdx().as_index(), trustRange.end.offset(),
              cppRange.begin.fileIdx().as_index(), cppRange.begin.offset(), cppRange.end.fileIdx().as_index(), cppRange.end.offset(),
              trustIt->second.from.begin.fileIdx().as_index(), trustIt->second.from.begin.offset(), trustIt->second.from.end.fileIdx().as_index(),
              trustIt->second.from.end.offset(), trustIt->second.to.begin.fileIdx().as_index(), trustIt->second.to.begin.offset(),
              trustIt->second.to.end.fileIdx().as_index(), trustIt->second.to.end.offset());
    }
    auto cppIt = m_backward.find(cppKey);
    if (cppIt != m_backward.end()) {
        FAULT("addRangeMapping: cppKey already mapped: "
              "key={:#x} new=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}] "
              "existing=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}]",
              cppKey, trustRange.begin.fileIdx().as_index(), trustRange.begin.offset(), trustRange.end.fileIdx().as_index(), trustRange.end.offset(),
              cppRange.begin.fileIdx().as_index(), cppRange.begin.offset(), cppRange.end.fileIdx().as_index(), cppRange.end.offset(),
              cppIt->second.from.begin.fileIdx().as_index(), cppIt->second.from.begin.offset(), cppIt->second.from.end.fileIdx().as_index(),
              cppIt->second.from.end.offset(), cppIt->second.to.begin.fileIdx().as_index(), cppIt->second.to.begin.offset(),
              cppIt->second.to.end.fileIdx().as_index(), cppIt->second.to.end.offset());
    }

    m_forward[trustKey] = RangeMap{trustRange, cppRange};
    m_backward[cppKey] = RangeMap{cppRange, trustRange};
    m_reader.reset();
    return true;
}

bool SourceMapWriter::addNameMapping(MapperRange trustRange, MapperRange cppRange, std::string_view trustName, std::string_view cppName) {
    if (mappingSuppressed()) {
        return true;
    }
    EXPECT(!trustRange.begin.isInvalid());
    EXPECT(!trustRange.end.isInvalid());
    EXPECT(!cppRange.begin.isInvalid());
    EXPECT(!cppRange.end.isInvalid());

    NameMap info;
    info.rangeMap = RangeMap{trustRange, cppRange};
    info.fromName = trustName;
    info.toName = cppName;
    m_nameMappings.push_back(info);
    m_nameBackward.emplace(std::string(cppName), std::string(trustName));
    m_reader.reset();
    return true;
}

bool SourceMapWriter::addMacroMapping(MapperRange bodyRange, MapperRange defRange) {
    if (mappingSuppressed()) {
        return true;
    }
    EXPECT(!bodyRange.begin.isInvalid());
    EXPECT(!bodyRange.end.isInvalid());
    EXPECT(!defRange.begin.isInvalid());
    EXPECT(!defRange.end.isInvalid());
    if (bodyRange.begin.isOutput()) {
        FAULT("bodyRange must be an input file");
    }
    if (defRange.begin.isOutput()) {
        FAULT("defRange must be an input file");
    }

    uint32_t key = bodyRange.begin.packed;
    if (m_macroForward.find(key) != m_macroForward.end()) {
        return true;
    }

    m_macroForward[key] = RangeMap{bodyRange, defRange};
    m_reader.reset();
    return true;
}

// ══════════════════════════════════════════════════════════════
//              SourceMapWriter::mapStart / mapStop
// ══════════════════════════════════════════════════════════════

MapperRange SourceMapWriter::mapStart(MapperFile from, uint32_t from_begin, uint32_t from_end, MapperFile to) {
    if (from.isInvalid()) {
        FAULT("mapStart: 'from' FileIdx is invalid");
    }
    if (from_begin > from_end) {
        FAULT("mapStart: from_begin ({}) > from_end ({})", from_begin, from_end);
    }

    MapperLocation begin = makeLoc(from, from_begin);
    MapperLocation end = makeLoc(from, from_end);
    return mapStart(MapperRange{begin, end}, to);
}

MapperRange SourceMapWriter::mapStart(MapperRange from, MapperFile to) {
    if (from.begin.isInvalid() || from.end.isInvalid()) {
        FAULT("mapStart(Range): 'from' Range is invalid");
    }
    if (to.isInvalid() || !to.isOutput()) {
        FAULT("mapStart(Range): 'to' FileIdx must be a valid output file");
    }
    if (mappingSuppressed()) {
        return from; // подавлено: не пушим и не маппим
    }

    Location outputBegin = makeLoc(to, get_file(to).size() + 1);

    m_mapStack.push_back({from, outputBegin});
    return from;
}

const SourceMapWriter::MapStartEntry& SourceMapWriter::mapStackTop() const {
    if (m_mapStack.empty()) {
        FAULT("mapStackTop: map stack is empty");
    }
    return m_mapStack.back();
}

MapperRange SourceMapWriter::mapStop(MapperRange from) {
    if (mappingSuppressed()) {
        return from; // подавлено: mapStart не пушил - нечего закрывать
    }
    EXPECT(!m_mapStack.empty());

    MapStartEntry entry = m_mapStack.back();
    m_mapStack.pop_back();

    EXPECT(entry.inputRange == from && "top of map stack does not match 'from'");

    MapperFile to = entry.outputBegin.fileIdx();
    uint32_t currentOffset = get_file(to).size() + 1;
    Location outputEnd = makeLoc(to, currentOffset);

    MapperRange cppRange{entry.outputBegin, outputEnd};

    EXPECT(!entry.inputRange.begin.isInvalid());
    EXPECT(!entry.inputRange.end.isInvalid());
    EXPECT(!cppRange.begin.isInvalid());
    EXPECT(!cppRange.end.isInvalid());

    uint32_t trustKey = entry.inputRange.begin.packed;
    uint32_t cppKey = cppRange.begin.packed;

    auto trustIt = m_forward.find(trustKey);
    if (trustIt != m_forward.end()) {
        FAULT("mapStop: trustKey already mapped: "
              "key={:#x} new=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}] "
              "existing=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}]",
              trustKey, entry.inputRange.begin.fileIdx().as_index(), entry.inputRange.begin.offset(), entry.inputRange.end.fileIdx().as_index(),
              entry.inputRange.end.offset(), cppRange.begin.fileIdx().as_index(), cppRange.begin.offset(), cppRange.end.fileIdx().as_index(),
              cppRange.end.offset(), trustIt->second.from.begin.fileIdx().as_index(), trustIt->second.from.begin.offset(),
              trustIt->second.from.end.fileIdx().as_index(), trustIt->second.from.end.offset(), trustIt->second.to.begin.fileIdx().as_index(),
              trustIt->second.to.begin.offset(), trustIt->second.to.end.fileIdx().as_index(), trustIt->second.to.end.offset());
    }
    auto cppIt = m_backward.find(cppKey);
    if (cppIt != m_backward.end()) {
        FAULT("mapStop: cppKey already mapped: "
              "key={:#x} new=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}] "
              "existing=[f{}:{}-f{}:{}] -> [f{}:{}-f{}:{}]",
              cppKey, entry.inputRange.begin.fileIdx().as_index(), entry.inputRange.begin.offset(), entry.inputRange.end.fileIdx().as_index(),
              entry.inputRange.end.offset(), cppRange.begin.fileIdx().as_index(), cppRange.begin.offset(), cppRange.end.fileIdx().as_index(),
              cppRange.end.offset(), cppIt->second.from.begin.fileIdx().as_index(), cppIt->second.from.begin.offset(),
              cppIt->second.from.end.fileIdx().as_index(), cppIt->second.from.end.offset(), cppIt->second.to.begin.fileIdx().as_index(),
              cppIt->second.to.begin.offset(), cppIt->second.to.end.fileIdx().as_index(), cppIt->second.to.end.offset());
    }

    m_forward[trustKey] = RangeMap{entry.inputRange, cppRange};
    m_backward[cppKey] = RangeMap{cppRange, entry.inputRange};
    m_reader.reset();
    return cppRange;
}

// ══════════════════════════════════════════════════════════════
//              SourceMapWriter::toReader
// ══════════════════════════════════════════════════════════════

const SourceMapReader* SourceMapWriter::toReader() const {
    if (m_reader) {
        return m_reader.get();
    }

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
        if (it != m_outputBuffers.end()) {
            std::string prefixStr = it->second.build();
            if (!prefixStr.empty()) {
                prependSizes[i] = static_cast<uint64_t>(prefixStr.size());
                fullContent.reserve(prefixStr.size() + m_outputs[i].size());
                fullContent.append(prefixStr);
                fullContent.append(m_outputs[i].getSource());
            } else {
                fullContent = std::string(m_outputs[i].getSource());
            }
        } else {
            fullContent = std::string(m_outputs[i].getSource());
        }
        finalizedOutputs.emplace_back(m_outputs[i].getFilename(), std::move(fullContent));
    }

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
            if (!cppIdx.isOutput()) {
                continue;
            }
            uint32_t outIdx = cppIdx.as_index();
            uint64_t prependSize = prependSizes[outIdx];
            if (prependSize == 0) {
                continue;
            }
            uint32_t beginOff = cppRange.begin.offset();
            using LocType = decltype(cppRange.begin);
            cppRange.begin = LocType::makeLoc(cppIdx, beginOff + static_cast<uint32_t>(prependSize));
            if (!cppRange.end.isInvalid()) {
                uint32_t endOff = cppRange.end.offset();
                cppRange.end = LocType::makeLoc(cppIdx, endOff + static_cast<uint32_t>(prependSize));
            }
        }
    };
    offsetPrepends(reader->m_backward, true);
    offsetPrepends(reader->m_forward, false);

    // Ключи m_backward - это cpp-begin (устанавливаются в mapStop как body-выровненные
    // `get_file(to).size()+1`). offsetPrepends сдвинул cpp-RANGE (value) на prependSize,
    // но ключ остался body-выровненным. Без пере-ключения findRangeMap/findRange
    // (upper_bound по ключу) не совпадёт с full-выровненным запросом (lspToLocation),
    // и обратный маппинг попадёт в «соседний» statement. Пере-ключаем на from.begin.
    {
        std::map<uint32_t, SourceMapReader::RangeMap> rekeyed;
        for (auto& [oldKey, entry] : reader->m_backward) {
            (void)oldKey;
            rekeyed[entry.from.begin.packed] = std::move(entry);
        }
        reader->m_backward = std::move(rekeyed);
    }

    for (auto& entry : reader->m_nameMappings) {
        auto& cppRange = entry.rangeMap.to;
        auto cppIdx = cppRange.begin.fileIdx();
        if (!cppIdx.isOutput()) {
            continue;
        }
        uint32_t outIdx = cppIdx.as_index();
        uint64_t prependSize = prependSizes[outIdx];
        if (prependSize == 0) {
            continue;
        }
        uint32_t beginOff = cppRange.begin.offset();
        cppRange.begin = ReaderLocation::makeLoc(cppIdx, beginOff + static_cast<uint32_t>(prependSize));
        if (!cppRange.end.isInvalid()) {
            uint32_t endOff = cppRange.end.offset();
            cppRange.end = ReaderLocation::makeLoc(cppIdx, endOff + static_cast<uint32_t>(prependSize));
        }
    }

    reader->m_inputs = std::move(finalizedInputs);
    reader->m_outputs = std::move(finalizedOutputs);

    m_reader = std::move(reader);
    return m_reader.get();
}

void SourceMapWriter::setBaseDirectory(std::string_view path) {
    if (path.empty()) {
        std::error_code ec;
        m_baseDirectory = fs::current_path(ec).generic_string();
    } else {
        m_baseDirectory = fs::absolute(fs::path(path)).generic_string();
    }
}

} // namespace trust