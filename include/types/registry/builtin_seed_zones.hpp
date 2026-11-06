#pragma once

// Состояние seed-сборки ядра встроенных типов: зоны живут на стеке BuiltinCoreBuilder.
// Отдельный заголовок нужен, чтобы регистрация и сборка ядра были разными TU.

#include "types/registry/builtin_registry.hpp"
#include "types/registry/method_table.hpp"
#include "types/registry/named_type_registrar.hpp"
#include "types/registry/structural_interner.hpp"
#include "types/registry/type_name_index.hpp"
#include "types/type_read.hpp"
#include "types/registry/type_store.hpp"

namespace trust {

struct BuiltinSeedZones {
    TypeStore& store;
    TypeNameIndex& names;
    NamedTypeRegistrar& registrar;
    BuiltinRegistry& builtins;
    MethodTable& methods;
    StructuralInterner& interner;
    const TypeReadModel& read;
};

} // namespace trust
