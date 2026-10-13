// src/types/subst/template_subst.cpp
// Реализация подстановки типовых параметров (engine), см. types/subst/template_subst.hpp.
#include "types/subst/template_subst.hpp"

#include "types/registry/structural_interner.hpp"
#include "types/type_read.hpp"
#include "types/type_system.hpp"
#include "types/type_data.hpp"
#include "types/typekind.hpp"
#include "utils/error.hpp"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trust {

TypeId substituteTemplateParams(TypeWriteContext ctx, const TemplateBindings& bindings, TypeId type) {
    if (type == INVALID_TYPE_ID || bindings.empty()) {
        return type;
    }
    const TypeId canonical = ctx.read.store().desugaredTypeId(type);
    // Прямая привязка типового параметра.
    for (const auto& b : bindings) {
        if (ctx.read.store().desugaredTypeId(b.param) != canonical) {
            continue;
        }
        if (!b.isPack) {
            if (b.types.empty()) {
                return INVALID_TYPE_ID;
            }
            // TypeId — identity-value: квалификаторы не переносятся (для state — QualType-обёртка).
            return getUnqualifiedType(b.types.front());
        }
        // Пакет в одиночной позиции недопустим (splice — `substituteTemplateArgList`).
        return INVALID_TYPE_ID;
    }
    // PackExpansion(pattern): подставляем только non-pack привязки в pattern.
    if (ctx.read.store().isPackExpansionType(canonical)) {
        const TypeId pattern = ctx.read.store().packExpansionPattern(canonical);
        TemplateBindings nonPack;
        nonPack.reserve(bindings.size());
        for (const auto& b : bindings) {
            if (!b.isPack) {
                nonPack.push_back(b);
            }
        }
        const TypeId sub = substituteTemplateParams(ctx, nonPack, pattern);
        if (sub == INVALID_TYPE_ID) {
            return INVALID_TYPE_ID;
        }
        return ctx.interner.getOrCreatePackExpansionType(sub);
    }
    // Pack-splice в списковом контексте native-шаблона. Только НЕ-ссылочные узлы: ссылочный
    // вид обрабатывает pair-механизм ниже (иначе потерялся бы ref-бит/узел).
    if (getRefType(getTypeKind(canonical)) == RefType::kValue) {
        // Нативный шаблон: splice нужен ТОЛЬКО при наличии PackExpansion-аргумента; обычная
        // подстановка уходит в pair-механизм (единая реализация, без дублирования).
        if (const auto* nt = ctx.read.store().getTypeDataAs<NativeTemplateTypeData>(canonical)) {
            const std::vector<TypeId> a0 = nt->args;
            bool hasPack = false;
            for (const TypeId a : a0) {
                if (ctx.read.store().isPackExpansionType(a)) {
                    hasPack = true;
                    break;
                }
            }
            if (hasPack) {
                const std::string cppTpl = nt->cppTemplate;
                std::vector<TypeId> args;
                args.reserve(a0.size());
                for (const TypeId a : a0) {
                    if (ctx.read.store().isPackExpansionType(a)) {
                        const TypeId pattern = ctx.read.store().desugaredTypeId(ctx.read.store().packExpansionPattern(a));
                        bool bound = false;
                        for (const auto& b : bindings) {
                            if (b.isPack && ctx.read.store().desugaredTypeId(b.param) == pattern) {
                                bound = true;
                                break;
                            }
                        }
                        if (!bound) {
                            return INVALID_TYPE_ID;
                        }
                        const auto spliced = substituteTemplateArgList(ctx, bindings, a);
                        args.insert(args.end(), spliced.begin(), spliced.end());
                        continue;
                    }
                    const TypeId sub = substituteTemplateParams(ctx, bindings, a);
                    if (sub == INVALID_TYPE_ID) {
                        return INVALID_TYPE_ID;
                    }
                    args.push_back(sub);
                }
                return ctx.interner.getOrCreateNativeTemplateType(cppTpl, std::move(args), ctx.read.builtins().preprocIncludeOf(canonical));
            }
        }
        // Структурный кортеж: pair-механизм его не покрывает. Элементы - одиночные контексты
        // (pack-элемент вернёт INVALID → явная ошибка вызывающего).
        if (const auto* tt = ctx.read.store().getTypeDataAs<TupleTypeData>(canonical)) {
            const std::vector<TupleElementData> e0 = tt->elements;
            std::vector<std::pair<std::string, QualType>> elems;
            elems.reserve(e0.size());
            for (const auto& e : e0) {
                const TypeId sub = substituteTemplateParams(ctx, bindings, e.type.getTypeId());
                if (sub == INVALID_TYPE_ID) {
                    return INVALID_TYPE_ID;
                }
                elems.emplace_back(e.name, e.type.withIdentity(sub));
            }
            return ctx.interner.getOrCreateTupleType(std::move(elems));
        }
    }
    // Обычная подстановка: pair-механизм (record-шаблоны, native без pack-аргументов и пр.).
    std::vector<std::pair<TypeId, TypeId>> mapping;
    mapping.reserve(bindings.size());
    for (const auto& b : bindings) {
        if (!b.isPack && !b.types.empty()) {
            mapping.emplace_back(b.param, b.types.front());
        }
    }
    return substituteTypeParams(ctx, mapping, type);
}

