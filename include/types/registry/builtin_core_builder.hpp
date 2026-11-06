#pragma once

// Seed-строитель ОБЩЕГО иммутабельного ядра встроенных типов.
// Строит ядро над собственным локальным набором зон (не над TypeSystem): корень лишь разделяет
// готовый результат через TypeSystem::builtinCore(). Иначе получился бы цикл «builder -> корень».
// Порядок seed-регистрации — часть контракта: он задаёт registry_index встроенных типов.

#include "types/group.hpp"
#include "types/registry/builtin_type_core.hpp"
#include "types/type_id.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

/// Состояние seed-сборки (определение — в .cpp): зоны живут на стеке builder'а.
struct BuiltinSeedZones;

class BuiltinCoreBuilder {
  public:
    static BuiltinTypeCore build();

  private:
    static TypeId registerBuiltinType(BuiltinSeedZones& z, std::string_view name, Group group, uint8_t data = 0, std::string_view cppName = {},
                                      bool markAttrs = false);
    static void registerBuiltinTypes(BuiltinSeedZones& z);
    static void registerBuiltinContainerMethods(BuiltinSeedZones& z);
};

} // namespace trust
