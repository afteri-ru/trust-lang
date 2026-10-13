// src/package/manifest.cpp
// Разбор/валидация/запись манифеста пакета trust (`trust.toml`, формат TOML, toml++).
#include "package/manifest.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <iterator>
#include <sstream>

namespace trust {

std::string_view PackageManifest::name() const {
    if (const auto v = data["name"].value<std::string_view>()) {
        return *v;
    }
    return {};
}

std::string_view PackageManifest::version() const {
    if (const auto v = data["version"].value<std::string_view>()) {
        return *v;
    }
    return {};
}

std::string_view PackageManifest::moduleRoot() const {
    if (const auto v = data["module-root"].value<std::string_view>()) {
        return *v;
    }
    return ".";
}

std::vector<PackageLayer> PackageManifest::layers() const {
    std::vector<PackageLayer> out;
    const toml::table* layout = data["layout"].as_table();
    if (layout == nullptr) {
        return out;
    }
    const toml::array* list = (*layout)["layers"].as_array();
    if (list == nullptr) {
        return out;
    }
    for (const toml::node& node : *list) {
        const toml::table* layer = node.as_table();
        if (layer == nullptr) {
            continue;
        }
        PackageLayer entry;
        if (const auto src = (*layer)["source-dir"].value<std::string>()) {
            entry.sourceDir = *src;
        }
        if (const auto dst = (*layer)["archive-dir"].value<std::string>()) {
            entry.archiveDir = *dst;
        }
        if (const toml::array* pats = (*layer)["patterns"].as_array()) {
            for (const toml::node& e : *pats) {
                if (const auto s = e.value<std::string>()) {
                    entry.patterns.push_back(*s);
                }
            }
        }
        out.push_back(std::move(entry));
    }
    return out;
}

namespace {

std::vector<std::string> readStringArray(const toml::node_view<const toml::node>& node) {
    std::vector<std::string> out;
    if (const toml::array* arr = node.as_array()) {
        for (const toml::node& item : *arr) {
            if (const auto s = item.value<std::string>()) {
                out.push_back(*s);
            }
        }
    }
    return out;
}

bool hasParentTraversal(std::string_view path) {
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::string_view comp = (slash == std::string_view::npos) ? path.substr(start) : path.substr(start, slash - start);
        if (comp == "..") {
            return true;
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    return false;
}

bool isSafeRelativePath(std::string_view path) {
    return !path.empty() && path.front() != '/' && !hasParentTraversal(path);
}

bool validateArtifacts(const toml::table& table, std::string& out_error) {
    if (!table.contains("artifact")) {
        return true;
    }
    const toml::array* list = table["artifact"].as_array();
    if (list == nullptr) {
        out_error = "'artifact' must be an array of tables ([[artifact]])";
        return false;
    }
    std::size_t index = 0;
    for (const toml::node& node : *list) {
        const toml::table* artifact = node.as_table();
        if (artifact == nullptr) {
            out_error = "artifact[" + std::to_string(index) + "] must be a table";
            return false;
        }
        const std::string kind = (*artifact)["kind"].value_or(std::string());
        ArtifactKind parsed_kind = ArtifactKind::Toolchain;
        if (kind == "source") {
            parsed_kind = ArtifactKind::Source;
        } else if (kind == "package") {
            parsed_kind = ArtifactKind::Package;
        } else if (kind == "toolchain") {
            parsed_kind = ArtifactKind::Toolchain;
        } else if (kind == "system") {
            parsed_kind = ArtifactKind::System;
        } else {
            out_error = "artifact[" + std::to_string(index) + "]: unknown or missing 'kind' (expected source|package|toolchain|system)";
            return false;
        }
        const std::vector<std::string> provides = readStringArray((*artifact)["provides"]);
        if (provides.empty()) {
            out_error = "artifact[" + std::to_string(index) + "]: 'provides' must list at least one logical library name";
            return false;
        }
        const std::vector<std::string> sources = readStringArray((*artifact)["sources"]);
        const std::vector<std::string> libs = readStringArray((*artifact)["libs"]);
        if (parsed_kind == ArtifactKind::Source) {
            if (sources.empty()) {
                out_error = "artifact[" + std::to_string(index) + "]: source artifact requires a non-empty 'sources' list";
                return false;
            }
            if (!libs.empty()) {
                out_error = "artifact[" + std::to_string(index) + "]: source artifact must not declare 'libs'";
                return false;
            }
            for (const std::string& src : sources) {
                if (!isSafeRelativePath(src)) {
                    out_error = "artifact[" + std::to_string(index) + "]: source path '" + src + "' must be relative without '..'";
                    return false;
                }
                if (!src.ends_with(".cpp")) {
                    out_error = "artifact[" + std::to_string(index) + "]: source path '" + src + "' must have a .cpp extension";
                    return false;
                }
            }
        } else {
            if (!sources.empty()) {
                out_error = "artifact[" + std::to_string(index) + "]: 'sources' is only valid for kind=\"source\"";
                return false;
            }
            if (libs.empty()) {
                out_error = "artifact[" + std::to_string(index) + "]: 'libs' must not be empty";
                return false;
            }
        }
        for (const std::string& lib : libs) {
            if (parsed_kind == ArtifactKind::Package) {
                if (!isSafeRelativePath(lib)) {
                    out_error = "artifact[" + std::to_string(index) + "]: package library path '" + lib + "' must be relative without '..'";
                    return false;
                }
            } else if (lib.empty() || lib.front() == '/' || hasParentTraversal(lib)) {
                out_error = "artifact[" + std::to_string(index) + "]: library name '" + lib + "' is invalid";
                return false;
            }
        }
        for (const std::string& opt : readStringArray((*artifact)["link-options"])) {
            if (opt.empty()) {
                out_error = "artifact[" + std::to_string(index) + "]: empty link option";
                return false;
            }
        }
        for (const std::string& cap : readStringArray((*artifact)["capabilities"])) {
            if (cap.empty()) {
                out_error = "artifact[" + std::to_string(index) + "]: empty capability name";
                return false;
            }
        }
        for (const std::string& req : readStringArray((*artifact)["requires"])) {
            if (req.empty()) {
                out_error = "artifact[" + std::to_string(index) + "]: empty 'requires' logical library name";
                return false;
            }
            if (std::find(provides.begin(), provides.end(), req) != provides.end()) {
                out_error = "artifact[" + std::to_string(index) + "]: 'requires' references its own logical library '" + req + "'";
                return false;
            }
        }
        ++index;
    }
    return true;
}

bool validateLayout(const toml::table& table, std::string& out_error) {
    const toml::node* layout_node = table.get("layout");
    if (layout_node == nullptr) {
        return true;
    }
    const toml::table* layout = layout_node->as_table();
    if (layout == nullptr) {
        out_error = "'layout' must be a table";
        return false;
    }
    if (!layout->contains("layers")) {
        return true;
    }
    const toml::array* list = (*layout)["layers"].as_array();
    if (list == nullptr) {
        out_error = "'layout.layers' must be an array of tables ([[layout.layers]])";
        return false;
    }
    if (list->empty()) {
        out_error = "'layout.layers' must not be empty";
        return false;
    }
    std::size_t index = 0;
    for (const toml::node& node : *list) {
        const toml::table* layer = node.as_table();
        if (layer == nullptr) {
            out_error = "layout.layers[" + std::to_string(index) + "] must be a table";
            return false;
        }
        for (const char* key : {"source-dir", "archive-dir"}) {
            const toml::node* field = layer->get(key);
            if (field == nullptr) {
                continue;
            }
            const auto value = field->value<std::string>();
            if (!value || value->empty()) {
                out_error = "layout.layers[" + std::to_string(index) + "]: '" + key + "' must be a non-empty string";
                return false;
            }
        }
        if (!layer->contains("patterns")) {
            out_error = "layout.layers[" + std::to_string(index) + "]: missing required 'patterns'";
            return false;
        }
        const toml::array* patterns = (*layer)["patterns"].as_array();
        if (patterns == nullptr || patterns->empty()) {
            out_error = "layout.layers[" + std::to_string(index) + "]: 'patterns' must be a non-empty array of strings";
            return false;
        }
        std::size_t pattern_index = 0;
        for (const toml::node& item : *patterns) {
            const auto pattern = item.value<std::string>();
            if (!pattern || pattern->empty()) {
                out_error = "layout.layers[" + std::to_string(index) + "].patterns[" + std::to_string(pattern_index) + "] must be a non-empty string";
                return false;
            }
            ++pattern_index;
        }
        ++index;
    }
    return true;
}

bool validateHeaderLinks(const toml::table& table, std::string& out_error) {
    const toml::table* link = table["link"].as_table();
    if (link == nullptr) {
        return true;
    }
    if (!link->contains("headers")) {
        return true;
    }
    const toml::array* list = (*link)["headers"].as_array();
    if (list == nullptr) {
        out_error = "'link.headers' must be an array of tables ([[link.headers]])";
        return false;
    }
    std::size_t index = 0;
    for (const toml::node& node : *list) {
        const toml::table* rule = node.as_table();
        if (rule == nullptr) {
            out_error = "link.headers[" + std::to_string(index) + "] must be a table";
            return false;
        }
        const std::string header = (*rule)["header"].value_or(std::string());
        if (header.empty()) {
            out_error = "link.headers[" + std::to_string(index) + "]: missing non-empty 'header'";
            return false;
        }
        if (readStringArray((*rule)["libs"]).empty()) {
            out_error = "link.headers[" + std::to_string(index) + "]: 'libs' must list at least one logical library name";
            return false;
        }
        ++index;
    }
    return true;
}

} // namespace

std::vector<PackageArtifact> PackageManifest::artifacts() const {
    std::vector<PackageArtifact> out;
    const toml::array* list = data["artifact"].as_array();
    if (list == nullptr) {
        return out;
    }
    for (const toml::node& node : *list) {
        const toml::table* table = node.as_table();
        if (table == nullptr) {
            continue;
        }
        PackageArtifact entry;
        const std::string kind = (*table)["kind"].value_or(std::string());
        if (kind == "source") {
            entry.kind = ArtifactKind::Source;
        } else if (kind == "package") {
            entry.kind = ArtifactKind::Package;
        } else if (kind == "toolchain") {
            entry.kind = ArtifactKind::Toolchain;
        } else if (kind == "system") {
            entry.kind = ArtifactKind::System;
        }
        entry.sources = readStringArray((*table)["sources"]);
        entry.libs = readStringArray((*table)["libs"]);
        entry.linkOptions = readStringArray((*table)["link-options"]);
        entry.provides = readStringArray((*table)["provides"]);
        entry.capabilities = readStringArray((*table)["capabilities"]);
        entry.requiresLibs = readStringArray((*table)["requires"]);
        out.push_back(std::move(entry));
    }
    return out;
}

std::vector<PackageHeaderLink> PackageManifest::headerLinks() const {
    std::vector<PackageHeaderLink> out;
    const toml::table* link = data["link"].as_table();
    if (link == nullptr) {
        return out;
    }
    const toml::array* list = (*link)["headers"].as_array();
    if (list == nullptr) {
        return out;
    }
    for (const toml::node& node : *list) {
        const toml::table* table = node.as_table();
        if (table == nullptr) {
            continue;
        }
        PackageHeaderLink entry;
        if (const auto h = (*table)["header"].value<std::string>()) {
            entry.header = *h;
        }
        entry.libs = readStringArray((*table)["libs"]);
        out.push_back(std::move(entry));
    }
    return out;
}

bool isValidPackageName(std::string_view name) {
    if (name.empty() || name.front() == '_' || name.back() == '_') {
        return false;
    }
    for (const char c : name) {
        if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            return false;
        }
    }
    return true;
}

bool isValidVersion(std::string_view v) {
    int parts = 0;
    std::size_t i = 0;
    while (i < v.size()) {
        std::size_t j = v.find('.', i);
        if (j == std::string_view::npos) {
            j = v.size();
        }
        if (j == i) {
            return false; // пустой компонент
        }
        for (std::size_t k = i; k < j; ++k) {
            if (std::isdigit(static_cast<unsigned char>(v[k])) == 0) {
                return false;
            }
        }
        ++parts;
        i = j + 1;
    }
    return parts == 3;
}

bool parseManifest(std::string_view text, PackageManifest& out, std::string& out_error, ManifestVersion version_policy) {
    toml::table table;
    try {
        table = toml::parse(text);
    } catch (const toml::parse_error& e) {
        const toml::source_position pos = e.source().begin;
        out_error = "invalid TOML: " + std::string(e.description()) + " (line " + std::to_string(pos.line) + ", column " + std::to_string(pos.column) + ")";
        return false;
    } catch (const std::exception& e) {
        out_error = std::string("invalid TOML: ") + e.what();
        return false;
    }

    const auto name = table["name"].value<std::string_view>();
    if (!name) {
        out_error = "missing string field 'name'";
        return false;
    }
    if (!isValidPackageName(*name)) {
        out_error = "invalid package name '" + std::string(*name) + "' (lowercase letters, digits, '_'; not first/last)";
        return false;
    }

    // Версия — semver ПАКЕТА (не версия компилятора): упаковщик сохраняет авторскую
    // версию, а при её отсутствии подставляет версию компилятора как provenance.
    // Версия toolchain фиксируется отдельно в [build].toolchain.
    const auto version = table["version"].value<std::string_view>();
    if (!version) {
        if (version_policy == ManifestVersion::Required) {
            out_error = "missing string field 'version'";
            return false;
        }
    } else if (!isValidVersion(*version)) {
        out_error = "invalid package version '" + std::string(*version) + "' (expected MAJOR.MINOR.PATCH)";
        return false;
    }

    if (!validateLayout(table, out_error)) {
        return false;
    }
    if (!validateArtifacts(table, out_error)) {
        return false;
    }
    if (!validateHeaderLinks(table, out_error)) {
        return false;
    }

    out.data = std::move(table);
    return true;
}

bool parseManifestFile(const std::filesystem::path& path, PackageManifest& out, std::string& out_error, ManifestVersion version_policy) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        out_error = "cannot read manifest file '" + path.string() + "'";
        return false;
    }
    const std::string text{std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
    return parseManifest(text, out, out_error, version_policy);
}

bool writeManifestFile(const std::filesystem::path& path, const PackageManifest& manifest, std::string& out_error) {
    std::ostringstream oss;
    oss << manifest.data;
    if (!oss) {
        out_error = "cannot serialize manifest to TOML";
        return false;
    }
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        out_error = "cannot write manifest file '" + path.string() + "'";
        return false;
    }
    ofs << oss.str();
    return true;
}

} // namespace trust
