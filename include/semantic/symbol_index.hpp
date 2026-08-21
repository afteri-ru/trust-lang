#pragma once

// include/semantic/symbol_index.hpp
// Результат сбора объявлений семантики для LSP-сервера: имя, разрешённый тип (TypeId),
// имя типа, диапазоны имени и скоупа объявления. Собирается SymbolCollectorHook
// (см. symbol_collector.hpp) в обходе NameResolutionPass.

#include "location/location.hpp"
#include "types/type_id.hpp"

#include <string>
#include <vector>

namespace trust {

/// Одна запись символа, собранная анализатором.
struct SymbolInfo {
    std::string name;       ///< Каноническое имя (как в таблице символов; может содержать $/%)
    TypeId type;            ///< Разрешённый тип (валиден: реестр живёт в CachedSource)
    std::string typeName;   ///< Каноническое имя типа (TypeRegistry::getFullTypeName)
    MapperRange nameRange;  ///< Диапазон ИМЕНИ в исходнике (go-to-definition / фильтр по позиции)
    MapperRange scopeRange; ///< Диапазон скоупа объявления (видимость локальных по позиции курсора)
    bool isMacro = false;   ///< true - имя макроса (добавляется отдельно, не из семантики)
    /// Имена полей словаря/кортежа из инициализатора-литерала `x := (a=1, b=2,)`.
    /// Для member-завершения `x.` (тип литерала - универсальный Dict, поля в нём не хранятся).
    std::vector<std::string> dictFields;
    /// Документирующий комментарий (`///`, `/** */`, `##`) перед объявлением (для hover/док).
    std::string documentation;
};

using SymbolIndex = std::vector<SymbolInfo>;

} // namespace trust
