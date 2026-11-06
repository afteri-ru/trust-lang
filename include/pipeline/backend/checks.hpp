// Пост-проверки AST в конвейере (признак обработки атрибутов).
#pragma once

#include "ast/ast_nodes.hpp"
#include "ir/hir_module.hpp"

#include <vector>

namespace trust {

struct RunServices;

/// Проверяет, что у каждого атрибута установлен признак обработки анализатором или
/// кодогенератором; иначе выдаёт -Wunhandled-attr на severity из опций.
void reportUnhandledAttributes(const RunServices& services, const std::vector<AstNodePtr>& roots);

/// То же по HIR (единственный вход кодогенерации: признаки обработки ставятся на его узлы).
void reportUnhandledAttributes(const RunServices& services, const HirModule& hir);

} // namespace trust
