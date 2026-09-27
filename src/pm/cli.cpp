// src/pm/cli.cpp
#include "pm/cli.hpp"

#include "pm/cache.hpp"
#include "pm/manifest.hpp"
#include "pm/net.hpp"
#include "pm/package.hpp"
#include "pm/protocol.hpp"
#include "pm/resolver.hpp"
#include "pm/semver.hpp"

#include "trust/version.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace trust::pm {
namespace {

namespace fs = std::filesystem;

struct CommandLine {
    std::string command;
    std::vector<std::string> positionals;
    std::map<std::string, std::vector<std::string>> values;
    std::set<std::string> flags;
    bool json = false;
};

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool isRemote(const std::string& source) {
    return startsWith(source, "http://") || startsWith(source, "https://");
}

std::string stripFileScheme(const std::string& source) {
    if (startsWith(source, "file://")) {
        return source.substr(7);
    }
    return source;
}

// Options that take a value; everything else that looks like an option is a
// flag. Unknown options are rejected (no silent fallback).
bool isValueOption(const std::string& name) {
    return name == "manifest" || name == "source" || name == "cache" || name == "lock";
}

bool isFlagOption(const std::string& name) {
    return name == "offline" || name == "frozen";
}

// Parses argv[1..] into command, positionals, options and flags. Unknown
// options and missing values are errors.
CommandLine parseCommandLine(const std::vector<std::string>& args) {
    CommandLine line;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--json") {
            line.json = true;
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            line.flags.insert("help");
            continue;
        }
        if (arg == "--version" || arg == "-V") {
            line.command = "version";
            continue;
        }
        if (startsWith(arg, "--")) {
            const std::size_t eq = arg.find('=');
            const std::string name = (eq == std::string::npos) ? arg.substr(2) : arg.substr(2, eq - 2);
            if (name.empty()) {
                throw PmException(PmError{"pm.usage", "empty option name", nlohmann::json::object()});
            }
            if (eq != std::string::npos) {
                line.values[name].push_back(arg.substr(eq + 1));
                continue;
            }
            if (isValueOption(name)) {
                if (i + 1 >= args.size()) {
                    throw PmException(PmError{"pm.usage", "option --" + name + " requires a value", nlohmann::json{{"option", name}}});
                }
                line.values[name].push_back(args[++i]);
            } else if (isFlagOption(name)) {
                line.flags.insert(name);
            } else {
                throw PmException(PmError{"pm.usage", "unknown option: --" + name, nlohmann::json{{"option", name}}});
            }
            continue;
        }
        if (arg.size() > 1 && arg[0] == '-') {
            if (arg == "-o") {
                if (i + 1 >= args.size()) {
                    throw PmException(PmError{"pm.usage", "option -o requires a value", nlohmann::json{{"option", "o"}}});
                }
                line.values["o"].push_back(args[++i]);
            } else {
                throw PmException(PmError{"pm.usage", "unknown option: " + arg, nlohmann::json{{"option", arg}}});
            }
            continue;
        }
        if (line.command.empty()) {
            line.command = arg;
        } else {
            line.positionals.push_back(arg);
        }
    }
    return line;
}

std::string firstValue(const CommandLine& line, const std::string& name, const std::string& fallback = {}) {
    const auto it = line.values.find(name);
    if (it == line.values.end() || it->second.empty()) {
        return fallback;
    }
    return it->second.front();
}

std::vector<std::string> valuesOf(const CommandLine& line, const std::string& name) {
    const auto it = line.values.find(name);
    return it == line.values.end() ? std::vector<std::string>{} : it->second;
}

bool hasFlag(const CommandLine& line, const std::string& name) {
    return line.flags.count(name) > 0 || line.values.count(name) > 0;
}