TypeId instantiateCallSignature(TypeWriteContext ctx, TypeId declaredSig, const TemplateBindings& bindings, std::span<const TypeId> argTypes) {
    const TypeId sig = getUnqualifiedType(declaredSig);
    const auto* fd = ctx.read.store().getTypeDataAs<FunctionTypeData>(sig);
    EXPECT(fd != nullptr && "instantiateCallSignature: declared signature must be a function type");
    const TypeId ret0 = fd->returnType;
    const TypeId var0 = fd->variadicType;
    const TypeId pack0 = fd->paramPackType;
    const std::vector<TypeId> p0 = fd->paramTypes;
    const bool hasTrust = isTrustedType(sig);
    std::vector<TypeId> params;
    params.reserve(p0.size() + argTypes.size());
    for (const TypeId p : p0) {
        const TypeId sub = substituteTemplateParams(ctx, bindings, p);
        // INVALID параметр (нетипизированный) - законный Any-подобный случай; ошибка только если
        // реальный тип не удалось подставить.
        EXPECT((p == INVALID_TYPE_ID || sub != INVALID_TYPE_ID) && "instantiateCallSignature: cannot substitute a call parameter type");
        params.push_back(sub);
    }
    TypeId pack = pack0;
    uint8_t packPos = fd->paramPackPosition;
    if (pack0 != INVALID_TYPE_ID) {
        // Trailing type-pack: фактические аргументы начиная с fixed-позиции образуют плоский
        // список параметров (порядок значим). В эффективной сигнатуре pack-слота НЕТ (C++-модель
        // специализации: пак развёрнут в конкретные параметры).
        EXPECT(argTypes.size() >= p0.size() && "instantiateCallSignature: fewer arguments than fixed parameters");
        for (std::size_t i = p0.size(); i < argTypes.size(); ++i) {
            params.push_back(getUnqualifiedType(argTypes[i]));
        }
        pack = INVALID_TYPE_ID;
        packPos = 0;
    }
    const TypeId ret = substituteTemplateParams(ctx, bindings, ret0);
    if (ret0 != INVALID_TYPE_ID && ret == INVALID_TYPE_ID) {
        // Пак в одиночной позиции возврата: эффективную сигнатуру построить нельзя. FAULT здесь
        // недопустим (это диагностируемая ошибка программы) - вызывающий обязан её сообщить.
        return INVALID_TYPE_ID;
    }
    const TypeId var = substituteTemplateParams(ctx, bindings, var0);
    return ctx.interner.getOrCreateFunctionType(ret, std::move(params), var, hasTrust, pack, packPos);
}

std::vector<TypeId> substituteTemplateArgList(TypeWriteContext ctx, const TemplateBindings& bindings, TypeId arg) {
    std::vector<TypeId> out;
    EXPECT(arg != INVALID_TYPE_ID && "substituteTemplateArgList: INVALID type argument");
    const TypeId canonical = ctx.read.store().desugaredTypeId(arg);
    if (ctx.read.store().isPackExpansionType(canonical)) {
        const TypeId pattern = ctx.read.store().packExpansionPattern(canonical);
        for (const auto& b : bindings) {
            if (!b.isPack || ctx.read.store().desugaredTypeId(b.param) != ctx.read.store().desugaredTypeId(pattern)) {
                continue;
            }
            for (const TypeId t : b.types) {
                TemplateBindings one = bindings;
                for (auto& x : one) {
                    if (ctx.read.store().desugaredTypeId(x.param) == ctx.read.store().desugaredTypeId(b.param)) {
                        x.isPack = false;
                        x.types = {t};
                    }
                }
                const TypeId sub = substituteTemplateParams(ctx, one, pattern);
                EXPECT(sub != INVALID_TYPE_ID && "substituteTemplateArgList: pack element substitution failed");
                out.push_back(sub);
            }
            return out;
        }
        EXPECT(false && "substituteTemplateArgList: pack binding is missing");
        return out;
    }
    const TypeId sub = substituteTemplateParams(ctx, bindings, arg);
    EXPECT(sub != INVALID_TYPE_ID && "substituteTemplateArgList: type substitution failed");
    out.push_back(sub);
    return out;
}

