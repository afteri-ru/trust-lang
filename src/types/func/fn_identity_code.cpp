// Канонический КОД типа (identity): единственный источник сегментов сигнатуры и C++-суффикса.
#include "types/func/fn_identity.hpp"

#include "types/group.hpp"
#include "types/names/type_names.hpp"
#include "types/registry/type_code_cache.hpp"
#include "types/registry/type_printer.hpp"
#include "types/registry/type_store.hpp"
#include "types/type/data_record_access.hpp"
#include "types/typekind.hpp"
#include "utils/error.hpp"

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace trust {

namespace {

template <typename>
inline constexpr bool kUnhandledTypeData = false;

/// Self-delimiting atom: length-prefix makes the encoding injective (a type name that
/// contains an escaped separator can never collide with a structural separator).
std::string atom(std::string_view text) {
    return std::to_string(text.size()) + ":" + std::string(text);
}

} // namespace

std::string FunctionIdentity::typeCode(TypeId type, const TemplateBinders& binders, int depth, TypeIdentityScope scope) const {
    if (type == INVALID_TYPE_ID) {
        return "void";
    }
    EXPECT(depth <= 128 && "FunctionIdentity: type nesting limit exceeded");
    // kSemantic: user aliases are nominal; kCanonical expands them (external C++ `using`).
    const TypeId id = m_store.identityOf(type, scope);
    // Cache only binder-free (concrete) codes: a code that references binder positions depends
    // on the declaration context and must never be shared between declarations.
    if (binders.empty()) {
        if (const std::string* cached = m_codes.find(id, scope)) {
            return *cached;
        }
        std::string computed = computeTypeCode(id, binders, depth, scope);
        return m_codes.store(id, scope, std::move(computed));
    }
    return computeTypeCode(id, binders, depth, scope);
}

std::string FunctionIdentity::returnTypeCode(TypeId type, const TemplateBinders& binders, int depth, TypeIdentityScope scope) const {
    if (type == INVALID_TYPE_ID) {
        return "void";
    }
    // Нет аннотации, `Void` и `None` кодогенер эмитит как `void` (decl_func_emit.cpp) — для
    // C++-идентичности это ОДИН тип. В позиции параметра правило НЕ применяется: `INVALID` там
    // означает нетипизированный `:Any` (`std::any`), а не `void`.
    const TypeId id = m_store.identityOf(type, scope);
    if (typeGroupOf(id) == Group::kVoid) {
        return "void";
    }
    return typeCode(type, binders, depth, scope);
}

std::string FunctionIdentity::computeTypeCode(TypeId id, const TemplateBinders& binders, int depth, TypeIdentityScope scope) const {
    // Reference/member-pointer KIND is checked BEFORE the TypeData: a fast-path reference
    // (`applyRefType`) keeps the pointee's registry index, so `getTypeData(id)` returns the
    // POINTEE's data. Reading the data first would collapse `shared<T>` into `T`.
    const RefType refKind = typeRefOf(id);
    if (refKind == RefType::kMptr) {
        const auto* mp = m_store.getTypeDataAs<MemberPointerTypeData>(id);
        EXPECT(mp != nullptr && "FunctionIdentity: member pointer requires MemberPointerTypeData");
        return "M(" + typeCode(mp->classType, binders, depth + 1, scope) + "," + typeCode(mp->memberType, binders, depth + 1, scope) + ")";
    }
    if (refKind != RefType::kValue) {
        std::string text = std::string(refTypeName(refKind)) + "<" + typeCode(m_store.getPointeeType(id), binders, depth + 1, scope);
        if (const auto* rd = m_store.getTypeDataAs<RefTypeData>(id); rd != nullptr) {
            if (rd->deleterType != INVALID_TYPE_ID) {
                text += ",D:" + typeCode(rd->deleterType, binders, depth + 1, scope);
            }
            if (rd->accessPolicyType != INVALID_TYPE_ID) {
                text += ",P:" + typeCode(rd->accessPolicyType, binders, depth + 1, scope);
            }
            if (rd->implType != INVALID_TYPE_ID) {
                text += ",I:" + typeCode(rd->implType, binders, depth + 1, scope);
            }
        }
        return text + ">";
    }

    // Nominal identity: the trust full name (builtins, aliases to nominal types, records,
    // enums, variants, native classes, forward declarations without data).
    const auto nominal = [this](TypeId nominalId) -> std::string {
        const std::string name = m_printer.typeNameOf(nominalId);
        EXPECT(!name.empty() && "FunctionIdentity: type has no canonical name");
        return "N" + atom(name);
    };

    const auto& data = m_store.getTypeData(id);
    if (!data.has_value()) {
        return nominal(id); // forward declaration: nominal type, no structural data yet
    }
    return std::visit(
        [&](const auto& d) -> std::string {
            using D = std::decay_t<decltype(d)>;
            if constexpr (std::is_same_v<D, TemplateParamTypeData>) {
                // Binder canonicalization: a template parameter is identified by its position in the
                // enclosing declaration, not by its source name (C++-like). Packs are a distinct kind.
                for (std::size_t i = 0; i < binders.size(); ++i) {
                    if (binders[i].name == d.name) {
                        return std::string(binders[i].isPack ? "P" : "T") + std::to_string(i) + ";";
                    }
                }
                // Not part of the current declaration's binder list: fall back to the declared name
                // (declaration-less contexts and template parameters of an outer scope).
                return "T#" + atom(d.name) + "#";
            } else if constexpr (std::is_same_v<D, FunctionTypeData>) {
                std::string text = "F(" + returnTypeCode(d.returnType, binders, depth + 1, scope) + ")N=" + std::to_string(d.paramTypes.size()) + ";";
                for (const TypeId param : d.paramTypes) {
                    text += "P:" + typeCode(param, binders, depth + 1, scope) + ";";
                }
                text += "V=" + (d.variadicType != INVALID_TYPE_ID ? typeCode(d.variadicType, binders, depth + 1, scope) : std::string("-")) + ";";
                text += "K=" + std::to_string(static_cast<unsigned>(d.paramPackPosition)) + "," +
                        (d.paramPackType != INVALID_TYPE_ID ? typeCode(d.paramPackType, binders, depth + 1, scope) : std::string("-")) + ";";
                return text;

            } else if constexpr (std::is_same_v<D, NativeTemplateTypeData>) {
                // Native template identity — LANGUAGE key: abstract template (nominal trust name) +
                // type arguments. The C++ template name is a TARGET fact, NOT part of the language
                // identity (R37, §4 ARCHITECTURE.md).
                const TypeId tplKey = d.templateOf != INVALID_TYPE_ID ? d.templateOf : id;
                std::string text = "C" + nominal(tplKey) + "(";
                for (std::size_t i = 0; i < d.args.size(); ++i) {
                    if (i) {
                        text += ",";
                    }
                    text += typeCode(d.args[i], binders, depth + 1, scope);
                }
                return text + ")";
            } else if constexpr (std::is_same_v<D, ArrayTypeData>) {
                std::string text = "A(" + typeCode(d.elementType, binders, depth + 1, scope) + ")D=";
                for (const uint64_t dim : d.dimensions) {
                    text += std::to_string(dim) + ",";
                }
                text += ";V=" + std::to_string(d.isVariadic ? 1 : 0);
                return text;
            } else if constexpr (std::is_same_v<D, TensorTypeData>) {
                // Dense/sparse is encoded in TypeKind::Data (not in TypeData) - single source of truth.
                std::string text =
                    "S(" + typeCode(d.elementType, binders, depth + 1, scope) + ")K=" + std::to_string(static_cast<unsigned>(typeDataOf(id))) + ";D=";
                for (const uint64_t dim : d.shape) {
                    text += std::to_string(dim) + ",";
                }
                return text;
            } else if constexpr (std::is_same_v<D, TemplateTypeData>) {
                // Parameterized abstract type (`Range<Elem>`): the template marker is part of the
                // identity, and ALL arguments matter (not just the first one).
                std::string text = "G" + atom(m_printer.typeNameOf(d.templateTypeId)) + "(";
                for (std::size_t i = 0; i < d.args.size(); ++i) {
                    if (i) {
                        text += ",";
                    }
                    text += typeCode(d.args[i], binders, depth + 1, scope);
                }
                return text + ")";
            } else if constexpr (std::is_same_v<D, RecordTypeData>) {
                if (RecordTypeAccess::templateOf(d) != INVALID_TYPE_ID) {
                    const auto& templateArgs = RecordTypeAccess::templateArgsOf(d);
                    std::string text = "R" + atom(m_printer.typeNameOf(RecordTypeAccess::templateOf(d))) + "(";
                    for (std::size_t i = 0; i < templateArgs.size(); ++i) {
                        if (i) {
                            text += ",";
                        }
                        text += typeCode(templateArgs[i], binders, depth + 1, scope);
                    }
                    return text + ")";
                }
                return nominal(id); // ordinary record: nominal identity
            } else if constexpr (std::is_same_v<D, TupleTypeData>) {
                std::string text = "U(";
                for (const auto& element : d.elements) {
                    text += atom(element.name) + ":" + typeCode(element.type.getTypeId(), binders, depth + 1, scope) + ";";
                }
                return text + ")";
            } else if constexpr (std::is_same_v<D, MemberPointerTypeData>) {
                return "M(" + typeCode(d.classType, binders, depth + 1, scope) + "," + typeCode(d.memberType, binders, depth + 1, scope) + ")";
            } else if constexpr (std::is_same_v<D, PackExpansionTypeData>) {
                return "E(" + typeCode(d.pattern, binders, depth + 1, scope) + ")";
            } else if constexpr (std::is_same_v<D, RefTypeData>) {
                // Unreachable in practice: reference nodes are decoded from TypeKind BEFORE the
                // data visit. Kept for compile-time exhaustiveness of the TypeData variant.
                std::string text = std::string(refTypeName(refKind)) + "<" + typeCode(d.pointeeType, binders, depth + 1, scope);
                if (d.deleterType != INVALID_TYPE_ID) {
                    text += ",D:" + typeCode(d.deleterType, binders, depth + 1, scope);
                }
                if (d.accessPolicyType != INVALID_TYPE_ID) {
                    text += ",P:" + typeCode(d.accessPolicyType, binders, depth + 1, scope);
                }
                if (d.implType != INVALID_TYPE_ID) {
                    text += ",I:" + typeCode(d.implType, binders, depth + 1, scope);
                }
                return text + ">";
            } else if constexpr (std::is_same_v<D, SimpleTypeData> || std::is_same_v<D, EnumTypeData> || std::is_same_v<D, VariantTypeData> ||
                                 std::is_same_v<D, NativeClassTypeData>) {
                // Nominal identity is fully expressed by the full name.
                return nominal(id);
            } else {
                static_assert(kUnhandledTypeData<D>, "FunctionIdentity::typeCode: unhandled TypeData variant - describe its identity");
                return {};
            }
        },
        *data);
}

} // namespace trust
