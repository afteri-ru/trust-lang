#pragma once

// Привязка аргументов вызова к параметрам: позиционные, именованные (`.name=`) и
// опциональные (default). Метаданные (`CallableParamInfo`) НЕ входят в TypeKey/суффикс;
// значения по умолчанию остаются в C++-объявлении (вызов опускает только ХВОСТОВЫЕ
// опциональные параметры). Пропуск default перед явно переданным аргументом запрещён.

#include "types/type_read.hpp"
#include "target/decl_cpp_facts.hpp"
#include "symbols/decl_binders.hpp"
#include "symbols/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "semantic/core/pass.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

/// Привязанные аргументы вызова: записи `узел + тип` одного набора.
struct BoundArg {
    const AstNodeBase* node = nullptr;
    TypeId type{INVALID_TYPE_ID};
};

struct BoundCallArguments {
    std::vector<BoundArg> args;
};

/// Узлы связанных аргументов (проекция для AST-факта `CallExpr::boundArgs`).
[[nodiscard]] inline std::vector<const AstNodeBase*> boundArgumentNodes(const BoundCallArguments& bound) {
    std::vector<const AstNodeBase*> nodes;
    nodes.reserve(bound.args.size());
    for (const BoundArg& arg : bound.args) {
        nodes.push_back(arg.node);
    }
    return nodes;
}

[[nodiscard]] inline CallableParamInfo callableParamInfo(const FuncDecl& f) {
    CallableParamInfo info;
    if (!f.m_params) {
        return info;
    }
    for (const auto& p : *f.m_params) {
        if (!p || p->kind() != ParserToken::Kind::ArgNode || isVariadicParamMarker(p.get())) {
            continue;
        }
        const auto& a = static_cast<const ArgNode&>(*p);
        if (a.isTypePack) {
            continue;
        }
        info.slots.push_back(ParamSlot{std::string(a.text()), a.m_value != nullptr});
    }
    return info;
}

/// Template binders of a declaration in DECLARATION ORDER are provided by
/// analysis/decl_binders.hpp (single extractor for SymbolTable and semantic).

[[nodiscard]] inline bool hasNamedArguments(const CallExpr& call) {
    if (!call.m_args) {
        return false;
    }
    for (const auto& a : *call.m_args) {
        if (a && a->kind() == ParserToken::Kind::ArgNode && !static_cast<const ArgNode&>(*a).text().empty()) {
            return true;
        }
    }
    return false;
}

/// Метаданные параметров ОБЪЯВЛЕНИЯ по индексу в наборе. Индекс (а не TypeId) - единственный
/// корректный ключ: два разных объявления могут делить один TypeId (позиционная identity).
/// nullptr - у символа нет вызываемых перегрузок (не-функция) либо метаданные неизвестны.
[[nodiscard]] inline const CallableParamInfo* overloadParamInfoAt(const Symbol& sym, std::size_t index) {
    if (sym.overloads.empty()) {
        EXPECT(index == 0 && "overloadParamInfoAt: a non-callable symbol has only index 0");
        return nullptr;
    }
    EXPECT(index < sym.overloads.size() && "overloadParamInfoAt: index is out of range");
    return &sym.overloads[index].params;
}

/// Метаданные параметров метода по индексу в наборе (nullptr - метаданные неизвестны:
/// native/builtin; это законный случай, не ошибка).
[[nodiscard]] inline const CallableParamInfo* methodParamInfoAt(const ::trust::MethodRef& method, std::size_t index) {
    return index < method.entries.size() ? &method.entries[index].params : nullptr;
}

/// C++-суффикс объявления-перегрузки по индексу набора (`DeclCppFacts::cppSuffixOf` — целевой факт
/// объявления, R1). Индекс обязан принадлежать набору - иначе это внутренняя ошибка.
[[nodiscard]] inline std::string declCppSuffix(const Symbol& sym, std::size_t index, const DeclCppFacts& facts) {
    const AstNodeBase* decl = nullptr;
    if (index < sym.overloads.size()) {
        decl = sym.overloads[index].decl;
    } else {
        EXPECT(index == 0 && "declCppSuffix: a non-callable symbol has only index 0");
        decl = sym.decl;
    }
    EXPECT(decl != nullptr && decl->is<FuncDecl>() && "declCppSuffix: a function declaration is required");
    return std::string(facts.cppSuffixOf(decl->as<FuncDecl>()->declId()));
}

