#pragma once

// include/types/registry/builtin_registry.hpp
// Зона «встроенные определения и рантайм-символы»: реализации предобъявленных builtin-типов
// (cppName + includes + libs), таблица рантайм-символов и доступ к общему BuiltinTypeCore.
//
// Seed-строитель ядра — зона BuiltinCoreBuilder: эта зона НЕ строит ядро, а владеет
// per-instance определениями и символами. Запись — только через API зоны.

#include "types/registry/builtin_type_core.hpp"
#include "types/runtime_symbols.hpp"
#include "types/type_id.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace trust {

class TypeReadModel;
class TypeStore;

class BuiltinRegistry {
  public:
    explicit BuiltinRegistry(const TypeStore& store) noexcept
    : m_store(store) {}

    /// Определение реализации предобъявленного встроенного типа по ИМЕНИ (резолв — чтение).
    /// Новый TypeId не создаётся; непредобъявленное имя — INVALID_TYPE_ID.
    TypeId define(const TypeReadModel& read, std::string_view name, std::string_view cppName, std::vector<std::string> preprocIncludes = {},
                  std::vector<std::string> libs = {});

    /// Определение встроенного типа (nullptr — не определено / не встроенный).
    [[nodiscard]] const BuiltinDefinition* getBuiltinDefinition(TypeId id) const noexcept;

    /// Регистрирует рантайм-символ (инвариант: не дублирует builtin-тип по cppName).
    void registerRuntimeSymbol(RuntimeSymbolId id);

    /// Встроенные рантайм-символы живут в общем ядре; per-instance — в зоне.
    [[nodiscard]] const std::vector<RuntimeSymbol>& runtimeSymbols() const noexcept;

    /// Заголовки include для типа: сигнатура → builtin-def → дескриптор (единый приоритет).
    [[nodiscard]] const std::vector<std::string>& preprocIncludesOf(TypeId id) const noexcept;
    [[nodiscard]] std::string_view preprocIncludeOf(TypeId id) const noexcept;
    /// Логические библиотеки (`@[link("name")]`) on-use: дескриптор → шаблон инстанциации →
    /// реализация провайдера (для тензоров).
    [[nodiscard]] std::vector<std::string> linkLibsOf(TypeId id) const;

    /// Очистка per-instance состояния (reset() реестра).
    void reset();

    /// Изъятие per-instance символов (при построении BuiltinTypeCore в seed-режиме).
    std::vector<RuntimeSymbol> takeRuntimeSymbols();

  private:
    /// Определение реализации предобъявленного встроенного типа (хранится на КАНОНИЧЕСКОМ id).
    void define(TypeId id, std::string_view cppName, std::vector<std::string> preprocIncludes, std::vector<std::string> libs);

    /// Общее иммутабельное ядро (владелец — TypeSystem); nullptr до setCore/в seed.
    void setCore(const BuiltinTypeCore* core) noexcept { m_core = core; }

    friend class TypeSystem;

    const TypeStore& m_store;
    const BuiltinTypeCore* m_core = nullptr;
    std::unordered_map<TypeId, BuiltinDefinition> m_definitions;
    std::vector<RuntimeSymbol> m_runtime;
};

} // namespace trust
