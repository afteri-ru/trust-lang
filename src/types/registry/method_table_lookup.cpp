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

// Метод по структурному ключу дескриптора (nullptr-эквивалент — ключа нет/набор пуст).
static std::optional<MethodRef> methodRefByKey(const TypeDescriptor& desc, const MethodKey& key) {
    const auto it = desc.methods.find(key);
    if (it == desc.methods.end() || it->second.overloads.empty()) {
        return std::nullopt;
    }
    return MethodRef{key, INVALID_TYPE_ID, it->second.overloads, it->second.isNative};
}

// Поиск метода в одном дескрипторе: алиас (имя → ключ цели) или метод с совпадающим именем
// (предпочтительно с точным совпадением константности запроса, иначе первый).
static std::optional<MethodRef> findMethodInDescriptor(const TypeDescriptor& desc, const MethodKey& want) {
    if (auto it = desc.methodAliases.find(want.name); it != desc.methodAliases.end()) {
        if (auto r = methodRefByKey(desc, it->second)) {
            return r;
        }
    }
    std::optional<MethodRef> pref;
    for (const auto& [key, entry] : desc.methods) {
        (void)entry;
        if (key.name != want.name) {
            continue;
        }
        if (key.isConst == want.isConst) {
            return methodRefByKey(desc, key);
        }
        if (!pref) {
            pref = methodRefByKey(desc, key);
        }
    }
    return pref;
}

std::optional<MethodRef> MethodTable::findMethodInfo(TypeId type, std::string_view trustSpelling) const {
    return findMethodInfo(type, methodKeyFromSpelling(trustSpelling));
}

std::optional<MethodRef> MethodTable::findMethodInfo(TypeId type, MethodKey want) const {
    const TypeId canonical = m_store.desugaredTypeId(type);
    if (want.name.empty()) {
        return std::nullopt;
    }
    // Методы, объявленные ШИРЕ ядрового интерфейса встроенного типа (`=`-тело модуля), живут в
    // per-instance overlay MethodTable (собственный дескриптор builtin иммутабелен).
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
            if (auto r = findMethodInDescriptor(*desc, want)) {
                r->owner = cur; // объявивший тип: неявный object-параметр имеет этот тип
                return r;
            }
            for (const BaseClass& b : desc->baseClasses) {
                const TypeId bc = m_store.desugaredTypeId(b.type);
                if (bc != cur) {
                    pending.push_back(bc);
                }
            }
        }
        if (auto r = findInBuiltinExtras(cur, want)) {
            return r;
        }
    }
    // Параметризованный Range<Elem> сам методов не несёт: они объявлены ОДИН раз на абстрактном
    // `:Range` (структурный ключ, типовой параметр T). Для структурного Range<Elem> ищем там
    // (сигнатуру с T подставит handleMethodCall/instantiateRangeMethod).
    if (m_store.isRangeType(canonical)) {
        const TypeId rangeId = m_store.builtinIdByName(type_category::Range);
        if (const TypeDescriptor* rangeDesc = m_store.descriptorOf(rangeId)) {
            if (auto r = findMethodInDescriptor(*rangeDesc, want)) {
                return r;
            }
        }
        if (auto r = findInBuiltinExtras(rangeId, want)) {
            return r;
        }
    }
    // Параметризованный Array<Elem> - аналогично: методы объявлены на абстрактном `:Array`.
    if (m_store.isArrayType(canonical)) {
        const TypeId arrayId = m_store.builtinIdByName(type::Array);
        if (const TypeDescriptor* arrDesc = m_store.descriptorOf(arrayId)) {
            if (auto r = findMethodInDescriptor(*arrDesc, want)) {
                return r;
            }
        }
        if (auto r = findInBuiltinExtras(arrayId, want)) {
            return r;
        }
    }
    // Параметризованный Tensor<Elem, shape> - методы объявлены на абстрактном `:Tensor`
    // (для sparse - на `:SparseTensor`); сигнатуры подставляет instantiateTensorMethod.
    if (m_store.isTensorType(canonical)) {
        const TypeId base = m_store.isSparseTensorType(canonical) ? m_store.builtinIdByName(type::SparseTensor) : m_store.builtinIdByName(type::Tensor);
        if (const TypeDescriptor* desc = m_store.descriptorOf(base)) {
            if (auto r = findMethodInDescriptor(*desc, want)) {
                return r;
            }
        }
        if (auto r = findInBuiltinExtras(base, want)) {
            return r;
        }
    }
    // Пользовательский native-шаблон-инстанс: методы объявлены на абстрактном шаблоне (языковой
    // ключ `templateOf`). Сигнатуры подставляет instantiateNativeTemplateMethod при резолве.
    if (m_store.isNativeTemplateType(canonical)) {
        const auto* tpl = m_store.getTypeDataAs<NativeTemplateTypeData>(canonical);
        if (tpl != nullptr && tpl->templateOf != INVALID_TYPE_ID) {
            if (const TypeDescriptor* tplDesc = m_store.descriptorOf(tpl->templateOf)) {
                if (auto r = findMethodInDescriptor(*tplDesc, want)) {
                    return r;
                }
            }
        }
    }
    return std::nullopt;
}

std::optional<MethodRef> MethodTable::findInBuiltinExtras(TypeId builtinId, const MethodKey& want) const {
    const auto it = m_builtinExtras.find(builtinId);
    if (it == m_builtinExtras.end()) {
        return std::nullopt;
    }
    auto found = findMethodInDescriptor(it->second, want);
    if (found) {
        found->owner = builtinId;
    }
    return found;
}

} // namespace trust
