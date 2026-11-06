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

/// A member of an EXTERNAL C++ symbol: a static member key (`ns::Class::name`) or a member of a
/// native class/template. External C++ does not distinguish `using`, so such duplicates must be
/// rejected by FFI identity (aliases expanded), not only by the nominal Trust identity.
bool isNativeMember(const TypeDescriptor& desc, const MethodKey& key, const MethodInfo& info) {
    if (key.isStatic || info.isNative) {
        return true;
    }
    if (!desc.data.has_value()) {
        return false;
    }
    return std::holds_alternative<NativeClassTypeData>(*desc.data) || std::holds_alternative<NativeTemplateTypeData>(*desc.data);
}

} // namespace

void MethodTable::addMethod(TypeId type, std::string_view trustSpelling, TypeId funcType, std::vector<std::string_view> aliases, CallableParamInfo params,
                            TemplateBinders binders, Visibility access, DeclId decl) {
    EXPECT(trustSpelling.find('%') == std::string_view::npos && "addMethod: method name is trust-only ('%' is a binding, not a name)");
    std::vector<MethodKey> aliasKeys;
    aliasKeys.reserve(aliases.size());
    for (std::string_view alias : aliases) {
        EXPECT(alias.find('%') == std::string_view::npos && "addMethod: method alias is trust-only ('%' is a binding, not a name)");
        aliasKeys.push_back(methodKeyFromSpelling(alias));
    }
    addMethodImpl(type, methodKeyFromSpelling(trustSpelling), false, funcType, std::move(aliasKeys), std::move(params), binders, access, decl);
}

void MethodTable::addMethod(TypeId type, MethodKey key, TypeId funcType, std::vector<MethodKey> aliases, CallableParamInfo params, TemplateBinders binders,
                            Visibility access, DeclId decl) {
    const TypeDescriptor* desc = m_store.descriptorOf(m_store.desugaredTypeId(type));
    const bool isNative =
        key.isStatic || (desc != nullptr && desc->data.has_value() &&
                         (std::holds_alternative<NativeClassTypeData>(*desc->data) || std::holds_alternative<NativeTemplateTypeData>(*desc->data)));
    addMethodImpl(type, std::move(key), isNative, funcType, std::move(aliases), std::move(params), binders, access, decl);
}

namespace {

/// Тело регистрации метода в ДЕСКРИПТОРЕ (собственном дескрипторе пользовательского типа или в
/// overlay встроенного): проверки формы ключа, добавление перегрузки и алиасов. ЕДИНАЯ точка для
/// addMethodImpl и addBuiltinExtraMethod (без дублирования правил).
void registerMethodIntoDescriptor(TypeDescriptor& desc, MethodKey key, bool isNative, TypeId funcType, std::vector<MethodKey> aliases, CallableParamInfo params,
                                  TemplateBinders binders, const FunctionIdentity& identity, Visibility access, DeclId decl) {
    EXPECT(isUnqualifiedType(funcType) && "addMethod: function type id must not carry type-state flags");
    EXPECT(!key.name.empty() && "addMethod: empty method name");
    EXPECT(key.name.find('%') == std::string::npos && "addMethod: method name is trust-only ('%' is a binding, not a name)");
    EXPECT(key.isStatic == (key.name.find("::") != std::string::npos) && "addMethod: isStatic must match the qualified method name");
    // Инвариант «одна форма имени»: другой КЛЮЧ с тем же именем и константностью недопустим
    // (нативный и обычный член с одним именем — один метод, регистрируется ОДНОЙ формой).
    for (const auto& [k, info] : desc.methods) {
        (void)info;
        EXPECT((k == key || k.name != key.name || k.isConst != key.isConst) &&
               "addMethod: method already registered on type (other name form, same name+constness)");
    }
    // Запись ключа: ПЕРЕГРУЗКИ добавляются; C++-идентичный дубль сигнатуры - EXPECT.
    MethodInfo& info = desc.methods[key];
    EXPECT((!info.overloads.empty() || !info.isNative) && "addMethod: native form set before registration");
    EXPECT(info.overloads.empty() || info.isNative == isNative && "addMethod: native form differs from the registered entry");
    info.isNative = isNative;
    std::vector<CallableSignature>& overloads = info.overloads;
    const TypeId sig = getUnqualifiedType(funcType);
    for (const CallableSignature& existing : overloads) {
        EXPECT(identity.compare(existing.signature, binders, sig, binders, IdentityScope::kOverload) != SignatureRelation::kEquivalent &&
               "addMethod: method already registered on type (duplicate C++ signature)");
        // Нативный метод: внешний C++ не различает `using`-алиасы -> нужна FFI-идентичность.
        if (isNativeMember(desc, key, info)) {
            EXPECT(identity.compare(existing.signature, binders, sig, binders, IdentityScope::kInterop) != SignatureRelation::kEquivalent &&
                   "addMethod: native method signatures are indistinguishable in C++ (aliases)");
        }
    }
    // Целевой C++-суффикс метода НЕ хранится здесь: его владелец — контракт `target`
    // (`DeclCppFacts` по `DeclId`), сюда пишется только ключ-идентичность объявления (R1).
    overloads.push_back(CallableSignature{sig, std::move(params), decl, access});
    // Алиасы: новые доверенные имена этого метода. Обязаны повторять семантику цели
    // (static/const совпадают; нативность — свойство записи, не ключа) и не конфликтовать.
    for (const MethodKey& aliasKey : aliases) {
        EXPECT(aliasKey.isStatic == key.isStatic && "addMethod: alias and target must be both static or both non-static");
        EXPECT(aliasKey.isConst == key.isConst && "addMethod: alias and target must be both const or both non-const");
        EXPECT(!aliasKey.name.empty() && "addMethod: empty method alias");
        for (const auto& [k, entry] : desc.methods) {
            (void)entry;
            EXPECT(k.name != aliasKey.name && "addMethod: alias conflicts with an existing method");
        }
        EXPECT(!desc.methodAliases.count(aliasKey.name) && "addMethod: alias already registered");
        desc.methodAliases[aliasKey.name] = key;
    }
}

} // namespace

