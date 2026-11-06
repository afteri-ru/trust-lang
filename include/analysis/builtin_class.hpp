#pragma once

// Общий контракт анализа для кодогена и семантики: КОГДА реализация builtin-типа
// (`Name = %cpp { ... }`) требует СГЕНЕРИРОВАННОГО C++-класса.
//
// Признак — данные узлов, а не политика потребителей: класс генерируется, если реализация
// объявляет БАЗИСЫ (`Name = %cpp, %base {...}`) либо метод с РЕАЛЬНЫМ телом
// (Trust/EMBED). Реализация только из нативных привязок `%member` (`Dict`, `Array`, `Range`,
// `Complex`, `Any` до миграции) классом не владеет — её C++-тип приходит из внешнего заголовка.
//
// Одна функция используется ДВУМЯ сторонами (иначе пути расходятся):
//  - семантика: владеющий модуль публикует интерфейс => include типа = заголовок этого модуля;
//  - кодоген: эмиссия класса (и out-of-line тел) в заголовок/`.cppt` модуля.

#include "ast/ast_nodes.hpp"
#include "ast/token_base.hpp"
#include "attrs/attr_pool.hpp"

namespace trust::analysis {

/// true — реализация builtin-типа описывает СВОЙ C++-класс (эмитится кодогеном).
inline bool builtinImplGeneratesCppClass(const ClassDecl& cls, const AttrPool& attrs) {
    if (!cls.m_baseTypes.empty() || !cls.m_nativeBaseCppNames.empty()) {
        return true;
    }
    for (const auto& member : cls.m_body) {
        if (!member || !member->is<FuncDecl>()) {
            continue;
        }
        const auto& fn = *member->as<FuncDecl>();
        if (!fn.bindsNativeCpp() && fn.hasBody()) {
            return true;
        }
    }
    return false;
}

} // namespace trust::analysis