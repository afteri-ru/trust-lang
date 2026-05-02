#include "utils/utils.hpp"
#include "debug/trust_source.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <algorithm>
#include <iterator>

namespace trust {

// ═══════════════════════════════════════════
//               TrustSource
// ═══════════════════════════════════════════

TrustSource::TrustSource(std::string_view basePath, std::string_view cppPath) {
    if (basePath.empty()) {
        FAULT("basePath must be non-empty in TrustSource constructor");
    }
    auto p = std::filesystem::path(basePath);
    base_directory_ = std::filesystem::absolute(p).lexically_normal().string();

    if (cppPath.empty()) {
        cpp_directory_ = trust::utils::resolveTempDir(base_directory_).string();
    } else {
        auto p = std::filesystem::path(cppPath);
        cpp_directory_ = std::filesystem::absolute(p).lexically_normal().string();
    }
}

// ── Нормализация путей ──
//
// Единая нормализация пути относительно заданного baseDir:
// - относительные пути → lexically_normal() как есть
// - абсолютные пути → удаляется префикс baseDir + "/"
// - если baseDir пуст → путь возвращается как есть (lexically_normal)

std::string TrustSource::normalizePath(std::string_view path, const std::string &baseDir) const {
    namespace fs = std::filesystem;
    fs::path p(path);

    if (p.is_relative() || baseDir.empty()) {
        return p.lexically_normal().string();
    }

    auto absPath = fs::absolute(p).lexically_normal().string();
    auto prefix = fs::path(baseDir).lexically_normal().string() + "/";

    if (absPath.find(prefix) == 0) {
        return absPath.substr(prefix.size());
    }

    FAULT("Path '{}' is not under base directory '{}'", path, baseDir);
    return "";
}

const FilePairEntry *TrustSource::setFilePair(std::string_view trustFile, std::string_view cppFile) {
    auto tStr = normalizePath(trustFile, base_directory_);
    auto cStr = normalizePath(cppFile, cpp_directory_);

    // Ищем существующую пару
    for (auto &entry : entries_) {
        if (entry.files.first == tStr && entry.files.second == cStr) {
            current_ = &entry;
            return current_;
        }
    }

    // Создаём новую
    FilePairEntry e;
    e.files.first = tStr;
    e.files.second = cStr;
    e.cpp_line_inserted = 0;
    entries_.push_back(std::move(e));
    current_ = &entries_.back();
    return current_;
}

const FilePairEntry *TrustSource::currentFilePair() const {
    return current_;
}

void TrustSource::setCppLineInserted(size_t n) {
    if (current_) {
        current_->cpp_line_inserted = n;
    }
}

bool TrustSource::checkMonotonicity(LineNumber trustLine, LineNumber cppLine) const {
    if (!current_) {
        FAULT("No current file pair set");
        return false;
    }

    const auto &idx = current_->trustToCppIndex;
    if (idx.empty()) {
        return true; // первая запись — всегда ок
    }

    // trust_line уже существует — проверяем что cppLine не уменьшился
    auto it = idx.find(trustLine);
    if (it != idx.end()) {
        if (cppLine < it->second) {
            FAULT("In C++ file {} cpp_line decreased {} -> {} for trust_line {}", current_->files.second, it->second, cppLine, trustLine);
            return false;
        }
        return true; // cppLine не уменьшился — ок
    }

    // Новый trust_line — проверяем монотонность относительно последней записи
    auto last = std::prev(idx.end());
    if (trustLine < last->first) {
        FAULT("In C++ file {} reorder trust lines {} < {}", current_->files.second, trustLine, last->first);
        return false;
    }
    if (cppLine < last->second) {
        FAULT("In C++ file {} reorder cpp lines {} < {}", current_->files.second, cppLine, last->second);
        return false;
    }

    return true;
}

bool TrustSource::addLineMapping(LineNumber trustLine, LineNumber cppLine) {
    if (!current_) {
        FAULT("No current file pair set");
        return false;
    }

    if (!checkMonotonicity(trustLine, cppLine)) {
        return false;
    }

    auto &idx = current_->trustToCppIndex;
    idx[trustLine] = cppLine;
    return true;
}

bool TrustSource::addVarMapping(LineNumber trustLine, LineNumber cppLine, std::string_view trustVar, std::string_view cppVar) {

    // Проверяем монотонность (но не добавляем запись в trustToCppIndex)
    if (!checkMonotonicity(trustLine, cppLine)) {
        return false;
    }

    std::string tv(trustVar);
    std::string cv(cppVar);

    LinePair lines{trustLine, cppLine};

    // Переменная может быть переопределена (например, для разных функций).
    // Всегда обновляем trustVarMapping — последняя запись для данного trust_var
    // считается актуальной. Но в cppToTrustVar не удаляем старые записи:
    // один cpp_var может соответствовать разным trust_var в разных контекстах
    // (локальные переменные с одинаковым именем в разных функциях).
    // Поиск (getTrustVar) разрешит неоднозначность:
    //   — сперва по имени файла (выбор entry из entries_),
    //   — затем по номеру строки (exact match по cpp_line, fallback — nearest).
    current_->trustVarMapping[tv] = VarMapValue{cv, lines};
    current_->cppToTrustVar.emplace(cv, tv);
    return true;
}

size_t TrustSource::new_line_count(std::string_view s) {
    size_t count = 0;
    for (auto pos = s.find('\n'); pos != std::string_view::npos; pos = s.find('\n', pos + 1)) {
        ++count;
    }
    return count;
}

void TrustSource::include_append(const std::vector<std::string> &files) {
    if (!current_)
        return;
    size_t total = 0;
    for (const auto &f : files) {
        total += new_line_count(f);
    }
    current_->cpp_line_inserted += total;
}

// ═══════════════════════════════════════════
//               findNearestCppLine
// ═══════════════════════════════════════════

// Поиск последнего trust_line, у которого cpp_line ≤ заданного.
// trustToCppIndex отсортирован по trust_line, но cpp_line монотонно возрастает,
// поэтому линейный проход с break — самый простой вариант.
// Для больших объёмов можно перейти на random-access вектор + lower_bound.
LineNumber TrustSource::findNearestCppLine(const std::map<LineNumber, LineNumber> &idx, LineNumber cppLine) {
    LineNumber bestTrustLine = 0; // 0 = no match found (line numbers start from 1)
    for (const auto &[tl, cl] : idx) {
        if (cl <= cppLine) {
            bestTrustLine = tl;
        } else {
            break; // монотонность гарантирует, что дальше все cl > cppLine
        }
    }
    return bestTrustLine;
}

// ── Трансляция строк ──

// ─── nearestTrustToCpp: trust_line → cpp_file, cpp_line ───
// Ищет exact match по trust_line, если нет — ближайший (≤) trust_line.
// Возвращает C++ строку с учётом cpp_line_inserted (если была вставлена преамбула).
std::optional<LineMapValue> TrustSource::nearestTrustToCpp(std::string_view trustFile, LineNumber trustLine) const {
    auto tPath = normalizePath(trustFile, base_directory_);
    for (const auto &entry : entries_) {
        if (entry.files.first != tPath)
            continue;
        const auto &idx = entry.trustToCppIndex;
        LineNumber cppLine = 0;
        // exact match
        auto it = idx.find(trustLine);
        if (it != idx.end()) {
            cppLine = it->second;
        } else {
            // nearest (≤) trustLine через upper_bound - 1
            auto ub = idx.upper_bound(trustLine);
            if (ub == idx.begin())
                return std::nullopt; // нет записей ≤ trustLine
            --ub;
            cppLine = ub->second;
        }
        cppLine += static_cast<LineNumber>(entry.cpp_line_inserted);
        return LineMapValue{entry.files.second, cppLine};
    }
    return std::nullopt;
}

// ─── nearestCppToTrust: cpp_line → trust_file, trust_line ───
// Обратный поиск через бинарный поиск по trustToCppIndex (монотонный по значению cpp_line)
std::optional<LineMapValue> TrustSource::nearestCppToTrust(std::string_view cppFile, LineNumber cppLine) const {
    auto cPath = normalizePath(cppFile, cpp_directory_);
    for (const auto &entry : entries_) {
        if (entry.files.second != cPath)
            continue;
        const auto &idx = entry.trustToCppIndex;
        LineNumber bestTrustLine = findNearestCppLine(idx, cppLine);
        if (bestTrustLine == 0)
            return std::nullopt;
        return LineMapValue{entry.files.first, bestTrustLine};
    }
    return std::nullopt;
}

// ── Трансляция переменных ──

std::optional<VarMapInfo> TrustSource::getCppVar(std::string_view trustFile, LineNumber trustLine, std::string_view trustVar) const {
    auto tPath = normalizePath(trustFile, base_directory_);
    std::string tv(trustVar);

    for (const auto &entry : entries_) {
        if (entry.files.first != tPath)
            continue;

        auto vit = entry.trustVarMapping.find(tv);
        if (vit == entry.trustVarMapping.end())
            return std::nullopt;

        const auto &[cppVar, lines] = vit->second;
        if (lines.first != trustLine)
            return std::nullopt;

        VarMapInfo info;
        info.files = entry.files;
        info.vars = {vit->first, cppVar};
        info.lines = lines;
        return info;
    }

    return std::nullopt;
}

std::optional<VarMapInfo> TrustSource::getTrustVar(std::string_view cppFile, LineNumber cppLine, std::string_view cppVar) const {
    auto cPath = normalizePath(cppFile, cpp_directory_);
    std::string cv(cppVar);

    for (const auto &entry : entries_) {
        if (entry.files.second != cPath)
            continue;

        auto [eqBegin, eqEnd] = entry.cppToTrustVar.equal_range(cv);
        if (eqBegin == eqEnd)
            return std::nullopt;

        // Пытаемся найти exact match по cpp_line
        for (auto eit = eqBegin; eit != eqEnd; ++eit) {
            const std::string &trustVar = eit->second;
            auto vit = entry.trustVarMapping.find(trustVar);
            if (vit == entry.trustVarMapping.end())
                continue;
            const auto &[cppVar_, lines] = vit->second;
            (void)cppVar_;
            if (lines.second + static_cast<LineNumber>(entry.cpp_line_inserted) == cppLine) {
                VarMapInfo info;
                info.files = entry.files;
                info.vars = {trustVar, cv};
                info.lines = lines;
                return info;
            }
        }

        // Exact match не найден — ищем nearest (≤) cpp_line с учётом смещения
        LineNumber adjCppLine = cppLine;
        if (adjCppLine > static_cast<LineNumber>(entry.cpp_line_inserted)) {
            adjCppLine -= static_cast<LineNumber>(entry.cpp_line_inserted);
        }
        LineNumber bestTrustLine = findNearestCppLine(entry.trustToCppIndex, adjCppLine);
        if (bestTrustLine == 0)
            return std::nullopt;

        for (auto eit = eqBegin; eit != eqEnd; ++eit) {
            const std::string &trustVar = eit->second;
            auto vit = entry.trustVarMapping.find(trustVar);
            if (vit == entry.trustVarMapping.end())
                continue;
            const auto &[cppVar_, lines] = vit->second;
            (void)cppVar_;
            if (lines.first == bestTrustLine) {
                VarMapInfo info;
                info.files = entry.files;
                info.vars = {trustVar, cv};
                info.lines = lines;
                return info;
            }
        }

        return std::nullopt;
    }

    return std::nullopt;
}

// ── Доступ к данным ──

const std::vector<FilePairEntry> &TrustSource::entries() const {
    return entries_;
}

// ═══════════════════════════════════════════
//               readElfSection (64-bit only)
// ═══════════════════════════════════════════

std::vector<unsigned char> TrustSource::readElfSection(const std::string &binaryPath, const std::string &sectionName) {
    std::ifstream file(binaryPath, std::ios::binary);
    if (!file.is_open())
        return {};

    char magic[4];
    file.read(magic, 4);
    if (magic[0] != 0x7f || magic[1] != 'E' || magic[2] != 'L' || magic[3] != 'F')
        return {};

    unsigned char class_byte;
    file.read(reinterpret_cast<char *>(&class_byte), 1);
    file.seekg(0, std::ios::beg);

    if (class_byte != 2)
        return {};

    struct ElfHeader64 {
        unsigned char e_ident[16];
        uint16_t e_type, e_machine;
        uint32_t e_version;
        uint64_t e_entry, e_phoff, e_shoff;
        uint32_t e_flags;
        uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
    } __attribute__((packed));

    struct ElfSectionHeader64 {
        uint32_t sh_name, sh_type;
        uint64_t sh_flags, sh_addr, sh_offset, sh_size;
        uint32_t sh_link, sh_info;
        uint64_t sh_addralign, sh_entsize;
    } __attribute__((packed));

    ElfHeader64 ehdr;
    file.read(reinterpret_cast<char *>(&ehdr), sizeof(ehdr));

    if (ehdr.e_shnum == 0)
        return {};

    std::vector<ElfSectionHeader64> shdrs(ehdr.e_shnum);
    file.seekg(ehdr.e_shoff, std::ios::beg);
    file.read(reinterpret_cast<char *>(shdrs.data()), ehdr.e_shnum * sizeof(ElfSectionHeader64));

    std::string section_name_table;
    if (ehdr.e_shstrndx < ehdr.e_shnum) {
        auto &strtab_hdr = shdrs[ehdr.e_shstrndx];
        section_name_table.resize(strtab_hdr.sh_size);
        file.seekg(strtab_hdr.sh_offset, std::ios::beg);
        file.read(&section_name_table[0], strtab_hdr.sh_size);
    }

    for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
        if (shdrs[i].sh_name >= section_name_table.size())
            continue;
        std::string name = section_name_table.c_str() + shdrs[i].sh_name;
        if (name == sectionName) {
            std::vector<unsigned char> data(shdrs[i].sh_size);
            file.seekg(shdrs[i].sh_offset, std::ios::beg);
            file.read(reinterpret_cast<char *>(data.data()), shdrs[i].sh_size);
            return data;
        }
    }

