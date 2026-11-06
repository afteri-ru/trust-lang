#pragma once

// База record-типа в КАНОНИЧЕСКОЙ модели типов: разрешённый TypeId + вариант наследования.
// Уровни/маркеры наследования — `types/visibility.hpp` (единый источник).
// `access` — единственный носитель доступа базы для инстанций шаблонов (у них нет AST-узла).

#include "types/type_id.hpp"
#include "types/visibility.hpp"

namespace trust {

struct BaseClass {
    TypeId type{INVALID_TYPE_ID};
    Visibility access{Visibility::Public};
};

} // namespace trust
