#pragma once

// include/symbols/table/scope_stack.hpp
// Стек лексических скоупов таблицы символов: единственный контейнер объявлений имени и
// связанных с ними локальных фактов скоупа (using-области, namespace-алиасы, неоднозначности).
// Порядок обхода имён — по StorageKey (std::map), он наблюдаем: от него зависит порядок
// диагностик переноса namespace-блока.

#include "symbols/table/symbol.hpp"
#include "location/location.hpp"
#include "location/module_index.hpp"
#include "types/names.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <unordered_map>
#include <vector>

namespace trust {

class AstNodeBase;

/// Один конфликт плоского имени: узлы-кандидаты (по ним резолв строит объединённый набор
/// перегрузок) и модули-кандидаты (диагностика на сайте использования). Список модулей НЕ
/// выводится из узлов полностью: модуль импортёра-инициатора конфликта объявления не имеет.
struct AmbiguousBinding {
    std::vector<const AstNodeBase*> decls;
    std::vector<ModuleIndex> modules;
};

/// Роль скоупа в дереве. `ModuleRoot` — уровень идентичности модуля (объявления
/// регистрируются под module-qualified ключом). Без `ModuleNode` (прелюдия/тесты) им
/// становится первый вложенный скоуп.
enum class ScopeKind : std::uint8_t { Global, ModuleRoot, Block };

/// Один лексический скоуп: объявления имени, неоднозначности и ИНДЕКСЫ своих записей в
/// AliasRegistry (using-области и namespace-алиасы хранятся один раз — в реестре).
struct Scope {
    static constexpr std::size_t kNoParent = static_cast<std::size_t>(-1);

    ScopeKind kind = ScopeKind::Block;    ///< Роль скоупа (см. ScopeKind).
    const AstNodeBase* creator = nullptr; ///< Узел AST, открывший скоуп (nullptr = глобальный).
    /// Индекс родительского узла в ScopeStack (`kNoParent` — глобальный скоуп).
    std::size_t parent = kNoParent;
    /// Скоуп покинут курсором; объявления и локальные факты продолжают жить (persistent-модель).
    bool closed = false;
    std::map<StorageKey, Symbol> symbols; ///< Ключи имён (см. types/storage_key.hpp).
    /// Индексы using-областей своего скоупа в AliasRegistry (порядок регистрации сохраняется).
    std::vector<std::size_t> usingRefs;
    /// Индексы namespace-алиасов своего скоупа в AliasRegistry.
    std::vector<std::size_t> aliasRefs;
    /// Имена, привязка которых НЕОДНОЗНАЧНА (плоский ключ занят объявлениями разных модулей
    /// либо импортом и локальным объявлением): ОДНА запись конфликта на имя. Хранятся ТОЛЬКО
    /// конфликтующие имена (не индекс по всем экспортам).
    std::unordered_map<StorageKey, AmbiguousBinding, StorageKeyHash> ambiguous;

    /// Поиск в пределах одного скоупа. nullptr - не найдено.
    [[nodiscard]] const Symbol* lookup(const StorageKey& key) const;
};

/// Persistent-дерево лексических скоупов с курсором активного пути. Глобальный скоуп
/// (индекс 0) присутствует всегда.
///
/// `pop()` НЕ удаляет узел: помечает его `closed` и снимает с активного пути. Объявления
/// остаются живыми (адреса `Symbol` стабильны: `deque` для узлов + `std::map` для элементов),
/// поэтому таблица локальных символов доступна после прогона. Индексы `0..depth()-1` —
/// позиции в АКТИВНОМ пути.
class ScopeStack {
  public:
    ScopeStack();

    void push(const AstNodeBase* creator = nullptr, ScopeKind kind = ScopeKind::Block);
    void pop();

    [[nodiscard]] std::size_t depth() const noexcept { return m_activePath.size(); }
    [[nodiscard]] Scope& current() noexcept { return node(m_activePath.back()); }
    [[nodiscard]] const Scope& current() const noexcept { return node(m_activePath.back()); }
    [[nodiscard]] Scope& at(std::size_t index) noexcept { return node(m_activePath[index]); }
    [[nodiscard]] const Scope& at(std::size_t index) const noexcept { return node(m_activePath[index]); }
    [[nodiscard]] Scope& operator[](std::size_t index) noexcept { return at(index); }
    [[nodiscard]] const Scope& operator[](std::size_t index) const noexcept { return at(index); }
    [[nodiscard]] Scope& front() noexcept { return node(0); }
    [[nodiscard]] const Scope& front() const noexcept { return node(0); }
    /// Узел, открывший текущий скоуп (nullptr - глобальный).
    [[nodiscard]] const AstNodeBase* currentCreator() const noexcept { return current().creator; }

    // -- Persistent-доступ: все узлы дерева (включая закрытые) ------------------------------
    /// Число узлов дерева (закрытые входят).
    [[nodiscard]] std::size_t size() const noexcept { return m_allScopes.size(); }
    [[nodiscard]] Scope& node(std::size_t index) noexcept { return m_allScopes[index]; }
    [[nodiscard]] const Scope& node(std::size_t index) const noexcept { return m_allScopes[index]; }
    /// Индекс узла под курсором (в дереве, не в активном пути).
    [[nodiscard]] std::size_t currentIndex() const noexcept { return m_activePath.back(); }
    /// Индексы узлов активного пути от глобального к текущему.
    [[nodiscard]] const std::vector<std::size_t>& activePath() const noexcept { return m_activePath; }

    /// Итерация от текущего (внутреннего) к глобальному. callback(const Scope&).
    template <typename F>
    void forEachFromInner(F&& f) const {
        for (auto it = m_activePath.rbegin(); it != m_activePath.rend(); ++it) {
            f(node(*it));
        }
    }

  private:
    std::deque<Scope> m_allScopes;         ///< Узлы дерева; адреса стабильны (deque).
    std::vector<std::size_t> m_activePath; ///< Индексы узлов: глобальный → текущий.
};

} // namespace trust
