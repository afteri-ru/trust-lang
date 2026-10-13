#pragma once

// include/semantic/verify/verify_typed_ast.hpp
// Верификатор ТИПИЗИРОВАННОЙ AST: проверка, что семантика материализовала все факты,
// которые обязан читать кодоген (и которые не выводятся повторно по имени).
//
// Это внутренняя проверка компилятора, а НЕ пользовательская диагностика: нарушение —
// ICE-политика `reportInternalError`. Гейт запускается только на УСПЕШНОМ прогоне
// семантики (`!crashed && !hasErrors()`), поэтому частичный AST LSP сюда не попадает.
//
// Проверяемые инварианты (см. `semantic/MEMORY.md`):
//   1) `CallExpr::resolvedSignature`, если задан, — интернированный функциональный тип;
//   2) типы материализованы в позициях, которые кодоген читает БЕЗ пере-резолва и которые
//      семантика ОБЯЗАНА заполнять на успешном прогоне:
//      `VarDecl::inferredType` (кроме `x := _` и типизированного forward-объявления),
//      `Binary::m_typeCheckConst`/`m_overflowCheck` для соответствующих операторов,
//      `CallExpr::argCasts`/`resolvedTemplateArgs`.
//
// Намеренно НЕ проверяются здесь: `Literal::typeState`, `Binary::resultType`,
// `IdentName::valueType` (может быть не выведен на момент завершения семантики — отложен
// к flow-анализам/DAO), `IdentName::resolvedTypeId` (тип-именные позиции точечны; полноту
// контракта HIR проверяет `verifyHir`), `ArgNode::resultType`, `RefMakeExpr::m_resultType`,
// `DestructureDecl::m_sourceType` (заполняются в частных ветках/позже lowering'ом).

#include "types/type_read.hpp"
#include "ast/ast_nodes.hpp"

#include <optional>
#include <string>
#include <vector>

namespace trust {

class TypeSystem;

/// Проверить инварианты типизированной AST на дереве `roots`. nullopt — нарушений нет;
/// иначе — человекочитаемое описание ПЕРВОГО нарушения (для ICE-сообщения).
[[nodiscard]] std::optional<std::string> verifyTypedAst(const TypeReadModel& types, const std::vector<AstNodePtr>& roots);

} // namespace trust
