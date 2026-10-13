// src/types/registry/type_name_index.cpp
// Зона имён типов: привязки ключей, встроенный домен, дубликат-диагностика.
#include "types/registry/type_name_index.hpp"

#include "diag/base_diags.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/report.hpp"
#include "types/registry/type_store.hpp"
#include "utils/error.hpp"

namespace trust {

TypeNameIndex::TypeNameIndex(DiagnosticEngine& diag, const Options& opts, const TypeStore& store)
: m_diag(diag)
, m_opts(opts)
, m_store(store) {
}

std::optional<TypeId> TypeNameIndex::find(const StorageKey& key) const noexcept {
    if (auto it = m_name_to_id.find(key); it != m_name_to_id.end()) {
        return it->second;
    }
    // Встроенные имена — отдельный домен (глобальные, без namespace): ищем по тексту ключа.
    if (m_builtin != nullptr && !key.isModuleQualified() && key.namespacePath.empty()) {
        if (auto it = m_builtin->name_to_id.find(exportedKeyOf(key.name)); it != m_builtin->name_to_id.end()) {
            return it->second;
        }
    }
    return std::nullopt;
}

std::optional<TypeId> TypeNameIndex::findExportByName(std::string_view name) const noexcept {
    // Текст: сначала site-видимый/встроенный домен, затем уникальное имя среди модульных
    // экспортов (скан без индекса). Неоднозначное имя текстом не резолвится.
    const StorageKey key = exportedKeyOf(name);
    if (auto id = find(key)) {
        return id;
    }
    std::optional<TypeId> found;
    for (const auto& [registered, id] : m_name_to_id) {
        if (registered.visibility != VisibilityClass::ModuleExport) {
            continue;
        }
        if (registered.namespacePath != key.namespacePath || registered.name != key.name) {
            continue;
        }
        if (found.has_value() && *found != id) {
            return std::nullopt;
        }
        found = id;
    }
    return found;
}

bool TypeNameIndex::isBuiltinTypeName(std::string_view name) const noexcept {
    return m_builtin != nullptr && m_builtin->name_to_id.find(exportedKeyOf(name)) != m_builtin->name_to_id.end();
}

bool TypeNameIndex::contains(const StorageKey& key) const noexcept {
    return m_name_to_id.find(key) != m_name_to_id.end();
}

StorageKey TypeNameIndex::registrationKey(const std::optional<StorageKey>& key, std::string_view name) const {
    // Единственный дефолт: ключ не задан ⇒ имя в экспортируемом домене (встроенные имена/тесты).
    return internStorageKey(key.value_or(exportedKeyOf(name)));
}

void TypeNameIndex::reportDuplicate(const StorageKey& key, std::string_view name, MapperRange sourceRange) const {
    if (const auto it = m_name_to_id.find(key); it != m_name_to_id.end()) {
        const MapperRange prevRange = m_store.getTypeSourceRange(it->second);
        if (!prevRange.isInvalid()) {
            diag::reportWithOptions(m_diag, m_opts, diag::DiagId::ParseError, prevRange, "previous definition of type '{}'", name);
        }
    }
    diag::reportWithOptions(m_diag, m_opts, diag::DiagId::ParseError, sourceRange, "duplicate type name '{}'", name);
}

bool TypeNameIndex::reserve(const StorageKey& key, std::string_view name, MapperRange sourceRange) {
    // Встроенные имена — отдельный домен: одноимённый пользовательский тип запрещён.
    if (m_name_to_id.find(key) != m_name_to_id.end() || isBuiltinTypeName(name)) {
        reportDuplicate(key, name, sourceRange);
        return false;
    }
    return true;
}

void TypeNameIndex::bind(const StorageKey& key, TypeId id, KeyPool pool) {
    const auto [it, inserted] = m_name_to_id.emplace(pool == KeyPool::kProcess ? internProcessStorageKey(key) : internStorageKey(key), id);
    (void)it;
    EXPECT(inserted && "TypeNameIndex::bind: key is already bound to a type");
}

void TypeNameIndex::aliasKey(TypeId id, const StorageKey& key) {
    if (key.name.empty()) {
        return;
    }
    // Ключ сохраняется в реестре — части интернируются (views обязаны пережить аргументы).
    const StorageKey stored = internStorageKey(key);
    const auto [it, inserted] = m_name_to_id.try_emplace(stored, id);
    if (inserted || it->second == id) {
        return;
    }
    // Плоская привязка занята другим типом — неоднозначность имени, не дубль объявления:
    // текстовая привязка снимается, выбор — ключом/квалификацией.
    if (isExportedVisibility(key.visibility)) {
        m_name_to_id.erase(it);
        return;
    }
    EXPECT(false && "aliasKey: module-qualified key already belongs to another type");
}

TypeId TypeNameIndex::aliasTypeName(TypeId target, const StorageKey& key, std::string_view name, MapperRange sourceRange) {
    EXPECT(target != INVALID_TYPE_ID && "aliasTypeName: target type id is invalid");
    EXPECT(!name.empty() && "aliasTypeName: alias name must not be empty");
    // Имя резервируется ДО вставки ключа: занятое имя — пользовательская диагностика, а не
    // EXPECT из aliasKey (дубль не должен доходить до вставки).
    if (!reserve(key, name, sourceRange)) {
        return INVALID_TYPE_ID;
    }
    aliasKey(target, key);
    return target;
}

void TypeNameIndex::forEachNamedType(const std::function<void(const StorageKey&, std::string_view, bool)>& cb) const {
    for (const auto& [key, id] : m_name_to_id) {
        emitNamedType(cb, key, id, true);
    }
    if (m_builtin != nullptr) {
        for (const auto& [key, id] : m_builtin->name_to_id) {
            emitNamedType(cb, key, id, false);
        }
    }
}

void TypeNameIndex::emitNamedType(const std::function<void(const StorageKey&, std::string_view, bool)>& cb, const StorageKey& key, TypeId id,
                                  bool userDefined) const {
    const TypeDescriptor* desc = m_store.descriptorOf(id);
    cb(key, desc != nullptr ? std::string_view(desc->name) : std::string_view{}, userDefined);
}

void TypeNameIndex::clearUser() {
    m_name_to_id.clear();
}

std::unordered_map<StorageKey, TypeId, StorageKeyHash> TypeNameIndex::takeUserMap() {
    std::unordered_map<StorageKey, TypeId, StorageKeyHash> out = std::move(m_name_to_id);
    m_name_to_id.clear();
    return out;
}

} // namespace trust