// Builds a candidate record from a `.trust` archive manifest.
PackageRecord recordFromArchive(const fs::path& archive, const std::string& source) {
    const std::string manifest_text = readPackageEntry(archive, kManifestFileName);
    const Manifest manifest = parseManifest(nlohmann::json::parse(manifest_text));
    PackageRecord record;
    record.name = manifest.name;
    record.version = manifest.version;
    record.dependencies = manifest.dependencies;
    record.file = archive;
    record.source = source;
    return record;
}

// Collects candidates from a local directory tree (all `*.trust` archives).
void collectLocalSource(const fs::path& dir, std::vector<PackageRecord>& out) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        throw PmException(PmError{"pm.source.missing", "source directory not found: " + dir.string(), nlohmann::json{{"source", dir.string()}}});
    }
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec) || it->path().extension() != ".trust") {
            continue;
        }
        out.push_back(recordFromArchive(it->path(), dir.string()));
    }
}

// Collects candidates from a remote JSON index ({"packages":[{...}]}).
void collectRemoteSource(const std::string& url, const DownloadOptions& net_options, std::vector<PackageRecord>& out) {
    const std::string body = downloadToString(url, net_options);
    nlohmann::json index;
    try {
        index = nlohmann::json::parse(body);
    } catch (const nlohmann::json::parse_error& e) {
        throw PmException(PmError{"pm.source.invalid", std::string("cannot parse source index: ") + e.what(), nlohmann::json{{"source", url}}});
    }
    if (!index.contains("packages") || !index.at("packages").is_array()) {
        throw PmException(PmError{"pm.source.invalid", "source index must contain a 'packages' array", nlohmann::json{{"source", url}}});
    }
    for (const nlohmann::json& item : index.at("packages")) {
        PackageRecord record;
        record.name = item.value("name", "");
        record.version = item.value("version", "");
        record.url = item.value("url", "");
        record.sha256 = item.value("sha256", "");
        record.source = url;
        if (record.name.empty() || record.version.empty() || record.url.empty()) {
            throw PmException(PmError{"pm.source.invalid", "source index entry requires name, version and url", nlohmann::json{{"source", url}}});
        }
        (void)parseSemver(record.version);
        if (item.contains("dependencies") && item.at("dependencies").is_object()) {
            for (auto it = item.at("dependencies").begin(); it != item.at("dependencies").end(); ++it) {
                record.dependencies.push_back(Dependency{it.key(), it.value().get<std::string>()});
            }
        }
        out.push_back(record);
    }
}

std::vector<PackageRecord> collectCandidates(const std::vector<std::string>& sources, const DownloadOptions& net_options) {
    std::vector<PackageRecord> out;
    for (const std::string& source : sources) {
        if (isRemote(source)) {
            collectRemoteSource(source, net_options, out);
        } else {
            collectLocalSource(stripFileScheme(source), out);
        }
    }
    return out;
}

DownloadOptions netOptions(const CommandLine& line) {
    DownloadOptions options;
    options.offline = hasFlag(line, "offline");
    return options;
}

} // namespace

