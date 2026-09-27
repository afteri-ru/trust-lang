// test/pcp/integration.cpp
// Интеграционный тест предкомпилированного пакета (.pcp):
//  * `trust -m -o X.pcp` создаёт именно .pcp (сам пакет, без динамической библиотеки);
//  * Trust-интерфейс <stem>.src перечисляет нативные символы (%<asm-name>), и КАЖДЫЙ из них
//    реально ОПРЕДЕЛЁН в упакованном <stem>.o (эмиссия символов эквивалентна объектному коду).
#include "packages/archive.hpp"
#include "packages/errors.hpp"

#include <elf.h>

#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void expect(bool cond, const std::string& message) {
    if (!cond) {
        std::cerr << "FAIL: " << message << "\n";
        ++g_failures;
    }
}

// Извлечь все %<symbol> из строк интерфейса вида `<decl> := %<symbol>;`.
std::vector<std::string> interfaceSymbols(const std::string& text) {
    std::vector<std::string> out;
    const std::string marker = ":= %";
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::string line = (nl == std::string::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
        const std::size_t p = line.find(marker);
        if (p != std::string::npos) {
            std::size_t s = p + marker.size();
            std::size_t e = s;
            while (e < line.size() && (std::isalnum(static_cast<unsigned char>(line[e])) != 0 || line[e] == '_')) {
                ++e;
            }
            if (e > s) {
                out.push_back(line.substr(s, e - s));
            }
        }
        if (nl == std::string::npos) {
            break;
        }
        pos = nl + 1;
    }
    return out;
}

// Определённые (st_shndx != SHN_UNDEF) имена из .symtab ELF64-объекта.
std::vector<std::string> objectDefinedSymbols(const std::vector<unsigned char>& data) {
    std::vector<std::string> out;
    if (data.size() < sizeof(Elf64_Ehdr)) {
        return out;
    }
    const auto* eh = reinterpret_cast<const Elf64_Ehdr*>(data.data());
    if (std::memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS64) {
        return out;
    }
    if (eh->e_shoff == 0 || eh->e_shnum == 0 ||
        eh->e_shoff + static_cast<std::uint64_t>(eh->e_shnum) * sizeof(Elf64_Shdr) > data.size()) {
        return out;
    }
    const auto* sh = reinterpret_cast<const Elf64_Shdr*>(data.data() + eh->e_shoff);
    for (int i = 0; i < eh->e_shnum; ++i) {
        if (sh[i].sh_type != SHT_SYMTAB) {
            continue;
        }
        if (sh[i].sh_offset + sh[i].sh_size > data.size() || sh[i].sh_link >= static_cast<std::uint32_t>(eh->e_shnum)) {
            continue;
        }
        const auto* symtab = reinterpret_cast<const Elf64_Sym*>(data.data() + sh[i].sh_offset);
        const std::size_t count = sh[i].sh_size / sizeof(Elf64_Sym);
        const Elf64_Shdr& strsec = sh[sh[i].sh_link];
        if (strsec.sh_offset + strsec.sh_size > data.size()) {
            continue;
        }
        const char* strtab = reinterpret_cast<const char*>(data.data() + strsec.sh_offset);
        for (std::size_t j = 0; j < count; ++j) {
            const Elf64_Sym& s = symtab[j];
            if (s.st_shndx == SHN_UNDEF || s.st_name == 0 || s.st_name >= strsec.sh_size) {
                continue;
            }
            out.emplace_back(strtab + s.st_name);
        }
    }
    return out;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (const std::string& e : v) {
        if (e == s) {
            return true;
        }
    }
    return false;
}

} // namespace


int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: pcp_integration <trust-binary> <workdir>\n";
        return 2;
    }
    const std::string trust = argv[1];
    const fs::path work(argv[2]);
    std::error_code ec;
    fs::create_directories(work, ec);

    const fs::path src = work / "pkgmod.src";
    {
        std::ofstream o(src);
        o << "f(a:Int32):Bool := { return a > 0; };\n";
        o << "x:Int32 := 42;\n";
        o << ":V ::= :Class{ v: Int32 := _; };\n";
        o << "`==`(a: V, b: V):Bool := { return true; };\n";
    }
    const fs::path pcp = work / "pkgmod.pcp";
    const std::string cmd = trust + " -q --temp-dir " + work.string() + " -m -o " + pcp.string() + " " + src.string();
    const int rc = std::system(cmd.c_str());
    expect(rc == 0, "trust -m failed (rc=" + std::to_string(rc) + ")");
    expect(fs::is_regular_file(pcp, ec), ".pcp was not created at -o path: " + pcp.string());

    std::vector<std::string> iface_syms;
    std::vector<std::string> obj_syms;
    try {
        const std::string iface = trust::packages::readArchiveEntry(pcp, "pkgmod.src");
        iface_syms = interfaceSymbols(iface);
        const std::string raw = trust::packages::readArchiveEntry(pcp, "pkgmod.o");
        obj_syms = objectDefinedSymbols(std::vector<unsigned char>(raw.begin(), raw.end()));
    } catch (const trust::packages::PackageException& e) {
        expect(false, std::string("cannot read .pcp: ") + e.what());
    }

    expect(!iface_syms.empty(), "interface lists no native symbols");
    bool saw_fn = false;
    bool saw_var = false;
    bool saw_op = false;
    for (const std::string& s : iface_syms) {
        saw_fn = saw_fn || s.rfind("__trust_fn_", 0) == 0;
        saw_var = saw_var || s.rfind("__trust_var_", 0) == 0;
        saw_op = saw_op || s.rfind("__trust_op_", 0) == 0;
    }
    expect(saw_fn, "interface is missing a function symbol (__trust_fn_*)");
    expect(saw_var, "interface is missing a variable symbol (__trust_var_*)");
    expect(saw_op, "interface is missing an operator symbol (__trust_op_*)");

    for (const std::string& s : iface_syms) {
        expect(contains(obj_syms, s), "interface symbol not defined in .o: " + s);
    }

    if (g_failures == 0) {
        std::cout << "pcp symbol equivalence OK: " << iface_syms.size() << " interface symbols defined in .o\n";
    }
    return g_failures == 0 ? 0 : 1;
}

