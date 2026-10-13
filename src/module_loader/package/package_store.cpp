// src/module_loader/package/package_store.cpp
#include "module_loader/package/package_store.hpp"

#include "module_loader/path/module_path_policy.hpp"
#include "package/manifest.hpp"
#include "package/package.hpp"
#include "utils/hash.hpp"

#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace trust {

std::filesystem::path PackageStore::ensureExtracted(const std::filesystem::path& archive, std::string& out_error) {
    namespace fs = std::filesystem;
    if (m_cacheDir.empty()) {
        out_error = "package cache directory is not set (owner must call setPackageCacheDir)";
        return {};
    }
    if (!isTrustPackage(archive)) {
        out_error = "'" + archive.string() + "' is not a trust package (not a gzip-tar archive)";
        return {};
    }
    module_path::appendUniqueCanonical(m_usedArchives, archive);
    const std::optional<std::string> archive_md5 = utils::md5HexFile(archive);
    if (!archive_md5) {
        out_error = "cannot hash package archive '" + archive.string() + "'";
        return {};
    }
    const fs::path dest = m_cacheDir / (archive.stem().string() + "-" + archive_md5->substr(0, 12));
    constexpr std::string_view kCacheMarker = ".trust-archive-md5";
    std::error_code ec;
    bool valid_cache = false;
    if (fs::is_directory(dest, ec)) {
        std::ifstream marker(dest / std::string(kCacheMarker), std::ios::binary);
        std::string cached_md5;
        if (marker && std::getline(marker, cached_md5) && cached_md5 == *archive_md5) {
            valid_cache = true;
        }
    }
    if (!valid_cache) {
        fs::remove_all(dest, ec);
    }
    if (!fs::is_directory(dest, ec)) {
        std::string tar_error;
        if (!extractAndVerifyPackage(archive, dest, tar_error)) {
            out_error = "cannot extract package '" + archive.string() + "': " + tar_error;
            return {};
        }
        std::ofstream marker(dest / std::string(kCacheMarker), std::ios::binary | std::ios::trunc);
        if (!marker) {
            out_error = "cannot write package cache marker in '" + dest.string() + "'";
            return {};
        }
        marker << *archive_md5 << "\n";
    }
    // Один кэш-каталог на имя пакета: старые версии (другой MD5) удаляются, чтобы link resolver
    // не видел два провайдера одного имени в <build>/packages.
    const std::string cache_prefix = archive.stem().string() + "-";
    for (const auto& entry : fs::directory_iterator(m_cacheDir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_directory(ec)) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.rfind(cache_prefix, 0) == 0 && entry.path() != dest) {
            std::error_code remove_ec;
            fs::remove_all(entry.path(), remove_ec);
        }
    }
    return dest;
}

bool PackageStore::resolveRoot(const ModulePath& path, const std::vector<std::filesystem::path>& roots, std::filesystem::path& outRoot,
                               std::string& outError) {
    namespace fs = std::filesystem;
    const std::string& package_name = path.package;
    if (package_name.empty()) {
        outError = "package path must start with a package name";
        return false;
    }
    std::vector<fs::path> packages;
    std::error_code ec;
    for (const fs::path& root : roots) {
        const fs::path archive = root / (package_name + ".trust");
        const fs::path dir = root / package_name;
        const bool has_archive = fs::is_regular_file(archive, ec);
        const bool has_dir = fs::is_directory(dir, ec);
        if (has_archive && has_dir) {
            outError = "both '" + archive.string() + "' and '" + dir.string() + "' exist; rename one";
            return false;
        }
        if (has_archive) {
            const fs::path extracted = ensureExtracted(archive, outError);
            if (extracted.empty()) {
                return false;
            }
            packages.push_back(extracted);
        } else if (has_dir) {
            if (!fs::is_regular_file(dir / "trust.toml", ec)) {
                outError = "'" + dir.string() + "' is not a package (no trust.toml)";
                return false;
            }
            packages.push_back(dir);
        }
    }
    if (packages.empty()) {
        outError = "package '" + package_name + "' not found in package search paths";
        return false;
    }
    std::vector<fs::path> unique;
    for (const fs::path& candidate : packages) {
        module_path::appendUniqueCanonical(unique, candidate);
    }
    if (unique.size() > 1) {
        outError = "package '" + package_name + "' is ambiguous (multiple archives/directories)";
        return false;
    }
    const fs::path package_root = unique.front();
    PackageManifest manifest;
    std::string manifest_error;
    if (!parseManifestFile(package_root / std::string(PackageManifest::kFileName), manifest, manifest_error, ManifestVersion::Optional)) {
        outError = "cannot read package manifest: " + manifest_error;
        return false;
    }
    if (manifest.name() != package_name) {
        outError = "package manifest name '" + std::string(manifest.name()) + "' does not match '" + package_name + "'";
        return false;
    }
    fs::path module_root(manifest.moduleRoot());
    if (module_root.is_absolute()) {
        outError = "package module-root must be relative: '" + module_root.string() + "'";
        return false;
    }
    for (const fs::path& part : module_root) {
        if (part == "..") {
            outError = "package module-root must not contain '..'";
            return false;
        }
    }
    const fs::path scope_root = package_root / module_root;
    if (!fs::is_directory(scope_root, ec)) {
        outError = "package module-root is not a directory: '" + scope_root.string() + "'";
        return false;
    }
    outRoot = scope_root;
    return true;
}

} // namespace trust

