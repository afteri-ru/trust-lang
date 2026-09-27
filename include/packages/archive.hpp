#pragma once

// include/packages/archive.hpp
// `.trust` package format: a deterministic tar+zstd archive of source entries.
// Packing is reproducible: entries are sorted, mtime=0, uid/gid=0, fixed permission
// bits and empty user/group names; the output archive is never embedded into itself.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace trust::packages {

/// Deterministically packs the tree rooted at `root` into a `.trust` archive at `out`.
/// Returns the lowercase-hex sha256 of the produced archive (integrity).
[[nodiscard]] std::string packArchive(const std::filesystem::path& root, const std::filesystem::path& out);

/// Relative (forward-slash) paths of the regular-file entries, in archive order.
[[nodiscard]] std::vector<std::string> listEntries(const std::filesystem::path& archive);

/// Reads one regular-file entry into memory.
[[nodiscard]] std::string readArchiveEntry(const std::filesystem::path& archive, std::string_view entry);

/// Extracts the archive into `dest` (created if needed). Rejects absolute paths,
/// `..` components and non-regular entries.
void unpackArchive(const std::filesystem::path& archive, const std::filesystem::path& dest);

} // namespace trust::packages
