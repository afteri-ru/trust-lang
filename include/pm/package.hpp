#pragma once

// include/pm/package.hpp
// `.trust` package: a deterministic tar+zstd archive containing only `*.src`
// files and/or nested packages, plus the manifest and a human-readable README.
// Packing is reproducible: entries are sorted, mtime=0, uid/gid=0, fixed
// permission bits and empty user/group names.

#include <filesystem>
#include <string>
#include <vector>

namespace trust::pm {

/// Deterministically packs the tree rooted at `root` into a `.trust` archive.
/// Throws PmException("pm.package.pack") on failure.
void packPackage(const std::filesystem::path& root, const std::filesystem::path& out);

/// Lists the regular-file entry paths of a `.trust` archive (relative, forward
/// slashes, archive order). Throws PmException("pm.package.read") on failure.
[[nodiscard]] std::vector<std::string> listPackage(const std::filesystem::path& archive);

/// Extracts a `.trust` archive into `dest` (created if needed).
/// Rejects absolute paths, `..` components and non-regular entries.
void extractPackage(const std::filesystem::path& archive, const std::filesystem::path& dest);

/// Reads a single regular-file entry into memory.
[[nodiscard]] std::string readPackageEntry(const std::filesystem::path& archive, const std::string& entry);

} // namespace trust::pm
