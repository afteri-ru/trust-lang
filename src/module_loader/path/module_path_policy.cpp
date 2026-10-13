// src/module_loader/path/module_path_policy.cpp
#include "module_loader/path/module_path_policy.hpp"

#include "location/module_name.hpp"

#include <algorithm>
#include <cstdlib>
#include <system_error>
#include <utility>

namespace trust::module_path {

fs::path canonicalPath(const fs::path& path) {
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(path, ec);
    return ec ? path : canonical;
}

void appendUniqueCanonical(std::vector<fs::path>& out, const fs::path& path) {
    fs::path canonical = canonicalPath(path);
    if (std::find(out.begin(), out.end(), canonical) == out.end()) {
        out.push_back(std::move(canonical));
    }
}

namespace {

bool validComponent(std::string_view component) {
    const std::string name = "\\" + std::string(component);
    return isValidModuleLogicalName(name);
}

struct ResolvedPath {
    fs::path path;
    bool isDirectory = false;
    std::string error;
};

/// Кандидат резолва: путь, корень поиска и вид (файл/каталог).
struct ResolvedCandidate {
    fs::path path;
    fs::path root;
    bool isDirectory = false;
};

/// Результат единого резолвера: канонизированные кандидаты (без дублей) ЛИБО первая ошибка.
struct ResolveResult {
    std::vector<ResolvedCandidate> candidates;
    std::string error;
};

/// Канонизирует пути и убирает дубликаты (один файл/каталог из нескольких корней).
std::vector<ResolvedCandidate> uniqueCandidates(std::vector<ResolvedCandidate> candidates) {
    std::vector<ResolvedCandidate> unique;
    unique.reserve(candidates.size());
    for (ResolvedCandidate& candidate : candidates) {
        candidate.path = canonicalPath(candidate.path);
        bool seen = false;
        for (const ResolvedCandidate& existing : unique) {
            if (existing.path == candidate.path) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            unique.push_back(std::move(candidate));
        }
    }
    return unique;
}

// ЕДИНСТВЕННЫЙ резолвер пути внутри корня: структурные проверки пути не зависят от режима,
// допустимую форму задаёт вызывающий (`PathForm`).
ResolvedPath resolveInPath(const fs::path& root, const ModulePath& target, PathForm form) {
    ResolvedPath result;
    const std::vector<std::string>& parts = target.components;
    if (parts.empty()) {
        result.error = "invalid path component";
        return result;
    }
    if (target.kind == ModulePath::Kind::File && parts.size() != 1) {
        result.error = "file path cannot have subpaths; use a module path (two leading backslashes)";
        return result;
    }
    for (const std::string& part : parts) {
        if (!validComponent(part)) {
            result.error = "invalid path component '" + part + "'";
            return result;
        }
    }
    fs::path current = root;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        current /= parts[i];
        std::error_code ec;
        if (!fs::is_directory(current, ec)) {
            result.error = "no directory '" + current.string() + "'";
            return result;
        }
    }
    const fs::path dir = current / parts.back();
    const fs::path file = current / (parts.back() + ".src");
    std::error_code ec;
    const bool has_file = fs::is_regular_file(file, ec);
    const bool has_dir = fs::is_directory(dir, ec);
    if (has_file && has_dir) {
        result.error = "both '" + file.string() + "' and directory '" + dir.string() + "' exist; rename one";
        return result;
    }
    if (has_file) {
        if (form == PathForm::Directory) {
            result.error = "module directory expected, found file '" + file.string() + "'";
            return result;
        }
        result.path = file;
        return result;
    }
    if (has_dir) {
        if (form == PathForm::File) {
            result.error = "directory import requires a module path (two leading backslashes)";
            return result;
        }
        result.path = dir;
        result.isDirectory = true;
        return result;
    }
    result.error = "no source file or directory '" + dir.string() + "'";
    return result;
}

/// ЕДИНСТВЕННАЯ точка резолва по корням поиска: перебор корней + канонизация/дедуп.
/// Первая ошибка резолва сохраняется — вызывающий обязан её использовать (без silent fallback).
ResolveResult resolveAcrossRoots(const ModulePath& path, const std::vector<fs::path>& roots, PathForm form) {
    ResolveResult result;
    std::vector<ResolvedCandidate> matches;
    for (const fs::path& root : roots) {
        const ResolvedPath resolved = resolveInPath(root, path, form);
        if (!resolved.error.empty()) {
            if (result.error.empty()) {
                result.error = resolved.error;
            }
            continue;
        }
        matches.push_back(ResolvedCandidate{resolved.path, root, resolved.isDirectory});
    }
    result.candidates = uniqueCandidates(std::move(matches));
    return result;
}

bool addDirectGroupFiles(const fs::path& dir, std::vector<fs::path>& files, std::string& out_error) {
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        std::error_code type_ec;
        const bool regular = entry.is_regular_file(type_ec);
        if (type_ec) {
            out_error = "cannot inspect '" + entry.path().string() + "': " + type_ec.message();
            return false;
        }
        if (regular && entry.path().extension() == ".src") {
            files.push_back(entry.path());
        }
    }
    if (ec) {
        out_error = "cannot scan group '" + dir.string() + "': " + ec.message();
        return false;
    }
    return true;
}

} // namespace

