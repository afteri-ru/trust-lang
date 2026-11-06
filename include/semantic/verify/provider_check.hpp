#pragma once

// Семантический гейт реализаций builtin-типов: ДО HIR/кодогена проверяет, что каждый
// использованный builtin-тип без собственной C++-репрезентации (интринсик) имеет загруженную
// реализацию (`=`-определение из `.src`). Имена stdlib-модулей компилятору НЕ известны,
// поэтому диагностика их не называет. Ошибки — обычная пользовательская диагностика (Error),
// поэтому после них pipeline не переходит к кодогенерации.

#include "location/location.hpp"
#include "ast/ast_nodes.hpp"
#include "types/type_read.hpp"

#include <vector>

namespace trust {

class Context;
class AstNodeBase;

/// Проверить provider-контракт на типизированной AST.
/// Возвращает true, если ошибок нет; иначе диагностики уже выданы.
[[nodiscard]] bool verifyBuiltinProviders(Context& ctx, const TypeReadModel& types, const std::vector<AstNodePtr>& roots);

} // namespace trust