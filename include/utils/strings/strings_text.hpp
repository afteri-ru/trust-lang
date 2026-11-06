#pragma once

#include "utils/error.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace trust::utils {

/// Имя файла без каталогов (после последнего '/' или '\\'); пустая строка - если путь пуст.
inline std::string_view fileNameOf(std::string_view path) noexcept {
    const std::size_t slash = path.find_last_of("/\\");
    return (slash == std::string_view::npos) ? path : path.substr(slash + 1);
}

/// Glob-сравнение: `*` - любая (возможно пустая) последовательность, `?` - ровно один
/// любой символ, остальные - литеральные. Случай-чувствительно.
constexpr bool globMatch(std::string_view pattern, std::string_view name) noexcept {
    std::size_t p = 0;
    std::size_t n = 0;
    std::size_t star = std::string_view::npos;
    std::size_t retry = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == name[n])) {
            ++p;
            ++n;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            retry = n;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            n = ++retry;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

/// Имя содержит namespace-квалификацию (`ns::name` / `ns::sub::name`).
/// Единый предикат: не заменять на локальный `find("::")` в потребителях.
inline bool isQualifiedName(std::string_view name) noexcept {
    return name.find("::") != std::string_view::npos;
}

/// Короткое имя квалифицированного trust-имени (часть после последнего "::").
inline std::string_view shortNameOf(std::string_view qualifiedName) noexcept {
    const std::size_t sep = qualifiedName.rfind("::");
    return sep == std::string_view::npos ? qualifiedName : qualifiedName.substr(sep + 2);
}

/// Namespace-путь квалифицированного trust-имени (часть до последнего "::").
inline std::string_view namespacePathOf(std::string_view qualifiedName) noexcept {
    const std::size_t sep = qualifiedName.rfind("::");
    return sep == std::string_view::npos ? std::string_view{} : qualifiedName.substr(0, sep);
}

/// Квалифицированное trust-имя из namespace-пути и короткого имени (пустой путь - короткое
/// имя). ЕДИНСТВЕННАЯ точка сборки trust-имени: потребители не склеивают путь и имя вручную.
/// Живёт в utils (а не в types/storage_key.hpp): имя нужно слоям ниже types (`ast`).
inline std::string qualifyTrustName(std::string_view namespacePath, std::string_view spelling) {
    if (namespacePath.empty()) {
        return std::string(spelling);
    }
    std::string result;
    result.reserve(namespacePath.size() + 2 + spelling.size());
    result.append(namespacePath);
    result.append("::");
    result.append(spelling);
    return result;
}

/// Имя namespace-алиаса без маркеров-двоеточий: `ns::`/`::ns::` -> `ns`. ЕДИНСТВЕННАЯ точка
/// нормализации записи алиаса (парсер/семантика/загрузчик не срезают `:` вручную).
inline std::string_view namespaceAliasName(std::string_view spelling) noexcept {
    while (!spelling.empty() && spelling.back() == ':') {
        spelling.remove_suffix(1);
    }
    while (!spelling.empty() && spelling.front() == ':') {
        spelling.remove_prefix(1);
    }
    return spelling;
}

/// Сокращённый путь для вывода: каталоги заменяются многоточием (`.../file.ext`).
/// Если каталогов нет - возвращает имя файла как есть.
inline std::string shortenedPath(std::string_view path) {
    const std::string_view name = fileNameOf(path);
    if (name.size() == path.size()) {
        return std::string(name);
    }
    return ".../" + std::string(name);
}

/// Префиксует КАЖДУЮ строку текста заданным префиксом (обычно `.../file:строка: `).
/// Хвостовой '\n' не создаёт лишней строки; результат всегда заканчивается '\n'.
inline std::string prefixEachLine(std::string_view text, std::string_view prefix) {
    if (!text.empty() && text.back() == '\n') {
        text.remove_suffix(1);
    }
    std::string out;
    out.reserve(text.size() + prefix.size());
    std::size_t start = 0;
    while (true) {
        const std::size_t nl = text.find('\n', start);
        const std::string_view line = text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        out += prefix;
        out += line;
        out += '\n';
        if (nl == std::string_view::npos) {
            break;
        }
        start = nl + 1;
    }
    return out;
}

inline std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

inline bool isNumber(std::string_view s) {
    if (s.empty()) {
        return false;
    }
    size_t i = 0;
    if (s[0] == '-') {
        i = 1;
        if (i >= s.size()) {
            return false; // только минус - не число
        }
    }
    for (; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

inline std::vector<std::string_view> tokenize(std::string_view s) {
    std::vector<std::string_view> tokens;
    const char* ptr = s.data();
    size_t len = s.size();
    size_t i = 0;
    while (i < len) {
        while (i < len && (ptr[i] == ' ' || ptr[i] == '\t')) {
            ++i;
        }
        if (i >= len) {
            break;
        }
        size_t start = i;
        while (i < len && ptr[i] != ' ' && ptr[i] != '\t') {
            ++i;
        }
        tokens.push_back(std::string_view(ptr + start, i - start));
    }
    return tokens;
}

} // namespace trust::utils
