#pragma once

#include "ast/token.hpp"
#include "ast/attr.hpp"
#include "diag/location.hpp"
#include "utils/error.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace trust {

// Forward declaration: TermPtr is defined in syntax/term_types.h (via syntax/term.h).
class Term;
using TermPtr = std::shared_ptr<Term>;

// Forward declarations
class AttrPool;
class AstNodeAttr;

/// AstNodeBase — базовый класс для всех узлов AST.
/// Хранит только kind и указатель на исходный Term (m_term), из которого узел построен.
/// Текст (text()) и диапазон (range()) читаются из m_term; узел без m_term — ошибка логики.
class AstNodeBase {
  public:
    ParserToken::Kind kind() const noexcept { return m_kind; }

    /// Текст узла. Требует m_term (иначе EXPECT/FAULT).
    [[nodiscard]] virtual std::string_view text() const;

    /// Диапазон в исходном файле. Требует m_term (иначе EXPECT/FAULT).
    [[nodiscard]] MapperRange range() const;

    [[nodiscard]] bool empty() const noexcept { return m_kind == ParserToken::Kind::END; }

    /// If this node is an AstNodeAttr, returns itself; otherwise nullptr.
    [[nodiscard]] virtual AstNodeAttr* as_attr() noexcept { return nullptr; }
    [[nodiscard]] virtual const AstNodeAttr* as_attr() const noexcept { return nullptr; }

    /// Dump token contents for debugging
    [[nodiscard]] virtual std::string dump(size_t indent = 0) const;

    AstNodeBase() = default;

    /// Kind is set at construction and never changes (like Clang's StmtClass).
    virtual ~AstNodeBase() = default;

    /// Construct with kind and the source Term this node is derived from.
    // Проверка m_term выполняется только в производных классах, где text()/range()
    // реально читаются из Term (Binary, JumpStmt, CallExpr). Классы с собственным
    // m_text/m_name (IdentName, Literal, Sequence и т.д.) не требуют Term для text().
    AstNodeBase(ParserToken::Kind k, TermPtr term)
    : m_kind(k)
    , m_term(std::move(term)) {}

  protected:
    /// Construct with kind only (no source Term; text()/range() will EXPECT).
    explicit AstNodeBase(ParserToken::Kind k)
    : m_kind(k) {}

    ParserToken::Kind m_kind{ParserToken::Kind::END};

    /// Исходный синтаксический узел, из которого построен этот AST-узел.
    TermPtr m_term;
};

/// AstNodeAttr — расширение AstNodeBase с поддержкой документации и атрибутов.
class AstNodeAttr : public AstNodeBase {
  public:
    const AstNodePtr& docs() const noexcept { return m_docs; }

    /// Read-only access to attributes
    const std::vector<AttrId>& attrs() const noexcept { return m_attrs; }

    AstNodeAttr() = default;

    virtual ~AstNodeAttr() = default;

    /// Construct with kind and the source Term.
    AstNodeAttr(ParserToken::Kind k, TermPtr term)
    : AstNodeBase(k, std::move(term)) {}

    /// Construct with kind only (no source Term).
    explicit AstNodeAttr(ParserToken::Kind k)
    : AstNodeBase(k) {}

    /// Add an attribute ID to this token.
    /// @param manual — set the manual bit (source-level attribute, default true).
    void add_attr(AttrId id, bool manual = true) { m_attrs.push_back(manual ? detail::with_manual(id) : id); }

    /// Check if this token has a specific attribute by ID.
    /// Search is performed by the base index, ignoring the bitmask, so
    /// manual/auto and builtin/user variants match the same attribute.
    [[nodiscard]] bool has_attr(AttrId id) const {
        AttrId idx = id & detail::kAttrIndexMask;
        return std::find_if(m_attrs.begin(), m_attrs.end(), [idx](AttrId stored) { return (stored & detail::kAttrIndexMask) == idx; }) != m_attrs.end();
    }

    /// Check if this token has an attribute by name (delegates to has_attr(AttrId)).
    [[nodiscard]] bool has_attr(const AttrPool& pool, std::string_view name) const;

    [[nodiscard]] AstNodeAttr* as_attr() noexcept override { return this; }
    [[nodiscard]] const AstNodeAttr* as_attr() const noexcept override { return this; }

    /// Dump token contents for debugging
    [[nodiscard]] std::string dump(size_t indent = 0) const override;

  protected:
    AstNodePtr m_docs; ///< The current namespace in the source file when this term is used

    std::vector<AttrId> m_attrs; ///< Attribute IDs attached to this token
};

} // namespace trust