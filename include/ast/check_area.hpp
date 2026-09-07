#pragma once

// include/ast/check_area.hpp
// Реестр синтаксических областей для встроенного макроса `@__CHECK_AREA__(...)`.
//
// ЕДИНСТВЕННЫЙ источник списка областей и их описаний - x-macro TRUST_CHECK_AREAS.
// Из него генерируются: enum AreaKind, таблицы имён/описаний, автопарсинг имени
// области и список для справки `-Whelp-check-areas`. Область - синтаксическая
// конструкция, содержащая выражения/инструкции (AST-узлы); "внутри области" выводится
// из ЕДИНОГО скоуп-стека (создатели скоупов в NameResolutionPass), без отдельного
// параллельного стека. Также здесь - enum поведения диагностики @__CHECK_AREA__.

#include <optional>
#include <string_view>

namespace trust {

// X-macro: M(EnumName, "имя", ("описание для справки")). Описание в скобках, чтобы
// запятые в тексте не разбивали аргумент макроса.
#define TRUST_CHECK_AREAS(M)                                           \
    M(Module, "module", ("Module (top-level compilation unit)"))       \
    M(Namespace, "namespace", ("Namespace body"))                      \
    M(Class, "class", ("Class body"))                                  \
    M(Method, "method", ("Function declared inside a class (method)")) \
    M(Function, "function", ("Free function body"))                    \
    M(Template, "template", ("Generic/template declaration body"))     \
    M(Block, "block", ("Block / lexical scope"))                       \
    M(While, "while", ("while loop body"))                             \
    M(DoWhile, "dowhile", ("do-while loop body"))                      \
    M(Repeat, "repeat", ("repeat loop body"))                          \
    M(Loop, "loop", ("Any loop (while/do-while/repeat)"))              \
    M(With, "with", ("with context-manager body"))                     \
    M(Try, "try", ("try body"))                                        \
    M(TryError, "try_error", ("try_error body"))                       \
    M(TryReturn, "try_return", ("try_return body"))                    \
    M(Catch, "catch", ("catch block"))                                 \
    M(If, "if", ("if statement (whole if, any branch: then/elseif/else)")) \
    M(Match, "match", ("match statement (whole match, any case/default)"))

/// Синтаксическая область (значение первого аргумента @__CHECK_AREA__).
enum class AreaKind : int {
#define CHECK_AREA_ENUM(name, str, desc) name,
    TRUST_CHECK_AREAS(CHECK_AREA_ENUM)
#undef CHECK_AREA_ENUM
        Count
};

/// Метаданные области (имя + описание для справки).
struct CheckAreaInfo {
    const char* name;
    const char* desc;
};

inline constexpr CheckAreaInfo kCheckAreas[] = {
#define CHECK_AREA_INFO(name, str, desc) {str, desc},
    TRUST_CHECK_AREAS(CHECK_AREA_INFO)
#undef CHECK_AREA_INFO
};

[[nodiscard]] constexpr std::string_view areaKindName(AreaKind a) noexcept {
    const int i = static_cast<int>(a);
    return (i >= 0 && i < static_cast<int>(AreaKind::Count)) ? kCheckAreas[i].name : std::string_view{};
}

[[nodiscard]] constexpr std::string_view areaKindDesc(AreaKind a) noexcept {
    const int i = static_cast<int>(a);
    return (i >= 0 && i < static_cast<int>(AreaKind::Count)) ? kCheckAreas[i].desc : std::string_view{};
}

/// Имя области -> AreaKind (nullopt, если неизвестно). Нечувствительно к обрамляющим кавычкам.
[[nodiscard]] inline std::optional<AreaKind> areaKindFromString(std::string_view name) noexcept {
    if (name.size() >= 2 && name.front() == name.back() && (name.front() == '\'' || name.front() == '"')) {
        name = name.substr(1, name.size() - 2);
    }
    for (int i = 0; i < static_cast<int>(AreaKind::Count); ++i) {
        if (kCheckAreas[i].name == name) {
            return static_cast<AreaKind>(i);
        }
    }
    return std::nullopt;
}

} // namespace trust
