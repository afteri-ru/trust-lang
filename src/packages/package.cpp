// src/packages/package.cpp
#include "packages/package.hpp"

#include "packages/errors.hpp"

#include <set>

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

bool hasSuffix(std::string_view name, std::string_view suffix) {
    return name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string withoutSuffix(std::string_view name, std::string_view suffix) {
    return std::string(name.substr(0, name.size() - suffix.size()));
}

/// Direct child names (files and directories) of `dir`, sorted and unique.
std::vector<std::string> directChildren(const SourceTree& tree, std::string_view dir) {
    std::set<std::string> names;
    const std::string prefix = dir.empty() ? std::string() : std::string(dir) + "/";
    for (const std::string& entry : tree.listRecursive()) {
        const std::string_view path(entry);
        if (!prefix.empty() && path.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        const std::string_view rest = path.substr(prefix.size());
        const std::size_t slash = rest.find('/');
        names.insert(std::string(slash == std::string_view::npos ? rest : rest.substr(0, slash)));
    }
    return {names.begin(), names.end()};
}

void walk(const SourceTree& tree, std::string_view dir, std::string_view prefix, Package& pkg) {
    for (const std::string& child : directChildren(tree, dir)) {
        const std::string full = join(dir, child);
        if (tree.isDirectory(full)) {
            if (isPackageRoot(tree, full)) {
                pkg.entries.push_back(PackageEntry{join(prefix, child), full, EntryKind::Package});
            } else {
                walk(tree, full, join(prefix, child), pkg);
            }
        } else if (hasSuffix(child, ".src")) {
            pkg.entries.push_back(PackageEntry{join(prefix, withoutSuffix(child, ".src")), full, EntryKind::Source});
        } else if (hasSuffix(child, ".trust")) {
            pkg.entries.push_back(PackageEntry{join(prefix, withoutSuffix(child, ".trust")), full, EntryKind::Package});
        } else if (hasSuffix(child, ".pcp")) {
            // Precompiled package: same package layout as `.trust`, each module is an interface.
            pkg.entries.push_back(PackageEntry{join(prefix, withoutSuffix(child, ".pcp")), full, EntryKind::Package});
        }
    }
}

} // namespace

bool isPackageRoot(const SourceTree& tree, std::string_view dir) {
    return tree.isRegularFile(join(dir, kManifestFileName));
}

Package discoverPackage(const SourceTree& tree, std::string_view dir) {
    Package pkg;
    pkg.root = std::string(dir);
    walk(tree, dir, "", pkg);
    return pkg;
}

} // namespace trust::packages
