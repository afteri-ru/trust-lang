// src/types/registry/method_table_lookup.cpp
// Зона методов: ПОИСК метода по имени (собственный дескриптор, базы, абстрактные контейнеры).
#include "types/registry/method_table.hpp"

#include "types/func/fn_identity.hpp"
#include "types/registry/structural_interner.hpp"
#include "types/registry/type_store.hpp"
#include "types/names/type_names.hpp"
#include "utils/strings.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace trust {

// Метод по полному ключу дескриптора (nullptr-эквивалент — ключа нет/набор пуст).
static std::optional<MethodRef> methodRefByKey(const TypeDescriptor& desc, std::string_view key) {
    const auto it = desc.methods.find(std::string(key));
    if (it == desc.methods.end() || it->second.empty()) {
        return std::nullopt;
    }
    return MethodRef{std::string(key), INVALID_TYPE_ID, it->second};
}

// Поиск метода в одном дескрипторе: алиас (bare → ключ цели) или метод с совпадающим bare-именем
// (предпочтительно с точным совпадением константности запроса, иначе первый).
static std::optional<MethodRef> findMethodInDescriptor(const TypeDescriptor& desc, std::string_view bare, bool wantConst) {
    if (auto it = desc.methodAliases.find(std::string(bare)); it != desc.methodAliases.end()) {
        if (auto r = methodRefByKey(desc, it->second)) {
            return r;
        }
    }
    std::optional<MethodRef> pref;
    for (const auto& [k, sigs] : desc.methods) {
        (void)sigs;
        if (utils::bareName(k) != bare) {
            continue;
        }
        if (utils::isConstName(k) == wantConst) {
            return methodRefByKey(desc, k);
        }
        if (!pref) {
            pref = methodRefByKey(desc, k);
        }
    }
    return pref;
}

std::optional<MethodRef> MethodTable::findMethodInfo(TypeId type, std::string_view name) const {
    const TypeId canonical = m_store.desugaredTypeId(type);
    const std::string bare = utils::bareName(name);
    if (bare.empty()) {
        return std::nullopt;
    }
    const bool wantConst = utils::isConstName(name);
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
        // C++-модель неявной инстанциации: первый доступ к члену record-специализации (в т.ч.
        // базовой/унаследованной) материализует её методы из шаблона; повторный — no-op (мемо).
        if (m_materializer != nullptr) {
            m_materializer->materializeRecordInstanceMembers(cur);
        }
        if (const TypeDescriptor* desc = m_store.descriptorOf(cur)) {
            if (auto r = findMethodInDescriptor(*desc, bare, wantConst)) {
                r->owner = cur; // объявивший тип: неявный object-параметр имеет этот тип
                return r;
            }
            for (const TypeId b : desc->baseClasses) {
                const TypeId bc = m_store.desugaredTypeId(b);
                if (bc != cur) {
                    pending.push_back(bc);
                }
            }
        }
    }
    // Параметризованный Range<Elem> сам методов не несёт: они объявлены ОДИН раз на абстрактном
    // `:Range` (ключ с '%'/'^', типовой параметр T). Для структурного Range<Elem> ищем там
    // (сигнатуру с T подставит handleMethodCall/instantiateRangeMethod).
    if (m_store.isRangeType(canonical)) {
        if (const TypeDescriptor* rangeDesc = m_store.descriptorOf(m_store.builtinIdByName(type_category::Range))) {
            if (auto r = findMethodInDescriptor(*rangeDesc, bare, wantConst)) {
                return r;
            }
        }
    }
    // Параметризованный Array<Elem> - аналогично: методы объявлены на абстрактном `:Array`.
    if (m_store.isArrayType(canonical)) {
        if (const TypeDescriptor* arrDesc = m_store.descriptorOf(m_store.builtinIdByName(type::Array))) {
            if (auto r = findMethodInDescriptor(*arrDesc, bare, wantConst)) {
                return r;
            }
        }
    }
    // Параметризованный Tensor<Elem, shape> - методы объявлены на абстрактном `:Tensor`
    // (для sparse - на `:SparseTensor`); сигнатуры подставляет instantiateTensorMethod.
    if (m_store.isTensorType(canonical)) {
        const TypeId base = m_store.isSparseTensorType(canonical) ? m_store.builtinIdByName(type::SparseTensor) : m_store.builtinIdByName(type::Tensor);
        if (const TypeDescriptor* desc = m_store.descriptorOf(base)) {
            if (auto r = findMethodInDescriptor(*desc, bare, wantConst)) {
                return r;
            }
        }
    }
    // Пользовательский native-шаблон-инстанс: методы объявлены на абстрактном шаблоне
    // (NativeTemplateTypeData с пустыми args и тем же cppTemplate). Сигнатуры подставляет
    // instantiateNativeTemplateMethod при резолве.
    if (m_store.isNativeTemplateType(canonical)) {
        const auto* tpl = m_store.getTypeDataAs<NativeTemplateTypeData>(canonical);
        if (tpl != nullptr && !tpl->args.empty()) {
            for (const TypeDescriptor& candidate : m_store.userDescriptors()) {
                if (!candidate.data.has_value() || !std::holds_alternative<NativeTemplateTypeData>(*candidate.data)) {
                    continue;
                }
                const auto& data = std::get<NativeTemplateTypeData>(*candidate.data);
                if (!data.args.empty() || data.cppTemplate != tpl->cppTemplate) {
                    continue;
                }
                if (auto r = findMethodInDescriptor(candidate, bare, wantConst)) {
                    return r;
                }
            }
        }
    }
    return std::nullopt;
}

} // namespace trust
