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

#include <map>
#include <set>
#include <string>
#include <vector>

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
    /// Предопределённые макросы (@__...__ и др.) - из реестра парсера.
    const std::vector<std::string>& predefMacros() const noexcept { return m_predefMacros; }
    /// Встроенные DSL-макросы (из trust/dsl.src).
    const std::set<std::string>& dslMacros() const noexcept { return m_dslMacros; }

  private:
    BuiltinCatalog();

    std::map<std::string, BuiltinTypeInfo> m_types;
    std::vector<std::string> m_predefMacros;
    std::set<std::string> m_dslMacros;
};

} // namespace trust
