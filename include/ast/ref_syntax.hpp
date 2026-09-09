#pragma once

// include/ast/ref_syntax.hpp
// Единый разбор СИНТАКСИСА вида ссылки (RefType):
//   - атрибут `@[reftype(<kind>[, <policy>[, <timeout>]])]`;
//   - символический маркер в аннотации типа (`&&`/`&*`/`&?`).
// Все потребители (семантика, транспилятор, term_to_ast) обязаны извлекать вид ссылки
// только через эти функции: строки маркеров/имена видов заданы в types/typekind.hpp
// (X-macro refTypeFromString / refTypeFromTypeSigil), а не дублируются по месту.

#include "attrs/attr_pool.hpp"
#include "ast/token_base.hpp"
#include "types/typekind.hpp"

#include <optional>
#include <string>
#include <vector>

namespace trust {

/// Вид ссылки из аргументов атрибута `@[reftype(...)]` (первый аргумент = имя вида).
/// nullopt - аргументов нет либо вид не распознан.
[[nodiscard]] std::optional<RefType> refKindFromAttrArgs(const std::vector<std::string>* args);

/// Вид ссылки из атрибута `@[reftype(...)]` на узле-носителе атрибутов (переменная/тип).
/// nullopt - атрибута нет либо вид не распознан.
[[nodiscard]] std::optional<RefType> refKindOfAttr(const AttrPool& attrs, const AstNodeAttr& node);

/// Вид ссылки из СИМВОЛИЧЕСКОГО маркера в аннотации типа (узлы RefMakeExpr/RefTakeExpr,
/// текст `&&`/`&*`/`&?`). nullopt - узел не маркер или маркер не распознан.
[[nodiscard]] std::optional<RefType> refKindOfTypeNode(const AstNodeBase* typeNode);

/// Вид ссылки, заданный на ТИПОВОЙ стороне объявления: символический маркер (`x : &&Int32`) ИЛИ
/// атрибут `@[reftype(...)]` на узле типа (`x : @[reftype("shared")@] Int32`). Единая точка для
/// проверки «вид задан у типа» в семантике. nullopt - у типа вида нет.
[[nodiscard]] std::optional<RefType> refKindOfTypeSpec(const AstNodeBase* typeNode, const AttrPool& attrs);

} // namespace trust
