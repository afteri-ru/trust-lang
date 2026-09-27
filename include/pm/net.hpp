#pragma once

// include/pm/net.hpp
// Network layer of the package manager (HTTPS via libcurl). Kept inside
// trust-pm: the compiler never performs network I/O.

#include <filesystem>
#include <string>

namespace trust::pm {

struct DownloadOptions {
    bool offline = false;      ///< forbid any network access
    long timeout_seconds = 60; ///< per-transfer timeout
};

/// Downloads `url` into `dest` (a file path). Returns the sha256 of the bytes.
/// When `options.offline` is true, throws PmException("pm.net.offline").
/// Throws PmException("pm.net.fetch") on transport failure.
[[nodiscard]] std::string downloadToFile(const std::string& url, const std::filesystem::path& dest, const DownloadOptions& options = {});

/// Fetches a small resource (e.g. an index) into memory.
/// When `options.offline` is true, throws PmException("pm.net.offline").
[[nodiscard]] std::string downloadToString(const std::string& url, const DownloadOptions& options = {});

} // namespace trust::pm
