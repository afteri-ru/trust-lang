// src/semantic/ref_expr_typer.cpp
// Типизация ссылочных ВЫРАЖЕНИЙ ExprTyper-а (вынесено из expr_typer.cpp как отдельная зона
// ответственности): `& expr` (address-of/borrow), `*ref`/`*^ref` (take/доступ к данным),
// нативные `%& expr` / `%* ref`. Публичный API класса не меняется - методы объявлены в
// include/semantic/expr_typer.hpp.
#include "semantic/expr_typer.hpp"
#include "semantic/diag.hpp"
#include "ast/token.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"

namespace trust {

void ExprTyper::typeRefExpr(AstNodeBase* node) {
    if (node->is<RefMakeExpr>()) {
        // `& expr` - взятие ссылки/заимствование (address-of). Допустимо ТОЛЬКО для переменной,
        // объявленной как ссылочная; weak (слабая ссылка) получается ТОЛЬКО из shared (`& shared_var`).
        // unique → ошибка; не-ссылочная переменная → ошибка. Результат - weak-тип pointee.
        RefMakeExpr& ref = *node->as<RefMakeExpr>();
        if (ref.m_body.empty()) {
            return;
        }
        const AstNodeBase* operand = ref.m_body[0].get();
        TypeRegistry& treg = m_actx.ctx().types();
        const TypeId opType = m_actx.exprType(*operand);
        if (opType == INVALID_TYPE_ID) {
            return;
        }
        const RefType rt = getRefType(getKindFromId(opType));
        if (rt == RefType::kShared) {
            const TypeId pointee = treg.getPointeeType(opType);
            if (pointee != INVALID_TYPE_ID) {
                const TypeId weak = clearFlag(treg.applyRefType(pointee, RefType::kWeak), SymbolFlag::Inferred);
                ref.m_resultType = weak; // для кодогенерации (локальные символы недоступны)
                m_actx.setExprType(node, weak);
            }
        } else if (rt == RefType::kUnique) {
            // Монопольное владение: вторую ссылку (alias) на то же значение получить нельзя -
            // допустимы только move/swap. `& u` запрещено.
            m_actx.ctx().diag().report(Severity::Error, node->range(),
                                       "cannot take a reference to a monopolistic 'unique' owner; use move/swap (':=:' / '@move') instead");
        } else {
            m_actx.ctx().diag().report(Severity::Error, node->range(), "operator '&' (address-of/borrow) requires a reference variable (shared), got '{}'",
                                       treg.getFullTypeName(opType));
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
        TypeRegistry& treg = m_actx.ctx().types();
        const TypeId opType = m_actx.exprType(*operand);
        if (opType == INVALID_TYPE_ID) {
            return;
        }
        const RefType rt = getRefType(getKindFromId(opType));
        if (rt == RefType::kShared || rt == RefType::kWeak || rt == RefType::kUnique || rt == RefType::kPtr) {
            ref.m_opRefKind = rt; // для прочих потребителей (borrow-checker)
            // Lowering доступа решает АНАЛИЗАТОР (кодоген — буквальный перевод по m_lowering).
            if (rt == RefType::kUnique) {
                const auto* rd = treg.getTypeDataAs<RefTypeData>(opType);
                const bool hasDeleter = rd != nullptr && rd->deleterType != INVALID_TYPE_ID;
                ref.m_lowering = hasDeleter ? RefTakeExpr::TakeLowering::UniqueDirect : RefTakeExpr::TakeLowering::StaticUnique;
            } else if (rt == RefType::kShared || rt == RefType::kWeak) {
                ref.m_lowering = RefTakeExpr::TakeLowering::SharedLocked;
            } else {
                ref.m_lowering = RefTakeExpr::TakeLowering::Direct;
            }
            const TypeId pointee = treg.getPointeeType(opType);
            if (pointee != INVALID_TYPE_ID) {
                m_actx.setExprType(node, clearFlag(pointee, SymbolFlag::Inferred));
            }
        } else {
            m_actx.ctx().diag().report(Severity::Error, node->range(), "operator '*' (dereference) requires a reference (shared/weak/unique/ptr), got '{}'",
                                       treg.getFullTypeName(opType));
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
