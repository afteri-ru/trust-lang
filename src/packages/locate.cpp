// src/packages/locate.cpp
#include "packages/locate.hpp"

#include "packages/errors.hpp"
#include "packages/module_path.hpp"

#include <vector>

namespace trust::packages {
namespace {

std::string join(std::string_view dir, std::string_view name) {
    if (dir.empty()) {
        return std::string(name);
    }
    std::string out(dir);
    out.push_back('/');
    out.append(name);
    return out;
}

} // namespace

LocatedEntry locate(const SourceTree& tree, std::string_view base, std::string_view path) {
    const std::string norm = normalizeModulePath(path);

    std::vector<LocatedEntry> found;
    const std::string src = join(base, norm + ".src");
    const std::string archive = join(base, norm + ".trust");
    const std::string directory = join(base, norm);
    if (tree.isRegularFile(src)) {
        found.push_back(LocatedEntry{src, EntryKind::Source});
    }
    if (tree.isRegularFile(archive)) {
        found.push_back(LocatedEntry{archive, EntryKind::Package});
    }
    if (tree.isDirectory(directory)) {
        found.push_back(LocatedEntry{directory, EntryKind::Package});
    }

    if (found.empty()) {
        fail("packages.locate.missing", "module not found: '{}'", norm);
    }
    if (found.size() > 1) {
        fail("packages.locate.ambiguous", "module '{}' matches {} entries (no silent choice)", norm, found.size());
    }
    return found.front();
}

} // namespace trust::packages
