#pragma once

// include/packages/package.hpp
// Package model: a package is a source-text composition. Its boundary is marked by a
// manifest file; a nested directory with its own manifest is a nested package and keeps
// its own addressing. A `*.trust` archive is likewise a nested package entry.

#include "packages/source_tree.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace trust::packages {

/// Manifest file name marking a package root (single source for the whole project).
inline constexpr const char* kManifestFileName = "trust.pkg.json";

/// Kind of a package entry.
enum class EntryKind {
    Source,  ///< a source module file (`*.src`)
    Package, ///< a nested package (a directory with a manifest, or a `*.trust` archive)
};

/// A direct entry of a package.
struct PackageEntry {
    std::string path;     ///< semantic path relative to the package root (no extension)
    std::string relative; ///< physical path relative to the tree root (with extension)
    EntryKind kind = EntryKind::Source;
};

/// A package: its direct entries. Nested packages are entries of kind `Package`
/// (recursion is on demand via `discoverPackage` on the entry's `relative` path).
struct Package {
    std::string root;                  ///< package root relative to the tree ("" = tree root)
    std::vector<PackageEntry> entries; ///< direct source modules and nested packages/archives
};

/// True if `dir` (relative to the tree) is a package root (manifest present).
[[nodiscard]] bool isPackageRoot(const SourceTree& tree, std::string_view dir);

/// Discovers the package rooted at `dir`. Modules of plain (manifest-less) subdirectories
/// are included with their sub-path; a manifest-marked subdirectory becomes a `Package` entry.
[[nodiscard]] Package discoverPackage(const SourceTree& tree, std::string_view dir);

} // namespace trust::packages
