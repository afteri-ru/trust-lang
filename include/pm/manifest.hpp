#pragma once

// include/pm/manifest.hpp
// Package manifest: a declarative data file `trust.pkg.json` in the package
// root. Its presence marks a package root; a directory containing its own
// manifest (nested) is a nested package. The human-readable description is a
// separate file (README.md), never a field of the manifest.

#include "packages/package.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace trust::pm {

/// Package-root marker, owned by the `packages` component (single source).
using packages::kManifestFileName;

/// Current manifest schema version.
inline constexpr int kManifestSchema = 1;

/// A single dependency: package name plus a version range.
struct Dependency {
    std::string name;
    std::string range;
};

/// Parsed package manifest.
struct Manifest {
    int schema = kManifestSchema;
    std::string name;
    std::string version;
    std::vector<Dependency> dependencies;
};

/// Validates and converts a JSON object into a Manifest.
/// Throws PmException("pm.manifest.invalid") with details on failure.
[[nodiscard]] Manifest parseManifest(const nlohmann::json& json);

/// Reads a manifest file from disk (JSON) and parses it.
[[nodiscard]] Manifest loadManifest(const std::filesystem::path& file);

/// Serializes a manifest back to JSON (deterministic key order).
[[nodiscard]] nlohmann::json manifestToJson(const Manifest& manifest);

} // namespace trust::pm
