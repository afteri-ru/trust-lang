// include/parser/naming.hpp
// Ident — класс для обработки идентификаторов по правилам NAMING.md
//
// Ident : public std::string — производный от std::string
// Предоставляет методы для:
//   - проверки типа имени (простое, квалифицированное, специальное, внутреннее)
//   - проверки квалификаторных признаков
//   - нормализации
//   - построения внутреннего имени
//   - манглинга и деманглинга

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace trust {

class Ident : public std::string {
  public:
    using std::string::string;
    Ident() = default;
    Ident(const std::string &s) : std::string(s) {}
    Ident(std::string_view s) : std::string(s) {}
    Ident(const char *s) : std::string(s) {}

    // ── Тип имени ──
    bool is_simple() const noexcept;
    bool is_qualified() const noexcept;
    bool is_special() const noexcept;
    bool is_internal() const noexcept;

    // ── Квалификаторные признаки ──
    bool is_macro() const noexcept;           // начинается с '@'
    bool is_temp() const noexcept;            // начинается с '$' (одиночный, не $$)
    bool is_static() const noexcept;          // содержит '::'
    bool is_field() const noexcept;           // начинается с '.'
    bool is_module() const noexcept;          // начинается с '\'
    bool is_type() const noexcept;            // начинается с ':'
    bool is_native() const noexcept;          // начинается с '%'
    bool is_absolute_module() const noexcept; // '\\' в начале
    bool is_relative_module() const noexcept; // '\' в начале (одинарный)

    // ── Специальные имена ──
    bool is_arg_ref() const noexcept;     // $1..$N
    bool is_self() const noexcept;        // $0
    bool is_parent() const noexcept;      // $$
    bool is_args_dict() const noexcept;   // $*
    bool is_last_result() const noexcept; // $^

    // ── Имя без квалификатора ──
    std::string_view bare_name() const noexcept;

    // ── Иммутабельность ──
    bool has_immutable() const noexcept; // содержит '^'
    Ident without_immutable() const;     // строка без '^'

    // ── Валидация ──
    static constexpr size_t max_name_length = 64;
    static bool is_valid_simple_name(std::string_view s) noexcept;
    static bool is_valid_module_name(std::string_view s) noexcept;

    // ── Нормализация ──
    Ident normalized() const;
    bool is_normalized() const noexcept;

    // ── Внутреннее имя ──
    Ident to_internal() const;

    // ── Разбивка квалифицированного имени на фрагменты ──
    std::vector<std::string_view> parts() const;

    // ── Манглинг / деманглинг ──
    Ident mangle(std::string_view module_name) const;
    static Ident demangle(std::string_view mangled);
};

} // namespace trust