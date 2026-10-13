// src/pipeline/artifacts.cpp
// Provider-package artifact resolution: [[artifact]] + [[link.headers]] -> build.conf flags.
#include "toolchain/build/link_plan.hpp"

#include "package/manifest.hpp"
#include "toolchain/runtime/runtime_locator.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace trust {
namespace toolchain {
namespace {

namespace fs = std::filesystem;

struct PackageEntry {
    fs::path dir;
    PackageManifest manifest;
};

enum class CapabilityState {
    Provided,
    Missing,
    Unknown,
};

CapabilityState capabilityState(std::string_view name) {
    if (name == "with-torch") {
#ifdef TRUST_HAS_TORCH
        return CapabilityState::Provided;
#else
        return CapabilityState::Missing;
#endif
    }
    return CapabilityState::Unknown;
}

void collectPackageDirs(const fs::path& build_dir, const fs::path& stdlib_root, std::vector<fs::path>& out) {
    std::error_code ec;
    if (fs::is_directory(stdlib_root, ec)) {
        out.push_back(stdlib_root);
    }
    const fs::path pkg_root = build_dir / "packages";
    if (!fs::is_directory(pkg_root, ec)) {
        return;
    }
    std::vector<fs::path> subdirs;
    for (fs::directory_iterator it(pkg_root, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        if (!it->is_directory(ec)) {
            continue;
        }
        if (!fs::is_regular_file(it->path() / std::string(PackageManifest::kFileName), ec)) {
            continue;
        }
        subdirs.push_back(it->path());
    }
    std::sort(subdirs.begin(), subdirs.end());
    out.insert(out.end(), subdirs.begin(), subdirs.end());
}

void appendUnique(std::vector<std::string>& out, std::set<std::string>& seen, const std::string& value) {
    if (seen.insert(value).second) {
        out.push_back(value);
    }
}

} // namespace

bool resolveLinkPlan(const fs::path& build_dir, const fs::path& stdlib_root, const std::vector<std::string>& headers, const std::vector<std::string>& link_libs,
                     LinkPlan& out, std::string& out_error) {
    out = LinkPlan{};
    std::error_code ec;

    std::vector<fs::path> dirs;
    collectPackageDirs(build_dir, stdlib_root, dirs);
    if (dirs.empty()) {
        out_error = "no package manifests found for link resolution";
        return false;
    }

    std::vector<PackageEntry> entries;
    entries.reserve(dirs.size());
    for (const fs::path& dir : dirs) {
        PackageEntry entry;
        entry.dir = dir;
        std::string parse_error;
        if (!parseManifestFile(dir / std::string(PackageManifest::kFileName), entry.manifest, parse_error, ManifestVersion::Required)) {
            out_error = "cannot read package manifest in '" + dir.string() + "': " + parse_error;
            return false;
        }
        entries.push_back(std::move(entry));
    }

    // -- Catalog of logical names -> (package, artifact) --
    struct Provider {
        std::size_t package = 0;
        std::size_t artifact = 0;
    };
    std::map<std::string, Provider> providers;
    for (std::size_t pi = 0; pi < entries.size(); ++pi) {
        const std::vector<PackageArtifact> artifacts = entries[pi].manifest.artifacts();
        for (std::size_t ai = 0; ai < artifacts.size(); ++ai) {
            for (const std::string& name : artifacts[ai].provides) {
                const auto [it, inserted] = providers.emplace(name, Provider{pi, ai});
                if (!inserted) {
                    const Provider& prev = it->second;
                    out_error = "logical library '" + name + "' is provided by both '" + std::string(entries[prev.package].manifest.name()) + "' and '" +
                                std::string(entries[pi].manifest.name()) + "'";
                    return false;
                }
            }
        }
    }

    // -- Demand: used headers via [[link.headers]] + explicit @[link] names --
    std::vector<std::string> demand;
    std::set<std::string> demand_seen;
    for (const PackageEntry& entry : entries) {
        for (const PackageHeaderLink& rule : entry.manifest.headerLinks()) {
            if (std::find(headers.begin(), headers.end(), rule.header) == headers.end()) {
                continue;
            }
            for (const std::string& lib : rule.libs) {
                if (demand_seen.insert(lib).second) {
                    demand.push_back(lib);
                }
            }
        }
    }
    std::vector<std::string> direct_libs;
    for (const std::string& flag : link_libs) {
        if (flag.size() > 2 && flag.rfind("-l", 0) == 0) {
            const std::string name = flag.substr(2);
            if (demand_seen.insert(name).second) {
                demand.push_back(name);
            }
        } else if (!flag.empty()) {
            direct_libs.push_back(flag);
        }
    }

    std::set<std::string> lib_seen;
    std::set<std::string> source_seen;
    std::set<std::string> option_seen;
    std::set<std::string> libdir_seen;
    // Indexed walk: `requires` extends `demand` in place, so iterator-based traversal
    // would be invalidated. Each name is visited once (demand_seen is the guard).
    for (std::size_t di = 0; di < demand.size(); ++di) {
        const std::string name = demand[di];
        const auto it = providers.find(name);
        if (it == providers.end()) {
            // No package provides it: preserve the @[link("name")] contract (-lname).
            appendUnique(out.libs, lib_seen, "-l" + name);
            continue;
        }
        const Provider& provider = it->second;
        const PackageEntry& entry = entries[provider.package];
        const std::vector<PackageArtifact> artifacts = entry.manifest.artifacts();
        const PackageArtifact& artifact = artifacts[provider.artifact];

        // Transitive dependencies: an artifact's `requires` are demanded as soon as the
        // artifact is linked (the archive member set is fixed, so its own deps are too).
        for (const std::string& req : artifact.requiresLibs) {
            if (demand_seen.insert(req).second) {
                demand.push_back(req);
            }
        }

        for (const std::string& cap : artifact.capabilities) {
            const CapabilityState state = capabilityState(cap);
            if (state == CapabilityState::Missing) {
                out_error = "package '" + std::string(entry.manifest.name()) + "' requires capability '" + cap + "', but this toolchain was built without it";
                return false;
            }
            if (state == CapabilityState::Unknown) {
                out_error = "package '" + std::string(entry.manifest.name()) + "' requires unknown capability '" + cap + "'";
                return false;
            }
        }

        switch (artifact.kind) {
        case ArtifactKind::Source:
            for (const std::string& src : artifact.sources) {
                const fs::path full = entry.dir / src;
                if (!fs::is_regular_file(full, ec)) {
                    out_error = "package '" + std::string(entry.manifest.name()) + "': source file '" + src + "' not found in " + entry.dir.string();
                    return false;
                }
                const fs::path rel = fs::relative(full, build_dir, ec);
                if (ec || rel.empty() || rel.generic_string().rfind("..", 0) == 0) {
                    out_error = "package '" + std::string(entry.manifest.name()) + "': source '" + src + "' is outside the build directory";
                    return false;
                }
                appendUnique(out.sources, source_seen, rel.generic_string());
            }
            break;
        case ArtifactKind::Package:
            for (const std::string& lib : artifact.libs) {
                const fs::path full = entry.dir / lib;
                if (!fs::is_regular_file(full, ec)) {
                    out_error = "package '" + std::string(entry.manifest.name()) + "': library file '" + lib + "' not found in " + entry.dir.string();
                    return false;
                }
                const fs::path rel = fs::relative(full, build_dir, ec);
                if (ec || rel.empty() || rel.generic_string().rfind("..", 0) == 0) {
                    out_error = "package '" + std::string(entry.manifest.name()) + "': library '" + lib + "' is outside the build directory";
                    return false;
                }
                appendUnique(out.libs, lib_seen, rel.generic_string());
            }
            break;
        case ArtifactKind::Toolchain:
            for (const std::string& lib : artifact.libs) {
                const fs::path located = locateToolchainLibrary(lib);
                if (located.empty()) {
                    out_error = "toolchain library '" + lib + "' (provided by package '" + std::string(entry.manifest.name()) +
                                "') not found next to trust, in lib/, ../lib/, LD_LIBRARY_PATH or cwd";
                    return false;
                }
                appendUnique(out.libs, lib_seen, "-l" + lib);
                const std::string dir = located.parent_path().string();
                if (libdir_seen.insert(dir).second) {
                    out.libraryDirs.push_back(located.parent_path());
                }
            }
            break;
        case ArtifactKind::System:
            for (const std::string& lib : artifact.libs) {
                appendUnique(out.libs, lib_seen, "-l" + lib);
            }
            break;
        }
        for (const std::string& opt : artifact.linkOptions) {
            appendUnique(out.linkOptions, option_seen, opt);
        }
    }
    for (const std::string& flag : direct_libs) {
        appendUnique(out.libs, lib_seen, flag);
    }

    return true;
}

} // namespace toolchain
} // namespace trust