/// Внутренний worker (file-local, реализация ниже): O(1)-карта param(канонический) -> arg для
/// однопроходной подстановки БЕЗ линейного поиска по типовым параметрам. Ключ —
/// desugaredTypeId(param): у типового параметра semantic-идентичность == сам id, а алиасы параметра
/// в теле схлопываются канонизацией — это эквивалент `identity.sameSemanticType(from,
/// occurrence)`, но за O(1), а не O(#params) на каждый узел.
using SubstArgMap = std::unordered_map<TypeId, TypeId, std::hash<TypeId>>;
TypeId substituteTypeParamsImpl(TypeWriteContext ctx, const SubstArgMap& byId, TypeId type);

TypeId substituteTypeParams(TypeWriteContext ctx, const std::vector<std::pair<TypeId, TypeId>>& mapping, TypeId type) {
    if (type == INVALID_TYPE_ID || mapping.empty()) {
        return type;
    }
    SubstArgMap byId;
    byId.reserve(mapping.size());
    for (const auto& [from, to] : mapping) {
        if (from == INVALID_TYPE_ID) {
            continue;
        }
        byId.emplace(ctx.read.store().desugaredTypeId(from), getUnqualifiedType(to));
    }
    return substituteTypeParamsImpl(ctx, byId, type);
}

