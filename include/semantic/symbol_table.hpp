#pragma once

// include/semantic/symbol_table.hpp
// Единая таблица символов: стек вложенных лексических скоупов.
// Объединяет бывшие SymbolTable + ScopeStack в одну структуру, которая служит
// одновременно и «таблицей символов», и «стеком скоупов» для разрешения имён.
//
// Каждый вложенный скоуп (модуль, блок, функция, будущий метод класса) открывает
// уровень со своим набором имён, хранящимся в std::map (детерминированный порядок
// обхода — стабильные диагностики). Глобальный скоуп (уровень 0) всегда присутствует
// и является плоской таблицей глобальных/статических имён.
//
// Разрешение имён: lookup() ищет в пределах ОДНОГО скоупа, resolve() — вверх по
// стеку (учитывает вложенность и shadowing). Владение символами — у SymbolTable;
// указатели на Symbol, возвращаемые lookup()/resolve(), невладеющие и валидны, пока
// соответствующий скоуп не удалён (pop) или таблица не пересоздана.
//
// Каждый скоуп хранит невладеющую ссылку (const AstNodeBase* creator) на узел AST,
// который этот скоуп открыл (nullptr для глобального) — для диагностик и хуков.

#include "location/location.hpp"
#include "ast/token.hpp"
#include "types/type_id.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

class AstNodeBase;

// ── Symbol ─────────────────────────────────────────────────

/// Результат регистрации символа с поддержкой завершения forward-объявления.
enum class DeclResult {
    Inserted,  ///< Новое имя вставлено в текущий скоуп.
    Completed, ///< Существующее forward-объявление завершено определением (символ обновлён).
    Duplicate, ///< Имя уже занято: два определения / два forward / конфликт kinds (func vs var).
};

/// Месторасположение переменной (физическая память) — как принято в компиляторах.
/// Фиксируется анализатором имён при создании переменной.
enum class Storage {
    Global,      ///< глобальный сегмент данных (модуль/глобальный скоуп)
    Local,       ///< стек (функция/блок)
    Static,      ///< статические данные области имён (имя содержит '::')
    ThreadLocal, ///< TLS-сегмент (атрибут @[thread_local])
};

/// Symbol entry stored in the symbol table.
/// Узел объявления (decl) — источник истины: kind, range, атрибуты и определение (init/body).
/// TypeId type — общий для всех имён: для переменной это тип значения,
/// для функции — FunctionTypeId (сигнатура).
struct Symbol {
    std::string name;                                           ///< Canonical name (ключ в скоупе)
    TypeId type;                                                ///< Resolved type id (INVALID_TYPE_ID if not yet known).
                                                                ///< Может нести бит kInferredFlag («тип ВЫВЕДЕН автоматически») — см. types/MEMORY.md.
    AstNodeBase* decl;                                          ///< Узел объявления (VarDecl/FuncDecl/TypeDecl/ArgNode); невладеющая ссылка.
                                                                ///< Не-const: анализатор пишет финальный тип в узел (VarDecl::inferredType);
                                                                ///< хук-анализаторы и диагностика читают узел как const.
    Storage storage = Storage::Global;                          ///< Месторасположение переменной (физическая память).
    int64_t dims = -1;                                          ///< Размерность (для словаря — число элементов) как компиляционное
                                                                ///< свойство переменной. -1 = неизвестно (absent); >= 0 = static.
                                                                ///< Копируется из типа/инициализатора; используется для статической
                                                                ///< проверки индекса `d.1`. В общем случае — единое свойство Dims
                                                                ///< (см. архитектуру).
    std::vector<std::pair<std::string, TypeId>> dictFieldTypes; ///< Типы полей словаря
                                                                ///< (имя → TypeId; пустое имя = позиционный элемент). Заполняется из
                                                                ///< литерала-инициализатора (иммутабельный словарь) и используется
                                                                ///< для вывода типа поля `d.two`/`d.1`/`d[0]` (см. вывод типа поля).
};

/// SymbolTable — стек вложенных скоупов (единая таблица символов).
class SymbolTable {
  public:
    /// Один лексический скоуп.
    struct Scope {
        const AstNodeBase* creator = nullptr;  ///< Узел AST, открывший скоуп (nullptr = глобальный).
        std::map<std::string, Symbol> symbols; ///< Имена в пределах этого скоупа.

        /// Поиск в пределах одного скоупа. nullptr — не найдено.
        const Symbol* lookup(std::string_view name) const;
    };

    /// Стек всегда содержит глобальный скоуп (depth() >= 1).
    SymbolTable() { m_scopes.emplace_back(); }

    /// Вход во вложенный скоуп; creator — узел, открывший скоуп (nullptr = глобальный).
    void push(const AstNodeBase* creator = nullptr);

    /// Выход из вложенного скоупа. Глобальный скоуп не удаляется.
    void pop();

    /// Глубина стека (>= 1).
    std::size_t depth() const { return m_scopes.size(); }

    /// Текущий (верхний) скоуп.
    Scope& current() { return m_scopes.back(); }
    const Scope& current() const { return m_scopes.back(); }

    /// Узел, открывший текущий скоуп (nullptr — глобальный).
    const AstNodeBase* currentCreator() const { return m_scopes.back().creator; }

    /// Регистрирует символ в текущем скоупе. false — имя уже объявлено в этом скоупе
    /// (дубликат). Диагностику формирует вызывающий (ядро), т.к. ему нужен range.
    bool declare(const Symbol& sym);

    /// Классифицирует символ как предварительное (forward) объявление: узел объявления
    /// без определения — переменная без инициализатора (VarDecl.m_initializer == nullptr)
    /// или функция без тела (FuncDecl.m_body == nullopt). TypeDecl — всегда определение.
    static bool isForwardDecl(const Symbol& sym);

    /// Регистрирует символ в текущем скоупе с поддержкой завершения forward-объявления.
    /// Возвращает DeclResult: Inserted (новое имя), Completed (существующее forward-объявление
    /// того же kind завершено определением — символ обновлён in-place) или Duplicate.
    /// Завершение допустимо ТОЛЬКО когда existing — forward (isForwardDecl) и новый символ —
    /// определение, при совпадении kind (variant index). Два forward, два определения, forward
    /// функции + определение переменной и т.п. → Duplicate.
    DeclResult declareOrComplete(Symbol& sym);

    /// Поиск имени от текущего скоупа вверх (учитывает вложенность и shadowing).
    const Symbol* resolve(std::string_view name) const;

    /// Не-const вариант resolve() — для on-the-fly расширения выводимого типа
    /// (join по истории присвоений) анализатором типов выражений.
    Symbol* resolveMutable(std::string_view name);

    /// Плоская таблица глобальных имён (уровень 0).
    const Scope& global() const { return m_scopes.front(); }
    std::size_t globalSize() const { return m_scopes.front().symbols.size(); }

    /// Итерация скоупов от текущего (внутреннего) к глобальному. callback(const Scope&).
    /// Используется анализатором для вывода контекста (область имён/функция) из создателей
    /// скоупов вместо параллельного состояния.
    template <typename F>
    void forEachScope(F&& f) const {
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            f(*it);
        }
    }

  private:
    std::vector<Scope> m_scopes;
};

} // namespace trust
