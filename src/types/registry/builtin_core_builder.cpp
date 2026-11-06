// Seed-строитель общего иммутабельного ядра встроенных типов (см. builtin_core_builder.hpp).
// Регистрация идёт над локальным набором зон; TypeSystem получает уже готовое ядро.
#include "types/registry/builtin_core_builder.hpp"

#include "types/registry/builtin_seed_zones.hpp"

#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "types/func/fn_identity.hpp"
#include "types/group.hpp"
#include "types/names/storage_key.hpp"
#include "types/names/type_names.hpp"
#include "types/registry/builtin_registry.hpp"
#include "types/registry/method_table.hpp"
#include "types/registry/named_type_registrar.hpp"
#include "types/registry/structural_interner.hpp"
#include "types/registry/type_code_cache.hpp"
#include "types/registry/type_name_index.hpp"
#include "types/registry/type_printer.hpp"
#include "types/registry/type_store.hpp"
#include "types/type_read.hpp"
#include "utils/error.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {

namespace {

constexpr bool isAbstractGroup(Group group) noexcept {
    switch (group) {
    case Group::kAny:
    case Group::kTensors:
    case Group::kContainers:
    case Group::kStructured:
    case Group::kCallable:
    case Group::kClasses:
    case Group::kRanges:
    case Group::kIterators:
    case Group::kDateTime:
    case Group::kAsync:
    case Group::kSync:
    case Group::kExceptions:
    case Group::kNative:
    case Group::kArithmetics:
    case Group::kTemplateParam:
        return true;
    default:
        return false;
    }
}

} // namespace

TypeId BuiltinCoreBuilder::registerBuiltinType(BuiltinSeedZones& z, std::string_view name, Group group, uint8_t data, std::string_view cpp_name,
                                               bool markAttrs) {
    if (isAbstractGroup(group)) {
        EXPECT(data == 0 && "abstract group must have data=0");
    } else if (data == 0) {
        // Alias: data=0 for a normally-concrete group - allowed.
    } else {
        EXPECT(data != 0 && "concrete group must have data!=0");
    }

    EXPECT(!z.names.find(exportedKeyOf(name)).has_value() && "duplicate builtin type name");

    auto tc = (data != 0) ? TypeClass::kTrivial : TypeClass::kComplex;
    TypeKind kind = makeTypeKind(group, data, tc);
    kind = setBuiltinFlag(kind); // mark as builtin
    if (markAttrs) {
        kind = setAttrsFlag(kind); // признак «тип несёт атрибуты» (напр. политики sync)
    }
    TypeDescriptor desc{
        std::string(name), // name - владеющая копия (не висячий string_view)
        {},                // attrs (builtin have no attrs)
        {},                // sourceRange (builtin have no source)
        INVALID_TYPE_ID,   // baseType - builtin types are not aliases
        SimpleTypeData{}   // data - builtin types are simple
    };
    const TypeId id = z.store.push(kind, std::move(desc), exportedKeyOf(name));
    z.names.bind(exportedKeyOf(name), id, TypeNameIndex::KeyPool::kProcess);
    if (!cpp_name.empty()) {
        z.names.bind(exportedKeyOf(cpp_name), id, TypeNameIndex::KeyPool::kProcess);
    }
    // Целевые C++-факты встроенных интринсиков (C++-имя/include) — в компоненте `target`
    // (сид-таблица `target::builtinCppSeed()`, засевает `Pipeline`); дескриптор их не несёт.
    return id;
}

BuiltinTypeCore BuiltinCoreBuilder::build() {
    DiagnosticEngine diag;
    Options opts(diag);
    TypeStore store;
    TypeNameIndex names(diag, opts, store);
    BuiltinRegistry builtins(store);
    TypePrinter printer(store, builtins);
    TypeCodeCache codes;
    FunctionIdentity identity(store, printer, codes);
    MethodTable methods(store, identity);
    TypeReadModel read(store, names, printer, builtins, methods, codes);
    StructuralInterner interner(read, store, names, diag, opts);
    NamedTypeRegistrar registrar(store, names, diag, opts);

    BuiltinSeedZones zones{store, names, registrar, builtins, methods, interner, read};
    registerBuiltinTypes(zones);

    BuiltinTypeCore core;
    core.descriptors = store.takeDescriptors();
    core.name_to_id = names.takeUserMap();
    core.builtinCount = core.descriptors.size();
    // Таблица интернирования seed-структурных типов: инстансы продолжают её (иначе та же
    // структура получила бы второй TypeId, нарушая инвариант «один экземпляр на структуру»).
    core.structural = interner.takeStructuralTable();
    return core;
}

} // namespace trust