namespace {

nlohmann::json resolvedToJson(const Resolved& resolved) {
    nlohmann::json item;
    item["name"] = resolved.name;
    item["version"] = resolved.version;
    item["source"] = resolved.source;
    if (!resolved.file.empty()) {
        item["file"] = resolved.file.string();
    }
    if (!resolved.url.empty()) {
        item["url"] = resolved.url;
    }
    if (!resolved.sha256.empty()) {
        item["sha256"] = resolved.sha256;
    }
    return item;
}

void printUsage(std::ostream& out) {
    out << kProgramName << " - TrustLang package manager (protocol " << kProtocolVersion << ")\n\n"
        << "Usage: trust-pm <command> [options]\n\n"
        << "Commands:\n"
        << "  version                          Print version and protocol\n"
        << "  capabilities                     List commands and features\n"
        << "  info <path>                      Show a package/manifest description\n"
        << "  pack <dir> [-o <out.trust>]      Create a deterministic .trust package\n"
        << "  resolve --manifest <file> --source <dir|url> [--source ...]\n"
        << "  fetch   --manifest <file> --source <dir|url> --cache <dir>\n"
        << "  install --manifest <file> --source <dir|url> --cache <dir>\n"
        << "  list --cache <dir>               List cached artifacts\n\n"
        << "Global options:\n"
        << "  --json                           Machine-readable JSON output\n"
        << "  --offline                        Forbid network access\n"
        << "  --frozen                         Use the lock file only\n";
}

int finish(const CommandLine& line, std::ostream& out, const nlohmann::json& data) {
    if (line.json) {
        out << okEnvelope(data).dump(2) << "\n";
    }
    return 0;
}

int runVersion(const CommandLine& line, std::ostream& out) {
    if (line.json) {
        return finish(line, out, nlohmann::json{{"program", kProgramName}, {"version", TRUST_VERSION}, {"protocol", kProtocolVersion}});
    }
    out << kProgramName << " " << TRUST_VERSION << " (protocol " << kProtocolVersion << ")\n";
    return 0;
}

int runCapabilities(const CommandLine& line, std::ostream& out) {
    nlohmann::json commands = nlohmann::json::array({"capabilities", "fetch", "info", "install", "list", "pack", "resolve", "version"});
    nlohmann::json features =
        nlohmann::json::array({"archive:tar+zstd", "integrity:sha256", "manifest:json:1", "offline", "frozen", "solver:libsolv", "sources:http,local"});
    nlohmann::json data{{"program", kProgramName}, {"protocol", kProtocolVersion}, {"commands", commands}, {"features", features}};
    if (line.json) {
        return finish(line, out, data);
    }
    out << kProgramName << " capabilities (protocol " << kProtocolVersion << ")\n";
    for (const auto& command : commands) {
        out << "  " << command.get<std::string>() << "\n";
    }
    for (const auto& feature : features) {
        out << "  feature: " << feature.get<std::string>() << "\n";
    }
    return 0;
}

fs::path rootManifestPath(const CommandLine& line) {
    const std::string explicit_path = firstValue(line, "manifest");
    if (!explicit_path.empty()) {
        return explicit_path;
    }
    if (!line.positionals.empty()) {
        fs::path path(line.positionals.front());
        if (fs::is_directory(path)) {
            path /= kManifestFileName;
        }
        return path;
    }
    throw PmException(PmError{"pm.usage", "a package manifest is required (--manifest <file> or a directory)", nlohmann::json::object()});
}

fs::path lockPathFor(const CommandLine& line, const fs::path& manifest_path) {
    const std::string explicit_path = firstValue(line, "lock");
    if (!explicit_path.empty()) {
        return explicit_path;
    }
    return manifest_path.parent_path() / "trust.lock.json";
}

void writeLock(const fs::path& path, const Manifest& root, const std::vector<Resolved>& resolved) {
    nlohmann::json packages = nlohmann::json::array();
    for (const Resolved& item : resolved) {
        nlohmann::json entry = resolvedToJson(item);
        if (!item.sha256.empty()) {
            entry["sha256"] = item.sha256;
        } else if (!item.file.empty() && fs::is_regular_file(item.file)) {
            entry["sha256"] = sha256File(item.file);
        }
        packages.push_back(std::move(entry));
    }
    const nlohmann::json lock{{"schema", 1}, {"root", root.name}, {"packages", packages}};
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw PmException(PmError{"pm.lock.write", "cannot write lock file: " + path.string(), nlohmann::json{{"file", path.string()}}});
    }
    out << lock.dump(2) << "\n";
}

