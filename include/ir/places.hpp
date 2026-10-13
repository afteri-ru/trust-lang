#pragma once

// include/ir/places.hpp
// Стабильные идентификаторы мест (places) для flow-анализов.
//
// Контракт:
//   * PlaceId - числовой ключ места в конкретном анализе; НЕ зависит от представления
//     (ключ представления: узел объявления HIR). Состояние анализа
//     индексируется PlaceId, а не указателем на узел AST.
//   * PlaceTable<Key> - отображение ключа представления в PlaceId; порядок добавления
//     стабилен (индекс = порядок первого добавления). Дубликат возвращает существующий id.
//   * Таблица живёт столько же, сколько анализ; сериализация/межпроходное хранение -
//     ответственность потребителя.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace trust::flow {

/// Идентификатор места: плотный индекс в PlaceTable (0 - валидное первое место).
struct PlaceId {
    std::uint32_t value = 0;

    friend constexpr bool operator==(PlaceId, PlaceId) noexcept = default;
    /// Порядок — по плотному идентификатору (порядок регистрации в `PlaceTable`), что даёт
    /// детерминированную итерацию состояния анализа/верификатора.
    friend constexpr auto operator<=>(PlaceId, PlaceId) noexcept = default;
};

struct PlaceIdHash {
    std::size_t operator()(PlaceId id) const noexcept { return std::hash<std::uint32_t>{}(id.value); }
};

/// Плотная таблица «ключ представления -> PlaceId» с дедупликацией.
template <class Key>
class PlaceTable {
  public:
    /// Регистрирует ключ (идемпотентно). Возвращает стабильный PlaceId.
    PlaceId add(const Key& key) {
        const auto it = m_index.find(key);
        if (it != m_index.end()) {
            return it->second;
        }
        const PlaceId id{.value = static_cast<std::uint32_t>(m_keys.size())};
        m_keys.push_back(key);
        m_index.emplace(key, id);
        return id;
    }

    /// PlaceId ключа; nullopt - ключ не зарегистрирован.
    [[nodiscard]] std::optional<PlaceId> find(const Key& key) const {
        const auto it = m_index.find(key);
        if (it == m_index.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_keys.size(); }

  private:
    std::vector<Key> m_keys;
    std::unordered_map<Key, PlaceId, std::hash<Key>> m_index;
};

} // namespace trust::flow
