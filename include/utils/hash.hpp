#pragma once

// include/utils/hash.hpp
// Integrity primitive: lowercase-hex SHA-256 over bytes and files (llvm::SHA256).
// Lives in the lowest layer so both the packages component and the package manager
// (trust-pm) reuse a single implementation.

#include <filesystem>
#include <string>
#include <string_view>

namespace trust::utils {

/// Lowercase hex sha256 of a byte range.
[[nodiscard]] std::string sha256Hex(std::string_view data);

/// Lowercase hex sha256 of a file (streamed). Throws std::runtime_error (FAULT)
/// if the file cannot be read.
[[nodiscard]] std::string sha256File(const std::filesystem::path& file);

} // namespace trust::utils
