// src/packages/manifest.cpp
#include "packages/manifest.hpp"

#include "packages/errors.hpp"

#include <fstream>
#include <sstream>

namespace trust::packages {

Manifest parseManifest(const nlohmann::json& json) {
    if (!json.is_object()) {
        fail("packages.manifest.invalid", "manifest must be a JSON object");
    }
    Manifest manifest;
    if (!json.contains("schema") || !json.at("schema").is_number_integer()) {
        fail("packages.manifest.invalid", "manifest field 'schema' is required and must be an integer");
    }
    manifest.schema = json.at("schema").get<int>();
    if (manifest.schema != kManifestSchema) {
        fail("packages.manifest.invalid", "unsupported manifest schema: {} (supported {})", manifest.schema, kManifestSchema);
    }
    if (!json.contains("name") || !json.at("name").is_string() || json.at("name").get<std::string>().empty()) {
        fail("packages.manifest.invalid", "manifest field 'name' is required and must be a non-empty string");
    }
    manifest.name = json.at("name").get<std::string>();
    if (!json.contains("version") || !json.at("version").is_string()) {
        fail("packages.manifest.invalid", "manifest field 'version' is required and must be a string");
    }
    // Version is DATA: the format (semver) is validated by the consumer, not by the package model.
    manifest.version = json.at("version").get<std::string>();
    if (json.contains("dependencies")) {
        const nlohmann::json& deps = json.at("dependencies");
        if (!deps.is_object()) {
            fail("packages.manifest.invalid", "manifest field 'dependencies' must be an object");
        }
        for (auto it = deps.begin(); it != deps.end(); ++it) {
            if (!it.value().is_string()) {
                fail("packages.manifest.invalid", "dependency range for '{}' must be a string", it.key());
            }
            manifest.dependencies.push_back(Dependency{it.key(), it.value().get<std::string>()});
        }
    }
    manifest.document = json;
    return manifest;
}

Manifest loadManifest(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        fail("packages.manifest.unreadable", "cannot open manifest file: '{}'", file.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(buffer.str());
    } catch (const nlohmann::json::parse_error& e) {
        fail("packages.manifest.invalid", "cannot parse manifest JSON '{}': {}", file.string(), e.what());
    }
    return parseManifest(json);
}

nlohmann::json manifestToJson(const Manifest& manifest) {
    // The document is kept as parsed: unknown fields are not lost and object keys are ordered
    // (deterministic output) by nlohmann::json.
    return manifest.document;
}

} // namespace trust::packages
