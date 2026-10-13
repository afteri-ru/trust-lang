// include/types/type_read.hpp
// READ-ONLY view зон типовой системы: запрос одной зоны идёт через её accessor; на модели —
// именной фасад (`getType` с FAULT-политикой, `findType`, `forEachType`) и `identity()`.
// Состояния не имеет; запись — у зон-владельцев.
#pragma once

#include "types/func/fn_identity.hpp"
#include "types/registry/builtin_registry.hpp"
#include "types/registry/method_table.hpp"
#include "types/registry/type_code_cache.hpp"
#include "types/registry/type_name_index.hpp"
#include "types/registry/type_printer.hpp"
#include "types/registry/type_store.hpp"
#include "types/type_id.hpp"
#include "utils/error.hpp"

#include <functional>
#include <optional>
#include <string_view>

namespace trust {

class TypeSystem;

class TypeReadModel {
  public:
    TypeReadModel(const TypeStore& store, const TypeNameIndex& names, const TypePrinter& printer, const BuiltinRegistry& builtins, const MethodTable& methods,
                  TypeCodeCache& codes) noexcept
    : m_store(store)
    , m_names(names)
    , m_printer(printer)
    , m_builtins(builtins)
    , m_methods(methods)
    , m_codes(codes) {}

    TypeReadModel(const TypeSystem& registry) noexcept;

    /// Кеш кодов — memo производных данных того же identity.
    [[nodiscard]] FunctionIdentity identity() const noexcept { return FunctionIdentity(m_store, m_printer, m_codes); }

    [[nodiscard]] const TypeStore& store() const noexcept { return m_store; }
    [[nodiscard]] const TypeNameIndex& names() const noexcept { return m_names; }
    [[nodiscard]] const TypePrinter& printer() const noexcept { return m_printer; }
    [[nodiscard]] const BuiltinRegistry& builtins() const noexcept { return m_builtins; }
    [[nodiscard]] const MethodTable& methods() const noexcept { return m_methods; }

    /// Неизвестное имя — FAULT: fallback-типа не существует.
    [[nodiscard]] TypeId getType(std::string_view name) const {
        if (auto id = m_names.findExportByName(name)) {
            return *id;
        }
        FAULT("type '{}' not found", name);
    }
    [[nodiscard]] std::optional<TypeId> findType(const StorageKey& key) const noexcept { return m_names.find(key); }
    [[nodiscard]] std::optional<TypeId> findType(std::string_view name) const noexcept { return m_names.findExportByName(name); }
    void forEachType(const std::function<void(const StorageKey&, std::string_view, bool)>& cb) const { m_names.forEachNamedType(cb); }

  private:
    const TypeStore& m_store;
    const TypeNameIndex& m_names;
    const TypePrinter& m_printer;
    const BuiltinRegistry& m_builtins;
    const MethodTable& m_methods;
    TypeCodeCache& m_codes;
};

} // namespace trust
