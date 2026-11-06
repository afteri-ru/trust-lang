// C++-рендеринг имени типа (перенесён из types/registry, R37). См. заголовок.
#include "transpiler/type/cpp_type_renderer.hpp"

#include "types/names.hpp"
#include "types/types.hpp"
#include "utils/strings.hpp"

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace trust {

namespace {

/// Безопасная метка типа для текста ошибки: рендерер НЕ должен зависеть от другого принтера
/// (`typeNameOf` может сам отказать), поэтому имя берётся из дескриптора напрямую.
std::string cppTypeLabel(const TypeReadModel& types, TypeId id) {
    const std::string_view name = types.store().nameOf(id);
    return !name.empty() ? std::string(name) : std::format("type#{}", id.raw());
}

/// C++-имя номинального пользовательского типа: namespace-путь из ключа регистрации + display-имя.
/// Модуль в имя НЕ входит (изоляция — через области имён программиста); имя всегда qualified.
std::string nominalCppName(const TypeReadModel& types, TypeId id) {
    const std::string_view namespacePath = types.store().namespacePath(id);
    const std::string_view name = types.store().nameOf(id);
    if (namespacePath.empty()) {
        return utils::cppIdent(name);
    }
    // Единые точки домена имён: сборка qualified trust-имени (qualifyTrustName) + C++-маппинг
    // сегментов (qualifiedCppName). Ручная сборка namespace-пути в кодогене запрещена guard'ом.
    return utils::qualifiedCppName(utils::qualifyTrustName(namespacePath, name));
}

std::expected<std::string, std::string> printCppName(const TypeReadModel& types, const TypeCppFactsView& facts, TypeId id, bool isConst);

std::expected<std::string, std::string> printCppRefWrapper(const TypeReadModel& types, const TypeCppFactsView& facts, TypeId id, std::string base) {
    const RefType rt = typeRefOf(id);
    // Deleter внешнего ресурса (`@[deleter(D)]`) — ЧАСТЬ типа `trust::Unique<T, D>`.
    std::string deleterCpp;
    if (rt == RefType::kUnique) {
        if (const auto* rd = types.store().getTypeDataAs<RefTypeData>(id); rd && rd->deleterType != INVALID_TYPE_ID) {
            auto del = printCppName(types, facts, rd->deleterType, false);
            if (!del) {
                return std::unexpected(std::move(del.error()));
            }
            deleterCpp = std::move(*del);
        }
    }
    // Sync-backend: shared/weak с политикой -> trust::AccessShared<T, Policy> (weak оборачивает его).
    if (rt == RefType::kShared || rt == RefType::kWeak) {
        if (const auto* rd = types.store().getTypeDataAs<RefTypeData>(id); rd && rd->accessPolicyType != INVALID_TYPE_ID) {
            const auto syncId = types.store().builtinIdByName(type::AccessShared);
            if (syncId == INVALID_TYPE_ID) {
                return std::unexpected(std::format("printType: sync wrapper type '{}' not found in the registry", type::AccessShared));
            }
            auto wrapper = printCppName(types, facts, syncId, false);
            auto policy = printCppName(types, facts, rd->accessPolicyType, false);
            if (!wrapper) {
                return std::unexpected(std::move(wrapper.error()));
            }
            if (!policy) {
                return std::unexpected(std::move(policy.error()));
            }
            std::string implArg;
            if (rd->implType != INVALID_TYPE_ID) {
                auto impl = printCppName(types, facts, rd->implType, false);
                if (!impl) {
                    return std::unexpected(std::move(impl.error()));
                }
                implArg = std::move(*impl);
            }
            const std::string strong = *wrapper + "<" + base + ", " + *policy + (implArg.empty() ? "" : (", " + implArg)) + ">";
            if (rt == RefType::kWeak) {
                return std::string(refTypeCppTemplateName(RefType::kWeak)) + "<" + strong + ">";
            }
            return strong;
        }
    }
    if (rt == RefType::kUnique) {
        if (!deleterCpp.empty()) {
            return refTypeCppName(RefType::kUnique, base, deleterCpp);
        }
        const auto wrapId = types.store().builtinIdByName(type::StaticUnique);
        if (wrapId == INVALID_TYPE_ID) {
            return std::unexpected(std::format("printType: wrapper type '{}' not found in the registry", type::StaticUnique));
        }
        auto wrap = printCppName(types, facts, wrapId, false);
        if (!wrap) {
            return std::unexpected(std::move(wrap.error()));
        }
        return *wrap + "<" + base + ">";
    }
    return refTypeCppName(rt, base, deleterCpp);
}

std::expected<std::string, std::string> printCppName(const TypeReadModel& types, const TypeCppFactsView& facts, TypeId id, bool isConst) {
    // Константность — ортогональный квалификатор: лидирующий `const `.
    const RefType rt = typeRefOf(id);

    // Функциональный тип (FunctionTypeData) — ЗНАЧЕНИЕ-функция: `std::function<Ret(Args...)>`.
    if (const auto* ft = types.store().getTypeDataAs<FunctionTypeData>(id)) {
        std::string name = "std::function<";
        if (ft->returnType == INVALID_TYPE_ID) {
            name += "void";
        } else {
            auto ret = printCppName(types, facts, ft->returnType, false);
            if (!ret) {
                return std::unexpected(std::move(ret.error()));
            }
            name += *ret;
        }
        name += "(";
        for (size_t i = 0; i < ft->paramTypes.size(); ++i) {
            if (i) {
                name += ", ";
            }
            if (ft->paramTypes[i] == INVALID_TYPE_ID) {
                name.append(facts.anyCppName());
                continue;
            }
            auto p = printCppName(types, facts, ft->paramTypes[i], false);
            if (!p) {
                return std::unexpected(std::move(p.error()));
            }
            name += *p;
        }
        name += ")>";
        if (isConst) {
            name = "const " + name;
        }
        return name;
    }

    // Базовое C++-имя pointee/значения.
    std::string base;
    if (const auto* data = types.store().getTypeDataAs<RefTypeData>(id)) {
        auto inner = printCppName(types, facts, data->pointeeType, false);
        if (!inner) {
            return std::unexpected(std::move(inner.error()));
        }
        base = std::move(*inner);
    } else if (const std::string_view declared = facts.declaredCppName(id); !declared.empty()) {
        base = std::string(declared);
    } else if (const auto* tp = types.store().getTypeDataAs<TemplateParamTypeData>(types.store().desugaredTypeId(id)); tp != nullptr) {
        base = tp->name;
    } else {
        if (!types.store().hasDescriptor(id)) {
            return std::unexpected(std::format("printType: no descriptor for type id {}", id.raw()));
        }
        if (types.store().baseTypeOf(id) != INVALID_TYPE_ID) {
            if (types.store().isUserDefinedType(id)) {
                base = nominalCppName(types, id);
            } else {
                auto canon = printCppName(types, facts, types.store().desugaredTypeId(id), false);
                if (!canon) {
                    return canon;
                }
                base = std::move(*canon);
            }
        } else if (types.store().isRecordType(id) || types.store().getTypeDataAs<EnumTypeData>(types.store().desugaredTypeId(id)) != nullptr ||
                   types.store().getTypeDataAs<VariantTypeData>(types.store().desugaredTypeId(id)) != nullptr) {
            const auto instance = types.store().instanceOf(types.store().desugaredTypeId(id));
            if (instance.has_value()) {
                const TypeId instanceOfTemplate = instance->templateOf;
                base = (types.store().hasDescriptor(instanceOfTemplate) ? nominalCppName(types, instanceOfTemplate)
                                                                        : utils::cppIdent(types.printer().typeNameOf(instanceOfTemplate))) +
                       "<";
                for (size_t i = 0; i < instance->templateArgs.size(); ++i) {
                    if (i) {
                        base += ", ";
                    }
                    auto arg = printCppName(types, facts, instance->templateArgs[i], false);
                    if (!arg) {
                        return arg;
                    }
                    base += *arg;
                }
                base += ">";
            } else {
                base = nominalCppName(types, id);
            }
        } else {
            return std::unexpected(types.store().isBuiltinType(id)
                                       ? std::format("printType: implementation of built-in type '{}' is not defined", cppTypeLabel(types, id))
                                       : std::format("printType: type '{}' has no C++ name", cppTypeLabel(types, id)));
        }
    }

    if (isConst) {
        base = "const " + base;
    }

    if (rt == RefType::kMptr) {
        const auto* mp = types.store().getTypeDataAs<MemberPointerTypeData>(id);
        if (!mp) {
            return std::unexpected(std::format("printType: mptr requires MemberPointerTypeData, type '{}'", cppTypeLabel(types, id)));
        }
        auto cls = printCppName(types, facts, mp->classType, false);
        if (!cls) {
            return cls;
        }
        auto mem = printCppName(types, facts, mp->memberType, false);
        if (!mem) {
            return mem;
        }
        return *mem + " " + *cls + "::*";
    }
    return printCppRefWrapper(types, facts, id, std::move(base));
}

} // namespace

std::expected<std::string, std::string> CppTypeRenderer::render(TypeId id) const {
    return printCppName(m_types, m_facts, id, false);
}

std::expected<std::string, std::string> CppTypeRenderer::render(QualType state) const {
    return printCppName(m_types, m_facts, state.getTypeId(), state.isConst());
}

} // namespace trust
