// src/transpiler/emit/expr_stackcheck_emit.cpp
// ExprEmitter: Явные вызовы функций контроля стека (%trust_stack_check*).

#include "transpiler/emit/expr_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_dispatch.hpp"
#include "ast/token_type.hpp"
#include "session/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "types/call.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace trust {

// C++-вызов проверки свободного места на стеке перед вызовом защищаемой функции
// (атрибут @[stack_check@]). Пустая строка - функция не защищена или режим off.
// Вставка кода - напрямую (функции %trust_stack_check/%trust_stack_check_set_limit - для явных вызовов в коде).
std::string ExprEmitter::stackCheckExpr(const CallExpr& n) {
    // Режим off -> никаких проверок.
    if (!analysis::stackCheckActive(m_ectx.m_behavioral)) {
        return {};
    }
    if (!n.m_callee || n.m_callee->kind() != ParserToken::Kind::Ident) {
        return {};
    }
    // Защищаемые функции собраны при эмиссии их объявлений и сайтов импорта
    // (DeclEmitter -> m_declFacts.stackCheck); ключ — ОБЪЯВЛЕНИЕ callee, а не текст.
    const auto* calleeIdent = n.m_callee->as<IdentName>();
    const StackCheckGuard* guard = m_ectx.m_declFacts.findStackCheck(calleeIdent != nullptr ? calleeIdent->resolvedDecl : nullptr);
    if (guard == nullptr) {
        return {};
    }
    const StackCheckGuard& g = *guard;
    if (g.size > 0) {
        return "trust::stack_check::check_overflow(" + std::to_string(g.size) + ")";
    }
    // size==0: @[stack_check@] без аргумента → check_stack_limit() (free >= m_stack_limit + reserve).
    return "trust::stack_check::check_stack_limit()";
}

// %trust_stack_check_set_limit(f1, f2, ...) -> trust::stack_check::set_limit({&c_f1, &c_f2})
// %trust_stack_check_set_limit(f1, f2, ...) -> trust::stack_check::set_limit({&c_f1, &c_f2})
// Резолвит имена функций в адреса; неизвестное имя - диагностика (см. resolveStackCheckAddresses).
// Пустой список -> set_limit({}) (по всем функциям .stack_sizes).
void ExprEmitter::emitSetLimitFunctions(const CallExpr& n) {
    m_driver.m_type.recordRequiredInclude("@trust/stack_check.hpp");
    if (!analysis::stackCheckActive(m_ectx.m_behavioral)) {
        return;
    }
    std::vector<std::string> names;
    if (n.m_args) {
        for (const auto& arg : *n.m_args) {
            names.push_back(arg ? std::string(requiredText(*arg)) : std::string{});
        }
    }
    const auto addrs = m_ectx.resolveStackCheckAddresses(names);
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "trust::stack_check::set_limit({");
    for (size_t i = 0; i < addrs.size(); ++i) {
        if (i) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ", ");
        }
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, addrs[i]);
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "})");
}

// Обработка ОТДЕЛЬНЫХ функций контроля стека (явные вызовы в коде, `%`-префикс). Это РЕАЛЬНЫЕ
// C++-функции рантайма (trust::stack_check::check_overflow/check_stack_limit/set_reserve/get_reserve/
// get_limit/set_limit), вызываемые непосредственно в коде TrustLang.
bool ExprEmitter::handleStackCheckNative(const CallExpr& n) {
    if (!n.m_callee) {
        return false;
    }
    const std::string name(requiredText(*n.m_callee));
    if (name == analysis::stack_check_fn::kCheck) {
        // %trust_stack_check(N) -> check_overflow(N); %trust_stack_check() -> check_stack_limit().
        m_driver.m_type.recordRequiredInclude("@trust/stack_check.hpp");
        if (!analysis::stackCheckActive(m_ectx.m_behavioral)) {
            return true;
        }
        if (n.m_args && !n.m_args->empty()) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "trust::stack_check::check_overflow(");
            m_driver.emitExpr((*n.m_args)[0].get());
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ")");
        } else {
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "trust::stack_check::check_stack_limit()");
        }
        return true;
    }
    if (name == analysis::stack_check_fn::kSetReserve) {
        emitIntrinsic(IntrinsicId::kTrustStackCheckReserve, n); // set_reserve(N)
        return true;
    }
    if (name == analysis::stack_check_fn::kGetReserve) {
        m_driver.m_type.recordRequiredInclude("@trust/stack_check.hpp");
        if (analysis::stackCheckActive(m_ectx.m_behavioral)) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "trust::stack_check::get_reserve()");
        }
        return true;
    }
    if (name == analysis::stack_check_fn::kGetLimit) {
        m_driver.m_type.recordRequiredInclude("@trust/stack_check.hpp");
        if (analysis::stackCheckActive(m_ectx.m_behavioral)) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "trust::stack_check::get_limit()");
        }
        return true;
    }
    if (name == analysis::stack_check_fn::kSetLimit) {
        emitSetLimitFunctions(n);
        return true;
    }
    return false;
}

} // namespace trust
