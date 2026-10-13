#include "sourcemap/writer.hpp"

#include "sourcemap/serialize/reader.hpp"
#include "utils/error.hpp"
#include "utils/utils.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace trust {
namespace fs = std::filesystem;

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
    // Явная диагностика вместо EXPECT/abort в make_input при исчерпании таблицы источников:
    // даём читаемое сообщение (разгон/цикл регистрации источников), а не краш.
    if (m_inputs.size() >= LocationPack::MAX_FILES_INPUT - 1) {
        throw std::runtime_error("Too many input source files (limit " + std::to_string(LocationPack::MAX_FILES_INPUT) +
                                 "); possible cyclic or unbounded source registration near '" + filename + "'");
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

    // Детекция цикла/повторной загрузки файла по данным самого маппера (m_inputs):
    // загрузка одного файла повторно (в т.ч. цикл A->B->A) - признак циклической зависимости.
    for (uint32_t i = 0; i < m_inputs.size(); ++i) {
        if (m_inputs[i].getFilename() == norm) {
            FAULT("Cyclic file load: '{}' is already loaded (index {})!", m_inputs[i].getFilename(), i);
        }
    }

    // Явная диагностика вместо EXPECT/abort при исчерпании таблицы источников.
    if (m_inputs.size() >= LocationPack::MAX_FILES_INPUT - 1) {
        throw std::runtime_error("Too many input source files (limit " + std::to_string(LocationPack::MAX_FILES_INPUT) +
                                 "); possible cyclic or unbounded source registration near '" + norm + "'");
    }

    auto content = utils::FileIO::read<std::vector<char>>(norm);
    if (!content) {
        content = utils::FileIO::read<std::vector<char>>(path);
    }

    if (!content) {
        FAULT("Module file '{}' not found!", norm);
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

bool SourceMapWriter::output_drop(MapperFile idx) {
    if (!idx.isOutput()) {
        return false;
    }
    const uint32_t index = static_cast<uint32_t>(idx.as_index());
    if (index + 1 != m_outputs.size()) {
        return false;
    }
    m_outputBuffers.erase(idx.raw);
    m_outputs.pop_back();
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

void SourceMapWriter::setBaseDirectory(std::string_view path) {
    if (path.empty()) {
        std::error_code ec;
        m_baseDirectory = fs::current_path(ec).generic_string();
    } else {
        m_baseDirectory = fs::absolute(fs::path(path)).generic_string();
    }
}

} // namespace trust
