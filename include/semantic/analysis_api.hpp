// Umbrella публичного API семантического анализа для оркестратора (`pipeline`): драйвер проходов,
// поведенческие режимы и flow-анализы по MIR. Детали реализации — в internal-подкаталогах
// `semantic/{core,ref,typer,decl,verify}`; включать их снаружи компонента запрещено.
#pragma once

#include "semantic/core/behavioral_modes.hpp"
#include "semantic/core/pass_runner.hpp"
#include "semantic/ref/borrow_flow.hpp"
#include "semantic/ref/definite_assignment.hpp"
#include "semantic/ref/stack_check.hpp"
#include "semantic/verify/provider_check.hpp"
#include "semantic/semantic_model.hpp"

namespace trust {

class Context;

/// Семантический provider-гейт (Pipeline, ПОСЛЕ успешной семантики и ДО HIR/кодогена):
/// реализации builtin-типов обязаны быть загружены, включая реализации типов, от которых
/// зависит C++-заголовок реализации. Ошибки — пользовательские Error.
[[nodiscard]] bool verifySemanticProviders(Context& ctx, const std::vector<AstNodePtr>& roots);

} // namespace trust