void MethodTable::addMethodImpl(TypeId type, MethodKey key, bool isNative, TypeId funcType, std::vector<MethodKey> aliases, CallableParamInfo params,
                                TemplateBinders binders, Visibility access, DeclId decl) {
    const TypeId canonical = m_store.desugaredTypeId(type);
    TypeDescriptor* desc = m_store.writableDescriptor(canonical);
    EXPECT(desc != nullptr && "addMethod: unknown type (methods can only be added to user-defined types)");
    registerMethodIntoDescriptor(*desc, std::move(key), isNative, funcType, std::move(aliases), std::move(params), binders, m_identity, access, decl);
}

void MethodTable::addBuiltinExtraMethod(TypeId type, MethodKey key, TypeId funcType, CallableParamInfo params) {
    // Дескрипторы встроенных типов живут в ИММУТАБЕЛЬНОМ ядре (BuiltinTypeCore): методы ШИРЕ
    // ядрового интерфейса (объявленные в `=`-реализации модуля) складываются в per-instance overlay.
    const TypeId canonical = m_store.desugaredTypeId(type);
    EXPECT(m_store.isBuiltinType(canonical) && "addBuiltinExtraMethod: methods beyond the core interface are only allowed on built-in types");
    TypeDescriptor& desc = m_builtinExtras[canonical];
    registerMethodIntoDescriptor(desc, std::move(key), /*isNative=*/false, funcType, /*aliases=*/{}, std::move(params), /*binders=*/{}, m_identity,
                                 /*access=*/Visibility::Public, /*decl=*/{});
}

void MethodTable::clearBuiltinExtraMethods() noexcept {
    m_builtinExtras.clear();
}

namespace {

bool descriptorHasMethodSignature(const TypeDescriptor& desc, const MethodKey& key, TypeId sig, TemplateBinders binders, const FunctionIdentity& identity) {
    for (const auto& [storedKey, info] : desc.methods) {
        if (storedKey.name != key.name || storedKey.isConst != key.isConst) {
            continue;
        }
        for (const CallableSignature& s : info.overloads) {
            if (identity.compare(s.signature, binders, sig, binders, IdentityScope::kOverload) == SignatureRelation::kEquivalent) {
                return true;
            }
            // Native member: also indistinguishable in C++ through alias-expanded signatures.
            if (isNativeMember(desc, storedKey, info) &&
                identity.compare(s.signature, binders, sig, binders, IdentityScope::kInterop) == SignatureRelation::kEquivalent) {
                return true;
            }
        }
    }
    return false;
}

bool descriptorHasMemberName(const TypeDescriptor& desc, const MethodKey& key) {
    for (const auto& [storedKey, info] : desc.methods) {
        (void)info;
        if (storedKey.name == key.name && storedKey.isConst == key.isConst) {
            return true;
        }
    }
    return false;
}

} // namespace

bool MethodTable::hasOwnMethodSignature(TypeId type, std::string_view trustSpelling, TypeId signature, TemplateBinders binders) const {
    return hasOwnMethodSignature(type, methodKeyFromSpelling(trustSpelling), signature, binders);
}

bool MethodTable::hasOwnMethodSignature(TypeId type, MethodKey key, TypeId signature, TemplateBinders binders) const {
    const TypeId canonical = m_store.desugaredTypeId(type);
    const TypeDescriptor* desc = m_store.descriptorOf(canonical);
    if (desc == nullptr) {
        return false;
    }
    const TypeId sig = getUnqualifiedType(signature);
    if (descriptorHasMethodSignature(*desc, key, sig, binders, m_identity)) {
        return true;
    }
    // Методы ШИРЕ ядрового интерфейса встроенного типа живут в per-instance overlay.
    if (const auto it = m_builtinExtras.find(canonical); it != m_builtinExtras.end()) {
        return descriptorHasMethodSignature(it->second, key, sig, binders, m_identity);
    }
    return false;
}

bool MethodTable::hasOwnMemberName(TypeId type, std::string_view trustSpelling) const {
    return hasOwnMemberName(type, methodKeyFromSpelling(trustSpelling));
}

bool MethodTable::hasOwnMemberName(TypeId type, MethodKey key) const {
    const TypeId canonical = m_store.desugaredTypeId(type);
    const TypeDescriptor* desc = m_store.descriptorOf(canonical);
    if (desc == nullptr) {
        return false;
    }
    if (descriptorHasMemberName(*desc, key)) {
        return true;
    }
    if (const auto it = m_builtinExtras.find(canonical); it != m_builtinExtras.end()) {
        return descriptorHasMemberName(it->second, key);
    }
    return false;
}

std::vector<TypeId> MethodTable::findStaticMethod(TypeId type, std::string_view name) const {
    const TypeId canonical = m_store.desugaredTypeId(type);
    const TypeDescriptor* desc = m_store.descriptorOf(canonical);
    if (!desc) {
        return {};
    }
    // Статический член: ключ несёт полное `ns::Class::name`, имя члена - последний сегмент.
    for (const auto& [k, info] : desc->methods) {
        if (!k.isStatic) {
            continue;
        }
        const std::string_view last = utils::shortNameOf(k.name);
        if (last == name) {
            std::vector<TypeId> out;
            out.reserve(info.overloads.size());
            for (const CallableSignature& entry : info.overloads) {
                out.push_back(entry.signature);
            }
            return out;
        }
    }
    return {};
}

} // namespace trust
