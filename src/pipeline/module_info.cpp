// src/pipeline/module_info.cpp
// Команда --module-info: вывод экспортного интерфейса предкомпилированного пакета (.pcp).
// Trust-интерфейс (<stem>.src) читается из архива .pcp через packages::readArchiveEntry -
// БЕЗ ELF/dlopen (семантику несёт интерфейс, а не ELF-секция).
#include "pipeline/module_info.hpp"
#include "module_loader/module_export.hpp"
#include "packages/archive.hpp"
#include "packages/errors.hpp"
#include "packages/manifest.hpp"
#include "packages/package.hpp"
#include "session/context.hpp"
#include "syntax/parser.h"
#include "utils/io.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>
namespace trust {

namespace {

// Разбить текст Trust-интерфейса на непустые строки-декларации (trim по краям).
std::vector<std::string> interfaceDecls(std::string_view text) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        std::string_view line = (nl == std::string_view::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
        std::size_t b = 0;
        std::size_t e = line.size();
        while (b < e && std::isspace(static_cast<unsigned char>(line[b])) != 0) {
            ++b;
        }
        while (e > b && std::isspace(static_cast<unsigned char>(line[e - 1])) != 0) {
            --e;
        }
        if (e > b) {
            out.emplace_back(line.substr(b, e - b));
        }
        if (nl == std::string_view::npos) {
            break;
        }
        pos = nl + 1;
    }
    return out;
}

// Найти Trust-интерфейс в .pcp: предпочтительно <stem>.src (имя пакета == имя модуля),
// иначе - единственный .src в архиве. Несколько кандидатов без совпадения по имени - ошибка.
std::string findInterfaceEntry(const std::string& pcp_path, const std::string& stem) {
    const std::vector<std::string> entries = packages::listEntries(pcp_path);
    const std::string preferred = stem + ".src";
    std::vector<std::string> src_entries;
    for (const std::string& e : entries) {
        if (e.size() >= 4 && e.compare(e.size() - 4, 4, ".src") == 0) {
            if (e == preferred) {
                return e;
            }
            src_entries.push_back(e);
        }
    }
    if (src_entries.size() == 1) {
        return src_entries.front();
    }
    packages::fail("packages.module_info.ambiguous", "'{}' contains {} interface modules; use a .pcp whose name matches the module ('{}.src' not found)", pcp_path,
                   src_entries.size(), stem);
}

} // namespace

// -- --module-info: show module interface and version from a .pcp package --
int showModuleInfo(Context& ctx, const std::string& module_path, const std::string& masks, bool verbose) {
    const std::string stem = std::filesystem::path(module_path).stem().string();
    std::string interface_text;
    std::string version;
    try {
        const std::string entry = findInterfaceEntry(module_path, stem);
        interface_text = packages::readArchiveEntry(module_path, entry);
        const packages::Manifest manifest = packages::parseManifest(nlohmann::json::parse(packages::readArchiveEntry(module_path, packages::kManifestFileName)));
        version = manifest.version.empty() ? std::string("0.0.0") : manifest.version;
    } catch (const packages::PackageException& e) {
        trust::errs() << "error: " << e.what() << " (expected a .pcp precompiled package)\n";
        return 1;
    } catch (const nlohmann::json::exception& e) {
        trust::errs() << "error: invalid package manifest in '" << module_path << "': " << e.what() << "\n";
        return 1;
    }

    // Валидация: декларации обязаны быть валидным Trust-текстом (восстановление парсером языка).
    Parser parser(ctx);
    parser.ParseText(interface_text, "@pcp:" + stem, /*expand_module=*/false);
    if (ctx.diag().errorCount() > 0) {
        trust::errs() << "error: invalid Trust interface in .pcp '" << module_path << "'\n";
        return 1;
    }

    const std::vector<std::string> decls = filterModuleDecls(interfaceDecls(interface_text), masks);
    trust::outs() << "version: " << version << "\n";
    trust::outs() << "exports: " << decls.size() << "\n";
    for (const auto& decl : decls) {
        trust::outs() << "  " << decl << "\n";
    }
    if (verbose) {
        trust::errs() << "info: module interface read from " << module_path << " (.pcp)\n";
    }
    return 0;
}
} // namespace trust

