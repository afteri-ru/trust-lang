// src/types/method_table.cpp
// Реестр методов типов (obj.method(...)): addMethod + поиск findMethodInfo/findMethod.
// Модуль (декомпозиция registry.cpp).
#include "types/registry.hpp"
#include "types/type_names.hpp"
#include "utils/error.hpp"
#include "utils/strings.hpp"
#include <algorithm>
#include <optional>
#include <string>
#include <vector>
namespace trust {

void TypeRegistry::addMethod(TypeId type, std::string_view name, TypeId funcType, std::vector<std::string_view> aliases) {
    const TypeId canonical = getCanonicalTypeId(type);
    TypeDescriptor* desc = userDescriptorOf(canonical);
    EXPECT(desc != nullptr && "addMethod: unknown type (methods can only be added to user-defined types)");
    // Полный ключ - как передан (нативность '%' и константность '^' кодируются в имени).
    const std::string key(name);
    EXPECT(!utils::bare_name(key).empty() && "addMethod: empty method name");
    const bool isConst = utils::is_const_name(key);
    // Инвариант «одна форма имени»: другой КЛЮЧ с тем же bare-именем и константностью
    // недопустим (нативное '%c_str' и обычное 'c_str' - один метод, регистрируется ОДНОЙ формой).
    // Перегрузки - в НАБОРЕ ТОГО ЖЕ ключа (см. ниже).
    for (const auto& [k, sigs] : desc->methods) {
        (void)sigs;
        EXPECT((k == key || utils::bare_name(k) != utils::bare_name(key) || utils::is_const_name(k) != isConst) &&
               "addMethod: method already registered on type (other name form, same name+constness)");
    }
    // Набор сигнатур ключа: ПЕРЕГРУЗКИ добавляются; точный дубль сигнатуры - EXPECT.
    std::vector<TypeId>& overloads = desc->methods[key];
    const TypeId sig = structuralType(funcType);
    for (const TypeId existing : overloads) {
        EXPECT(structuralType(existing) != sig && "addMethod: method already registered on type (duplicate signature)");
    }
    overloads.push_back(sig);
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
    const auto make = [&desc](std::string_view k) -> std::optional<TypeRegistry::MethodRef> {
        auto m = desc.methods.find(std::string(k));
        if (m == desc.methods.end() || m->second.empty()) {
            return std::nullopt;
        }
        return TypeRegistry::MethodRef{std::string(k), m->second};
    };
    if (auto it = desc.methodAliases.find(std::string(bare)); it != desc.methodAliases.end()) {
        if (auto r = make(it->second)) {
            return r;
        }
    }
    std::optional<TypeRegistry::MethodRef> pref;
    for (const auto& [k, sigs] : desc.methods) {
        (void)sigs;
        if (utils::bare_name(k) != bare) {
            continue;
        }
        if (utils::is_const_name(k) == wantConst) {
            return make(k);
        }
        if (!pref) {
            pref = make(k);
        }
    }
    return pref;
}

std::optional<TypeRegistry::MethodRef> TypeRegistry::findMethodInfo(TypeId type, std::string_view name) const {
    const TypeId canonical = getCanonicalTypeId(type);
    const std::string bare = utils::bare_name(name);
    if (bare.empty()) {
        return std::nullopt;
    }
    const bool wantConst = utils::is_const_name(name);
    // Собственный дескриптор типа + базовые классы (наследование Record-типов): метод может быть
    // унаследован. Обход с защитой от циклов (производный не должен ссылаться на себя).
    std::vector<TypeId> pending{canonical};
    std::vector<TypeId> visited;
    while (!pending.empty()) {
        const TypeId cur = pending.back();
        pending.pop_back();
        if (std::find(visited.begin(), visited.end(), cur) != visited.end()) {
            continue;
        }
        visited.push_back(cur);
        if (const TypeDescriptor* desc = descriptorOf(cur)) {
            if (auto r = findMethodInDescriptor(*desc, bare, wantConst)) {
                return r;
            }
            for (const TypeId b : desc->baseClasses) {
                const TypeId bc = getCanonicalTypeId(b);
                if (bc != cur) {
                    pending.push_back(bc);
                }
            }
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

std::vector<TypeId> TypeRegistry::findStaticMethod(TypeId type, std::string_view name) const {
    const TypeId canonical = getCanonicalTypeId(type);
    const TypeDescriptor* desc = descriptorOf(canonical);
    if (!desc) {
        return {};
    }
    // Статический член: ключ содержит '::' (вид `ns::Class::name`), имя члена - последний сегмент.
    for (const auto& [k, sigs] : desc->methods) {
        if (!utils::is_static_name(k)) {
            continue;
        }
        const size_t p = k.rfind("::");
        const std::string_view last = (p == std::string_view::npos) ? std::string_view(k) : std::string_view(k).substr(p + 2);
        if (last == name) {
            return sigs;
        }
    }
    return {};
}
} // namespace trust
