#ifndef TRUST_UTILS_URI_HPP
#define TRUST_UTILS_URI_HPP

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace trust::utils {

// ── Преобразование file:// URI в путь с URL-decoding ──
// Отсекает фрагмент #L{line},{col}-{line},{col} если есть
inline std::string uriToFilePath(const std::string& uri) {
    if (uri.rfind("file://", 0) != 0) {
        return uri;
    }
    // Отсекаем фрагмент (часть после #)
    std::string path;
    auto hashPos = uri.find('#', 7);
    if (hashPos != std::string::npos) {
        path = uri.substr(7, hashPos - 7);
    } else {
        path = uri.substr(7); // срезаем "file://"
    }

    // URL-decoding: %XX → символ
    std::string decoded;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '%' && i + 2 < path.size()) {
            char hex[3] = {path[i + 1], path[i + 2], '\0'};
            char* end = nullptr;
            long val = std::strtol(hex, &end, 16);
            if (end == hex + 2) {
                decoded += static_cast<char>(val);
                i += 2;
                continue;
            }
        }
        decoded += path[i];
    }
    return decoded;
}

// ── Преобразование пути в "file://" URI ──
inline std::string filePathToUri(const std::string& path) {
    if (path.rfind("file://", 0) == 0)
        return path;
    return "file://" + std::filesystem::absolute(path).string();
}

// ── Превращает относительный путь в абсолютный, используя projectDir ──
inline std::string resolvePath(const std::string& path, const std::string& projectDir) {
    if (path.empty() || path[0] == '/')
        return path;
    if (projectDir.empty())
        return path;
    return std::filesystem::path(projectDir) / path;
}

} // namespace trust::utils

#endif // TRUST_UTILS_URI_HPP