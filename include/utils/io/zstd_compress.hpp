#pragma once
/// @file zstd_compress.hpp
/// Zstd compression helpers with per-buffer trained dictionary.
/// Replaces the previous zlib-based compression in serialization.
///
/// Format: [orig_size:LE4][dict_size:LE4][dictionary][zstd_compressed][md5_checksum:LE8]

#include <vector>
#include <cstdint>
#include <cstddef>

namespace trust::detail {

/// Maximum dictionary size for training.
constexpr size_t kZstdMaxDictSize = 128 * 1024;

/// Zstd compression level (22 = maximum, speed doesn't matter).
constexpr int kZstdCompressionLevel = 22;

/// Compress data using Zstd with a per-buffer trained dictionary.
/// Returns vector in format: [orig_size:LE4][dict_size:LE4][dictionary][compressed][MD5:LE8]
/// On failure returns empty vector.
[[nodiscard]] std::vector<unsigned char> zstd_compress(const unsigned char* data, size_t size);

/// Decompress data previously compressed with zstd_compress.
/// Expects the same format: [orig_size:LE4][dict_size:LE4][dictionary][compressed][MD5:LE8]
/// Returns nullptr when checksum or decompression fails.
[[nodiscard]] std::vector<unsigned char> zstd_decompress(const unsigned char* data, size_t size);

} // namespace trust::detail