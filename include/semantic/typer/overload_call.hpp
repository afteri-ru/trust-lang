#pragma once

// Единые хелперы РАЗРЕШЕНИЯ ПЕРЕГРУЗКИ ВЫЗОВА: сбор типов аргументов и разрешение с
// диагностикой для свободных функций, методов и operator().

#include "ast/ast_nodes.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "semantic/core/pass.hpp"
#include "types/call.hpp"

#include <algorithm>
#include <optional>
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
            argTypes.push_back(an.m_value ? actx.facts().exprType(*an.m_value) : INVALID_TYPE_ID);
        } else {
            argTypes.push_back(actx.facts().exprType(*a));
        }
    }
    return argTypes;
}

/// Разрешить перегрузку набора `sigs` по `argTypes`. При неуспехе - ЯВНАЯ диагностика
/// (`what` - человекочитаемое описание вызываемого, напр. "function 'f'", "method 'f' of type 'T'").
/// Возвращает выбранную сигнатуру либо INVALID_TYPE_ID (диагностика уже выдана).
/// `what` - по значению: вызывающий обычно передаёт `std::format(...)` (временный объект).
inline OverloadResolution resolveCallOverload(AnalysisContext& actx, std::span<const TypeId> sigs, std::span<const TypeId> argTypes, MapperRange range,
                                              std::string what) {
    TypeSystem& reg = actx.registry();
    std::vector<OverloadCandidate> candidates;
    candidates.reserve(sigs.size());
    for (const TypeId sig : sigs) {
        candidates.push_back(OverloadCandidate{sig, std::vector<TypeId>(argTypes.begin(), argTypes.end()), 0, false, false});
    }
    OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    if (r.chosen != INVALID_TYPE_ID) {
        return r;
    }
    if (!r.ambiguous.empty()) {
        actx.ctx().diag().report(Severity::Error, range, "call to overloaded {} is ambiguous: {} matching candidates", what, r.ambiguous.size());
    } else if (r.failure == OverloadResolution::Failure::PackWithoutTemplate) {
        actx.ctx().diag().report(Severity::Error, range, "call to {} requires template-aware resolution: a candidate has a type-pack parameter", what);
    } else {
        actx.ctx().diag().report(Severity::Error, range, "no matching overload for {} with {} argument(s)", what, argTypes.size());
    }
    return r;
}

/// Расширенный вариант: разрешение с унификацией типовых параметров.
/// Возвращает объявленную сигнатуру, инстанцированный return и привязки (для кодогена).
inline OverloadResolution resolveCallOverloadWithTemplates(AnalysisContext& actx, std::span<const TypeId> sigs, std::span<const TypeId> argTypes,
                                                           MapperRange range, std::string what) {
    TypeSystem& reg = actx.registry();
    std::vector<OverloadCandidate> candidates;
    candidates.reserve(sigs.size());
    for (const TypeId sig : sigs) {
        candidates.push_back(OverloadCandidate{sig, std::vector<TypeId>(argTypes.begin(), argTypes.end()), 0, true, false});
    }
    OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    if (r.chosen != INVALID_TYPE_ID && r.effectiveSig != INVALID_TYPE_ID) {
        return r;
    }
    if (!r.ambiguous.empty()) {
        actx.ctx().diag().report(Severity::Error, range, "call to overloaded {} is ambiguous: {} matching candidates", what, r.ambiguous.size());
        return r;
    }
    if (r.failure == OverloadResolution::Failure::Conflict) {
        actx.ctx().diag().report(Severity::Error, range, "cannot deduce template parameter for {}: conflicting deductions", what);
    } else if (r.failure == OverloadResolution::Failure::Unbound) {
        const std::string param = r.unboundParam != INVALID_TYPE_ID ? std::string(reg.read().store().templateParamName(r.unboundParam)) : std::string{};
        if (!param.empty()) {
            actx.ctx().diag().report(Severity::Error, range, "cannot deduce template parameter '{}' for {} from the call arguments", param, what);
        } else {
            actx.ctx().diag().report(Severity::Error, range, "cannot deduce template parameters for {} from the call arguments", what);
        }
    } else if (r.failure == OverloadResolution::Failure::ReturnPack) {
        actx.ctx().diag().report(Severity::Error, range, "cannot instantiate the result of {}: a type parameter pack is not allowed in a single-type position",
                                 what);
    } else if (r.failure == OverloadResolution::Failure::PackWithoutTemplate) {
        actx.ctx().diag().report(Severity::Error, range, "call to {} requires template-aware resolution: a candidate has a type-pack parameter", what);
    } else {
        actx.ctx().diag().report(Severity::Error, range, "no matching overload for {} with {} argument(s)", what, argTypes.size());
    }
    return r;
}

/// Тексты неуспеха принадлежат ФОРМЕ вызова (`[]`, `()`, статический член); разрешение общее.
/// `uninstantiable` - набор содержит неинстанцируемую сигнатуру (native-template с несколькими
/// типовыми параметрами). `ambiguousCount` - к тексту амбигуа дописывается число кандидатов.
struct CallFailureText {
    std::string ambiguous;
    std::string noMatch;
    std::string uninstantiable;
    bool ambiguousCount = false;
};

/// ПОЗИЦИОННОЕ разрешение набора сигнатур с диагностикой формы (без binding/template-aware).
/// Возвращает ЭФФЕКТИВНУЮ сигнатуру вызова (для не-generic совпадает с объявленной);
/// nullopt - выбора нет (диагностика уже выдана).
[[nodiscard]] inline std::optional<TypeId> resolveCallablePositional(AnalysisContext& actx, std::span<const TypeId> sigs, std::span<const TypeId> argTypes,
                                                                     MapperRange range, const CallFailureText& text) {
    TypeSystem& reg = actx.registry();
    if (std::find(sigs.begin(), sigs.end(), INVALID_TYPE_ID) != sigs.end()) {
        actx.ctx().diag().report(Severity::Error, range, "{}", text.uninstantiable);
        return std::nullopt;
    }
    std::vector<OverloadCandidate> candidates;
    candidates.reserve(sigs.size());
    for (const TypeId sig : sigs) {
        candidates.push_back(OverloadCandidate{sig, std::vector<TypeId>(argTypes.begin(), argTypes.end()), 0, false, false});
    }
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    if (r.chosen == INVALID_TYPE_ID) {
        if (r.ambiguous.empty()) {
            actx.ctx().diag().report(Severity::Error, range, "{}", text.noMatch);
        } else if (text.ambiguousCount) {
            actx.ctx().diag().report(Severity::Error, range, "{}: {} matching candidates", text.ambiguous, r.ambiguous.size());
        } else {
            actx.ctx().diag().report(Severity::Error, range, "{}", text.ambiguous);
        }
        return std::nullopt;
    }
    const TypeId effective = (r.effectiveSig != INVALID_TYPE_ID) ? r.effectiveSig : r.chosen;
    EXPECT(reg.read().store().getTypeDataAs<FunctionTypeData>(effective) != nullptr && "resolveCallablePositional: chosen signature must be a function type");
    return effective;
}

} // namespace trust
