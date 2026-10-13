#pragma once

// include/types/registry/type_code_cache.hpp
// Кеш канонических кодов типов: ключ — (unqualified structural TypeId, отношение identity).
// Коды binder-free типов не зависят от контекста объявления, поэтому binders в ключ не входят.
// Владелец — композиционный корень (per-instance, очищается reset()); читают FunctionIdentity.

#include "types/registry/type_identity_scope.hpp"
#include "types/type_id.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>

namespace trust {

class TypeCodeCache {
  public:
    /// Код типа в заданном отношении; nullptr — не закеширован.
    [[nodiscard]] const std::string* find(TypeId type, TypeIdentityScope scope) const noexcept {
        const auto it = m_codes.find(Key{getUnqualifiedType(type), scope});
        return it == m_codes.end() ? nullptr : &it->second;
    }

    /// Сохраняет код и возвращает ссылку на сохранённую копию (владелец — кеш).
    const std::string& store(TypeId type, TypeIdentityScope scope, std::string code) {
        const auto [it, inserted] = m_codes.emplace(Key{getUnqualifiedType(type), scope}, std::move(code));
        (void)inserted;
        return it->second;
    }

    void clear() noexcept { m_codes.clear(); }

  private:
    struct Key {
        TypeId type;
        TypeIdentityScope scope;

        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        [[nodiscard]] std::size_t operator()(const Key& key) const noexcept {
            return std::hash<TypeId>{}(key.type) ^ (static_cast<std::size_t>(key.scope) * 0x9e3779b9u);
        }
    };

    std::unordered_map<Key, std::string, KeyHash> m_codes;
};

} // namespace trust
