#pragma once

// include/semantic/overload_call.hpp
// Единые хелперы РАЗРЕШЕНИЯ ПЕРЕГРУЗКИ ВЫЗОВА: сбор типов аргументов и разрешение с
// диагностикой. Устраняют дублирование, ранее размазанное по typeCallNode (функции),
// handleMethodCall (методы), операторам `()`/`[]`.

#include "ast/ast_nodes.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "semantic/pass.hpp"
#include "types/overload_resolve.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

/// Типы аргументов вызова ПОЗИЦИОННО: `ArgNode` → тип его значения (named-аргумент), прочий
/// узел → тип выражения (`exprType`). Отсутствующий узел → INVALID (any-подобный).
inline std::vector<TypeId> callArgTypes(AnalysisContext& actx, const CallExpr& call) {
    std::vector<TypeId> argTypes;
    if (!call.m_args) {
        return argTypes;
    }
    argTypes.reserve(call.m_args->size());
    for (const auto& a : *call.m_args) {
        if (!a) {
            argTypes.push_back(INVALID_TYPE_ID);
        } else if (a->kind() == ParserToken::Kind::ArgNode) {
            const auto& an = static_cast<const ArgNode&>(*a);
            argTypes.push_back(an.m_value ? actx.exprType(*an.m_value) : INVALID_TYPE_ID);
        } else {
            argTypes.push_back(actx.exprType(*a));
        }
    }
    return argTypes;
}

/// Разрешить перегрузку набора `sigs` по `argTypes`. При неуспехе - ЯВНАЯ диагностика
/// (`what` - человекочитаемое описание вызываемого, напр. "function 'f'", "method 'f' of type 'T'").
/// Возвращает выбранную сигнатуру либо INVALID_TYPE_ID (диагностика уже выдана).
/// `what` - по значению: вызывающий обычно передаёт `std::format(...)` (временный объект).
inline TypeId resolveCallOverload(AnalysisContext& actx, std::span<const TypeId> sigs, std::span<const TypeId> argTypes, MapperRange range, std::string what) {
    const OverloadResolution r = resolveOverload(actx.ctx().types(), sigs, argTypes);
    if (r.chosen != INVALID_TYPE_ID) {
        return r.chosen;
    }
    if (!r.ambiguous.empty()) {
        actx.ctx().diag().report(Severity::Error, range, "call to overloaded {} is ambiguous: {} matching candidates", what, r.ambiguous.size());
    } else {
        actx.ctx().diag().report(Severity::Error, range, "no matching overload for {} with {} argument(s)", what, argTypes.size());
    }
    return INVALID_TYPE_ID;
}

} // namespace trust
