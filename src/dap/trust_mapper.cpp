#include "trust_mapper.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <filesystem>

using json = nlohmann::json;

// ELF header structures
struct ElfHeader32 {
    uint32_t ei_magic, ei_class, ei_data, ei_version;
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint32_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
};

struct ElfSectionHeader32 {
    uint32_t sh_name, sh_type;
    uint32_t sh_flags, sh_addr, sh_offset, sh_size;
    uint32_t sh_link, sh_info;
    uint32_t sh_addralign, sh_entsize;
};

struct ElfHeader64 {
    unsigned char e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
};

struct ElfSectionHeader64 {
    uint32_t sh_name, sh_type;
    uint64_t sh_flags, sh_addr, sh_offset, sh_size;
    uint32_t sh_link, sh_info;
    uint64_t sh_addralign, sh_entsize;
};

TrustMapper::TrustMapper() {}

bool TrustMapper::parseJson(const std::string& jsonStr) {
    try {
        auto j = json::parse(jsonStr);
        
        if (!j.contains("sources") || !j["sources"].is_array()) {
            return false;
        }
        
        for (const auto& source : j["sources"]) {
            TrustMapEntry entry;
            
            if (source.contains("trust_file")) {
                entry.trust_file = source["trust_file"].get<std::string>();
            }
            if (source.contains("cpp_file")) {
                entry.cpp_file = source["cpp_file"].get<std::string>();
            }
            
            if (source.contains("mappings") && source["mappings"].is_array()) {
                for (const auto& mapping : source["mappings"]) {
                    TrustMappingEntry m;
                    if (mapping.contains("trust_line")) {
                        m.trust_line = mapping["trust_line"].get<int>();
                    }
                    if (mapping.contains("cpp_line")) {
                        m.cpp_line = mapping["cpp_line"].get<int>();
                    }
                    if (mapping.contains("trust_vars") && mapping["trust_vars"].is_array()) {
                        for (const auto& v : mapping["trust_vars"]) {
                            m.trust_vars.push_back(v.get<std::string>());
                        }
                    }
                    if (mapping.contains("cpp_vars") && mapping["cpp_vars"].is_array()) {
                        for (const auto& v : mapping["cpp_vars"]) {
                            m.cpp_vars.push_back(v.get<std::string>());
                        }
                    }
                    
                    if (m.trust_line > 0 && m.cpp_line > 0) {
                        entry.mappings.push_back(m);
                    }
                }
            }
            
            if (!entry.trust_file.empty() && !entry.mappings.empty()) {
                entries_.push_back(entry);
            }
        }
        
        return !entries_.empty();
    } catch (const json::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        return false;
    }
}

bool TrustMapper::loadFromElfSection(const std::string& binaryPath) {
    std::ifstream file(binaryPath, std::ios::binary);
    if (!file.is_open()) return false;
    
    // Check ELF magic
    char magic[4];
    file.read(magic, 4);
    if (magic[0] != 0x7f || magic[1] != 'E' || magic[2] != 'L' || magic[3] != 'F') {
        return false;
    }
    
    // Determine 32 or 64 bit
    unsigned char class_byte;
    file.read(reinterpret_cast<char*>(&class_byte), 1);
    file.seekg(0, std::ios::beg);
    
    std::string section_name_table;
    std::string json_data;
    
    if (class_byte == 2) { // 64-bit
        ElfHeader64 ehdr;
        file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
        
        if (ehdr.e_shnum == 0) return false;
        
        // Read section headers
        std::vector<ElfSectionHeader64> shdrs(ehdr.e_shnum);
        file.seekg(ehdr.e_shoff, std::ios::beg);
        file.read(reinterpret_cast<char*>(shdrs.data()), ehdr.e_shnum * sizeof(ElfSectionHeader64));
        
        // Read section name string table (.shstrndx)
        if (ehdr.e_shstrndx < ehdr.e_shnum) {
            auto& strtab_hdr = shdrs[ehdr.e_shstrndx];
            section_name_table.resize(strtab_hdr.sh_size);
            file.seekg(strtab_hdr.sh_offset, std::ios::beg);
            file.read(&section_name_table[0], strtab_hdr.sh_size);
        }
        
        // Find .trust_map section
        for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
            std::string name = section_name_table.c_str() + shdrs[i].sh_name;
            if (name == ".trust_map") {
                json_data.resize(shdrs[i].sh_size);
                file.seekg(shdrs[i].sh_offset, std::ios::beg);
                file.read(&json_data[0], shdrs[i].sh_size);
                
                // Null-terminate
                size_t null_pos = json_data.find('\0');
                if (null_pos != std::string::npos) {
                    json_data = json_data.substr(0, null_pos);
                }
                break;
            }
        }
    } else {
        // 32-bit - similar logic
        ElfHeader32 ehdr;
        file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
        
        if (ehdr.e_shnum == 0) return false;
        
        std::vector<ElfSectionHeader32> shdrs(ehdr.e_shnum);
        file.seekg(ehdr.e_shoff, std::ios::beg);
        file.read(reinterpret_cast<char*>(shdrs.data()), ehdr.e_shnum * sizeof(ElfSectionHeader32));
        
        if (ehdr.e_shstrndx < ehdr.e_shnum) {
            auto& strtab_hdr = shdrs[ehdr.e_shstrndx];
            section_name_table.resize(strtab_hdr.sh_size);
            file.seekg(strtab_hdr.sh_offset, std::ios::beg);
            file.read(&section_name_table[0], strtab_hdr.sh_size);
        }
        
        for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
            std::string name = section_name_table.c_str() + shdrs[i].sh_name;
            if (name == ".trust_map") {
                json_data.resize(shdrs[i].sh_size);
                file.seekg(shdrs[i].sh_offset, std::ios::beg);
                file.read(&json_data[0], shdrs[i].sh_size);
                
                size_t null_pos = json_data.find('\0');
                if (null_pos != std::string::npos) {
                    json_data = json_data.substr(0, null_pos);
                }
                break;
            }
        }
    }
    
    file.close();
    
    if (json_data.empty()) return false;
    return parseJson(json_data);
}

