#include "stdlib/name_utils.hpp"
#include "stdlib/patterns.hpp"
#include "stdlib/matcher.hpp"

namespace trust {

// ─────────────────────────────────────────────────────────────
// short_name — извлекает последний компонент после "::"
// ─────────────────────────────────────────────────────────────
std::string short_name(const std::string& qualified) {
    auto pos = qualified.rfind("::");
    return (pos != std::string::npos) ? qualified.substr(pos + 2) : qualified;
}

// ─────────────────────────────────────────────────────────────
// class_name — извлекает родительский класс/namespace
// ─────────────────────────────────────────────────────────────
std::string class_name(const std::string& qualified) {
    auto pos = qualified.rfind("::");
    if (pos == std::string::npos)
        return qualified;
    return qualified.substr(0, pos);
}

// ─────────────────────────────────────────────────────────────
// remove_template_args — убирает всё между '<' и '>'
// ─────────────────────────────────────────────────────────────
std::string remove_template_args(const std::string& name) {
    std::string result;
    int depth = 0;
    for (char c : name) {
        if (c == '<') {
            depth++;
            continue;
        }
        if (c == '>') {
            depth--;
            continue;
        }
        if (depth == 0)
            result += c;
    }
    return result;
}

// ─────────────────────────────────────────────────────────────
// is_internal_name — проверка, что любой компонент начинается с '_'
// ─────────────────────────────────────────────────────────────
bool is_internal_name(const std::string& qualified) {
    size_t start = 0;
    while (true) {
        auto pos = qualified.find("::", start);
        std::string component = (pos == std::string::npos) ? qualified.substr(start) : qualified.substr(start, pos - start);
        if (!component.empty() && component[0] == '_')
            return true;
        if (pos == std::string::npos)
            break;
        start = pos + 2;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────
// matches_ignore_pattern — единая проверка игнор-паттернов
// ─────────────────────────────────────────────────────────────
bool matches_ignore_pattern(const std::string& name) {
    const auto& ignore_patterns = get_ignore_patterns();
    for (const auto& pattern : ignore_patterns) {
        if (PatternMatchesString(name, pattern.data(), pattern.data() + pattern.size())) {
            return true;
        }
        std::string prefix = pattern + "::";
        if (name.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────
// count_occurrences — количество вхождений подстроки
// ─────────────────────────────────────────────────────────────
size_t count_occurrences(const std::string& str, const std::string& sub) {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = str.find(sub, pos)) != std::string::npos) {
        ++count;
        pos += sub.size();
    }
    return count;
}

} // namespace trust