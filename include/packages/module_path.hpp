#pragma once

// include/packages/module_path.hpp
// Semantic module path: a normalized, storage-independent module identity.
// The source-level separator '\' is normalized to '/'.

#include <string>
#include <string_view>

namespace trust::packages {

/// Normalizes a semantic module path:
///  - '\' (the source-level separator) becomes '/';
///  - absolute paths are rejected;
///  - empty segments, "." and ".." are rejected (no silent choice).
/// Returns the normalized path; throws PackageException("packages.path.invalid") otherwise.
[[nodiscard]] std::string normalizeModulePath(std::string_view path);

} // namespace trust::packages
