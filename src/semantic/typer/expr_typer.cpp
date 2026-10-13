#include "types/type_read.hpp"
#include "semantic/typer/literal_annotation.hpp"
#include "semantic/typer/expr_typer.hpp"
#include "semantic/typer/binary_typer.hpp"
#include "semantic/typer/container_typer.hpp"
#include "semantic/typer/var_typer.hpp"
#include "semantic/typer/call_typer.hpp"
#include "semantic/typer/method_call_typer.hpp"
#include "semantic/typer/ref_typer.hpp"
#include "semantic/core/name_resolution.hpp"
#include "semantic/typer/ellipsis.hpp"
#include "semantic/verify/format_check.hpp"
#include "semantic/solver.hpp"
#include "symbols/symbol_table.hpp"
#include "semantic/typer/type_inference.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "semantic/ref/ref_kind.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "utils/strings.hpp"
#include <algorithm>
#include <format>
#include <string>

namespace trust {

ExprTyper::ExprTyper(AnalysisContext& actx, NameResolutionPass& core)
: TyperBase(actx, core)
, m_container(std::make_unique<ContainerTyper>(actx, core))
, m_var(std::make_unique<VarTyper>(actx, core, *m_container))
, m_call(std::make_unique<CallTyper>(actx, core))
, m_method_call(std::make_unique<MethodCallTyper>(actx, core))
, m_ref(std::make_unique<RefTyper>(actx, core))
, m_binary(std::make_unique<BinaryTyper>(actx, core)) {
}

ExprTyper::~ExprTyper() = default;

// -- Фасад: контейнерные литералы делегируются ContainerTyper --
void ExprTyper::analyzeDictLiteral(Sequence& dict_node) {
    m_container->analyzeDictLiteral(dict_node);
}
void ExprTyper::analyzeArrayInit(DictLiteralNode& node) {
    m_container->analyzeArrayInit(node);
}
void ExprTyper::analyzeTensorInit(DictLiteralNode& node, TypeId target) {
    m_container->analyzeTensorInit(node, target);
}
TypeId ExprTyper::arrayElementJoin(const std::vector<TypeId>& elementTypes) const {
    return m_container->arrayElementJoin(elementTypes);
}
void ExprTyper::analyzeRangeExpr(RangeExpr& range_node) {
    m_container->analyzeRangeExpr(range_node);
}
void ExprTyper::coerceArrayInitToTarget(DictLiteralNode& node, TypeId targetType) {
    m_container->coerceArrayInitToTarget(node, targetType);
}
int64_t ExprTyper::dictSizeOf(const AstNodeBase* obj) const {
    return m_container->dictSizeOf(obj);
}
QualType ExprTyper::dictElementType(const AstNodeBase* valueNode) const {
    return m_container->dictElementType(valueNode);
}
std::vector<QualType> ExprTyper::dictElementTypes(const AstNodeBase* src) const {
    return m_container->dictElementTypes(src);
}
TypeId ExprTyper::naturalRuntimeType(TypeId nominal) const {
    return m_container->naturalRuntimeType(nominal);
}
TypeId ExprTyper::joinElementTypes(const std::vector<TypeId>& naturalized) const {
    return m_container->joinElementTypes(naturalized);
}
TypeId ExprTyper::dictFieldTypeOf(const Binary& access) const {
    return m_container->dictFieldTypeOf(access);
}

void ExprTyper::typeMethodCall(Binary& n) {
    m_method_call->typeMethodCall(n);
}

// -- Типизация выражений (post-order): диспетчеризация по семействам --
// Семейства обрабатываются профильными типизаторами (Var/Call/MethodCall/Container/Binary/Ref).
void ExprTyper::typeExpr(AstNodeBase* node) {
    if (!node) {
        return;
    }
    if (node->kind() == ParserToken::Kind::Filling) {
        typeFillingNode(static_cast<Sequence&>(*node));
        return;
    }
    if (is_binary_expr_kind(node->kind())) {
        m_binary->typeBinaryNode(static_cast<Binary&>(*node));
        return;
    }
    if (node->is<FuncDecl>()) {
        m_var->typeLambdaNode(*node->as<FuncDecl>());
        return;
    }
    if (node->is<VarDecl>()) {
        m_var->typeVarDeclNode(*node->as<VarDecl>());
        return;
    }
    if (is_literal_kind(node->kind())) {
        typeLiteralNode(*node->as<Literal>());
        return;
    }
    if (node->is<CallExpr>()) {
        m_call->typeCallNode(*node->as<CallExpr>());
        return;
    }
    // Ссылочные выражения (address-of/borrow, take, нативные операторы) - отдельная зона
    // ответственности, реализована в ref_typer.cpp (RefTyper::typeRefExpr).
    if (node->is<RefMakeExpr>() || node->is<RefTakeExpr>()) {
        m_ref->typeRefExpr(node);
    }
}

// -- Компиляйт-тайм проверка printf-формата (атрибут @[format("printf", ...)]) --

namespace {

void reportLiteralAnnotProblem(trust::Context& ctx, const Literal& lit, std::optional<TypeId> ann, const TypeReadModel& reg, LiteralAnnotProblem problem) {
    switch (problem) {
    case LiteralAnnotProblem::UnknownType:
        ctx.diag().report(Severity::Error, lit.typeAnnotation->range(), "unknown type annotation '{}' on a literal", requiredText(*lit.typeAnnotation));
        break;
    case LiteralAnnotProblem::RationalExpected:
        ctx.diag().report(Severity::Error, lit.range(), "a rational literal 'num\\den' must be typed 'Rational' (found '{}')",
                          reg.printer().getTypeName(reg.store().desugaredTypeId(*ann)));
        break;
    case LiteralAnnotProblem::BoolNot01:
        ctx.diag().report(Severity::Error, lit.range(), "boolean literal must be 0 or 1 (found '{}')", lit.text());
        break;
    case LiteralAnnotProblem::IntOverflow:
        ctx.diag().report(Severity::Error, lit.range(), "integer literal '{}' does not fit the annotated type '{}'", lit.text(),
                          reg.printer().getTypeName(reg.store().desugaredTypeId(*ann)));
        break;
    case LiteralAnnotProblem::NonNumeric:
        ctx.diag().report(Severity::Error, lit.range(), "cannot annotate a numeric literal with non-numeric type '{}'",
                          reg.printer().getTypeName(reg.store().desugaredTypeId(*ann)));
        break;
    case LiteralAnnotProblem::None:
        break;
    }
}

} // namespace

// Типизация `... expr ...` (Filling) как элемента списка: тип = тип операнда (нужен для
// вывода элементного типа массива). Расширение списка/capacity - в analyzeArrayInit/
// analyzeCallFilling; самостоятельным выражением FILLING не является.
void ExprTyper::typeFillingNode(Sequence& f) {
    if (!f.m_body.empty() && f.m_body[0]) {
        const QualType t = m_actx.facts().exprState(*f.m_body[0]);
        if (t.isValid()) {
            m_actx.facts().setExprState(&f, t);
        }
    }
}

// Типизация литерала: постфиксная аннотация `literal :Type` либо вывод по виду/тексту.
void ExprTyper::typeLiteralNode(Literal& lit) {
    const TypeReadModel& reg = m_actx.ctx().types();
    // Литерал: тип задаёт постфиксная аннотация `literal :Type` (0 :Bool, 5 :Rational,
    // 5 :BigInteger, 256 :Int16, 1.5 :Float32; RationalLiteral `num\den` - только :Rational)
    // либо выводится по виду/тексту (literalType). Единые правила и единственный репортёр
    // ошибок - annotatedLiteralType / reportLiteralAnnotProblem.
    if (lit.typeAnnotation) {
        const auto ann = m_actx.types().resolveTypeRef(*lit.typeAnnotation);
        const LiteralAnnotResult r = annotatedLiteralType(lit, ann, reg);
        if (r.problem == LiteralAnnotProblem::None) {
            lit.typeState = QualType::fromTypeId(r.type).withInferred();
            m_actx.facts().setExprState(&lit, lit.typeState);
        } else {
            // Единственный репортёр невалидной аннотации (typeExpr - авторитетный проход);
            // кешируем INVALID, чтобы exprType не возвращал текст-тип/не пересчитывал.
            reportLiteralAnnotProblem(m_actx.ctx(), lit, ann, reg, r.problem);
            m_actx.facts().setExprType(&lit, INVALID_TYPE_ID);
        }
        return;
    }
    // Кешируем выведенный тип (literalType), чтобы exprType не пересчитывал его; тип с
    // битом inferred (для join/расширения; см. typeBinaryResult). Транспилятор литерала
    // словаря читает lit.typeState, не пересчитывая диапазоны.
    const TypeId t = literalType(lit, reg);
    if (t != INVALID_TYPE_ID) {
        lit.typeState = QualType::fromTypeId(t).withInferred();
        m_actx.facts().setExprState(&lit, lit.typeState);
    }
    return;
}

} // namespace trust
