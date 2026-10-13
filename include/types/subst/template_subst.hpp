#pragma once

// include/types/subst/template_subst.hpp
// Подстановка типовых параметров (engine) отдельно от хранилища типов TypeSystem.
// Симметрично types/call/template_unify.hpp: алгоритм - свободные функции над реестром,
// интернирование результата выполняет StructuralInterner (см. TypeWriteContext).
#include "types/call/binders.hpp"
#include "types/subst/write_context.hpp"
#include "types/type_id.hpp"

#include <span>
#include <utility>
#include <vector>

namespace trust {

/// Подстановка отображения param -> arg в произвольный тип (рекурсивно по структуре).
/// Identity-only: TypeId не несёт state; квалификаторы принадлежат QualType-владельцу.
TypeId substituteTypeParams(TypeWriteContext ctx, const std::vector<std::pair<TypeId, TypeId>>& mapping, TypeId type);

/// Подстановка TemplateBindings в одиночную типовую позицию (pack-параметр недопустим).
/// Identity-only (см. substituteTypeParams).
TypeId substituteTemplateParams(TypeWriteContext ctx, const TemplateBindings& bindings, TypeId type);

/// Эффективная сигнатура вызова: подстановка bindings + разворот типового пакета.
TypeId instantiateCallSignature(TypeWriteContext ctx, TypeId declaredSig, const TemplateBindings& bindings, std::span<const TypeId> argTypes);

/// Подстановка в список типовых аргументов (pack splice).
std::vector<TypeId> substituteTemplateArgList(TypeWriteContext ctx, const TemplateBindings& bindings, TypeId arg);

} // namespace trust
