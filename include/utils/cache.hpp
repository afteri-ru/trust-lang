#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trust {

// ══════════════════════════════════════════════════════════════
//  LruCache — LRU-кеш с O(1) операциями
// ══════════════════════════════════════════════════════════════
template <typename TKey, typename TValue>
class LruCache {
    struct Node {
        TKey key;
        TValue value;
    };

  public:
    explicit LruCache(int capacity)
    : m_capacity(capacity) {
        m_entries.reserve(capacity);
    }

    [[nodiscard]] const TValue* lookup(TKey key) {
        auto it = m_map.find(key);
        if (it == m_map.end())
            return nullptr;

        // Promotion: перемещаем элемент в конец (самый свежий)
        Node node = std::move(m_entries[it->second]);
        m_entries.erase(m_entries.begin() + it->second);
        m_entries.push_back(std::move(node));

        // Обновляем map: старые позиции сдвинулись
        // Нужно пересчитать индексы для *всех* элементов, начиная с 0,
        // т.к. erase сдвинул индексы элементов после удалённого.
        int new_idx = static_cast<int>(m_entries.size() - 1);
        it->second = new_idx;
        for (int i = 0; i <= new_idx; ++i)
            m_map[m_entries[i].key] = i;

        return &m_entries.back().value;
    }

    void insert(TKey key, TValue value) {
        // Если ключ уже существует — удаляем старую запись
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            m_entries.erase(m_entries.begin() + it->second);
            m_map.erase(it);
        }

        // Вытеснение самого старого (первого) элемента
        if (static_cast<int>(m_entries.size()) >= m_capacity) {
            m_map.erase(m_entries.front().key);
            m_entries.erase(m_entries.begin());
        }

        // Добавляем в конец
        int idx = static_cast<int>(m_entries.size());
        m_entries.push_back({key, std::move(value)});
        m_map[key] = idx;
    }

    [[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }
    [[nodiscard]] int size() const noexcept { return static_cast<int>(m_entries.size()); }
    [[nodiscard]] int capacity() const noexcept { return m_capacity; }

  private:
    int m_capacity;
    std::vector<Node> m_entries;
    std::unordered_map<TKey, int> m_map;
};

// ══════════════════════════════════════════════════════════════
//  SparseCache — разреженный кеш строк: бинарный поиск по
//  равномерно распределённым точкам (offset, line).
// ══════════════════════════════════════════════════════════════
class SparseCache {
  public:
    struct Entry {
        uint32_t offset; // 0-based offset первого символа строки
        uint32_t line;   // 1-based номер строки
    };

    SparseCache() = default;

    // Построить кеш по исходному тексту. Размер = max(2, sqrt(number_of_lines)).
    void build(std::string_view source);

    // Очистить кеш.
    void invalidate() noexcept { m_entries.clear(); }

    [[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }
    [[nodiscard]] const Entry* data() const noexcept { return m_entries.data(); }
    [[nodiscard]] size_t size() const noexcept { return m_entries.size(); }

    // Найти запись с максимальным offset <= target (0-based).
    // Возвращает nullptr если кеш пуст.
    [[nodiscard]] const Entry* find_by_offset(uint32_t target) const;

    // Найти запись с максимальным line <= target (1-based).
    // Возвращает nullptr если кеш пуст.
    [[nodiscard]] const Entry* find_by_line(uint32_t target) const;

  private:
    std::vector<Entry> m_entries;
};

} // namespace trust