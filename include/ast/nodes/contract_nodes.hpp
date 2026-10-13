#pragma once
// include/ast/nodes/contract_nodes.hpp
// Trust-contract AST nodes: TrustContract and TrustElem (solver terms).

#include "ast/token_base.hpp"
#include "ast/slots/slot_engine.hpp"
#include "ast/binary_op.hpp"
#include "ast/token.hpp"
#include "syntax/check_area.hpp"
#include "ast/ident_name.hpp"
#include "ast/token_type.hpp"
#include "diag/severity.hpp"
#include "ast/trust_prop.hpp"
#include "ast/nodes/z3_term.hpp"
#include "types/type_id.hpp"
#include "utils/error.hpp"
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace trust {

class Context;

/// TrustContract - единый узел trust-контракта (pre/post/assert/invariant/type).
/// Хранит СТРОГО ОДНО логическое выражение (m_expr), разобранное парсером как полноценное
/// логическое выражение (нетерминал `logical`). Тип контракта - в поле kind (PropertyKind);
/// может быть задан явно (`@{ pre --> expr @}` — kind одиночным именем после `@{`, разделитель -->)
/// либо выведен из места привязки. Может находиться:
///   - в m_trust узла-объявления (функция, переменная, тип, цикл);
///   - автономным узлом в последовательности (assert в позиции выражения, `@{ check --> expr @};`).
/// КОНТРАКТ: m_expr — владеемый слот `Internal` (ast/slots/node_slots_x.hpp): в children()/collectChildren
/// не входит, поэтому анализатор и транспилятор игнорируют узел и его содержимое.
class TrustContract : public AstNodeBase {
  public:
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
    AST_SLOT_METHOD(AstNodeBase, SLOTS_TrustContract)
    TrustContract() { m_kind = ParserToken::Kind::TrustContract; }

    /// Терм-конструктор: выражение строится из term->m_right (грамматика
    /// `trust_contract: BEGIN [kind COLON] logical END`); kind резолвится из префикса
    /// `IDENT COLON` (см. ast_base.cpp). Объявлен здесь, определён в ast_base.cpp.
    TrustContract(ParserToken::Kind k, TermPtr term, Context* ctx = nullptr);

    /// Manual-конструктор: выражение задано явно, без TermPtr (test-only/синтетический).
    TrustContract(ParserToken::Kind k, AstNodePtr expr, PropertyKind kind = PropertyKind::kUnknown)
    : AstNodeBase(k)
    , m_expr(std::move(expr))
    , kind(kind) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    AstNodePtr m_expr{};                        ///< Логическое выражение контракта (или nullptr для ручного пустого)
    PropertyKind kind = PropertyKind::kUnknown; ///< Тип контракта (kUnknown - автовывод из места)
};

/// TrustElem - узел термина решателя (SMT/Z3) внутри выражения trust-контракта:
/// `@( term, args... @)` → old/forall/exists/fresh/length/result. Первый аргумент
/// (маркер) резолвится в Z3TermKind::kind; остальные - в m_args. Для кванторов (Forall/Exists)
/// первый аргумент m_args - переменная-связка, последний - тело. НЕ входит в children().
class TrustElem : public AstNodeBase {
  public:
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
    AST_SLOT_METHOD(AstNodeBase, SLOTS_TrustElem)
    TrustElem() { m_kind = ParserToken::Kind::TrustElem; }

    /// Терм-конструктор: kind резолвится из первого аргумента (маркера), остальные аргументы -
    /// в m_args. Объявлен здесь, определён в ast_base.cpp.
    TrustElem(ParserToken::Kind k, TermPtr term, Context* ctx = nullptr);

    /// Manual-конструктор (test-only/синтетический).
    TrustElem(ParserToken::Kind k, Z3TermKind term, std::vector<AstNodePtr> args)
    : AstNodeBase(k)
    , kind(term)
    , m_args(std::move(args)) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    Z3TermKind kind = Z3TermKind::kUnknown; ///< Маркер термина решателя (резолвится из первого аргумента).
    std::vector<AstNodePtr> m_args;         ///< Аргументы (для forall/exists: [var, body]).
    /// Разрешённый тип переменной-связки квантора (`@( forall, i, P @)`): берётся из объявления
    /// `i` ранее (разрешение имён), НЕ выводится. Ставит семантика; solver читает его как сорт.
    /// INVALID_TYPE_ID - не установлен (ошибка на этапе semantic). Переживает таблицу символов.
    TypeId m_boundVarType = INVALID_TYPE_ID;
};

} // namespace trust
