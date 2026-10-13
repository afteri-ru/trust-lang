// src/types/registry/method_table.cpp
// Зона методов типа: наборы сигнатур, алиасы имён, статические члены.
#include "types/registry/method_table.hpp"

#include "types/func/fn_identity.hpp"
#include "types/registry/type_store.hpp"
#include "types/names/type_names.hpp"
#include "utils/error.hpp"
#include "utils/strings.hpp"
#include <algorithm>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace trust {

namespace {

/// A member of an EXTERNAL C++ symbol: a native key (`%name`), a static member key
/// (`ns::Class::name`) or a member of a native class/template. External C++ does not distinguish
/// `using`, so such duplicates must be rejected by FFI identity (aliases expanded), not only by
/// the nominal Trust identity.
bool isNativeMember(const TypeDescriptor& desc, std::string_view key) {
    if (utils::isNativeName(key) || utils::isStaticName(key)) {
        return true;
    }
    if (!desc.data.has_value()) {
        return false;
    }
    return std::holds_alternative<NativeClassTypeData>(*desc.data) || std::holds_alternative<NativeTemplateTypeData>(*desc.data);
}

} // namespace

void MethodTable::addMethod(TypeId type, std::string_view name, TypeId funcType, std::vector<std::string_view> aliases, CallableParamInfo params,
                            TemplateBinders binders) {
    EXPECT(isUnqualifiedType(funcType) && "addMethod: function type id must not carry type-state flags");
    const TypeId canonical = m_store.desugaredTypeId(type);
    TypeDescriptor* desc = m_store.writableDescriptor(canonical);
    EXPECT(desc != nullptr && "addMethod: unknown type (methods can only be added to user-defined types)");
    // Полный ключ - как передан (нативность '%' и константность '^' кодируются в имени).
    const std::string key(name);
    EXPECT(!utils::bareName(key).empty() && "addMethod: empty method name");
    const bool isConst = utils::isConstName(key);
    // Инвариант «одна форма имени»: другой КЛЮЧ с тем же bare-именем и константностью
    // недопустим (нативное '%c_str' и обычное 'c_str' - один метод, регистрируется ОДНОЙ формой).
    // Перегрузки - в НАБОРЕ ТОГО ЖЕ ключа (см. ниже).
    for (const auto& [k, sigs] : desc->methods) {
        (void)sigs;
        EXPECT((k == key || utils::bareName(k) != utils::bareName(key) || utils::isConstName(k) != isConst) &&
               "addMethod: method already registered on type (other name form, same name+constness)");
    }
    // Набор сигнатур ключа: ПЕРЕГРУЗКИ добавляются; C++-идентичный дубль сигнатуры - EXPECT.
    std::vector<CallableSignature>& overloads = desc->methods[key];
    const TypeId sig = getUnqualifiedType(funcType);
    const FunctionIdentity& identity = m_identity;
    for (const CallableSignature& existing : overloads) {
        EXPECT(identity.compare(existing.signature, binders, sig, binders, IdentityScope::kOverload) != SignatureRelation::kEquivalent &&
               "addMethod: method already registered on type (duplicate C++ signature)");
        // Нативный метод: внешний C++ не различает `using`-алиасы -> нужна FFI-идентичность.
        if (isNativeMember(*desc, key)) {
            EXPECT(identity.compare(existing.signature, binders, sig, binders, IdentityScope::kInterop) != SignatureRelation::kEquivalent &&
                   "addMethod: native method signatures are indistinguishable in C++ (aliases)");
        }
    }
    // C++-имя метода - identity-суффикс его объявления (та же формула, что у свободных функций).
    overloads.push_back(CallableSignature{sig, std::move(params), identity.cppSuffix(sig, binders)});
    // Алиасы: новые доверенные имена этого метода. Обязаны полностью повторять семантику цели
    // (нативность и константность совпадают) и не конфликтовать с существующими именами.
    for (const auto& aliasName : aliases) {
        EXPECT(utils::isNativeName(aliasName) == utils::isNativeName(key) && "addMethod: alias and target must be both native or both non-native");
        EXPECT(utils::isConstName(aliasName) == isConst && "addMethod: alias and target must be both const or both non-const");
        const std::string bare = utils::bareName(aliasName);
        for (const auto& [k, ft] : desc->methods) {
            (void)ft;
            EXPECT(utils::bareName(k) != bare && "addMethod: alias conflicts with an existing method");
        }
        EXPECT(!desc->methodAliases.count(bare) && "addMethod: alias already registered");
        desc->methodAliases[bare] = key;
    }
}

bool MethodTable::hasOwnMethodSignature(TypeId type, std::string_view name, TypeId signature, TemplateBinders binders) const {
    const TypeId canonical = m_store.desugaredTypeId(type);
    const TypeDescriptor* desc = m_store.descriptorOf(canonical);
    if (!desc) {
        return false;
    }
    const std::string bare = utils::bareName(name);
    const bool wantConst = utils::isConstName(name);
    const TypeId sig = getUnqualifiedType(signature);
    const FunctionIdentity& identity = m_identity;
    for (const auto& [key, sigs] : desc->methods) {
        if (utils::bareName(key) != bare || utils::isConstName(key) != wantConst) {
            continue;
        }
        for (const CallableSignature& s : sigs) {
            if (identity.compare(s.signature, binders, sig, binders, IdentityScope::kOverload) == SignatureRelation::kEquivalent) {
                return true;
            }
            // Native member: also indistinguishable in C++ through alias-expanded signatures.
            if (isNativeMember(*desc, key) && identity.compare(s.signature, binders, sig, binders, IdentityScope::kInterop) == SignatureRelation::kEquivalent) {
                return true;
            }
        }
    }
    return false;
}

bool MethodTable::hasOwnMemberName(TypeId type, std::string_view name) const {
    const TypeId canonical = m_store.desugaredTypeId(type);
    const TypeDescriptor* desc = m_store.descriptorOf(canonical);
    if (!desc) {
        return false;
    }
    const std::string bare = utils::bareName(name);
    const bool wantConst = utils::isConstName(name);
    for (const auto& [key, sigs] : desc->methods) {
        (void)sigs;
        if (utils::bareName(key) == bare && utils::isConstName(key) == wantConst) {
            return true;
        }
    }
    return false;
}

std::vector<TypeId> MethodTable::findStaticMethod(TypeId type, std::string_view name) const {
    const TypeId canonical = m_store.desugaredTypeId(type);
    const TypeDescriptor* desc = m_store.descriptorOf(canonical);
    if (!desc) {
        return {};
    }
    // Статический член: ключ содержит '::' (вид `ns::Class::name`), имя члена - последний сегмент.
    for (const auto& [k, sigs] : desc->methods) {
        if (!utils::isStaticName(k)) {
            continue;
        }
        const std::string_view last = utils::shortNameOf(k);
        if (last == name) {
            std::vector<TypeId> out;
            out.reserve(sigs.size());
            for (const CallableSignature& entry : sigs) {
                out.push_back(entry.signature);
            }
            return out;
        }
    }
    return {};
}

} // namespace trust
