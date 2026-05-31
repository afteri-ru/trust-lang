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

} // anonymous namespace

std::optional<std::vector<unsigned char>> readElfSection(const std::string& elfPath, const std::string& sectionName) {
    // Open ELF file
    std::ifstream file(elfPath, std::ios::binary);
    if (!file)
        return std::nullopt;

    // Read ELF header
    Elf64_Ehdr ehdr;
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    if (!file || file.gcount() != static_cast<std::streamsize>(sizeof(ehdr)))
        return std::nullopt;

    // Validate ELF magic
    if (memcmp(ehdr.e_ident, ELF_MAGIC, 4) != 0)
        return std::nullopt;

    // Validate 64-bit, little-endian
    if (ehdr.e_ident[4] != ELFCLASS64)
        return std::nullopt;
    if (ehdr.e_ident[5] != ELFDATA2LSB)
        return std::nullopt;

    // Validate section header table
    if (ehdr.e_shoff == 0 || ehdr.e_shnum == 0 || ehdr.e_shentsize != sizeof(Elf64_Shdr))
        return std::nullopt;

    // Read section header string table
    if (ehdr.e_shstrndx >= ehdr.e_shnum)
        return std::nullopt;

    std::vector<Elf64_Shdr> sections(ehdr.e_shnum);
    file.seekg(static_cast<std::streamoff>(ehdr.e_shoff));
    file.read(reinterpret_cast<char*>(sections.data()), static_cast<std::streamsize>(ehdr.e_shnum * sizeof(Elf64_Shdr)));
    if (!file)
        return std::nullopt;

    // Read section name string table
    const auto& shstrtab_hdr = sections[ehdr.e_shstrndx];
    std::vector<char> shstrtab(static_cast<size_t>(shstrtab_hdr.sh_size));
    file.seekg(static_cast<std::streamoff>(shstrtab_hdr.sh_offset));
    file.read(shstrtab.data(), static_cast<std::streamsize>(shstrtab_hdr.sh_size));
    if (!file)
        return std::nullopt;

    // Find the requested section
    for (const auto& shdr : sections) {
        if (shdr.sh_name >= shstrtab.size())
            continue;

        const char* name = shstrtab.data() + shdr.sh_name;
        if (name != sectionName)
            continue;

        // Read section data
        std::vector<unsigned char> section_data(static_cast<size_t>(shdr.sh_size));
        file.seekg(static_cast<std::streamoff>(shdr.sh_offset));
        file.read(reinterpret_cast<char*>(section_data.data()), static_cast<std::streamsize>(shdr.sh_size));
        if (!file)
            return std::nullopt;

        return section_data;
    }

    // Section not found
    return std::nullopt;
}

} // namespace trust::utils