/// C++-суффикс метода по индексу набора: единственный владелец факта — контракт `target`
/// (`DeclCppFacts::cppSuffixOf` по идентичности объявления метода). Пусто — суффикс не назначался
/// (native/builtin/синтетический метод без объявления).
[[nodiscard]] inline std::string methodCppSuffix(const ::trust::MethodRef& method, std::size_t index, const DeclCppFacts& facts) {
    EXPECT(index < method.entries.size() && "methodCppSuffix: suffix index is out of range");
    return std::string(facts.cppSuffixOf(method.entries[index].decl));
}

/// Привязать сырые аргументы вызова к слотам параметров. nullopt - ошибка (error заполнен).
[[nodiscard]] inline std::optional<BoundCallArguments> bindCallArguments(AnalysisContext& actx, const std::vector<AstNodePtr>& raw,
                                                                         const CallableParamInfo& info, bool variadic, std::string_view what,
                                                                         std::string* error) {
    const auto fail = [&](std::string message) -> std::optional<BoundCallArguments> {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return std::nullopt;
    };
    bool seenDefault = false;
    for (const ParamSlot& slot : info.slots) {
        if (slot.hasDefault) {
            seenDefault = true;
        } else if (seenDefault) {
            return fail("optional parameter before a required parameter");
        }
    }
    const std::size_t n = info.slots.size();
    std::vector<const AstNodeBase*> slots(n);
    std::vector<bool> bound(n, false);
    std::vector<const AstNodeBase*> extras;

    for (const auto& arg : raw) {
        if (!arg) {
            return fail("null argument node for " + std::string(what));
        }
        std::string name;
        const AstNodeBase* value = arg.get();
        if (arg->kind() == ParserToken::Kind::ArgNode) {
            const auto& an = static_cast<const ArgNode&>(*arg);
            name = std::string(an.text());
            value = an.m_value.get();
            if (value == nullptr) {
                return fail("argument '" + name + "' has no value");
            }
        }
        if (!name.empty()) {
            std::size_t idx = n;
            for (std::size_t i = 0; i < n; ++i) {
                if (info.slots[i].name == name) {
                    idx = i;
                    break;
                }
            }
            if (idx == n) {
                return fail("unknown named argument '." + name + "' for " + std::string(what));
            }
            if (bound[idx]) {
                return fail("duplicate argument '." + name + "' for " + std::string(what));
            }
            slots[idx] = value;
            bound[idx] = true;
        } else {
            std::size_t idx = 0;
            while (idx < n && bound[idx]) {
                ++idx;
            }
            if (idx < n) {
                slots[idx] = value;
                bound[idx] = true;
            } else {
                extras.push_back(value);
            }
        }
    }

    std::size_t last = 0;
    bool any = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (bound[i]) {
            last = i + 1;
            any = true;
        }
    }
    const std::size_t required = info.requiredCount();
    for (std::size_t i = 0; i < required; ++i) {
        if (!bound[i]) {
            return fail("missing required argument '" + info.slots[i].name + "' for " + std::string(what));
        }
    }
    if (any) {
        for (std::size_t i = 0; i < last; ++i) {
            if (!bound[i]) {
                if (info.slots[i].hasDefault) {
                    return fail("cannot omit default parameter '" + info.slots[i].name + "' before a provided argument for " + std::string(what) +
                                " (C++ default arguments can only be omitted at the tail)");
                }
                return fail("missing required argument '" + info.slots[i].name + "' for " + std::string(what));
            }
        }
    }
    if (!extras.empty() && !variadic) {
        return fail("too many arguments for " + std::string(what));
    }

    BoundCallArguments out;
    out.args.reserve(last + extras.size());
    for (std::size_t i = 0; i < last; ++i) {
        out.args.push_back(BoundArg{slots[i], actx.facts().exprType(*slots[i])});
    }
    for (const auto& extra : extras) {
        out.args.push_back(BoundArg{extra, actx.facts().exprType(*extra)});
    }
    return out;
}

/// Результат сбора bound-кандидатов (общая часть free-функций/методов/operator()).
struct BoundCandidateSet {
    std::vector<OverloadCandidate> candidates;
    std::vector<BoundCallArguments> bound;
    std::string firstError;
};

