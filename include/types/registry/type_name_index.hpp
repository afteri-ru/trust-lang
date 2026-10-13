#pragma once

// include/types/registry/type_name_index.hpp
// Зона «имена типов»: единственное хранилище привязки «storage-ключ → TypeId», домен встроенных
// имён и дубликат-диагностика.
//
// Владение: TypeNameIndex — единственный владелец привязок имён. Запись — ТОЛЬКО через приватные
// примитивы-друзья (TypeSystem/NamedTypeRegistrar/StructuralInterner). Публично доступно только чтение.
//
// Ключи ПОЛЬЗОВАТЕЛЬСКИХ имён интернируются в пул компиляции (internStorageKey), ключи
// ВСТРОЕННЫХ — в процессный пул (internProcessStorageKey): builtin-ядро живёт весь процесс.

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>

#include "location/location.hpp"
#include "types/registry/builtin_type_core.hpp"
#include "types/names/storage_key.hpp"
#include "types/type_id.hpp"

namespace trust {

class DiagnosticEngine;
class Options;
class TypeStore;

class TypeNameIndex {
  public:
    TypeNameIndex(DiagnosticEngine& diag, const Options& opts, const TypeStore& store);

    /// Поиск по КЛЮЧУ регистрации: пользовательские привязки, затем встроенный домен.
    [[nodiscard]] std::optional<TypeId> find(const StorageKey& key) const noexcept;
    /// Поиск по тексту в экспортируемом домене: плоская привязка, иначе УНИКАЛЬНОЕ имя среди
    /// модульных экспортов (неоднозначное текстом не резолвится).
    [[nodiscard]] std::optional<TypeId> findExportByName(std::string_view name) const noexcept;

    [[nodiscard]] bool isBuiltinTypeName(std::string_view name) const noexcept;

    [[nodiscard]] bool contains(const StorageKey& key) const noexcept;

    /// Итерация именованных типов (пользовательские ключи, затем встроенный домен ядра).
    /// Тип с несколькими ключами даёт вызов на КАЖДЫЙ ключ (дедупликация — на потребителе).
    void forEachNamedType(const std::function<void(const StorageKey& key, std::string_view displayName, bool userDefined)>& cb) const;

    // -- Примитивы записи имён: зона — единственный владелец привязок имён ------------------
    /// Плоская привязка сайта (`:A ::- T;`, алиас импорта): конфликт плоского ключа с другим
    /// типом снимает привязку (неоднозначность), module-qualified конфликт — нарушение
    /// инварианта (EXPECT).
    void aliasKey(TypeId id, const StorageKey& key);
    /// Второе имя существующего типа (`:A ::- T;`): резерв + привязка. Дубль — пользовательская
    /// диагностика, INVALID_TYPE_ID.
    TypeId aliasTypeName(TypeId target, const StorageKey& key, std::string_view name, MapperRange sourceRange);

  private:
    // -- Примитивы записи: доступны только друзьям-оркестраторам -----------------------------
    /// Домен встроенных имён (невладеющий указатель; задаёт TypeSystem из BuiltinTypeCore).
    void setBuiltinCore(const BuiltinTypeCore* core) noexcept { m_builtin = core; }

    /// Пул интернирования ключа: пользовательские имена — компиляция, builtin-ядро — процесс.
    enum class KeyPool : uint8_t { kCompilation, kProcess };
    /// Ключ регистрации: явный ключ, иначе имя в экспортируемом домене (единственный дефолт).
    [[nodiscard]] StorageKey registrationKey(const std::optional<StorageKey>& key, std::string_view name) const;

    /// Резерв имени под ключом: false — ключ занят (диагностика уже выдана).
    bool reserve(const StorageKey& key, std::string_view name, MapperRange sourceRange);
    /// Диагностика дубликата: `previous definition` (если ключ занят) + `duplicate type name`.
    void reportDuplicate(const StorageKey& key, std::string_view name, MapperRange sourceRange) const;

    /// Привязка имени (пул выбирает владелец домена). Вызывается ПОСЛЕ reserve.
    void bind(const StorageKey& key, TypeId id, KeyPool pool = KeyPool::kCompilation);

    void clearUser();
    /// Изъятие пользовательской таблицы (при построении BuiltinTypeCore).
    std::unordered_map<StorageKey, TypeId, StorageKeyHash> takeUserMap();

    /// Выдача одной записи forEachNamedType (display-имя из дескриптора).
    void emitNamedType(const std::function<void(const StorageKey&, std::string_view, bool)>& cb, const StorageKey& key, TypeId id, bool userDefined) const;

    friend class TypeSystem;
    friend class NamedTypeRegistrar;
    friend class StructuralInterner;
    friend class BuiltinCoreBuilder;

    DiagnosticEngine& m_diag;
    const Options& m_opts;
    const TypeStore& m_store;
    const BuiltinTypeCore* m_builtin = nullptr;
    std::unordered_map<StorageKey, TypeId, StorageKeyHash> m_name_to_id;
};

} // namespace trust