std::vector<Resolved> readLock(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw PmException(
            PmError{"pm.lock.missing", "lock file not found (--frozen requires a lock): " + path.string(), nlohmann::json{{"file", path.string()}}});
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    nlohmann::json lock;
    try {
        lock = nlohmann::json::parse(buffer.str());
    } catch (const nlohmann::json::parse_error& e) {
        throw PmException(PmError{"pm.lock.invalid", std::string("cannot parse lock file: ") + e.what(), nlohmann::json{{"file", path.string()}}});
    }
    if (!lock.contains("packages") || !lock.at("packages").is_array()) {
        throw PmException(PmError{"pm.lock.invalid", "lock file must contain a 'packages' array", nlohmann::json{{"file", path.string()}}});
    }
    std::vector<Resolved> resolved;
    for (const nlohmann::json& item : lock.at("packages")) {
        Resolved entry;
        entry.name = item.value("name", "");
        entry.version = item.value("version", "");
        entry.source = item.value("source", "");
        entry.file = item.value("file", "");
        entry.url = item.value("url", "");
        entry.sha256 = item.value("sha256", "");
        if (entry.name.empty() || entry.version.empty()) {
            throw PmException(PmError{"pm.lock.invalid", "lock entry requires name and version", nlohmann::json{{"file", path.string()}}});
        }
        (void)parseSemver(entry.version);
        resolved.push_back(entry);
    }
    return resolved;
}

// A frozen lock must be consistent with the root manifest: every dependency is
// present and its locked version satisfies the requested range.
void verifyLockAgainstManifest(const Manifest& root, const std::vector<Resolved>& resolved) {
    for (const Dependency& dep : root.dependencies) {
        const Resolved* found = nullptr;
        for (const Resolved& item : resolved) {
            if (item.name == dep.name) {
                found = &item;
                break;
            }
        }
        if (found == nullptr) {
            throw PmException(PmError{"pm.lock.invalid", "lock file has no entry for dependency: " + dep.name, nlohmann::json{{"dependency", dep.name}}});
        }
        if (!satisfiesRange(parseSemver(found->version), dep.range)) {
            throw PmException(PmError{"pm.lock.invalid", "locked version does not satisfy the manifest range",
                                      nlohmann::json{{"dependency", dep.name}, {"range", dep.range}, {"locked", found->version}}});
        }
    }
}

int runInfo(const CommandLine& line, std::ostream& out) {
    std::string path = firstValue(line, "manifest");
    if (path.empty() && !line.positionals.empty()) {
        path = line.positionals.front();
    }
    if (path.empty()) {
        throw PmException(PmError{"pm.usage", "info requires a package path", nlohmann::json::object()});
    }
    const fs::path target(path);
    nlohmann::json data;
    if (fs::is_regular_file(target) && target.extension() == ".trust") {
        const Manifest manifest = parseManifest(nlohmann::json::parse(readPackageEntry(target, kManifestFileName)));
        data["manifest"] = manifestToJson(manifest);
        data["sha256"] = sha256File(target);
        nlohmann::json entries = nlohmann::json::array();
        for (const std::string& entry : listPackage(target)) {
            entries.push_back(entry);
        }
        data["entries"] = entries;
    } else {
        fs::path manifest_file = target;
        if (fs::is_directory(target)) {
            manifest_file = target / kManifestFileName;
        }
        data["manifest"] = manifestToJson(loadManifest(manifest_file));
    }
    if (line.json) {
        return finish(line, out, data);
    }
    out << data.at("manifest").dump(2) << "\n";
    return 0;
}

} // namespace

