// src/pm/manifest.cpp
#include "pm/manifest.hpp"

#include "pm/protocol.hpp"
#include "pm/semver.hpp"

#include <fstream>
#include <sstream>

namespace trust::pm {
namespace {

[[noreturn]] void fail(std::string message, nlohmann::json context) {
    throw PmException(PmError{"pm.manifest.invalid", std::move(message), std::move(context)});
}

} // namespace

Manifest parseManifest(const nlohmann::json& json) {
    if (!json.is_object()) {
        fail("manifest must be a JSON object", nlohmann::json::object());
    }
    Manifest manifest;
    if (!json.contains("schema") || !json.at("schema").is_number_integer()) {
        fail("manifest field 'schema' is required and must be an integer", nlohmann::json{{"file", kManifestFileName}});
    }
    manifest.schema = json.at("schema").get<int>();
    if (manifest.schema != kManifestSchema) {
        fail("unsupported manifest schema", nlohmann::json{{"schema", manifest.schema}, {"supported", kManifestSchema}});
    }
    if (!json.contains("name") || !json.at("name").is_string() || json.at("name").get<std::string>().empty()) {
        fail("manifest field 'name' is required and must be a non-empty string", nlohmann::json::object());
    }
    manifest.name = json.at("name").get<std::string>();
    if (!json.contains("version") || !json.at("version").is_string()) {
        fail("manifest field 'version' is required and must be a string", nlohmann::json{{"name", manifest.name}});
    }
    manifest.version = json.at("version").get<std::string>();
    // Throws pm.version.invalid if malformed.
    (void)parseSemver(manifest.version);

    if (json.contains("dependencies")) {
        const nlohmann::json& deps = json.at("dependencies");
        if (!deps.is_object()) {
            fail("manifest field 'dependencies' must be an object", nlohmann::json{{"name", manifest.name}});
        }
        for (auto it = deps.begin(); it != deps.end(); ++it) {
            if (!it.value().is_string()) {
                fail("dependency range must be a string", nlohmann::json{{"dependency", it.key()}});
            }
            manifest.dependencies.push_back(Dependency{it.key(), it.value().get<std::string>()});
        }
    }
    return manifest;
}

Manifest loadManifest(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw PmException(PmError{"pm.manifest.unreadable", "cannot open manifest file: " + file.string(), nlohmann::json{{"file", file.string()}}});
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(buffer.str());
    } catch (const nlohmann::json::parse_error& e) {
        throw PmException(PmError{"pm.manifest.invalid", std::string("cannot parse manifest JSON: ") + e.what(), nlohmann::json{{"file", file.string()}}});
    }
    return parseManifest(json);
}

nlohmann::json manifestToJson(const Manifest& manifest) {
    nlohmann::json deps = nlohmann::json::object();
    for (const Dependency& d : manifest.dependencies) {
        deps[d.name] = d.range;
    }
    return nlohmann::json{{"schema", manifest.schema}, {"name", manifest.name}, {"version", manifest.version}, {"dependencies", deps}};
}

} // namespace trust::pm
