#pragma once

// Единственный владелец текстов AST/HIR/символов (spelling токенов, macro expansions,
// predef values, semantic renames, joined docs). Исходный буфер остаётся в SourceMapWriter;
// AST интернирует spelling при конвертации (узлы-носители WithText/HasText); исходный текст сюда не копируется целиком.
// Строки append-only: адреса стабильны на всё время жизни интернера, поэтому
// std::string_view, выданный intern()/concat(), не инвалидируется. Это позволяет
// хранить в AST/HIR только view без собственного владения текстом.
// Две формы одной реализации (разная потоковая модель):
//   - `StringInterner`       - пул КОМПИЛЯЦИИ (per-Context), без блокировок: компиляция
//                              однопоточная (один поток на Context);
//   - `SharedStringInterner` - процессный `manualStringInterner()` (ручные/тестовые узлы вне
//                              компиляции; доступ только по явному `ManualInternerScope`),
//                              доступен из разных потоков -> mutex.
// Хранение: `std::deque<std::string>` (стабильные адреса, без per-string node-аллокации) +
// индекс `std::unordered_set<std::string_view>` по этим строкам.

#include <cstddef>
#include <deque>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>

#include "utils/error.hpp"
#include "utils/string_view_hash.hpp"

namespace trust {

namespace detail {

/// No-op блокировка для однопоточного пула (std::lock_guard совместим по BasicLockable).
struct NullMutex {
    void lock() noexcept {}
    void unlock() noexcept {}
};

/// Общая реализация пула строк; ThreadSafe выбирает наличие mutex.
template <bool ThreadSafe>
class StringInternerBase {
  public:
    /// Стабильный view. При повторном вызове возвращает тот же data().
    [[nodiscard]] std::string_view intern(std::string_view text) {
        const std::lock_guard<Mutex> lock(m_mutex);
        auto it = m_index.find(text);
        if (it != m_index.end()) {
            return *it;
        }
        m_storage.emplace_back(text);
        const std::string_view stored(m_storage.back());
        m_index.emplace(stored);
        return stored;
    }

    /// Склейка (док из нескольких комментариев, макро-строка) + интернирование.
    [[nodiscard]] std::string_view concat(std::initializer_list<std::string_view> parts) {
        std::size_t total = 0;
        for (std::string_view p : parts) {
            total += p.size();
        }
        std::string joined;
        joined.reserve(total);
        for (std::string_view p : parts) {
            joined.append(p);
        }
        return intern(joined);
    }

    [[nodiscard]] std::size_t size() const {
        const std::lock_guard<Mutex> lock(m_mutex);
        return m_index.size();
    }

    /// true - строка уже интернирована в этот пул (сравнение по содержимому).
    [[nodiscard]] bool owns(std::string_view text) const {
        const std::lock_guard<Mutex> lock(m_mutex);
        return m_index.find(text) != m_index.end();
    }

    /// Предварительный размер индекса (избежать серии rehash на первых сотнях spelling).
    void reserve(std::size_t count) {
        const std::lock_guard<Mutex> lock(m_mutex);
        m_index.reserve(count);
    }

  private:
    using Mutex = std::conditional_t<ThreadSafe, std::mutex, NullMutex>;

    mutable Mutex m_mutex;
    std::deque<std::string> m_storage;                         ///< Владелец байтов; адреса стабильны (deque end-insert).
    std::unordered_set<std::string_view, StringViewHash, std::equal_to<>> m_index; ///< Индекс по строкам m_storage.
};

} // namespace detail

/// Пул строк компиляции (per-Context). Контракт: один поток на Context (как весь Pipeline).
using StringInterner = detail::StringInternerBase<false>;

/// Потокобезопасный пул (процессный manualStringInterner): тесты/синтетика/playground.
using SharedStringInterner = detail::StringInternerBase<true>;

/// Процессный пул строк для интернирования ВНЕ компиляции (ручные/тестовые узлы без Context,
/// builtin-ключи, живущие весь процесс). Использовать напрямую можно только там, где владелец
/// факта — процесс; обычный путь — `internText()` под `ManualInternerScope`.
inline SharedStringInterner& manualStringInterner() {
    static SharedStringInterner interner;
    return interner;
}

namespace detail {
/// Текущий пул активной компиляции (thread-local): задаётся StringInternerScope.
inline StringInterner*& activeInternerSlot() noexcept {
    thread_local StringInterner* slot = nullptr;
    return slot;
}

/// Явное разрешение интернировать в процессный пул вне компиляции (ManualInternerScope).
inline bool& manualInternerSlot() noexcept {
    thread_local bool allowed = false;
    return allowed;
}
} // namespace detail

/// RAII-установка активного пула на время фазы компиляции (frontend/semantic/HIR/codegen
/// или прямой TermToAstConverter::termToAst). Восстанавливает предыдущий пул.
class StringInternerScope {
  public:
    explicit StringInternerScope(StringInterner& interner) noexcept
    : m_previous(detail::activeInternerSlot()) {
        detail::activeInternerSlot() = &interner;
    }

    ~StringInternerScope() { detail::activeInternerSlot() = m_previous; }

    StringInternerScope(const StringInternerScope&) = delete;
    StringInternerScope& operator=(const StringInternerScope&) = delete;

  private:
    StringInterner* m_previous;
};

/// RAII-разрешение интернировать в процессный пул ВНЕ компиляции (ручные/тестовые узлы,
/// синтетика без Context). Отсутствие любого scope — нарушение контракта: `internText()`
/// не имеет молчаливого процессного fallback (E9/R1).
class ManualInternerScope {
  public:
    ManualInternerScope() noexcept
    : m_previous(detail::manualInternerSlot()) {
        detail::manualInternerSlot() = true;
    }

    ~ManualInternerScope() { detail::manualInternerSlot() = m_previous; }

    ManualInternerScope(const ManualInternerScope&) = delete;
    ManualInternerScope& operator=(const ManualInternerScope&) = delete;

  private:
    bool m_previous;
};

/// Интернирование в активный пул компиляции (StringInternerScope). Вне компиляции процессный
/// пул доступен только по явному ManualInternerScope; иначе — EXPECT (не silent fallback).
[[nodiscard]] inline std::string_view internText(std::string_view text) {
    if (StringInterner* active = detail::activeInternerSlot()) {
        return active->intern(text);
    }
    EXPECT(detail::manualInternerSlot() &&
           "internText(): no active StringInternerScope; out-of-compilation interning requires an explicit ManualInternerScope");
    return manualStringInterner().intern(text);
}

/// Каноническое значение для слота интернированного текста AST: пустое написание НЕ
/// интернируется (пустой view), непустое — интернируется в активный пул. Единственная
/// реализация правила «empty -> {}», общая для `HasText` и `AstNodeBase::documentation`.
[[nodiscard]] inline std::string_view internedTextSlot(std::string_view text) {
    return text.empty() ? std::string_view{} : internText(text);
}

} // namespace trust
