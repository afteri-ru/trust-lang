// token_type.hpp - специализированные AST-узлы: IdentName и IdentType
//
// Иерархия:
//   AstNodeBase
//     └-- IdentName    (kind = Ident, имя в text())
//          └-- IdentType (kind = TypeName, добавляет m_dims, m_params)

#pragma once

#include "ast/ident_name.hpp"
#include "ast/token.hpp"
#include <optional>
#include <string>
#include <vector>

namespace trust {

/// IdentType - узел AST для хранения типа вида :Ident[...](...).
/// kind всегда ParserToken::Kind::TypeName.
/// Имя типа (то, что после ':') хранится в унаследованном text().
class IdentType : public IdentName {
  public:
    IdentType() { m_kind = ParserToken::Kind::TypeName; }

    explicit IdentType(std::string name)
    : IdentName(std::move(name)) {
        m_kind = ParserToken::Kind::TypeName;
    }

    /// Терм-конструктор: имя читается из Term; kind=TypeName нормализует через
    /// normalizeTermText (срез ведущего ':' и хвостового '^') в базовом IdentName.
    IdentType(TermPtr term, std::optional<std::vector<AstNodePtr>> dims = std::nullopt, std::optional<std::vector<AstNodePtr>> params = std::nullopt)
    : IdentName(std::move(term), ParserToken::Kind::TypeName)
    , m_dims(std::move(dims))
    , m_params(std::move(params)) {}

    /// Uniform term-constructor for the generated factory (kind всегда TypeName).
    /// При ctx != nullptr сам строит детей: m_dims из term->m_type (ARGS-терм `[...]`),
    /// m_params из term->m_args (call-аргументы `(...)`). Объявлен здесь, определён в token_type.cpp.
    IdentType(ParserToken::Kind /*k*/, TermPtr term, Context* ctx = nullptr);

    /// Read-only access to dims/params
    const auto& dims() const noexcept { return m_dims; }
    const auto& params() const noexcept { return m_params; }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

  protected:
    std::optional<std::vector<AstNodePtr>> m_dims;   ///< Из [...] в `:TypeName[...](...)`
    std::optional<std::vector<AstNodePtr>> m_params; ///< Из (...) в `:TypeName[...](...)`
};

} // namespace trust