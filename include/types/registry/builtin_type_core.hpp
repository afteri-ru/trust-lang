#pragma once

// include/types/registry/builtin_type_core.hpp
// Общее иммутабельное ядро встроенных типов: строится ОДИН раз и разделяется всеми
// экземплярами TypeSystem. Пользовательские типы живут в каждом экземпляре отдельно.
//
// Таблица `structural` — стартовая таблица интернирования seed-структурных типов (сигнатуры
// методов встроенных типов, `Array<Int64>` для `%shape^` и т.п.). Инстанс продолжает её,
// иначе та же структура получила бы второй TypeId.

#include <cstddef>
#include <deque>
#include <unordered_map>
#include <vector>

#include "types/runtime_symbols.hpp"
#include "types/names/storage_key.hpp"
#include "types/type_data.hpp"

namespace trust {

struct BuiltinTypeCore {
    std::deque<TypeDescriptor> descriptors;                            ///< index = registry_index - 1
    std::unordered_map<StorageKey, TypeId, StorageKeyHash> name_to_id; ///< встроенные имена (+ cpp-имена)
    std::vector<RuntimeSymbol> runtimeSymbols;
    std::size_t builtinCount = 0;                                      ///< = descriptors.size()
    std::unordered_map<TypeKey, TypeId, TypeKeyHash> structural;
};

} // namespace trust
