#pragma once

// include/packages/manifest.hpp
// Package manifest `trust.pkg.json`: a JSON document that marks a package root and carries the
// package-model fields. It is parsed as DATA: `version` and dependency `range` are opaque strings
// (their semantics — semver, ranges — belong to the consumer, e.g. the package manager/solver).
// Unknown fields (signature, url, custom metadata) are preserved in `document` and never
// interpreted here.

#include "packages/package.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace trust::packages {

/// Current manifest schema version.
inline constexpr int kManifestSchema = 1;

/// A single dependency: package name plus an opaque version-range string.
struct Dependency {
    std::string name;
    std::string range;
};

/// Parsed package manifest (a typed view over the package-model fields).
struct Manifest {
    int schema = kManifestSchema;
    std::string name;
    std::string version;                  ///< package version as DATA (string); semver is the consumer's concern
    std::vector<Dependency> dependencies; ///< range strings are opaque here
    nlohmann::json document;              ///< the source JSON document (unknown fields preserved as data)
};

/// Validates and converts a JSON object into a Manifest.
/// Throws PackageException("packages.manifest.invalid") with details on failure.
[[nodiscard]] Manifest parseManifest(const nlohmann::json& json);

/// Reads a manifest JSON file from disk and parses it.
/// Throws PackageException("packages.manifest.unreadable" | "packages.manifest.invalid").
[[nodiscard]] Manifest loadManifest(const std::filesystem::path& file);

/// The manifest's JSON document (all fields, including ones not interpreted by the package model).
[[nodiscard]] nlohmann::json manifestToJson(const Manifest& manifest);

} // namespace trust::packages