    return {};
}

// ═══════════════════════════════════════════
//               LoadFromBinary
// ═══════════════════════════════════════════

std::unique_ptr<const TrustSource> TrustSource::LoadFromBinary(const std::string &binaryPath, const std::string &mapPath) {
    auto sectionData = readElfSection(binaryPath, ".debug_trust_map");
    if (!sectionData.empty()) {
        auto ts = TrustSource::unpack(sectionData.data(), sectionData.size());
        if (ts) {
            std::cerr << "Loaded source map from ELF section" << std::endl;
            return ts;
        }
    }

    if (!mapPath.empty()) {
        std::ifstream file(mapPath, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize fsize = file.tellg();
            file.seekg(0, std::ios::beg);

            std::vector<unsigned char> buffer(static_cast<size_t>(fsize));
            if (file.read(reinterpret_cast<char *>(buffer.data()), fsize)) {
                auto ts = TrustSource::unpack(buffer.data(), buffer.size());
                if (ts) {
                    std::cerr << "Loaded source map from file: " << mapPath << std::endl;
                    return ts;
                }
            }
        }
    }

    std::cerr << "Warning: No source map found (tried ELF section and " << mapPath << ")" << std::endl;
    return nullptr;
}

// ═══════════════════════════════════════════
//               generateEmbeddedMapCode
// ═══════════════════════════════════════════

std::string TrustSource::generateEmbeddedMapCode(const std::vector<unsigned char> &mapData) {
    std::string code;
    code += "// === TRUST SOURCE MAP (embedded, msgpack) ===\n";
    code += "__attribute__((section(\".debug_trust_map\"), used))\n";
    code += "static const unsigned char debug_trust_map_data[] = {\n";
    for (size_t i = 0; i < mapData.size(); ++i) {
        if (i % 16 == 0)
            code += "    ";
        char buf[8];
        std::snprintf(buf, sizeof(buf), "0x%02x", mapData[i]);
        code += buf;
        if (i + 1 < mapData.size())
            code += ", ";
        if (i % 16 == 15)
            code += "\n";
    }
    if (mapData.size() % 16 != 0)
        code += "\n";
    code += "};\n";
    return code;
}

// ═══════════════════════════════════════════
//               writeMapFile
// ═══════════════════════════════════════════

bool TrustSource::writeMapFile(const std::vector<unsigned char> &mapData, const std::string &path) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;
    file.write(reinterpret_cast<const char *>(mapData.data()), mapData.size());
    file.close();
    return true;
}

} // namespace trust