namespace {

std::string sanitizeName(const std::string& name) {
    std::string out = name;
    std::replace(out.begin(), out.end(), '/', '-');
    return out;
}

int runPack(const CommandLine& line, std::ostream& out) {
    if (line.positionals.empty()) {
        throw PmException(PmError{"pm.usage", "pack requires a package directory", nlohmann::json::object()});
    }
    const fs::path dir = line.positionals.front();
    // A package root is marked by its manifest; validate it before packing.
    const Manifest manifest = loadManifest(dir / kManifestFileName);
    std::string out_path = firstValue(line, "o");
    if (out_path.empty() && line.positionals.size() > 1) {
        out_path = line.positionals[1];
    }
    if (out_path.empty()) {
        out_path = (sanitizeName(manifest.name) + "-" + manifest.version + ".trust");
    }
    packPackage(dir, out_path);
    nlohmann::json data{{"archive", out_path}, {"sha256", sha256File(out_path)}, {"entries", listPackage(out_path).size()}};
    if (line.json) {
        return finish(line, out, data);
    }
    out << "packed " << out_path << " (" << data.at("sha256").get<std::string>() << ")\n";
    return 0;
}

int runResolve(const CommandLine& line, std::ostream& out) {
    const fs::path manifest_path = rootManifestPath(line);
    const Manifest root = loadManifest(manifest_path);
    const DownloadOptions net_options = netOptions(line);
    std::vector<Resolved> resolved;
    if (hasFlag(line, "frozen")) {
        resolved = readLock(lockPathFor(line, manifest_path));
        verifyLockAgainstManifest(root, resolved);
    } else {
        const std::vector<PackageRecord> candidates = collectCandidates(valuesOf(line, "source"), net_options);
        resolved = resolveDependencies(root, candidates);
        writeLock(lockPathFor(line, manifest_path), root, resolved);
    }
    nlohmann::json packages = nlohmann::json::array();
    for (const Resolved& item : resolved) {
        packages.push_back(resolvedToJson(item));
    }
    const nlohmann::json data{{"root", root.name}, {"packages", packages}};
    if (line.json) {
        return finish(line, out, data);
    }
    out << "resolved " << resolved.size() << " package(s) for " << root.name << "\n";
    for (const Resolved& item : resolved) {
        out << "  " << item.name << " " << item.version << "\n";
    }
    return 0;
}

} // namespace

namespace {

fs::path resolveStored(Cache& cache, const Resolved& item, const DownloadOptions& net_options, std::string& hash_out) {
    std::string hash = item.sha256;
    // A cached artifact keyed by hash satisfies the request regardless of the
    // original source being reachable (enables --offline/--frozen).
    if (!hash.empty() && cache.has(hash)) {
        hash_out = hash;
        return cache.artifactPath(hash);
    }
    if (!item.file.empty()) {
        const std::string actual = sha256File(item.file);
        if (!hash.empty() && hash != actual) {
            throw PmException(PmError{"pm.integrity", "local package hash mismatch: " + item.file.string(),
                                      nlohmann::json{{"file", item.file.string()}, {"expected", hash}, {"actual", actual}}});
        }
        hash = actual;
        hash_out = hash;
        return cache.store(hash, item.file);
    }
    if (hash.empty()) {
        throw PmException(PmError{"pm.integrity", "remote package has no sha256 in the index/lock", nlohmann::json{{"package", item.name}}});
    }
    hash_out = hash;
    if (cache.has(hash)) {
        return cache.artifactPath(hash);
    }
    const fs::path tmp = cache.root() / "tmp" / (hash + ".download");
    const std::string actual = downloadToFile(item.url, tmp, net_options);
    if (actual != hash) {
        std::error_code ec;
        fs::remove(tmp, ec);
        throw PmException(
            PmError{"pm.integrity", "downloaded package hash mismatch", nlohmann::json{{"package", item.name}, {"expected", hash}, {"actual", actual}}});
    }
    const fs::path stored = cache.store(hash, tmp);
    std::error_code ec;
    fs::remove(tmp, ec);
    return stored;
}

int runFetch(const CommandLine& line, std::ostream& out, bool install) {
    const fs::path manifest_path = rootManifestPath(line);
    const Manifest root = loadManifest(manifest_path);
    const DownloadOptions net_options = netOptions(line);
    std::vector<Resolved> resolved;
    if (hasFlag(line, "frozen")) {
        resolved = readLock(lockPathFor(line, manifest_path));
        verifyLockAgainstManifest(root, resolved);
    } else {
        resolved = resolveDependencies(root, collectCandidates(valuesOf(line, "source"), net_options));
        writeLock(lockPathFor(line, manifest_path), root, resolved);
    }
    const std::string cache_dir = firstValue(line, "cache");
    if (cache_dir.empty()) {
        throw PmException(PmError{"pm.usage", "fetch/install require --cache <dir>", nlohmann::json::object()});
    }
    Cache cache(cache_dir);
    FileLock lock = cache.lock();
    nlohmann::json fetched = nlohmann::json::array();
    for (const Resolved& item : resolved) {
        std::string hash;
        const fs::path stored = resolveStored(cache, item, net_options, hash);
        if (install) {
            extractPackage(stored, cache.root() / "installed" / item.name / item.version);
        }
        fetched.push_back(nlohmann::json{{"name", item.name}, {"version", item.version}, {"sha256", hash}, {"path", stored.string()}});
    }
    const nlohmann::json data{{"cache", cache_dir}, {"packages", fetched}};
    if (line.json) {
        return finish(line, out, data);
    }
    out << (install ? "installed " : "fetched ") << fetched.size() << " package(s) into " << cache_dir << "\n";
    for (const auto& item : fetched) {
        out << "  " << item.at("name").get<std::string>() << " " << item.at("version").get<std::string>() << " -> " << item.at("path").get<std::string>()
            << "\n";
    }
    return 0;
}

int runList(const CommandLine& line, std::ostream& out) {
    const std::string cache_dir = firstValue(line, "cache");
    if (cache_dir.empty()) {
        throw PmException(PmError{"pm.usage", "list requires --cache <dir>", nlohmann::json::object()});
    }
    const Cache cache(cache_dir);
    nlohmann::json artifacts = nlohmann::json::array();
    std::error_code ec;
    const fs::path dir = cache.root() / "artifacts";
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (it->is_regular_file(fec)) {
            artifacts.push_back(nlohmann::json{{"sha256", it->path().filename().string()}, {"path", it->path().string()}});
        }
    }
    const nlohmann::json data{{"cache", cache_dir}, {"artifacts", artifacts}};
    if (line.json) {
        return finish(line, out, data);
    }
    out << cache_dir << ": " << artifacts.size() << " artifact(s)\n";
    for (const auto& item : artifacts) {
        out << "  " << item.at("sha256").get<std::string>() << "\n";
    }
    return 0;
}

} // namespace

