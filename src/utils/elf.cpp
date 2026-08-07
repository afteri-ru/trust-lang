#include "utils/elf.hpp"

#include <cstring>
#include <cstdint>
#include <fstream>
#include <vector>

namespace trust::utils {

namespace {

// ELF64 structures
struct Elf64_Ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64_Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

// ELF magic
constexpr unsigned char ELF_MAGIC[] = {0x7f, 'E', 'L', 'F'};
constexpr int ELFCLASS64 = 2;
constexpr int ELFDATA2LSB = 1;

// ar archive magic (System V / GNU). The first 8 bytes of the file.
constexpr char AR_MAGIC[] = "!<arch>\n";
// Length of a single ar member header.
constexpr std::size_t AR_HEADER_SIZE = 60;

// Parses an ELF64 section directly from a byte buffer (used both for whole
// files and for individual members of an ar archive).
std::optional<std::vector<unsigned char>> parseElfSection(const unsigned char* data, std::size_t size, const std::string& sectionName) {
    if (size < sizeof(Elf64_Ehdr)) {
        return std::nullopt;
    }

    const auto* ehdr = reinterpret_cast<const Elf64_Ehdr*>(data);

    // Validate ELF magic
    if (memcmp(ehdr->e_ident, ELF_MAGIC, 4) != 0) {
        return std::nullopt;
    }

    // Validate 64-bit, little-endian
    if (ehdr->e_ident[4] != ELFCLASS64) {
        return std::nullopt;
    }
    if (ehdr->e_ident[5] != ELFDATA2LSB) {
        return std::nullopt;
    }

    // Validate section header table
    if (ehdr->e_shoff == 0 || ehdr->e_shnum == 0 || ehdr->e_shentsize != sizeof(Elf64_Shdr)) {
        return std::nullopt;
    }

    // Section header string table index
    if (ehdr->e_shstrndx >= ehdr->e_shnum) {
        return std::nullopt;
    }

    // Bounds-check the section header table against the buffer.
    std::size_t sections_bytes = static_cast<std::size_t>(ehdr->e_shnum) * sizeof(Elf64_Shdr);
    std::size_t shoff = static_cast<std::size_t>(ehdr->e_shoff);
    if (shoff > size || sections_bytes > size - shoff) {
        return std::nullopt;
    }

    const auto* sections = reinterpret_cast<const Elf64_Shdr*>(data + shoff);

    // Section name string table.
    const Elf64_Shdr& shstrtab_hdr = sections[ehdr->e_shstrndx];
    std::size_t shstr_off = static_cast<std::size_t>(shstrtab_hdr.sh_offset);
    std::size_t shstr_size = static_cast<std::size_t>(shstrtab_hdr.sh_size);
    if (shstr_off > size || shstr_size > size - shstr_off) {
        return std::nullopt;
    }
    const char* shstrtab = reinterpret_cast<const char*>(data + shstr_off);

    // Find the requested section
    for (uint16_t i = 0; i < ehdr->e_shnum; ++i) {
        const Elf64_Shdr& shdr = sections[i];
        if (shdr.sh_name >= shstr_size) {
            continue;
        }

        const char* name = shstrtab + shdr.sh_name;
        if (name != sectionName) {
            continue;
        }

        // Bounds-check and read section data.
        std::size_t off = static_cast<std::size_t>(shdr.sh_offset);
        std::size_t sz = static_cast<std::size_t>(shdr.sh_size);
        if (off > size || sz > size - off) {
            return std::nullopt;
        }
        return std::vector<unsigned char>(data + off, data + off + sz);
    }

    // Section not found
    return std::nullopt;
}

} // anonymous namespace

std::optional<std::vector<unsigned char>> readElfSectionFromBuffer(const std::vector<unsigned char>& elfData, const std::string& sectionName) {
    if (elfData.empty()) {
        return std::nullopt;
    }
    return parseElfSection(elfData.data(), elfData.size(), sectionName);
}

std::optional<std::vector<unsigned char>> readElfSection(const std::string& elfPath, const std::string& sectionName) {
    // Open ELF file
    std::ifstream file(elfPath, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad()) {
        return std::nullopt;
    }
    return parseElfSection(data.data(), data.size(), sectionName);
}

std::optional<std::vector<unsigned char>> readSectionFromLibrary(const std::string& libraryPath, const std::string& sectionName) {
    // Open library file
    std::ifstream file(libraryPath, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad()) {
        return std::nullopt;
    }

    // Fast path: a plain ELF file (.so / .o).
    if (data.size() >= 4 && memcmp(data.data(), ELF_MAGIC, 4) == 0) {
        return parseElfSection(data.data(), data.size(), sectionName);
    }

    // ar archive (.a): iterate regular members; each member that is an ELF object
    // is scanned for the requested section.
    if (data.size() >= AR_HEADER_SIZE && memcmp(data.data(), AR_MAGIC, sizeof(AR_MAGIC) - 1) == 0) {
        std::size_t pos = sizeof(AR_MAGIC) - 1; // skip the magic line
        while (pos + AR_HEADER_SIZE <= data.size()) {
            const char* hdr = reinterpret_cast<const char*>(data.data() + pos);

            // The name field (0..15) is the member name. Members whose name starts
            // with '/' are either special (symbol table `/`/`/SYM64/`, long-name
            // table `//`) or regular members with long names referenced as `/NNN`
            // (offset into the long-name table). Only the former must be skipped;
            // a `/NNN` member is still a regular ELF object to scan.
            const char name0 = hdr[0];
            const bool nameIsLongRef = (name0 == '/' && hdr[1] >= '0' && hdr[1] <= '9');
            const bool special = (name0 == '/' && !nameIsLongRef) || name0 == ' ' || name0 == '\0';

            // Size field: bytes 48..57, decimal, space-padded.
            std::size_t member_size = 0;
            for (int i = 0; i < 10; ++i) {
                char c = hdr[48 + i];
                if (c >= '0' && c <= '9') {
                    member_size = member_size * 10 + static_cast<std::size_t>(c - '0');
                }
            }

            pos += AR_HEADER_SIZE;
            if (member_size > data.size() - pos) {
                return std::nullopt; // truncated member
            }

            // Only regular ELF members can carry the requested section; skip the
            // special members (symbol table, long-name table) and non-ELF members.
            if (!special && member_size >= 4 && memcmp(data.data() + pos, ELF_MAGIC, 4) == 0) {
                if (auto section = parseElfSection(data.data() + pos, member_size, sectionName)) {
                    return section;
                }
            }

            // Members are padded to even offsets.
            pos += member_size;
            if (pos < data.size() && data[pos] == '\n') {
                ++pos;
            }
        }
    }

    return std::nullopt;
}

} // namespace trust::utils