bool TrustMapper::loadFromJsonFile(const std::string& mapPath) {
    std::ifstream file(mapPath);
    if (!file.is_open()) return false;
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string jsonStr = buffer.str();
    
    return parseJson(jsonStr);
}

void TrustMapper::load(const std::string& binaryPath, const std::string& mapPath) {
    if (!loadFromElfSection(binaryPath)) {
        if (!loadFromJsonFile(mapPath)) {
            std::cerr << "Warning: No source map found (tried ELF section and " << mapPath << ")" << std::endl;
        } else {
            std::cerr << "Loaded source map from JSON file: " << mapPath << std::endl;
        }
    } else {
        std::cerr << "Loaded source map from ELF section" << std::endl;
    }
}

std::pair<std::string, int> TrustMapper::trustToCpp(const std::string& trustFile, int trustLine) {
    // Нормализуем входной путь для сравнения
    std::string normalizedInput = std::filesystem::weakly_canonical(trustFile).string();
    std::string inputFilename = std::filesystem::path(trustFile).filename().string();
    
    for (const auto& entry : entries_) {
        // Пробуем точное совпадение, затем по имени файла
        bool match = (entry.trust_file == trustFile) ||
                     (std::filesystem::weakly_canonical(entry.trust_file).string() == normalizedInput) ||
                     (std::filesystem::path(entry.trust_file).filename().string() == inputFilename);
        
        if (match) {
            for (const auto& m : entry.mappings) {
                if (m.trust_line == trustLine) {
                    return {entry.cpp_file, m.cpp_line};
                }
            }
        }
    }
    return {"", -1};
}

std::pair<std::string, int> TrustMapper::cppToTrust(const std::string& cppFile, int cppLine) {
    for (const auto& entry : entries_) {
        if (entry.cpp_file == cppFile) {
            // Find nearest mapping (highest cpp_line <= cppLine)
            int bestTrustLine = -1;
            int nearestCpp = -1;
            
            for (const auto& m : entry.mappings) {
                if (m.cpp_line <= cppLine && m.cpp_line >= nearestCpp) {
                    nearestCpp = m.cpp_line;
                    bestTrustLine = m.trust_line;
                }
            }
            
            if (bestTrustLine > 0) {
                return {entry.trust_file, bestTrustLine};
            }
        }
    }
    return {cppFile, cppLine}; // fallback: no mapping
}

std::vector<std::string> TrustMapper::getTrustVars(const std::string& cppFile, int cppLine,
                                                    const std::vector<std::string>& cppVars) {
    std::vector<std::string> result;
    for (const auto& entry : entries_) {
        if (entry.cpp_file == cppFile) {
            for (const auto& m : entry.mappings) {
                if (m.cpp_line == cppLine) {
                    // Map cpp_vars to their trust counterparts
                    for (const auto& v : cppVars) {
                        bool found = false;
                        for (size_t i = 0; i < m.cpp_vars.size(); i++) {
                            if (m.cpp_vars[i] == v && i < m.trust_vars.size()) {
                                result.push_back(m.trust_vars[i]);
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            result.push_back(v); // keep original if no mapping
                        }
                    }
                    return result;
                }
            }
            break;
        }
    }
    return cppVars;
}