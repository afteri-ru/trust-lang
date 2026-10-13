// src/semantic/typer/ref_typer.cpp
// Типизация ссылочных выражений (`ExprTyper` делегирует этот класс семейств).
#include "semantic/typer/ref_typer.hpp"
#include "semantic/core/name_resolution.hpp"
#include "semantic/diag.hpp"
#include "semantic/typer/operator_resolve.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/token.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include <array>

namespace trust {

void RefTyper::typeRefExpr(AstNodeBase* node) {
    if (node->is<RefMakeExpr>()) {
        // `& expr` - взятие ссылки/заимствование (address-of). Допустимо ТОЛЬКО для переменной,
        // объявленной как ссылочная; weak (слабая ссылка) получается ТОЛЬКО из shared (`& shared_var`).
        // unique → ошибка; не-ссылочная переменная → ошибка. Результат - weak-тип pointee.
        RefMakeExpr& ref = *node->as<RefMakeExpr>();
        if (ref.m_body.empty()) {
            return;
        }
        const AstNodeBase* operand = ref.m_body[0].get();
        TypeSystem& treg = m_actx.ctx().types();
        const QualType opState = m_actx.facts().exprState(*operand);
        const TypeId opType = opState.getTypeId();
        if (opType == INVALID_TYPE_ID) {
            return;
        }
        const RefType rt = getRefType(getTypeKind(opType));
        if (rt == RefType::kShared) {
            const QualType pointee = treg.read().store().getPointeeType(opState);
            if (pointee.isValid()) {
                const QualType weak = treg.interner().applyRefType(pointee.withoutInferred(), RefType::kWeak);
                ref.m_resultType = weak; // для кодогенерации (локальные символы недоступны)
                m_actx.facts().setExprState(node, weak);
            }
        } else if (rt == RefType::kUnique) {
            // Монопольное владение: вторую ссылку (alias) на то же значение получить нельзя -
            // допустимы только move/swap. `& u` запрещено.
            m_actx.ctx().diag().report(Severity::Error, node->range(),
                                       "cannot take a reference to a monopolistic 'unique' owner; use move/swap (':=:' / '@move') instead");
        } else {
            m_actx.ctx().diag().report(Severity::Error, node->range(), "operator '&' (address-of/borrow) requires a reference variable (shared), got '{}'",
                                       treg.read().printer().getTypeName(opType));
        }
        return;
    }
    if (node->is<RefTakeExpr>()) {
        // `*ref` / `*^ref` (take): разыменование ссылочного операнда - прямой доступ к данным
        // (семантика std::reference_wrapper). Результат - тип pointee. Вид ссылки операнда
        // сохраняется в RefTakeExpr::m_opRefKind для кодогенерации (транспилятору локальные
        // символы недоступны): shared/weak → *(ref.lock()), unique/ptr → *ref.
        // Операнд - единственный ребёнок RefTakeExpr (m_body[0]).
        RefTakeExpr& ref = *node->as<RefTakeExpr>();
        if (ref.m_body.empty()) {
            return;
        }
        const AstNodeBase* operand = ref.m_body[0].get();
        TypeSystem& treg = m_actx.ctx().types();
        const QualType opState = m_actx.facts().exprState(*operand);
        const TypeId opType = opState.getTypeId();
        if (opType == INVALID_TYPE_ID) {
            return;
        }
        // Пользовательский operator* (record/native/native-template): `*it` -> C++ `(*it)`.
        // Обрабатывается ДО ссылочной ветки: у класса `*` - обычный объявленный оператор.
        if (semantic::isUserOperatorType(treg, opType)) {
            if (const auto* attrNode = node->as_attr(); attrNode != nullptr && attrNode->has_attr(m_actx.ctx().attrs(), attr::ReadOnly)) {
                m_actx.ctx().diag().report(Severity::Error, node->range(),
                                           "read-only dereference '*^' is not supported for a user-defined operator '*'; use plain '*'");
                return;
            }
            const std::array<TypeId, 1> operands{opType};
            const auto resolved = semantic::resolveUserOperator(m_actx, "*", operands, /*postfix=*/false, node->range());
            if (!resolved.has_value()) {
                return;
            }
            ref.m_lowering = RefTakeExpr::TakeLowering::UserOperator;
            if (resolved->overloaded) {
                ref.resolvedOperatorSignature = resolved->signature;
                ref.resolvedOperatorIsMember = resolved->member;
            }
            ref.resolvedOperatorModule = resolved->module;
            ref.resolvedOperatorNamespace = resolved->namespacePath;
            m_actx.facts().setExprState(node, QualType::fromTypeId(resolved->returnType).withoutInferred());
            return;
        }
        const RefType rt = getRefType(getTypeKind(opType));
        if (rt == RefType::kShared || rt == RefType::kWeak || rt == RefType::kUnique || rt == RefType::kPtr) {
            ref.m_opRefKind = rt; // для прочих потребителей (borrow-checker)
            // Lowering доступа решает АНАЛИЗАТОР (кодоген — буквальный перевод по m_lowering).
            if (rt == RefType::kUnique) {
                const auto* rd = treg.read().store().getTypeDataAs<RefTypeData>(opType);
                const bool hasDeleter = rd != nullptr && rd->deleterType != INVALID_TYPE_ID;
                ref.m_lowering = hasDeleter ? RefTakeExpr::TakeLowering::UniqueDirect : RefTakeExpr::TakeLowering::StaticUnique;
            } else if (rt == RefType::kShared || rt == RefType::kWeak) {
                ref.m_lowering = RefTakeExpr::TakeLowering::SharedLocked;
            } else {
                ref.m_lowering = RefTakeExpr::TakeLowering::Direct;
            }
            const QualType pointee = treg.read().store().getPointeeType(opState);
            if (pointee.isValid()) {
                m_actx.facts().setExprState(node, pointee.withoutInferred());
            }
        } else {
            m_actx.ctx().diag().report(Severity::Error, node->range(), "operator '*' (dereference) requires a reference (shared/weak/unique/ptr), got '{}'",
                                       treg.read().printer().getTypeName(opType));
        }
        // -Wnative-ref: разименование нативной (сырой) ссылки/указателя (`*` - общий оператор
        // для всех видов, но для нативных выводится диагностика о его использовании).
        if (rt == RefType::kPtr || rt == RefType::kRef) {
            m_actx.ctx().report(node->range(), semantic::DiagId::NativeRef, "native (raw) C++ dereference operator is used");
        }
        return;
    }
}

} // namespace trust
