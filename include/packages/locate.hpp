#pragma once

// include/packages/locate.hpp
// Placement of a module inside a package tree: resolve a semantic module path to a
// single entry. Any ambiguity is an error (no silent choice).

#include "packages/package.hpp"

#include <string>
#include <string_view>

namespace trust::packages {

/// A located entry.
struct LocatedEntry {
    std::string relative; ///< physical path relative to the tree root
    EntryKind kind = EntryKind::Source;
};

/// Resolves a module `path` relative to `base` within the tree.
/// Candidates: `<base>/<path>.src`, `<base>/<path>.trust`, `<base>/<path>/` (a directory).
/// Absence and ambiguity (more than one candidate) are errors.
[[nodiscard]] LocatedEntry locate(const SourceTree& tree, std::string_view base, std::string_view path);

} // namespace trust::packages
