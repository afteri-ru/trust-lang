#pragma once

// include/pm/resolver.hpp
// Dependency resolution. Version ranges are matched by the pm semver module;
// the final consistent selection (one version per package name) is computed by
// libsolv, fed with the candidate pool built from the configured sources.

#include "pm/manifest.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace trust::pm {

/// A candidate package known to the resolver.
struct PackageRecord {
    std::string name;
    std::string version;
    std::vector<Dependency> dependencies;
    std::filesystem::path file; ///< local archive path (empty for remote records)
    std::string url;            ///< remote archive URL (empty for local records)
    std::string sha256;         ///< expected content hash (may be empty for local)
    std::string source;         ///< origin identifier (directory or URL)
};

/// A resolved package with its origin.
struct Resolved {
    std::string name;
    std::string version;
    std::filesystem::path file; ///< local archive path (may be empty for remote)
    std::string url;
    std::string sha256;
    std::string source;
};

/// Selects a consistent set of packages satisfying the root manifest.
/// Throws PmException("pm.resolve.conflict") when no solution exists.
[[nodiscard]] std::vector<Resolved> resolveDependencies(const Manifest& root, const std::vector<PackageRecord>& candidates);

} // namespace trust::pm