std::vector<fs::path> collectGroupFiles(const fs::path& root, bool packageRoot, std::string& out_error) {
    out_error.clear();
    std::vector<fs::path> files;
    if (!addDirectGroupFiles(root, files, out_error)) {
        return {};
    }
    if (!packageRoot) {
        std::sort(files.begin(), files.end());
        return files;
    }
    std::error_code ec;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        std::error_code type_ec;
        const bool directory = entry.is_directory(type_ec);
        if (type_ec) {
            out_error = "cannot inspect '" + entry.path().string() + "': " + type_ec.message();
            return {};
        }
        if (!directory) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (!name.empty() && name.front() == '.') {
            continue;
        }
        if (!addDirectGroupFiles(entry.path(), files, out_error)) {
            return {};
        }
    }
    if (ec) {
        out_error = "cannot scan package root '" + root.string() + "': " + ec.message();
        return {};
    }
    std::sort(files.begin(), files.end());
    return files;
}

TargetFiles collectTargetFiles(const ModulePath& path, const std::vector<fs::path>& roots, PathForm form) {
    TargetFiles result;
    const ResolveResult resolved = resolveAcrossRoots(path, roots, form);
    if (resolved.candidates.empty()) {
        std::string tried;
        for (const fs::path& root : roots) {
            if (!tried.empty()) {
                tried += ", ";
            }
            tried += root.string();
        }
        result.error = (resolved.error.empty() ? "module not found" : resolved.error) + " (roots tried: " + tried + ")";
        return result;
    }
    if (resolved.candidates.size() > 1) {
        result.error = "module is ambiguous (multiple candidates)";
        return result;
    }
    const ResolvedCandidate& candidate = resolved.candidates.front();
    result.scopeRoot = candidate.root;
    if (!candidate.isDirectory) {
        result.files.push_back(candidate.path);
        return result;
    }
    result.files = collectGroupFiles(candidate.path, /*packageRoot=*/false, result.error);
    return result;
}

std::string moduleGroupKey(const ModulePath& path) {
    if (path.kind == ModulePath::Kind::Package) {
        return "pkg:" + path.package;
    }
    if (path.kind == ModulePath::Kind::File || path.components.empty()) {
        return {}; // корневая группа проекта
    }
    return path.components.front();
}

void appendEnvList(std::vector<fs::path>& out, const char* env_name) {
    const char* env = std::getenv(env_name);
    if (env == nullptr || *env == '\0') {
        return;
    }
    const std::string paths(env);
    std::size_t pos = 0;
    while (pos <= paths.size()) {
        const std::size_t sep = paths.find(':', pos);
        const std::string dir = paths.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos);
        if (!dir.empty()) {
            out.emplace_back(dir);
        }
        if (sep == std::string::npos) {
            break;
        }
        pos = sep + 1;
    }
}

} // namespace trust::module_path
