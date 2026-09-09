#pragma once

// include/semantic/operator_check.hpp
// Валидация объявления ПЕРЕГРУЖАЕМОГО ОПЕРАТОРА (FuncDecl с m_isOperator): символ из реестра
// (types/operator_registry.hpp), допустимость member/free-формы, обязательный тип возврата,
// арность. Общая точка для пользовательских Record-типов, нативных Class-ов и свободных
// операторов верхнего уровня - правила не дублируются.

#include "ast/ast_nodes.hpp"
#include "session/context.hpp"

namespace trust::semantic {

/// Проверка объявления оператора. isMember - объявление внутри тела типа (Record/Class);
/// false - свободный оператор. Диагностика выдаётся здесь; false - объявление невалидно
/// (регистрацию вызывающий должен пропустить).
[[nodiscard]] bool validateOperatorDecl(Context& ctx, const FuncDecl& f, bool isMember);

} // namespace trust::semantic
