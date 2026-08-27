// src/types/method_table.cpp
// Реестр методов типов (obj.method(...)): addMethod + поиск findMethodInfo/findMethod.
// Модуль (декомпозиция registry.cpp).
#include "types/registry.hpp"
#include "types/type_names.hpp"
#include "utils/error.hpp"
#include "utils/strings.hpp"
#include <optional>
#include <string>
namespace trust {

void TypeRegistry::addMethod(TypeId type, std::string_view name, TypeId funcType, std::vector<std::string_view> aliases) {
    const TypeId canonical = getCanonicalTypeId(type);
    TypeDescriptor* desc = userDescriptorOf(canonical);
    EXPECT(desc != nullptr && "addMethod: unknown type (methods can only be added to user-defined types)");
    // Полный ключ - как передан (нативность '%' и константность '^' кодируются в имени).
    const std::string key(name);
    EXPECT(!utils::bare_name(key).empty() && "addMethod: empty method name");
    // Дубликат: то же bare-имя + та же константность (независимо от '%' - нативное и обычное
    // написание - один метод). const и не-const перегрузки с одинаковыми аргументами - разные.
    const bool isConst = utils::is_const_name(key);
    for (const auto& [k, ft] : desc->methods) {
        (void)ft;
        EXPECT((utils::bare_name(k) != utils::bare_name(key) || utils::is_const_name(k) != isConst) &&
               "addMethod: method already registered on type (same name+constness)");
    }
    desc->methods[key] = funcType;
    // Алиасы: новые доверенные имена этого метода. Обязаны полностью повторять семантику цели
    // (нативность и константность совпадают) и не конфликтовать с существующими именами.
    for (const auto& aliasName : aliases) {
        EXPECT(utils::is_native_name(aliasName) == utils::is_native_name(key) && "addMethod: alias and target must be both native or both non-native");
        EXPECT(utils::is_const_name(aliasName) == isConst && "addMethod: alias and target must be both const or both non-const");
        const std::string bare = utils::bare_name(aliasName);
        for (const auto& [k, ft] : desc->methods) {
            (void)ft;
            EXPECT(utils::bare_name(k) != bare && "addMethod: alias conflicts with an existing method");
        }
        EXPECT(!desc->methodAliases.count(bare) && "addMethod: alias already registered");
        desc->methodAliases[bare] = key;
    }
}

// Поиск метода в одном дескрипторе: алиас (bare → ключ цели) или метод с совпадающим bare-именем
// (предпочтительно с точным совпадением константности запроса, иначе первый).
static std::optional<TypeRegistry::MethodRef> findMethodInDescriptor(const TypeDescriptor& desc, std::string_view bare, bool wantConst) {
    if (auto it = desc.methodAliases.find(std::string(bare)); it != desc.methodAliases.end()) {
        if (auto m = desc.methods.find(it->second); m != desc.methods.end()) {
            return TypeRegistry::MethodRef{it->second, m->second};
        }
    }
    TypeRegistry::MethodRef fallback;
    bool have = false;
    for (const auto& [k, ft] : desc.methods) {
        if (utils::bare_name(k) == bare) {
            if (utils::is_const_name(k) == wantConst) {
                return TypeRegistry::MethodRef{k, ft};
            }
            if (!have) {
                fallback = {k, ft};
                have = true;
            }
        }
    }
    if (have) {
        return fallback;
    }
    return std::nullopt;
}

std::optional<TypeRegistry::MethodRef> TypeRegistry::findMethodInfo(TypeId type, std::string_view name) const {
    const TypeId canonical = getCanonicalTypeId(type);
    const std::string bare = utils::bare_name(name);
    if (bare.empty()) {
        return std::nullopt;
    }
    const bool wantConst = utils::is_const_name(name);
    // Собственный дескриптор типа.
    if (const TypeDescriptor* desc = descriptorOf(canonical)) {
        if (auto r = findMethodInDescriptor(*desc, bare, wantConst)) {
            return r;
        }
    }
    // Параметризованный Range<Elem> сам методов не несёт: они объявлены ОДИН раз на абстрактном
    // `:Range` (ключ с '%'/'^', типовой параметр T). Для структурного Range<Elem> ищем там
    // (сигнатуру с T подставит handleMethodCall/instantiateRangeMethod).
    if (isRangeType(canonical)) {
        if (const TypeDescriptor* rangeDesc = descriptorOf(getType(type_category::Range))) {
            if (auto r = findMethodInDescriptor(*rangeDesc, bare, wantConst)) {
                return r;
            }
        }
    }
    // Параметризованный Array<Elem> - аналогично: методы объявлены на абстрактном `:Array`.
    if (isArrayType(canonical)) {
        if (const TypeDescriptor* arrDesc = descriptorOf(getType(type::Array))) {
            if (auto r = findMethodInDescriptor(*arrDesc, bare, wantConst)) {
                return r;
            }
        }
    }
    return std::nullopt;
}

TypeId TypeRegistry::findMethod(TypeId type, std::string_view name) const {
    const auto m = findMethodInfo(type, name);
    return m ? m->funcType : INVALID_TYPE_ID;
}
} // namespace trust
