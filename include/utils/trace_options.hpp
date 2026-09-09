#pragma once

// include/utils/trace_options.hpp
// ЕДИНЫЙ источник именованных опций дампа скоупа (`@__DEBUG_SCOPE__(<masks>, key=value, ...)`
// и C++-макроса `TRUST_SCOPE`). Лежит в utils (низший общий слой): именам/значениям опций нужны
// и парсер макроса (`src/syntax`, валидация с диагностикой), и семантика (`semantic/debug_scope.hpp`,
// применение к ScopeDumpOpts). Значения-строки не зависят от AST/семантики.
//
// X-macro: M(EnumSuffix, "name", "allowed values", "description").
// Используется для: enum, таблицы имён/значений/описаний и текста-подсказки со ВСЕМ списком
// поддерживаемых опций (выводится в диагностике при неизвестном имени/значении).

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace trust::trace {

#define TRUST_SCOPE_DUMP_OPTIONS(M)                                                                        \
    M(Level, "level", "current|all", "current - only the current (innermost) scope; all - the whole stack") \
    M(Types, "types", "on|off", "show TypeId and storage for each symbol")                                  \
    M(Max, "max", "<N>", "max names printed per level (0 = unlimited)")                                     \
    M(CountOnly, "count", "only", "print only depth/names totals, without per-level details")

/// Идентификаторы именованных опций дампа (генерируются из TRUST_SCOPE_DUMP_OPTIONS).
enum class ScopeDumpOptionId : int {
#define X(Name, str, values, desc) Name,
    TRUST_SCOPE_DUMP_OPTIONS(X)
#undef X
        Count,
};

/// Таблица (имя, допустимые значения, описание), генерируемая из x-macro.
inline constexpr struct {
    const char* name;
    const char* values;
    const char* desc;
} kScopeDumpOptions[] = {
#define X(Name, str, values, desc) {str, values, desc},
    TRUST_SCOPE_DUMP_OPTIONS(X)
#undef X
};

/// Имя опции по идентификатору.
constexpr const char* scopeDumpOptionName(ScopeDumpOptionId id) {
    return kScopeDumpOptions[static_cast<int>(id)].name;
}

/// Допустимые значения опции (для диагностики).
constexpr const char* scopeDumpOptionValues(ScopeDumpOptionId id) {
    return kScopeDumpOptions[static_cast<int>(id)].values;
}

/// Описание опции (для справки).
constexpr const char* scopeDumpOptionDesc(ScopeDumpOptionId id) {
    return kScopeDumpOptions[static_cast<int>(id)].desc;
}

/// Поиск идентификатора опции по имени (точное совпадение).
inline std::optional<ScopeDumpOptionId> scopeDumpOptionByName(std::string_view name) {
    for (int i = 0; i < static_cast<int>(ScopeDumpOptionId::Count); ++i) {
        if (kScopeDumpOptions[i].name == name) {
            return static_cast<ScopeDumpOptionId>(i);
        }
    }
    return std::nullopt;
}

/// Полный список поддерживаемых опций с допустимыми значениями, напр.:
/// `level=current|all, names=<masks>, types=on|off, max=<N>, count=only`.
/// Выводится в диагностике при неизвестном имени/значении.
inline std::string scopeDumpOptionsUsage() {
    std::string out;
    for (int i = 0; i < static_cast<int>(ScopeDumpOptionId::Count); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += kScopeDumpOptions[i].name;
        out += "=";
        out += kScopeDumpOptions[i].values;
    }
    return out;
}

/// Снятие обрамляющих кавычек у значения-строки (`names="x*"` -> `x*`); значение возвращается как
/// view внутрь исходного аргумента.
constexpr std::string_view stripQuotes(std::string_view v) {
    if (v.size() >= 2 && v.front() == v.back() && (v.front() == '"' || v.front() == '\'')) {
        return v.substr(1, v.size() - 2);
    }
    return v;
}

/// Проверка допустимости значения опции (с учётом именованных наборов и числового `max`).
/// false + err при недопустимом значении. Пустое значение недопустимо для любой опции.
inline bool validateScopeDumpOptionValue(ScopeDumpOptionId id, std::string_view value, std::string& err) {
    const auto name = std::string(scopeDumpOptionName(id));
    const auto values = std::string(scopeDumpOptionValues(id));
    if (value.empty()) {
        err = "'" + name + "' requires a value (expected: " + values + ")";
        return false;
    }
    switch (id) {
    case ScopeDumpOptionId::Level:
        if (value != "current" && value != "all") {
            err = "'" + name + "' must be one of '" + values + "', got '" + std::string(value) + "'";
            return false;
        }
        return true;
    case ScopeDumpOptionId::Types:
        if (value != "on" && value != "off") {
            err = "'" + name + "' must be one of '" + values + "', got '" + std::string(value) + "'";
            return false;
        }
        return true;
    case ScopeDumpOptionId::CountOnly:
        if (value != "only") {
            err = "'" + name + "' must be '" + values + "', got '" + std::string(value) + "'";
            return false;
        }
        return true;
    case ScopeDumpOptionId::Max:
        for (const char c : value) {
            if (c < '0' || c > '9') {
                err = "'" + name + "' must be a non-negative integer, got '" + std::string(value) + "'";
                return false;
            }
        }
        return true;
    case ScopeDumpOptionId::Count:
        break; // сентинел: недостижимо
    }
    return true;
}

/// Разбор одного именованного аргумента `key=value`: id + значение.
/// false + err при отсутствии '=', неизвестном имени или недопустимом значении.
inline bool parseScopeDumpOption(std::string_view arg, ScopeDumpOptionId& id, std::string_view& value, std::string& err) {
    const std::size_t eq = arg.find('=');
    if (eq == std::string_view::npos) {
        err = "expected a named option 'key=value', got '" + std::string(arg) + "'";
        return false;
    }
    const std::string_view key = arg.substr(0, eq);
    // Значение опции можно задавать без кавычек (`level=all`, `max=10`) либо строкой-фильтром
    // (`names="x*"`); кавычки снимаются.
    value = stripQuotes(arg.substr(eq + 1));
    const auto found = scopeDumpOptionByName(key);
    if (!found.has_value()) {
        err = "unknown option '" + std::string(key) + "'";
        return false;
    }
    id = *found;
    return validateScopeDumpOptionValue(id, value, err);
}

} // namespace trust::trace