int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    CommandLine line;
    // Pre-scan for --json so that even argument-parsing errors are reported in
    // the machine-readable envelope when the caller asked for JSON.
    for (const std::string& arg : args) {
        if (arg == "--json") {
            line.json = true;
        }
    }
    try {
        line = parseCommandLine(args);
        if (line.command.empty() || line.command == "help" || hasFlag(line, "help")) {
            printUsage(out);
            return 0;
        }
        if (line.command == "version") {
            return runVersion(line, out);
        }
        if (line.command == "capabilities") {
            return runCapabilities(line, out);
        }
        if (line.command == "info") {
            return runInfo(line, out);
        }
        if (line.command == "pack") {
            return runPack(line, out);
        }
        if (line.command == "resolve") {
            return runResolve(line, out);
        }
        if (line.command == "fetch") {
            return runFetch(line, out, false);
        }
        if (line.command == "install") {
            return runFetch(line, out, true);
        }
        if (line.command == "list") {
            return runList(line, out);
        }
        throw PmException(PmError{"pm.usage", "unknown command: " + line.command, nlohmann::json{{"command", line.command}}});
    } catch (const PmException& e) {
        if (line.json) {
            out << errorEnvelope(e.error()).dump(2) << "\n";
        } else {
            err << kProgramName << ": " << e.error().message << "\n";
        }
        return 1;
    } catch (const std::exception& e) {
        if (line.json) {
            out << errorEnvelope(PmError{"pm.internal", e.what()}).dump(2) << "\n";
        } else {
            err << kProgramName << ": " << e.what() << "\n";
        }
        return 1;
    }
}

} // namespace trust::pm
