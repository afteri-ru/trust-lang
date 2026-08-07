// include/ast/term_visitor.hpp
// TermID-visitor: вход ПО TERMID (из X-макроса TERMS).
//
// Это отдельный механизм от KindVisitor (вход по Kind из PARSER_TOKEN_KINDS):
//   - TermID-visitor (этот файл) — КОНВЕРТАЦИЯ Term -> AstNode (TermToAstConverter).
//   - Kind-visitor (kind_visitor.hpp) — АНАЛИЗ и ГЕНЕРАЦИЯ КОДА (analyzeNode, CppTranspiler).
//
// Интерфейс: по одному методу visit_<NAME> на каждую пару _(NAME, Kind) из TERMS
// (записи _(NAME) без Kind и TermID::END в интерфейс НЕ попадают — такие TermID не должны
// конвертироваться в AstNode). Диспетчер dispatchTerm — исчерпывающий switch по TermID.
//
// «Типовые» visit-методы (группы TermID с общим построением: Ident, TypeName, FuncDecl,
// control-flow) генерируются из x-macro автоматически через convertForKind<Kind> (класс узла
// берётся ТОЛЬКО из PARSER_TOKEN_KINDS); спец-термы (MODULE, CREATE_TYPE, CREATE_NAME,
// ARGUMENT) переопределяются в TermToAstConverter. Ручной копипаст однотипных методов НЕ нужен.

#pragma once

#include "ast/token.hpp"
#include "diag/context.hpp"
#include "syntax/term_types.h"
#include "utils/error.hpp"

namespace trust {

/// Абстрактный контракт TermID-visitor.
struct TermVisitor {
#define TRUST_TV_NOCASE(name)
#define TRUST_TV_GENCASE(name, kind) virtual AstNodePtr visit_##name(const trust::TermPtr& term, Context& ctx) = 0;
#define TRUST_TV_GENCASE3(name, kind, T) virtual AstNodePtr visit_##name(const trust::TermPtr& term, Context& ctx) = 0;
#define TRUST_TV_GET(_1, _2, _3, NAME, ...) NAME
#define TRUST_TV_DISPATCH(...) TRUST_TV_GET(__VA_ARGS__, TRUST_TV_GENCASE3, TRUST_TV_GENCASE, TRUST_TV_NOCASE)(__VA_ARGS__)
#define TRUST_TV_CASE(...) TRUST_TV_DISPATCH(__VA_ARGS__)
    TERMS(TRUST_TV_CASE)
#undef TRUST_TV_CASE
#undef TRUST_TV_DISPATCH
#undef TRUST_TV_GET
#undef TRUST_TV_GENCASE3
#undef TRUST_TV_GENCASE
#undef TRUST_TV_NOCASE

    virtual ~TermVisitor() = default;
};

/// Диспетчер по TermID: switch(term->getTermID()) из TERMS.
/// Записи _(NAME) без Kind и TermID::END — default: FAULT (не конвертируются).
/// Определение — в term_to_ast.cpp (нужен полный тип trust::Term).
AstNodePtr dispatchTerm(const trust::TermPtr& term, TermVisitor& visitor, Context& ctx);

/// Базовая реализация TermID-visitor: каждый visit_<NAME> строится через convertForKind<Kind>
/// (generic-путь). Определения методов — в term_to_ast.cpp (нужны полные типы узлов).
struct TermVisitorDefault : TermVisitor {
#define TRUST_TVD_NOCASE(name)
#define TRUST_TVD_GENCASE(name, kind) AstNodePtr visit_##name(const trust::TermPtr& term, Context& ctx) override;
#define TRUST_TVD_GENCASE3(name, kind, T) AstNodePtr visit_##name(const trust::TermPtr& term, Context& ctx) override;
#define TRUST_TVD_GET(_1, _2, _3, NAME, ...) NAME
#define TRUST_TVD_DISPATCH(...) TRUST_TVD_GET(__VA_ARGS__, TRUST_TVD_GENCASE3, TRUST_TVD_GENCASE, TRUST_TVD_NOCASE)(__VA_ARGS__)
#define TRUST_TVD_CASE(...) TRUST_TVD_DISPATCH(__VA_ARGS__)
    TERMS(TRUST_TVD_CASE)
#undef TRUST_TVD_CASE
#undef TRUST_TVD_DISPATCH
#undef TRUST_TVD_GET
#undef TRUST_TVD_GENCASE3
#undef TRUST_TVD_GENCASE
#undef TRUST_TVD_NOCASE

    virtual ~TermVisitorDefault() = default;

  protected:
    /// Типовое построение узла по Kind: Ident→класс-селекция (CallExpr vs IdentName),
    /// control-flow→make_shared<node_type_for_kind_t<K>>+expandControlFlowRange,
    /// прочее→make_shared<node_type_for_kind_t<K>>(K, term, &ctx). Класс узла — из PARSER_TOKEN_KINDS;
    /// раскладку детей строят сами терм-конструкторы узлов.
    template <ParserToken::Kind K>
    AstNodePtr convertForKind(const trust::TermPtr& term, Context& ctx);
};

} // namespace trust
