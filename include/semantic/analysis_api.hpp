// include/semantic/analysis_api.hpp
// Umbrella публичного API семантического анализа для оркестратора (`pipeline`): драйвер проходов,
// поведенческие режимы и flow-анализы по MIR. Детали реализации — в internal-подкаталогах
// `semantic/{core,ref,typer,decl,verify}`; включать их снаружи компонента запрещено.
#pragma once

#include "semantic/core/behavioral_modes.hpp"
#include "semantic/core/pass_runner.hpp"
#include "semantic/ref/borrow_flow.hpp"
#include "semantic/ref/definite_assignment.hpp"
#include "semantic/ref/stack_check.hpp"
#include "semantic/semantic_model.hpp"
