// token_type.hpp - специализированные AST-узлы: IdentName и IdentType
//
// Иерархия:
//   AstNodeBase
//     └-- IdentName    (kind = Ident, имя в text())
//          └-- IdentType (kind = TypeName, добавляет m_dims, m_params)

#pragma once

#include "ast/ident_name.hpp"
#include "ast/slots/slot_engine.hpp"
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
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
    AST_SLOT_METHOD(IdentName, SLOTS_IdentType)
    IdentType() { m_kind = ParserToken::Kind::TypeName; }

    explicit IdentType(std::string name)
    : IdentName(ParserToken::Kind::TypeName, std::string_view(name), nullptr) {}

    /// Терм-конструктор: имя читается из Term; правило имени (срез ведущего ':' и хвостового '^')
    /// применяет assignNameFromRaw в базовом IdentName.
    IdentType(TermPtr term, std::optional<std::vector<AstNodePtr>> dims = std::nullopt, std::optional<std::vector<AstNodePtr>> params = std::nullopt,
              AttrPool* pool = nullptr)
    : IdentName(term, ParserToken::Kind::TypeName, pool)
    , m_dims(std::move(dims))
    , m_params(std::move(params)) {}

    /// Uniform term-constructor for the generated factory (kind всегда TypeName).
    /// При ctx != nullptr сам строит детей: m_dims из term->m_type (ARGS-терм `[...]`),
    /// m_params из term->m_args (call-аргументы `(...)`). Объявлен здесь, определён в token_type.cpp.
    IdentType(ParserToken::Kind /*k*/, TermPtr term, Context* ctx = nullptr);

    /// Read-only access to dims/params
    const auto& dims() const noexcept { return m_dims; }
    const auto& params() const noexcept { return m_params; }

    /// Истина, если это нативный шаблон-тип `vector<Int32>` (template-аргументы в m_templateArgs).
    /// Отличие от кортежных параметров `:Type(...)` (m_params).
    [[nodiscard]] bool isTemplate() const noexcept { return m_templateArgs.has_value(); }
    const auto& templateArgs() const noexcept { return m_templateArgs; }
    /// Устанавливает типовые аргументы нативного шаблона (visit_TYPE для `vector<Int32>`).
    void setTemplateArgs(std::vector<AstNodePtr> args) { m_templateArgs = std::move(args); }
    /// Параллельные флаги pack-expansion для templateArgs (`Tuple<:T...>`).
    void setTemplateArgPacks(std::vector<bool> packs) { m_templateArgPacks = std::move(packs); }
    const auto& templateArgPacks() const noexcept { return m_templateArgPacks; }

    /// Материализованный семантикой тип аннотации с квалификаторами (const/reftype/policy/impl).
    /// Невалиден, если аннотация без квалификаторов. Кодоген рендерит его без повторного применения.
    mutable QualType resolvedState{};

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

  protected:
    std::optional<std::vector<AstNodePtr>> m_dims;   ///< Из [...] в `:TypeName[...](...)`
    std::optional<std::vector<AstNodePtr>> m_params; ///< Из (...) в `:TypeName[...](...)`
    /// Типовые аргументы нативного шаблона `vector<Int32>` (из `LT template_args GT`).
    /// Каждый аргумент - узел типа (IdentType/IdentName). Наличие = это шаблон-тип.
    std::optional<std::vector<AstNodePtr>> m_templateArgs;
    /// Параллельные флаги pack-expansion (`:T...`): true - аргумент является PackExpansion(T).
    std::optional<std::vector<bool>> m_templateArgPacks;
};

} // namespace trust