template <typename InfoFor>
[[nodiscard]] inline BoundCandidateSet gatherBoundCandidates(AnalysisContext& actx, const std::vector<AstNodePtr>& raw, std::span<const TypeId> signatures,
                                                             InfoFor infoFor, std::string_view what) {
    BoundCandidateSet out;
    const TypeReadModel& reg = actx.registry();
    // A candidate without CallableParamInfo (native/builtin) cannot be named/default-bound, but it
    // MUST stay in the set: dropping it silently would let a metadata-bearing overload win against
    // a viable native one. It participates with positional arguments only.
    const auto hasNamed = [&]() {
        for (const auto& arg : raw) {
            if (arg && arg->kind() == ParserToken::Kind::ArgNode && !static_cast<const ArgNode&>(*arg).text().empty()) {
                return true;
            }
        }
        return false;
    };
    const bool namedPresent = hasNamed();
    for (std::size_t i = 0; i < signatures.size(); ++i) {
        const TypeId sig = signatures[i];
        const CallableParamInfo* info = infoFor(i, sig);
        const auto* fd = reg.store().getTypeDataAs<FunctionTypeData>(sig);
        if (fd == nullptr) {
            continue;
        }
        if (info == nullptr) {
            if (namedPresent) {
                if (out.firstError.empty()) {
                    out.firstError = "named arguments require parameter metadata for a callable candidate";
                }
                continue;
            }
            const std::size_t fixed = fd->paramTypes.size();
            const bool variadic = fd->variadicType != INVALID_TYPE_ID;
            if ((!variadic && raw.size() != fixed) || (variadic && raw.size() < fixed)) {
                if (out.firstError.empty()) {
                    out.firstError = "argument count does not match a callable candidate without parameter metadata";
                }
                continue;
            }
            BoundCallArguments bound;
            bound.args.reserve(raw.size());
            bool ok = true;
            for (const auto& arg : raw) {
                if (!arg) {
                    ok = false;
                    break;
                }
                const AstNodeBase* value = arg.get();
                if (arg->kind() == ParserToken::Kind::ArgNode) {
                    const auto& an = static_cast<const ArgNode&>(*arg);
                    value = an.m_value.get();
                    if (value == nullptr) {
                        ok = false;
                        break;
                    }
                }
                bound.args.push_back(BoundArg{value, actx.facts().exprType(*value)});
            }
            if (!ok) {
                if (out.firstError.empty()) {
                    out.firstError = "null argument node for " + std::string(what);
                }
                continue;
            }
            std::vector<TypeId> argumentTypes;
            argumentTypes.reserve(bound.args.size());
            for (const BoundArg& arg : bound.args) {
                argumentTypes.push_back(arg.type);
            }
            out.candidates.push_back(OverloadCandidate{sig, std::move(argumentTypes), fixed, false, true});
            out.bound.push_back(std::move(bound));
            continue;
        }
        std::string err;
        auto bound = bindCallArguments(actx, raw, *info, fd->variadicType != INVALID_TYPE_ID, what, &err);
        if (!bound) {
            if (out.firstError.empty()) {
                out.firstError = std::move(err);
            }
            continue;
        }
        std::vector<TypeId> argumentTypes;
        argumentTypes.reserve(bound->args.size());
        for (const BoundArg& arg : bound->args) {
            argumentTypes.push_back(arg.type);
        }
        out.candidates.push_back(OverloadCandidate{sig, std::move(argumentTypes), info->requiredCount(), false, true});
        out.bound.push_back(std::move(*bound));
    }
    return out;
}

/// ЕДИНАЯ диагностика неуспеха binding-пути (аргументы не разложены по слотам).
inline void reportBoundCandidatesFailure(AnalysisContext& actx, MapperRange range, const BoundCandidateSet& gathered, std::string_view what) {
    actx.ctx().diag().report(Severity::Error, range, "no matching overload for {}: {}", what,
                             gathered.firstError.empty() ? "arguments cannot be bound" : gathered.firstError);
}

/// ЕДИНАЯ диагностика неуспеха ранжирования после binding (ambiguous / no match).
inline void reportBoundResolutionFailure(AnalysisContext& actx, MapperRange range, const OverloadResolution& r, std::string_view what) {
    if (!r.ambiguous.empty()) {
        actx.ctx().diag().report(Severity::Error, range, "call to overloaded {} is ambiguous: {} matching candidates", what, r.ambiguous.size());
        return;
    }
    actx.ctx().diag().report(Severity::Error, range, "no matching overload for {} after argument binding", what);
}

} // namespace trust
