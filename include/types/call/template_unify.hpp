#pragma once

// include/types/call/template_unify.hpp
// Унификация типовых параметров generic-функций/шаблонных native-import:
//   AsyncTask<T>  ~  AsyncTask<Int32>   =>  T := Int32
//
// Модель привязок - TemplateBindings (types/binders.hpp): non-pack хранит ровно один тип,
// pack - набор. Pack-параметр функции (`<T...>`/`xs: T...`) собирается здесь; splice
// в списковые позиции выполняет substituteTemplateArgList (types/subst/template_subst.hpp).
//
// Унификация строгая: после связывания T требует КАНОНИЧЕСКОГО равенства. Числовые
// конверсии применяются позже, к уже инстанцированной сигнатуре (overloadConversionRank).

#include "types/type_read.hpp"
#include "types/func/fn_identity.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/type/type_traversal.hpp"

#include <span>
#include <string>
#include <utility>
#include <vector>

namespace trust {

struct TemplateDeduction {
    enum class Status { Ok, NotMatch, Conflict, Unbound };

    TemplateBindings bindings;
    Status status = Status::Ok;
    TypeId conflictingParam = INVALID_TYPE_ID;

    [[nodiscard]] bool ok() const noexcept { return status == Status::Ok; }
    [[nodiscard]] bool hasBinding(TypeId param) const noexcept {
        for (const auto& b : bindings) {
            if (b.param == param) {
                return true;
            }
        }
        return false;
    }
    TemplateBinding* findBinding(TypeId param) noexcept {
        for (auto& b : bindings) {
            if (b.param == param) {
                return &b;
            }
        }
        return nullptr;
    }
};

/// Собрать типовые параметры, встречающиеся в типе (рекурсивно). Используется для проверки
/// «все ли параметры выведены» (T только в возврате => Unbound).
void collectTemplateParams(const TypeReadModel& reg, TypeId type, std::vector<TypeId>& out);

/// Унифицировать один тип-параметр с конкретным типом.
bool unifyTemplateType(const TypeReadModel& reg, TypeId param, TypeId arg, TemplateDeduction& ded);

/// Унифицировать сигнатуру `sig` с типами аргументов вызова.
/// Для не-generic сигнатур (без TemplateParam/paramPack) ведёт себя как проверка арности.
bool unifyTemplateSignature(const TypeReadModel& reg, TypeId sig, std::span<const TypeId> argTypes, TemplateDeduction& ded);

inline void collectTemplateParams(const TypeReadModel& reg, TypeId type, std::vector<TypeId>& out) {
    if (type == INVALID_TYPE_ID) {
        return;
    }
    std::vector<TypeId> stack{type};
    std::vector<TypeId> seen;
    while (!stack.empty()) {
        const TypeId raw = stack.back();
        stack.pop_back();
        if (raw == INVALID_TYPE_ID) {
            continue;
        }
        const TypeId t = reg.store().identityOf(raw, TypeIdentityScope::kCanonical);
        bool already = false;
        for (const TypeId s : seen) {
            if (s == t) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        seen.push_back(t);
        // ⚠ Ref-вид проверяется ДО `isTemplateParamType`: fast-path ref-бит поверх `TemplateParam`
        // (`shared<T>`) по группе выглядит как сам `T`, но это ссылочный тип; его pointee/aux
        // поля - отдельные type-дети.
        if (const auto* mp = reg.store().getTypeDataAs<MemberPointerTypeData>(t); mp != nullptr) {
            stack.push_back(mp->classType);
            stack.push_back(mp->memberType);
            continue;
        }
        if (getRefType(getTypeKind(t)) != RefType::kValue) {
            stack.push_back(reg.store().getPointeeType(t));
            if (const auto* rd = reg.store().getTypeDataAs<RefTypeData>(t); rd != nullptr) {
                stack.push_back(rd->deleterType);
                stack.push_back(rd->accessPolicyType);
                stack.push_back(rd->implType);
            }
            continue;
        }
        if (reg.store().isTemplateParamType(t)) {
            bool dup = false;
            for (const TypeId p : out) {
                if (p == t) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                out.push_back(t);
            }
            continue;
        }
        // Все остальные type-поля - ЕДИНЫЙ исчерпывающий обход (types/type_traversal.hpp):
        // PackExpansion, NativeTemplate, Array, Tensor, Tuple, Range, Record, Function и т.д.
        const std::optional<TypeData>& data = reg.store().getTypeData(t);
        if (data.has_value()) {
            forEachTypeChild(*data, [&](TypeId child) { stack.push_back(child); });
        }
    }
}
inline bool unifyTemplateType(const TypeReadModel& reg, TypeId param, TypeId arg, TemplateDeduction& ded) {
    // Нетипизированный параметр = Any-подобный (существующая модель перегрузки): не блокирует.
    if (param == INVALID_TYPE_ID) {
        return true;
    }
    if (arg == INVALID_TYPE_ID) {
        std::vector<TypeId> params;
        collectTemplateParams(reg, param, params);
        if (!params.empty()) {
            ded.status = TemplateDeduction::Status::Unbound;
            ded.conflictingParam = params.front();
            return false;
        }
        return true; // unknown arg для не-generic параметра: решает C++-слой
    }
    const TypeId p = reg.store().identityOf(param, TypeIdentityScope::kCanonical);
    const TypeId a = reg.store().identityOf(arg, TypeIdentityScope::kCanonical);
    const FunctionIdentity& identity = reg.identity();

    // ⚠ Ref-вид / указатель на член проверяются ДО `isTemplateParamType`: fast-path ref-бит
    // поверх `TemplateParam` (`shared<T>`) по группе выглядит как сам `T`, но это ссылочный тип.
    if (const auto* pm = reg.store().getTypeDataAs<MemberPointerTypeData>(p); pm != nullptr) {
        const auto* am = reg.store().getTypeDataAs<MemberPointerTypeData>(a);
        if (am == nullptr) {
            ded.status = TemplateDeduction::Status::NotMatch;
            return false;
        }
        if (!unifyTemplateType(reg, pm->classType, am->classType, ded)) {
            return false;
        }
        return unifyTemplateType(reg, pm->memberType, am->memberType, ded);
    }
    const RefType pr = getRefType(getTypeKind(p));
    const RefType ar = getRefType(getTypeKind(a));
    if (pr != RefType::kValue || ar != RefType::kValue) {
        if (pr != ar) {
            ded.status = TemplateDeduction::Status::NotMatch;
            return false;
        }
        if (!unifyTemplateType(reg, reg.store().getPointeeType(p), reg.store().getPointeeType(a), ded)) {
            return false;
        }
        const auto* prd = reg.store().getTypeDataAs<RefTypeData>(p);
        const auto* ard = reg.store().getTypeDataAs<RefTypeData>(a);
        // Отсутствующий узел (fast-path ref-бит) эквивалентен узлу с INVALID aux-полями:
        // `shared<Int32>` битом и `shared<Int32>` структурным узлом — один C++-тип.
        const TypeId pd = prd != nullptr ? prd->deleterType : INVALID_TYPE_ID;
        const TypeId ad = ard != nullptr ? ard->deleterType : INVALID_TYPE_ID;
        if (!unifyTemplateType(reg, pd, ad, ded)) {
            return false;
        }
        const TypeId pp = prd != nullptr ? prd->accessPolicyType : INVALID_TYPE_ID;
        const TypeId ap = ard != nullptr ? ard->accessPolicyType : INVALID_TYPE_ID;
        if (!unifyTemplateType(reg, pp, ap, ded)) {
            return false;
        }
        const TypeId pi = prd != nullptr ? prd->implType : INVALID_TYPE_ID;
        const TypeId ai = ard != nullptr ? ard->implType : INVALID_TYPE_ID;
        return unifyTemplateType(reg, pi, ai, ded);
    }
    if (reg.store().isTemplateParamType(p)) {
        if (TemplateBinding* existing = ded.findBinding(p); existing != nullptr) {
            if (existing->isPack) {
                ded.status = TemplateDeduction::Status::Conflict;
                ded.conflictingParam = p;
                return false;
            }
            if (existing->types.empty() || !identity.sameSemanticType(existing->types.front(), a)) {
                ded.status = TemplateDeduction::Status::Conflict;
                ded.conflictingParam = p;
                return false;
            }
            return true;
        }
        ded.bindings.push_back(TemplateBinding{p, false, {a}});
        return true;
    }
    // Не-generic параметр: форму проверяет обычное ранжирование (включая числовые конверсии),
    // унификация здесь не должна отвергать совместимые по C++ типы.
    std::vector<TypeId> pparams;
    collectTemplateParams(reg, p, pparams);
    if (pparams.empty()) {
        return true;
    }
    if (identity.sameSemanticType(p, a)) {
        return true;
    }
    // `:Any` — КОНКРЕТНЫЙ тип (`std::any`): для структурного параметра (`Array<T>`,
    // `shared<T>`, ...) это обычное несовпадение форм (NotMatch) — структуру из него вывести
    // нельзя.
    // Pack-expansion: узлы расширяются только в списковых контекстах; здесь допустимо
    // унифицировать только pattern-в-pattern (одинаковая форма).
    if (reg.store().isPackExpansionType(p) || reg.store().isPackExpansionType(a)) {
        if (!reg.store().isPackExpansionType(p) || !reg.store().isPackExpansionType(a)) {
            ded.status = TemplateDeduction::Status::NotMatch;
            return false;
        }
        return unifyTemplateType(reg, reg.store().packExpansionPattern(p), reg.store().packExpansionPattern(a), ded);
    }
    // Нативные шаблоны (`AsyncTask<Int32>`): C++-имя + арность + рекурсия по аргументам.
    if (reg.store().isNativeTemplateType(p) && reg.store().isNativeTemplateType(a)) {
        if (reg.store().nativeTemplateCppName(p) != reg.store().nativeTemplateCppName(a)) {
            ded.status = TemplateDeduction::Status::NotMatch;
            return false;
        }
        const auto& pa = reg.store().nativeTemplateArgs(p);
        const auto& aa = reg.store().nativeTemplateArgs(a);
        if (pa.size() != aa.size()) {
            ded.status = TemplateDeduction::Status::NotMatch;
            return false;
        }
        for (size_t i = 0; i < pa.size(); ++i) {
            if (!unifyTemplateType(reg, pa[i], aa[i], ded)) {
                return false;
            }
        }
        return true;
    }

    // Инстанциации record-шаблонов.
    if (reg.store().isRecordType(p) && reg.store().isRecordType(a)) {
        const auto* pr = reg.store().recordData(p);
        const auto* ar = reg.store().recordData(a);
        if (pr != nullptr && ar != nullptr && pr->templateOf != INVALID_TYPE_ID && ar->templateOf != INVALID_TYPE_ID) {
            if (!identity.sameSemanticType(pr->templateOf, ar->templateOf) || pr->templateArgs.size() != ar->templateArgs.size()) {
                ded.status = TemplateDeduction::Status::NotMatch;
                return false;
            }
            for (size_t i = 0; i < pr->templateArgs.size(); ++i) {
                if (!unifyTemplateType(reg, pr->templateArgs[i], ar->templateArgs[i], ded)) {
                    return false;
                }
            }
            return true;
        }
        ded.status = TemplateDeduction::Status::NotMatch;
        return false;
    }
    // Array<Elem, dims>.
    if (reg.store().isArrayType(p) && reg.store().isArrayType(a)) {
        const auto* pd = reg.store().getTypeDataAs<ArrayTypeData>(p);
        const auto* ad = reg.store().getTypeDataAs<ArrayTypeData>(a);
        if (pd == nullptr || ad == nullptr || pd->dimensions != ad->dimensions || pd->isVariadic != ad->isVariadic) {
            ded.status = TemplateDeduction::Status::NotMatch;
            return false;
        }
        return unifyTemplateType(reg, pd->elementType, ad->elementType, ded);
    }
    // Range<Elem>.
    if (reg.store().isRangeType(p) && reg.store().isRangeType(a)) {
        return unifyTemplateType(reg, reg.store().rangeElementType(p), reg.store().rangeElementType(a), ded);
    }
    // Tensor<Elem, shape>: вид хранения (dense/sparse) и shape входят в идентичность типа.
    if (reg.store().isTensorType(p) && reg.store().isTensorType(a)) {
        const auto* pd = reg.store().getTypeDataAs<TensorTypeData>(p);
        const auto* ad = reg.store().getTypeDataAs<TensorTypeData>(a);
        if (pd == nullptr || ad == nullptr || pd->shape != ad->shape || reg.store().isSparseTensorType(p) != reg.store().isSparseTensorType(a)) {
            ded.status = TemplateDeduction::Status::NotMatch;
            return false;
        }
        return unifyTemplateType(reg, pd->elementType, ad->elementType, ded);
    }
    // Structural tuple: element names and types (types may contain template parameters).
    if (const auto* pt = reg.store().getTypeDataAs<TupleTypeData>(p); pt != nullptr) {
        const auto* at = reg.store().getTypeDataAs<TupleTypeData>(a);
        if (at == nullptr || pt->elements.size() != at->elements.size()) {
            ded.status = TemplateDeduction::Status::NotMatch;
            return false;
        }
        for (std::size_t i = 0; i < pt->elements.size(); ++i) {
            if (pt->elements[i].name != at->elements[i].name ||
                !unifyTemplateType(reg, pt->elements[i].type.getTypeId(), at->elements[i].type.getTypeId(), ded)) {
                ded.status = TemplateDeduction::Status::NotMatch;
                return false;
            }
        }
        return true;
    }
    // Функциональные типы.
    if (const auto* pf = reg.store().getTypeDataAs<FunctionTypeData>(p); pf != nullptr) {
        const auto* af = reg.store().getTypeDataAs<FunctionTypeData>(a);
        if (af == nullptr || pf->paramTypes.size() != af->paramTypes.size() || pf->paramPackPosition != af->paramPackPosition) {
            ded.status = TemplateDeduction::Status::NotMatch;
            return false;
        }
        // C++-identity: variadic/pack slots compare canonically (aliases and template parameters
        // by position), not by raw TypeId.
        if (!identity.sameSemanticType(pf->variadicType, af->variadicType)) {
            ded.status = TemplateDeduction::Status::NotMatch;
            return false;
        }
        if (!unifyTemplateType(reg, pf->returnType, af->returnType, ded)) {
            return false;
        }
        for (size_t i = 0; i < pf->paramTypes.size(); ++i) {
            if (!unifyTemplateType(reg, pf->paramTypes[i], af->paramTypes[i], ded)) {
                return false;
            }
        }
        if (pf->paramPackType != INVALID_TYPE_ID || af->paramPackType != INVALID_TYPE_ID) {
            if (pf->paramPackType == INVALID_TYPE_ID || af->paramPackType == INVALID_TYPE_ID) {
                ded.status = TemplateDeduction::Status::NotMatch;
                return false;
            }
            return unifyTemplateType(reg, pf->paramPackType, af->paramPackType, ded);
        }
        return true;
    }
    ded.status = TemplateDeduction::Status::NotMatch;
    return false;
}

inline bool unifyTemplateSignature(const TypeReadModel& reg, TypeId sig, std::span<const TypeId> argTypes, TemplateDeduction& ded) {
    const TypeId s = getUnqualifiedType(sig);
    const auto* fd = reg.store().getTypeDataAs<FunctionTypeData>(s);
    if (fd == nullptr) {
        ded.status = TemplateDeduction::Status::NotMatch;
        return false;
    }
    const bool hasPack = fd->paramPackType != INVALID_TYPE_ID;
    if (hasPack && fd->variadicType != INVALID_TYPE_ID) {
        ded.status = TemplateDeduction::Status::NotMatch; // C-вариадик и type-pack несовместимы
        return false;
    }
    if (!hasPack) {
        if (argTypes.size() < fd->paramTypes.size()) {
            ded.status = TemplateDeduction::Status::NotMatch;
            return false;
        }
        if (argTypes.size() != fd->paramTypes.size() && fd->variadicType == INVALID_TYPE_ID) {
            ded.status = TemplateDeduction::Status::NotMatch;
            return false;
        }
        for (size_t i = 0; i < fd->paramTypes.size(); ++i) {
            if (!unifyTemplateType(reg, fd->paramTypes[i], argTypes[i], ded)) {
                return false;
            }
        }
        // Дополнительные аргументы C-вариадика проверяет ранжирование (variadicType),
        // включая допустимые числовые конверсии; унификация их не блокирует.
    } else {
        // Поддерживается только trailing pack с простым pattern (TemplateParam).
        const size_t fixed = fd->paramTypes.size();
        if (argTypes.size() < fixed) {
            ded.status = TemplateDeduction::Status::NotMatch;
            return false;
        }
        for (size_t i = 0; i < fixed; ++i) {
            if (!unifyTemplateType(reg, fd->paramTypes[i], argTypes[i], ded)) {
                return false;
            }
        }
        const TypeId pattern = reg.store().identityOf(fd->paramPackType, TypeIdentityScope::kCanonical);
        if (!reg.store().isTemplateParamType(pattern)) {
            ded.status = TemplateDeduction::Status::NotMatch; // сложные pack-паттерны пока не поддержаны
            return false;
        }
        TemplateBinding* binding = ded.findBinding(pattern);
        if (binding == nullptr) {
            ded.bindings.push_back(TemplateBinding{pattern, true, {}});
            binding = &ded.bindings.back();
        } else if (!binding->isPack) {
            ded.status = TemplateDeduction::Status::Conflict;
            ded.conflictingParam = pattern;
            return false;
        }
        for (size_t i = fixed; i < argTypes.size(); ++i) {
            if (argTypes[i] == INVALID_TYPE_ID) {
                ded.status = TemplateDeduction::Status::Unbound;
                ded.conflictingParam = pattern;
                return false;
            }
            binding->types.push_back(reg.store().identityOf(argTypes[i], TypeIdentityScope::kCanonical));
        }
    }
    // ВСЕ типовые параметры объявленной сигнатуры (параметры + variadic + pack + возврат)
    // обязаны быть выведены: иначе эффективная сигнатура содержала бы T вне шаблона.
    std::vector<TypeId> sigParams;
    collectTemplateParams(reg, sig, sigParams);
    for (const TypeId p : sigParams) {
        if (!ded.hasBinding(p)) {
            ded.status = TemplateDeduction::Status::Unbound;
            ded.conflictingParam = p;
            return false;
        }
    }
    ded.status = TemplateDeduction::Status::Ok;
    return true;
}

} // namespace trust