TypeId substituteTypeParamsImpl(TypeWriteContext ctx, const SubstArgMap& byId, TypeId type) {
    if (type == INVALID_TYPE_ID) {
        return type;
    }
    const TypeId canonical = ctx.read.store().desugaredTypeId(type);
    // O(1) поиск по канонической идентичности параметра; квалификаторы occurrence остаются в
    // QualType и не переносятся (TypeId — identity-value).
    const auto it = byId.find(canonical);
    if (it != byId.end()) {
        return getUnqualifiedType(it->second);
    }
    // Fast-path ref-вид (первая ссылка на тип без структурного узла): вид лежит в TypeKind, а
    // TypeData отсутствует, поэтому ветки ниже не сработали бы и `shared<T>` в шаблоне НЕ
    // подставился бы. Снимаем вид, подставляем pointee, применяем вид заново.
    {
        const RefType fastKind = getRefType(getTypeKind(canonical));
        if (fastKind != RefType::kValue && ctx.read.store().getTypeDataAs<RefTypeData>(canonical) == nullptr) {
            const TypeId pointee = ctx.read.store().getPointeeType(canonical);
            return ctx.interner.applyRefType(substituteTypeParamsImpl(ctx, byId, pointee), fastKind);
        }
    }
    // ВАЖНО: рекурсивные подстановки добавляют дескрипторы в TypeStore; дескрипторы хранятся в
    // std::deque (адреса стабильны), но набор данных шаблона может измениться — снимаем нужные
    // поля в ЛОКАЛЬНЫЕ копии ДО рекурсии.
    // Функциональная сигнатура (метод): возврат/параметры/variadic/pack.
    if (const auto* fd = ctx.read.store().getTypeDataAs<FunctionTypeData>(canonical)) {
        const TypeId ret0 = fd->returnType;
        const TypeId var0 = fd->variadicType;
        const TypeId pack0 = fd->paramPackType;
        const uint8_t packPos = fd->paramPackPosition;
        const std::vector<TypeId> p0 = fd->paramTypes;
        // Pack-слот и trust-бит — ЧАСТЬ идентичности сигнатуры: потеряв их, подставленная
        // сигнатура совпала бы с сигнатурой без паков/условий (дубль в TypeKey).
        const bool hasTrust = isTrustedType(canonical);
        std::vector<TypeId> params;
        params.reserve(p0.size());
        for (const TypeId p : p0) {
            params.push_back(substituteTypeParamsImpl(ctx, byId, p));
        }
        const TypeId ret = substituteTypeParamsImpl(ctx, byId, ret0);
        const TypeId var = substituteTypeParamsImpl(ctx, byId, var0);
        const TypeId pack = substituteTypeParamsImpl(ctx, byId, pack0);
        return ctx.interner.getOrCreateFunctionType(ret, std::move(params), var, hasTrust, pack, packPos);
    }
    // Ссылочный узел (shared<T>/unique<T,...> и пр.).
    if (const auto* rd = ctx.read.store().getTypeDataAs<RefTypeData>(canonical)) {
        const RefType rk = getRefType(getTypeKind(canonical));
        const TypeId pointee0 = rd->pointeeType;
        const TypeId deleter0 = rd->deleterType;
        const TypeId policy0 = rd->accessPolicyType;
        const TypeId impl0 = rd->implType;
        return ctx.interner.getOrCreateRefType(rk, substituteTypeParamsImpl(ctx, byId, pointee0), substituteTypeParamsImpl(ctx, byId, deleter0),
                                               substituteTypeParamsImpl(ctx, byId, policy0), substituteTypeParamsImpl(ctx, byId, impl0));
    }
    // Array<Elem> (размерности не зависят от параметра).
    if (const auto* ad = ctx.read.store().getTypeDataAs<ArrayTypeData>(canonical)) {
        const TypeId elem0 = ad->elementType;
        const std::vector<uint64_t> dims = ad->dimensions;
        return ctx.interner.getOrCreateArrayType(substituteTypeParamsImpl(ctx, byId, elem0), dims);
    }
    // Range<Elem> (TemplateTypeData).
    if (const auto* td = ctx.read.store().getTypeDataAs<TemplateTypeData>(canonical)) {
        if (!td->args.empty()) {
            const TypeId elem0 = td->args[0];
            return ctx.interner.getOrCreateRangeType(substituteTypeParamsImpl(ctx, byId, elem0));
        }
    }
    // Tensor<Elem, shape>.
    if (const auto* td = ctx.read.store().getTypeDataAs<TensorTypeData>(canonical)) {
        const TypeId elem0 = td->elementType;
        const std::vector<uint64_t> shape = td->shape;
        return ctx.interner.getOrCreateTensorType(substituteTypeParamsImpl(ctx, byId, elem0), shape, ctx.read.store().isSparseTensorType(canonical));
    }
    // Structural tuple: element types may contain template parameters (names are not types).
    if (const auto* tt = ctx.read.store().getTypeDataAs<TupleTypeData>(canonical)) {
        const std::vector<TupleElementData> e0 = tt->elements;
        std::vector<std::pair<std::string, QualType>> elems;
        elems.reserve(e0.size());
        for (const auto& e : e0) {
            elems.emplace_back(e.name, e.type.withIdentity(substituteTypeParamsImpl(ctx, byId, e.type.getTypeId())));
        }
        return ctx.interner.getOrCreateTupleType(std::move(elems));
    }
    // Нативный шаблон-тип (`std::pair<T,U>`).
    if (const auto* nt = ctx.read.store().getTypeDataAs<NativeTemplateTypeData>(canonical)) {
        const std::string cppTpl = nt->cppTemplate;
        const std::vector<TypeId> a0 = nt->args;
        std::vector<TypeId> args;
        args.reserve(a0.size());
        for (const TypeId a : a0) {
            args.push_back(substituteTypeParamsImpl(ctx, byId, a));
        }
        return ctx.interner.getOrCreateNativeTemplateType(cppTpl, std::move(args), ctx.read.builtins().preprocIncludeOf(canonical));
    }
    // Вложенная инстанциация record-шаблона (напр. база `Base<:T>`).
    if (const auto* rec = ctx.read.store().getTypeDataAs<RecordTypeData>(canonical)) {
        if (rec->templateOf != INVALID_TYPE_ID) {
            const TypeId tmplOf0 = rec->templateOf;
            const std::vector<TypeId> a0 = rec->templateArgs;
            std::vector<TypeId> args;
            args.reserve(a0.size());
            for (const TypeId a : a0) {
                args.push_back(substituteTypeParamsImpl(ctx, byId, a));
            }
            return ctx.interner.getOrCreateRecordTemplateInstance(tmplOf0, std::move(args));
        }
    }
    return type;
}

} // namespace trust
