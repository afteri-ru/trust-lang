// Umbrella выбора вызова и сигнатур: кандидаты/доминирование (`resolveCall`), сигнатуры методов,
// binder'ы, унификация шаблонов, продвижение арифметики, целые литералы, реестр операторов,
// C++-идентичность сигнатур (`FunctionIdentity`).
#pragma once

#include "types/types.hpp"

#include "types/call/binders.hpp"
#include "types/call/callable_info.hpp"
#include "types/call/int_literal.hpp"
#include "types/call/method_signatures.hpp"
#include "types/call/operator_registry.hpp"
#include "types/call/overload_resolve.hpp"
#include "types/call/promotion.hpp"
#include "types/call/template_unify.hpp"
#include "types/func/fn_identity.hpp"
