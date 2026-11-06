#pragma once

// ЕДИНЫЙ источник трёх уровней видимости области имён И варианта наследования базового класса.
// Одна таблица `TRUST_VISIBILITY_LEVELS` задаёт: имя enum, C++-ключевое слово и имя области
// (маркер = имя области + `::`). Из неё ПРОИЗВОДЯТСЯ enum, ключевое слово и разбор spelling —
// отдельного перечисления строк нет.
//   private `__::`, protected `_::`, public `::`.
// Дефолт при отсутствии маркера — Public (и для видимости, и для наследования).
// Аналог реестра областей `syntax/check_area.hpp`: новый уровень добавляется ТОЛЬКО здесь.

#include <cstdint>
#include <optional>
#include <string_view>

#define TRUST_VISIBILITY_LEVELS(M) \
    M(Public, "public", "")        \
    M(Protected, "protected", "_") \
    M(Private, "private", "__")

namespace trust {

/// Уровень видимости области имён = вариант наследования базы (C++ public/protected/private).
enum class Visibility : uint8_t {
#define TRUST_VISIBILITY_ENUM(name, keyword, area) name,
    TRUST_VISIBILITY_LEVELS(TRUST_VISIBILITY_ENUM)
#undef TRUST_VISIBILITY_ENUM
};

/// C++-ключевое слово уровня (печать `: <kw> Base`).
[[nodiscard]] constexpr std::string_view visibilityKeyword(Visibility level) noexcept {
    switch (level) {
#define TRUST_VISIBILITY_KEYWORD(name, keyword, area) \
    case Visibility::name:                            \
        return keyword;
        TRUST_VISIBILITY_LEVELS(TRUST_VISIBILITY_KEYWORD)
#undef TRUST_VISIBILITY_KEYWORD
    }
    return {}; // недостижимо: switch исчерпывающий
}

/// Разбор spelling области: маркер (`::`/`_::`/`__::`), имя области (`""`/`"_"`/`"__"`) или
/// анонимный блок (`"{"`). nullopt — ИМЕНОВАННАЯ область (`ns::`), уровень видимости не выражает.
[[nodiscard]] constexpr std::optional<Visibility> visibilityOf(std::string_view spelling) noexcept {
    std::string_view area = (spelling == "{") ? std::string_view{} : spelling;
    if (area.size() >= 2 && area.ends_with("::")) {
        area.remove_suffix(2);
    }
#define TRUST_VISIBILITY_MATCH(name, keyword, areaStr) \
    if (area == areaStr) {                             \
        return Visibility::name;                       \
    }
    TRUST_VISIBILITY_LEVELS(TRUST_VISIBILITY_MATCH)
#undef TRUST_VISIBILITY_MATCH
    return std::nullopt;
}

} // namespace trust
