#pragma once

// include/lsp/builtin_catalog.h
// Глобальный read-only каталог встроенных имён для автодополнения LSP.
//
// Единый источник встроенных имён (типы, методы, предопределённые и DSL-макросы)
// для всех открытых документов. Строится ОДИН раз (лениво, thread-safe) из общего
// иммутабельного ядра TypeRegistry::builtinCore() + реестра предопределённых
// макросов парсера + встроенного DSL. В отличие от прежнего пер-файлового
// `typeSnapshot`, каталог НЕ копируется в CachedSource и не дублирует общее ядро.
//
// Пользовательские имена (переменные/функции/типы/макросы кода) живут в таблице
// анализатора (SymbolIndex). Каталог дополняет её только встроенными именами,
// которых в таблице может и не быть (если они не встречаются в коде).

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "diag/context.hpp"

namespace trust {

/// Информация о встроенном типе в каталоге (только встроенные, иммутабельные).
struct BuiltinTypeInfo {
    /// true для пользовательских типов. В каталоге всегда false (только встроенные);
    /// поле сохранено для единообразия с представлением пользовательских типов.
    bool userDefined = false;
    /// Методы типа: имя (может начинаться с '%' у нативных) → true, если это функция.
    std::map<std::string, bool> methods;
};

/// Глобальный каталог встроенных имён (см. комментарий к файлу).
class BuiltinCatalog {
  public:
    /// Единственный экземпляр (ленивый, thread-safe magic static).
    static const BuiltinCatalog& instance();

    /// Встроенные типы и их методы (ключ - каноническое trust-имя типа).
    const std::map<std::string, BuiltinTypeInfo>& types() const noexcept { return m_types; }
    /// Предопределённые макросы (@__...__ и др.) - из реестра парсера. Имена (с '@'),
    /// для автодополнения. Доки см. macroDocs().
    const std::vector<std::string>& predefMacros() const noexcept { return m_predefMacros; }
    /// Встроенные DSL-макросы (из trust/dsl.src). Имена (первый терм группы, без '@'),
    /// для автодополнения. Доки см. macroDocs().
    const std::set<std::string>& dslMacros() const noexcept { return m_dslMacros; }
    /// ЕДИНСТВЕННЫЙ источник доков макросов (предdef + DSL + переопределения прагмой).
    /// Возвращается ССЫЛКА на глобальное хранилище Context::macroDocs() - БЕЗ копии:
    /// ключ = первый терм макроса без '@'. Прозрачный компаратор std::less<> - поиск по string_view.
    const std::map<std::string, std::string, std::less<>>& macroDocs() const noexcept { return Context::macroDocs(); }
    /// Док макроса по ключу - читается и с ведущим '@', и без (нормализуется). nullptr если нет.
    const std::string* macroDoc(std::string_view name) const noexcept { return Context::macroDoc(name); }

  private:
    BuiltinCatalog();

    std::map<std::string, BuiltinTypeInfo> m_types;
    std::vector<std::string> m_predefMacros; ///< Имена предdef-макросов (с '@'), для автодополнения
    std::set<std::string> m_dslMacros;       ///< Имена DSL-макросов (первый терм группы), для автодополнения
};

} // namespace trust
