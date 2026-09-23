// Generated: src/transpiler/expr_emit.cpp
#include "transpiler/expr_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_visitor.hpp"
#include "ast/token_type.hpp"
#include "session/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "analysis/symbol_table.hpp"
#include "syntax/term.h"
#include "types/registry.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/group.hpp"
#include "types/type_id.hpp"
#include "types/typekind.hpp"
#include "types/type_names.hpp"
#include "transpiler/diag.hpp"
#include "utils/strings.hpp"
#include <format>
#include <memory>

namespace trust {

void ExprEmitter::visit_RefMakeExpr(const RefMakeExpr& n) {
    // `& expr` - создание ссылки. Форму РЕШИЛ АНАЛИЗАТОР (`RefMakeExpr::m_lowering`); кодоген
    // переводит буквально. Операнд - m_body[0].
    if (n.m_body.empty()) {
        m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "operator '&' has no operand");
        return;
    }
    const AstNodeBase* operand = n.m_body[0].get();
    // shared -> weak: `trust::Weak<...>(c_x)` (тип результата вычислен семантикой в m_resultType).
    m_driver.m_type.recordRequiredInclude("@trust/trusted-cpp.hpp");
    std::string cpp;
    if (auto nm = m_driver.m_type.emitTypeName(n.m_resultType, "")) {
        cpp = *nm;
    } else {
        cpp = "std::any";
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, cpp + "(");
    m_driver.emitExpr(operand);
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
}

void ExprEmitter::visit_RefTakeExpr(const RefTakeExpr& n) {
    // `*ref` / `*^ref` (take): разыменование ссылочного операнда - доступ к данным. Форму доступа
    // РЕШИЛ АНАЛИЗАТОР (`RefTakeExpr::m_lowering`, `typeRefExpr`); кодоген - буквальный перевод
    // (без обращения к таблице символов). Операнд - m_body[0]; read-only `*^` - attr::ReadOnly.
    if (n.m_body.empty()) {
        m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "operator '*' (take) has no operand");
        return;
    }
    using TL = RefTakeExpr::TakeLowering;
    switch (n.m_lowering) {
    case TL::StaticUnique:
        // trust::StaticUnique (inline-значение): `*u` -> `(u.get())`.
        m_driver.m_type.recordRequiredInclude("@trust/trusted-cpp.hpp");
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "(");
        m_driver.emitExpr(n.m_body[0].get());
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ".get())");
        return;
    case TL::UniqueDirect:
        // trust::Unique с deleter'ом: `trust::checked_deref(u.get())` (проверка nullptr).
        m_driver.m_type.recordRequiredInclude("@trust/trusted-cpp.hpp");
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "trust::checked_deref(");
        m_driver.emitExpr(n.m_body[0].get());
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ".get())");
        return;
    case TL::Direct:
        // ptr: `trust::checked_deref(p)`.
        m_driver.m_type.recordRequiredInclude("@trust/trusted-cpp.hpp");
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "trust::checked_deref(");
        m_driver.emitExpr(n.m_body[0].get());
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
        return;
    case TL::SharedLocked: {
        // `*(ref.lock())` / `*(ref.lock_const())` (lock() проверяет null/истечение).
        const bool immutable = n.has_attr(m_ectx.m_ctx.attrs(), attr::ReadOnly);
        m_driver.m_type.recordRequiredInclude("@trust/trusted-cpp.hpp");
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "*(");
        m_driver.emitExpr(n.m_body[0].get());
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, immutable ? ".lock_const()" : ".lock()");
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
        return;
    }
    }
}

void ExprEmitter::visit_RefLockExpr(const Sequence& n) {
    // Захват блокировки ссылки (создаёт LOWERING для `with`): <ref>.lock() / <ref>.lock_const().
    // Операнд - единственный ребёнок (m_body[0]). Read-only (`.lock_const()`) - синтетический
    // узел без Term, признак задаёт lowering атрибутом attr::ReadOnly (общий принцип).
    if (n.m_body.empty()) {
        m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "ref lock capture '*' has no operand");
        return;
    }
    m_driver.m_type.recordRequiredInclude("@trust/trusted-cpp.hpp");
    m_driver.emitExpr(n.m_body[0].get());
    const bool immutable = n.has_attr(m_ectx.m_ctx.attrs(), attr::ReadOnly);
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, immutable ? ".lock_const()" : ".lock()");
}

void ExprEmitter::visit_RefLockDeref(const Sequence& n) {
    // Разыменование удерживаемого Locker-темпа (создаёт LOWERING для `with`): *<temp>.
    // Операнд - единственный ребёнок (m_body[0] - имя Locker-темпа).
    if (n.m_body.empty()) {
        m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "ref lock deref '*' has no operand");
        return;
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "*");
    m_driver.emitExpr(n.m_body[0].get());
}

// Многоточие не должно доходить до кодогенерации: семейство `...`/`... expr ...` раскрывает
// (МАТЕРИАЛИЗУЕТ) семантика (semantic/ellipsis), а нераскрытые случаи диагностирует её пост-проход
// (reportUnresolvedEllipsis). Сюда узел может попасть только при нарушении инварианта.
void ExprEmitter::visit_Ellipsis(const Sequence& n) {
    (void)n;
    FAULT("visit_Ellipsis: многоточие не раскрыто семантикой (кодоген получает материализованный список)");
}

// `... expr ...` (FILLING) - то же самое: раскрывается семантикой (semantic/ellipsis).
void ExprEmitter::visit_Filling(const Sequence& n) {
    (void)n;
    FAULT("visit_Filling: FILLING не раскрыт семантикой (кодоген получает материализованный список)");
}

} // namespace trust
