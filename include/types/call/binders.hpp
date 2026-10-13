#pragma once

// include/types/call/binders.hpp
// Лёгкий value-тип binder-контекста сигнатуры. Вынесен отдельно, чтобы `type_system.hpp` и
// `symbols/symbol_table.hpp` могли принимать binder-контекст БЕЗ зависимости от тяжёлого
// `fn_identity.hpp` (который включает registry). `FunctionIdentity` использует эти же типы.

#include "types/type_id.hpp"

#include <string>
#include <vector>

namespace trust {

/// Один шаблонный binder объявления (позиция = индекс в списке).
/// Пак — отдельный вид binder'а: `T` и `T...` никогда не канонизируются одинаково.
struct TemplateBinder {
    std::string name;   ///< имя типового параметра в объявлении
    bool isPack{false}; ///< объявлен как `<T...>`

    friend bool operator==(const TemplateBinder&, const TemplateBinder&) = default;
};

using TemplateBinders = std::vector<TemplateBinder>;

/// Привязка типового параметра к конкретным типам (результат унификации).
/// Инвариант: non-pack — types.size() == 1; pack — произвольный набор.
struct TemplateBinding {
    TypeId param{INVALID_TYPE_ID}; ///< TemplateParam TypeId
    bool isPack{false};            ///< пакетный параметр
    std::vector<TypeId> types;     ///< конкретные типы (non-pack: ровно 1)
};

using TemplateBindings = std::vector<TemplateBinding>;

} // namespace trust
