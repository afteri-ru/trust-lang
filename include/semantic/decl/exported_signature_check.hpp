#pragma once

// include/semantic/decl/exported_signature_check.hpp
// Видимость типов в ЭКСПОРТИРУЕМЫХ объявлениях модуля: file-private тип в сигнатуре/полях/
// базах/enum-variant/алиасе экспортируемого объявления делает C++-имя недоступным в TU
// импортёра (private эмитится в anonymous namespace) - это ошибка компиляции, а не warning.

#include "location/location.hpp"
#include "types/type_id.hpp"

#include <string_view>

namespace trust {

class AnalysisContext;

namespace semantic {

/// Истина, если текущее место анализа - ЭКСПОРТИРУЕМОЕ top-level объявление модуля:
/// модульный корневой скоуп, не `__::` (private), не внутри функции и не внутри тела типа.
[[nodiscard]] bool isExportedTopLevel(const AnalysisContext& actx);

/// Транзитивный обход графа типа (`forEachTypeChild`) с ошибкой на file-private тип.
/// @param root корень проверяемого типа (INVALID - no-op)
/// @param range позиция объявления (диагностика)
/// @param entity что объявляется (`"function"`, `"variable"`, `"type"`, `"field"`...)
void checkExportedTypeVisibility(AnalysisContext& actx, TypeId root, MapperRange range, std::string_view entity);

} // namespace semantic
} // namespace trust
