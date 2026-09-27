// src/packages/module_path.cpp
#include "packages/module_path.hpp"

#include "packages/errors.hpp"

namespace trust::packages {

std::string normalizeModulePath(std::string_view path) {
    if (path.empty()) {
        fail("packages.path.invalid", "empty module path");
    }
    if (path.front() == '/') {
        fail("packages.path.invalid", "absolute module path is not allowed: '{}'", path);
    }
    std::string normalized;
    normalized.reserve(path.size());
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t sep = path.find_first_of("/\\", start);
        const std::string_view segment = (sep == std::string_view::npos) ? path.substr(start) : path.substr(start, sep - start);
        if (segment.empty()) {
            fail("packages.path.invalid", "empty segment in module path: '{}'", path);
        }
        if (segment == "." || segment == "..") {
            fail("packages.path.invalid", "'.'/'..' segment in module path: '{}'", path);
        }
        if (!normalized.empty()) {
            normalized.push_back('/');
        }
        normalized.append(segment);
        if (sep == std::string_view::npos) {
            break;
        }
        start = sep + 1;
    }
    return normalized;
}

} // namespace trust::packages
