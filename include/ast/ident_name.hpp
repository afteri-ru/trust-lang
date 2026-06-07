// include/ast/ident_name.hpp
// IdentName — AST-узел для идентификатора с методами проверки,
// нормализации, манглинга (перенесено из trust::Ident).

#pragma once

#include "ast/token_base.hpp"
#include "ast/attr_builtin.hpp"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

class AttrPool; // forward declaration

/// IdentName — узел AST для хранения идентификатора.
/// kind всегда ParserToken::Kind::Ident.
/// Имя хранится в m_name / text().
class IdentName : public AstNodeAttr {
  public:
    IdentName()
    : AstNodeAttr() {
        m_kind = ParserToken::Kind::Ident;
    }

    explicit IdentName(std::string name, AttrPool* pool = nullptr);

    /// Конструктор из исходного Term (имя копируется в m_name).
    IdentName(std::string name, TermPtr term, AttrPool* pool = nullptr)
    : AstNodeAttr(ParserToken::Kind::Ident, std::move(term))
    , m_name(std::move(name)) {
        stripCaretAndApplyReadonly(pool);
    }

    [[nodiscard]] std::string_view text() const noexcept override { return m_name; }

    /// Implicit conversion to string_view for compatibility.
    operator std::string_view() const noexcept { return text(); }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    // ── Тип имени ──
    [[nodiscard]] bool is_simple() const noexcept;
    [[nodiscard]] bool is_qualified() const noexcept;
    [[nodiscard]] bool is_special() const noexcept;
    [[nodiscard]] bool is_internal() const noexcept;

    // ── Квалификаторные признаки ──
    [[nodiscard]] bool is_macro() const noexcept;           // начинается с '@'
    [[nodiscard]] bool is_local() const noexcept;           // начинается с '$' (одиночный, не $$)
    [[nodiscard]] bool is_static() const noexcept;          // содержит '::'
    [[nodiscard]] bool is_field() const noexcept;           // начинается с '.'
    [[nodiscard]] bool is_module() const noexcept;          // начинается с '\'
    [[nodiscard]] bool is_type() const noexcept;            // начинается с ':'
    [[nodiscard]] bool is_native() const noexcept;          // начинается с '%'
    [[nodiscard]] bool is_absolute_module() const noexcept; // '\\' в начале
    [[nodiscard]] bool is_relative_module() const noexcept; // '\' в начале (одинарный)

    // ── Специальные имена ──
    [[nodiscard]] bool is_arg_ref() const noexcept;     // $1..$N
    [[nodiscard]] bool is_self() const noexcept;        // $0
    [[nodiscard]] bool is_parent() const noexcept;      // $$
    [[nodiscard]] bool is_args_dict() const noexcept;   // $*
    [[nodiscard]] bool is_last_result() const noexcept; // $^

    // ── Имя без квалификатора ──
    [[nodiscard]] std::string_view bare_name() const noexcept;

    // ── Валидация ──
    static constexpr size_t max_name_length = 64;
    static bool is_valid_simple_name(std::string_view s) noexcept;
    static bool is_valid_module_name(std::string_view s) noexcept;

    // ── Нормализация ──
    [[nodiscard]] IdentName normalized() const;
    [[nodiscard]] bool is_normalized() const noexcept;

    // ── Внутреннее имя ──
    [[nodiscard]] IdentName to_internal() const;

    // ── Разбивка квалифицированного имени на фрагменты ──
    [[nodiscard]] std::vector<std::string_view> parts() const;

    // ── Манглинг / деманглинг ──
    [[nodiscard]] IdentName mangle(std::string_view module_name) const;
    static IdentName demangle(std::string_view mangled);

    // ── Преобразование имени модуля в файловый путь ──
    static std::filesystem::path module_name_to_path(std::string_view module_name, const std::filesystem::path& base_dir,
                                                     const std::filesystem::path& sys_dir = "/");

    // ── Преобразование файлового пути в имя модуля ──
    static IdentName path_to_module_name(const std::filesystem::path& path, const std::filesystem::path& base_dir);

    // ── Сравнение по тексту (для замены std::string) ──
    friend bool operator==(const IdentName& a, const IdentName& b) noexcept { return a.text() == b.text(); }
    friend bool operator!=(const IdentName& a, const IdentName& b) noexcept { return a.text() != b.text(); }
    friend bool operator==(const IdentName& a, std::string_view b) noexcept { return a.text() == b; }
    friend bool operator==(std::string_view a, const IdentName& b) noexcept { return a == b.text(); }

  private:
    void stripCaretAndApplyReadonly(AttrPool* pool);

    std::string m_name; ///< Имя идентификатора (нормализованная строка)
};

} // namespace trust