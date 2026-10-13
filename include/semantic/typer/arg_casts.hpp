#pragma once

// include/semantic/typer/arg_casts.hpp
// Материализация целей НЕЯВНЫХ конверсий аргументов: решение принимает семантика
// (по УЖЕ выбранной сигнатуре), кодоген только печатает. Устраняет повторный вывод
// приведения из FunctionTypeData в эмиттерах (semantic ↔ codegen не расходятся).

#include "ast/ast_nodes.hpp"

#include <vector>

namespace trust {

class Context;

/// Заполняет `CallExpr::argCasts` (порядок `boundArgs`/`m_args`) и `Binary::argCasts`
/// (member - правый операнд, free - левый+правый) по выбранным сигнатурам
/// (`resolvedSignature`/`resolvedOperatorSignature`). INVALID_TYPE_ID в слоте = без приведения
/// (Any/INVALID-параметр); для Binary тип параметра записывается всегда (кодоген приводит).
/// Контракт: эмиттеры печатают приведение по этим слотам и не выводят его из сигнатуры.
void materializeArgCasts(std::vector<AstNodePtr>& roots, Context& ctx);

} // namespace trust
