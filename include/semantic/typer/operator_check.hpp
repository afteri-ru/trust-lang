#pragma once

// Валидация объявления ПЕРЕГРУЖАЕМОГО ОПЕРАТОРА (FuncDecl с m_isOperator): символ из реестра
// (types/operator_registry.hpp), допустимость member/free-формы, обязательный тип возврата,
// арность. Общая точка для пользовательских Record-типов, нативных Class-ов и свободных
// операторов верхнего уровня - правила не дублируются.

#include "types/type_read.hpp"
#include "ast/ast_nodes.hpp"
#include "session/context.hpp"
#include "types/type_id.hpp"
#include <string>

namespace trust {
class TypeSystem;
}

namespace trust::semantic {

/// Проверка объявления оператора. isMember - объявление внутри тела типа (Record/Class);
/// false - свободный оператор. Диагностика выдаётся здесь; false - объявление невалидно
/// (регистрацию вызывающий должен пропустить).
[[nodiscard]] bool validateOperatorDecl(Context& ctx, const FuncDecl& f, bool isMember);

/// Свободный оператор обязан иметь хотя бы один параметр пользовательского типа (record/native/
/// native-template): иначе объявление никогда не участвует в разрешении (built-in операторы -
/// интринсики) и было бы мёртвым.
[[nodiscard]] bool operatorHasUserOperand(const TypeReadModel& reg, TypeId signature);

} // namespace trust::semantic
