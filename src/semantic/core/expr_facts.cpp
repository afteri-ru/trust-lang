// src/semantic/core/expr_facts.cpp
// Единственный читатель/писатель type-state выражения в полях узлов AST.
#include "types/type_read.hpp"
#include "semantic/typer/literal_annotation.hpp"
#include "semantic/typer/type_inference.hpp"
#include "semantic/core/expr_facts.hpp"

#include "semantic/core/type_resolver.hpp"
#include "session/context.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/token_type.hpp"
#include "diag/diag.hpp"
#include "types/type_system.hpp"
#include "types/names.hpp"

namespace trust {

ExprFacts::ExprFacts(Context& ctx, const TypeResolution& types)
: m_ctx(ctx)
, m_types(types) {
}

QualType ExprFacts::exprState(const AstNodeBase& node) const {
    // ЧТЕНИЕ МАТЕРИАЛИЗОВАННОГО ФАКТА из поля узла: результат не зависит от завершённости
    // обхода (единый контракт для ядра и хуков).
    const ParserToken::Kind kind = node.kind();
    if (kind == ParserToken::Kind::Ident) {
        const auto& ident = *node.as<IdentName>();
        // Тип ЗНАЧЕНИЯ-ссылки в точке использования (материализует lookupOrError).
        if (ident.valueType.isValid()) {
            return ident.valueType;
        }
        // Тип-имя (`Color.RED`, `Cls.st()`): тип обозначаемого типа уже материализован
        // семантикой в resolvedTypeId (не зависит от SymbolTable).
        return QualType::fromTypeId(ident.resolvedTypeId);
    }
    if (kind == ParserToken::Kind::CallExpr) {
        // Единственный носитель типа результата вызова: resultState (resultType — тип-цель
        // конструктора шаблона, которая зеркалируется в resultState при её установке).
        return node.as<CallExpr>()->resultState;
    }
    if (kind == ParserToken::Kind::ArgNode) {
        return node.as<ArgNode>()->resultType;
    }
    if (kind == ParserToken::Kind::RangeExpr) {
        return node.as<RangeExpr>()->resultState;
    }
    if (kind == ParserToken::Kind::Filling) {
        // Замыкающее многоточие не является самостоятельным выражением: его тип — тип операнда.
        if (const auto* seq = node.as<Sequence>(); seq != nullptr && !seq->m_body.empty() && seq->m_body[0]) {
            return exprState(*seq->m_body[0]);
        }
        return QualType{};
    }
    if (node.is<Binary>()) {
        return node.as<Binary>()->resultType;
    }
    if (is_literal_kind(node.kind())) {
        const auto& lit = *node.as<Literal>();
        if (lit.typeState.isValid()) {
            return lit.typeState;
        }
        // Литерал ещё не типизирован (объявление enum/variant анализируется до обхода детей):
        // тип детерминированно выводится из аннотации/текста — это НЕ fallback, а тот же
        // единый решатель, что и в typeLiteralNode.
        const TypeReadModel& reg = m_ctx.types();
        if (lit.typeAnnotation) {
            return QualType::fromTypeId(annotatedLiteralType(lit, m_types.resolveTypeRef(*lit.typeAnnotation), reg).type).withInferred();
        }
        return QualType::fromTypeId(literalType(lit, reg)).withInferred();
    }
    if (node.is<RefMakeExpr>()) {
        return node.as<RefMakeExpr>()->m_resultType;
    }
    if (node.is<RefTakeExpr>()) {
        return node.as<RefTakeExpr>()->m_resultType;
    }
    if (node.is<DictLiteralNode>()) {
        const auto& dl = *node.as<DictLiteralNode>();
        if (dl.kind() == ParserToken::Kind::ArrayInit && dl.arrayType.isValid()) {
            return dl.arrayType;
        }
        if (dl.tensorType.isValid()) {
            return dl.tensorType;
        }
        return dl.resultState;
    }
    if (node.is<VarDecl>()) {
        return node.as<VarDecl>()->inferredType;
    }
    if (node.is<FuncDecl>()) {
        const auto* fn = node.as<FuncDecl>();
        if (fn->isLambda()) {
            // Тип лямбды — детерминированная функция объявления (реестр доступен).
            return QualType::fromTypeId(m_types.buildFuncType(*fn));
        }
    }
    // Не-значения (тип-имена/наборы типов, маркеры, ErrorExpr-recovery, embed, HIR-сахар
    // RefLock/VarRef) не имеют type-state: INVALID — осознанный контракт, а не пропуск.
    return QualType{};
}

TypeId ExprFacts::exprType(const AstNodeBase& node) const {
    // IDENTITY-only: единый контракт для всех узлов (в т.ч. Ident). Полное состояние - exprState.
    return exprState(node).getTypeId();
}

void ExprFacts::setExprType(AstNodeBase* node, TypeId id) {
    if (node != nullptr) {
        EXPECT(isUnqualifiedType(id) && "setExprType: expression type must not carry type-state flags; use setExprState");
        setExprState(node, QualType::fromTypeId(id));
    }
}

void ExprFacts::setExprState(AstNodeBase* node, QualType state) {
    if (node == nullptr) {
        return;
    }
    // ЕДИНСТВЕННАЯ точка записи type-state выражения: поле узла-носителя (см. exprState).
    const ParserToken::Kind kind = node->kind();
    if (kind == ParserToken::Kind::Ident) {
        node->as<IdentName>()->valueType = state;
        return;
    }
    if (kind == ParserToken::Kind::CallExpr) {
        node->as<CallExpr>()->resultState = state;
        return;
    }
    if (kind == ParserToken::Kind::ArgNode) {
        node->as<ArgNode>()->resultType = state;
        return;
    }
    if (kind == ParserToken::Kind::RangeExpr) {
        node->as<RangeExpr>()->resultState = state;
        return;
    }
    if (node->is<Binary>()) {
        node->as<Binary>()->resultType = state;
        return;
    }
    if (is_literal_kind(node->kind())) {
        node->as<Literal>()->typeState = state;
        return;
    }
    if (node->is<RefMakeExpr>()) {
        node->as<RefMakeExpr>()->m_resultType = state;
        return;
    }
    if (node->is<RefTakeExpr>()) {
        node->as<RefTakeExpr>()->m_resultType = state;
        return;
    }
    if (node->is<DictLiteralNode>()) {
        node->as<DictLiteralNode>()->resultState = state;
        return;
    }
    if (node->is<VarDecl>()) {
        node->as<VarDecl>()->inferredType = state;
        return;
    }
    if (node->is<FuncDecl>()) {
        EXPECT(node->as<FuncDecl>()->isLambda() && "setExprState: only a lambda FuncDecl carries an expression type");
        return; // тип лямбды выводится из объявления (exprState -> buildFuncType)
    }
    if (kind == ParserToken::Kind::Filling || kind == ParserToken::Kind::Ellipsis) {
        return; // тип выводится из операнда (exprState)
    }
    FAULT("setExprState: kind '{}' has no type-state storage", ParserToken::name(kind));
}

} // namespace trust
