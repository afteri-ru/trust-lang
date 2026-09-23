#pragma once
// include/semantic/type_set.hpp
// Наборы допустимых типов (`:A + :B`, ветки с исключениями `-`): ПРОВЕРКА КОМБИНАЦИЙ.
// Выполняется ТОЛЬКО анализатором (нужны TypeId + иерархия типов).
//
// Представление узла: `Sequence` (kind=TypeSet); члены - в `m_body` (ArgNode):
// знак ('+'/'-') в `text()`, тип в `m_type`.
//
// Правила комбинаций (см. validateTypeSet):
//   * ветка начинается первым типом или типом после '+';
//   * '-' относится к последней ветке и допустим ТОЛЬКО для подтипа её начального типа;
//   * ошибки: ведущий '-'; дубль начального типа ветки; исключение чужого/более широкого типа;
//     составной тип-член (шаблон/кортеж/ссылка/... - «составное внутри составного»).

#include "types/type_id.hpp"

#include <optional>
#include <vector>

namespace trust {

class Sequence;
class AnalysisContext;
class TypeRegistry;

namespace semantic {

/// Ветка набора: начальный тип + исключаемые (через '-') подтипы.
struct TypeSetBranch {
    TypeId root{INVALID_TYPE_ID};
    std::vector<TypeId> excluded;
};

/// Разобранный набор типов.
struct TypeSetModel {
    std::vector<TypeSetBranch> branches;
};

/// Является ли `sub` подтипом `root`: равенство ИЛИ Record-класс (root ∈ baseClasses*(sub))
/// ИЛИ одна Group (напр. `:Integer` и `:Int8` - обе `kIntegers`). Отдельная функция - под unit-тест.
[[nodiscard]] bool isTypeSetSubtype(const TypeRegistry& reg, TypeId sub, TypeId root);

/// Проверить комбинацию набора. Возврат: модель при успехе; `nullopt` - ошибки выданы в диагностику.
[[nodiscard]] std::optional<TypeSetModel> validateTypeSet(const Sequence& node, AnalysisContext& actx);

} // namespace semantic
} // namespace trust
