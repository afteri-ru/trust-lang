// Generated: src/transpiler/contract_emit.cpp
#include "transpiler/contract_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_visitor.hpp"
#include "ast/token_type.hpp"
#include "diag/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "semantic/symbol_table.hpp"
#include "semantic/solver.hpp"
#include "syntax/term.h"
#include "types/registry.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "transpiler/diag.hpp"
#include "utils/operators.hpp"
#include "utils/strings.hpp"
#include <format>
#include <memory>

namespace trust {

void ContractEmitter::emitRuntimeAssertCheck(const CallExpr& call) {
    // Аргументы интринсика intrinsic_assert(<cond>[, <message>]): условие и (опционально) текст
    // условия для сообщения (@assert передаёт @# @$cond - т.к. range макро-раскрытия покрывает
    // весь вызов макроса, а не только выражение условия).
    const AstNodeBase* cond = (call.m_args && !call.m_args->empty()) ? (*call.m_args)[0].get() : nullptr;
    std::string_view message;
    if (call.m_args && call.m_args->size() > 1) {
        if (const AstNodeBase* msg = (*call.m_args)[1].get(); msg && (msg->kind() == ParserToken::Kind::StrChar || msg->kind() == ParserToken::Kind::StrWide)) {
            message = msg->text();
        }
    }
    emitRuntimeAssert(cond, call.range(), message);
}

void ContractEmitter::emitRuntimeAssert(const AstNodeBase* cond, MapperRange range, std::string_view message) {
    if (!cond) {
        return;
    }
    m_driver.m_type.recordRequiredInclude("@trust/assert.hpp");
    // Имя файла и строку берём из ИСХОДНОГО .src (единый источник sourceLocation).
    const SourceLocation loc = sourceLocation(m_ectx.m_ctx.source(), range);
    // Текст условия - из исходника (для сообщения об ошибке); если передан явно - используем его.
    std::string condText;
    if (!message.empty()) {
        condText = std::string(message);
    } else {
        const MapperRange crange = cond->range();
        if (!crange.isInvalid()) {
            condText = std::string(m_ectx.m_ctx.source().getText(crange));
        } else {
            condText = std::string(cond->text());
        }
    }
    const bool backtrace = m_ectx.m_ctx.opts().is_enabled(transpiler::FlagKind::Backtrace);
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, m_ectx.indentPrefix());
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "if (!(");
    m_driver.emitExpr(cond);
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")) trust::trust__abort__(\"");
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, utils::escape_cpp_string(loc.file));
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "\", " + std::to_string(loc.line) + ", \"");
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, utils::escape_cpp_string(condText));
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "\", " + std::to_string(backtrace ? 1 : 0) + ");\n");
}

void ContractEmitter::emitTrustCheck(const TrustContract& tc) {
    // Проверки генерируются только в assert-режиме (--solver-mode=assert); в прочих - no-op
    // (presence-warning -Wsolver и export/calculate обрабатывает семантика/pipeline).
    if (!semantic::solverAssertEnabled(m_ectx.m_ctx.opts())) {
        return;
    }
    // Trust-контракт - тот же интринсик intrinsic_assert (единый источник с @assert/@verify).
    emitRuntimeAssert(tc.m_expr.get(), tc.range());
}

void ContractEmitter::emitTrustChecks(const std::vector<AstNodePtr>& trust) {
    // Проверки генерируются только в assert-режиме (--solver-mode=assert).
    if (!semantic::solverAssertEnabled(m_ectx.m_ctx.opts())) {
        return;
    }
    for (const auto& t : trust) {
        if (const auto* tc = dynamic_cast<const TrustContract*>(t.get())) {
            emitTrustCheck(*tc);
        }
    }
}

void ContractEmitter::emitTypeTrustChecks(const std::vector<AstNodePtr>& conds, std::string_view trustName, std::string_view varCpp) {
    // Проверки генерируются только в assert-режиме (--solver-mode=assert).
    if (!semantic::solverAssertEnabled(m_ectx.m_ctx.opts())) {
        return;
    }
    if (conds.empty()) {
        return;
    }
    // Имя типа в условии - плейсхолдер значения → подставляем значение переменной (varCpp).
    // (visit_Ident: text()==m_ectx.m_resultName → эмитит m_ectx.m_resultCpp).
    m_ectx.m_resultName = std::string(trustName);
    m_ectx.m_resultCpp = std::string(varCpp);
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "\n"); // проверка - на отдельной строке после ';'
    emitTrustChecks(conds);
    m_ectx.m_resultName.clear();
    m_ectx.m_resultCpp.clear();
}

void ContractEmitter::emitTypeChecksAfterAssignment(const AstNodeBase* expr) {
    // Проверки генерируются только в assert-режиме (--solver-mode=assert).
    if (!semantic::solverAssertEnabled(m_ectx.m_ctx.opts())) {
        return;
    }
    if (!expr || expr->kind() != ParserToken::Kind::AssignOp) {
        return;
    }
    const auto& assign = static_cast<const Binary&>(*expr);
    const AstNodeBase* left = assign.m_left.get();
    if (!left || left->kind() != ParserToken::Kind::Ident) {
        return; // присваивание не в простую переменную (элемент/поле) - отложено
    }
    // Тип цели присваивания - УЖЕ на узле (AssignOp::lhsType); trust-условия типа - из узла
    // декларации типа (Binary::m_typeDecl, ставит семантика на AssignOp). Никакого трекинга.
    const TypeId typeId = clearInferred(clearConst(assign.lhsType));
    if (!assign.m_typeDecl || assign.m_typeDecl->m_trust.empty()) {
        return; // цель не доверенного типа (или тип без trust-условий)
    }
    const TypeDescriptor* d = m_ectx.m_ctx.types().lookup(typeId);
    if (!d) {
        return;
    }
    // Значение переменной изменилось - проверяем тип-утверждение заново (имя типа → переменная).
    emitTypeTrustChecks(assign.m_typeDecl->m_trust, d->name, utils::name_to_cpp(left->text()));
}

// Автономные trust-узлы в последовательности (`@{ [kind:] expr @};` - контракт в теле).
// Привязанные к объявлению пред/пост-условия эмитятся отдельно (см. generateFuncDeclToFile /
// m_trust); здесь - только автономный узел, обрабатываемый visit_TrustContract. В assert-режиме
// эмитятся assert/инвариант; pre/post - проверяются на границах функции, здесь no-op.
void ContractEmitter::visit_TrustContract(const TrustContract& n) {
    // assert (и инвариант) эмитятся как runtime-проверка; pre/post - нет (границы функции).
    if (n.kind == PropertyKind::Assert || n.kind == PropertyKind::Invariant || n.kind == PropertyKind::kUnknown) {
        emitTrustCheck(n);
    }
}

// Элемент контрактного программирования - обрабатывается внутри TrustContract::m_expr
// (old в assert-режиме, forall/exists - только verify-time). Автономно не эмитится. no-op.
void ContractEmitter::visit_TrustElem(const TrustElem& node) {
    (void)node;
}
} // namespace trust
