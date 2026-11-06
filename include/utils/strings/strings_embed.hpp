#pragma once

#include "utils/error.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "utils/strings/strings_text.hpp"
#include "utils/strings/strings_ident.hpp"
#include "utils/strings/strings_names.hpp"

namespace trust::utils {

// Конвертирует текст C++-вставки ({% ... %}) для кодогенерации: идентификаторы,
// начинающиеся с '$' или '@', интерпретируются как trust-имена и заменяются результатом
// `nameToCpp(sigil, name)` (по умолчанию — `nameToCpp`). '$name' - локальное trust-имя;
// '@name' - trust-имя (допускается квалификация '::', напр. @ns::x). Остальной текст не меняется.
// Чтение имени - через extractName (единая точка входа; включает юникод и '::').
// Callback позволяет кодогену учесть trust-namespace имени, не зная деталей преобразования.
template <typename NameToCpp>
inline std::string transformEmbedCpp(std::string_view text, NameToCpp&& nameToCpp) {
    std::string result;
    result.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '$' || c == '@') {
            std::string_view name = extractName(text, i + 1);
            if (!name.empty()) {
                result += nameToCpp(c, name);
                i += 1 + name.size();
                continue;
            }
        }
        result += c;
        ++i;
    }
    return result;
}

/// Преобразование по умолчанию: trust-имя маркера → C++-имя без модульной квалификации.
[[nodiscard]] inline std::string defaultEmbedNameToCpp(char /*sigil*/, std::string_view name) {
    return nameToCpp(name);
}

inline std::string transformEmbedCpp(std::string_view text) {
    return transformEmbedCpp(text, defaultEmbedNameToCpp);
}

// Извлекает trust-имена, на которые ссылается C++-вставка (маркеры $/@).
// Используется семантическим анализатором для валидации в таблице символов.
// Для маркера `$name` возвращается `$name` (локальная переменная; после нормализации имён
// без сигила локальная хранится с `$` - `$d`), для `@name` - `name` (квалифицированное/глобальное).
inline std::vector<std::string> extractEmbedNames(std::string_view text) {
    std::vector<std::string> names;
    size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '$' || c == '@') {
            std::string_view name = extractName(text, i + 1);
            if (!name.empty()) {
                if (c == '$') {
                    names.emplace_back(std::string("$").append(name));
                } else {
                    names.emplace_back(name);
                }
                i += 1 + name.size();
                continue;
            }
        }
        ++i;
    }
    return names;
}

} // namespace trust::utils
