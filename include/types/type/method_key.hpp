#pragma once
// ИМЯ-ФОРМА члена типа: имя + константность + статичность. Это НЕ полная идентичность вызова:
// полная = owner TypeId × MethodKey × FunctionIdentity (сигнатура). Свободная функция имеет
// симметричную форму: StorageKey(scope,name) × FunctionIdentity.
// Нативность ('%', C++-проекция без манглинга) — свойство ЗАПИСИ (`MethodInfo::isNative`),
// как C++-имя нативной привязки (`DeclCppFacts::cppNameOf`) у свободной функции; в идентичность не входит.
// Строковое представление (`%count^`, `ns::Class::st`) разбирается ОДИН раз на границе.

#include "utils/error.hpp"

#include <string>
#include <string_view>

namespace trust {

struct MethodKey {
    std::string name;      ///< имя члена; у статического — полное `ns::Class::name`
    bool isConst = false;  ///< константный МЕТОД (cv неявного объекта); у поля всегда false
    bool isStatic = false; ///< статический член (форма доступа `Type::name`)
};

/// Разбор trust-spelling члена: хвостовой `^` (const), `::` (static). `%` в имени метода
/// недопустим: нативное имя задаётся только биндингом/записью (`MethodInfo::isNative`).
[[nodiscard]] inline MethodKey methodKeyFromSpelling(std::string_view spelling) {
    EXPECT(spelling.find('%') == std::string_view::npos &&
           "method name is trust-only: '%' marks a native binding, not an identifier");
    MethodKey key;
    if (!spelling.empty() && spelling.back() == '^') {
        key.isConst = true;
        spelling.remove_suffix(1);
    }
    key.name.assign(spelling);
    key.isStatic = key.name.find("::") != std::string::npos;
    return key;
}

[[nodiscard]] inline MethodKey trustMethodKey(std::string name, bool isConst = false) {
    return MethodKey{std::move(name), isConst, false};
}

[[nodiscard]] inline MethodKey staticMethodKey(std::string fullName) {
    MethodKey key;
    key.name = std::move(fullName);
    key.isStatic = true;
    return key;
}

[[nodiscard]] inline bool operator==(const MethodKey& a, const MethodKey& b) noexcept {
    return a.name == b.name && a.isConst == b.isConst && a.isStatic == b.isStatic;
}

[[nodiscard]] inline bool operator<(const MethodKey& a, const MethodKey& b) noexcept {
    if (a.name != b.name) {
        return a.name < b.name;
    }
    if (a.isConst != b.isConst) {
        return a.isConst < b.isConst;
    }
    return a.isStatic < b.isStatic;
}

} // namespace trust