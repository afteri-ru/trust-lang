// Umbrella базовой модели типов: идентичность (TypeId/TypeKind/QualType), данные типов, реестр,
// группы, встроенные типы и зарезервированные рантайм-символы. Детали реализации — в
// internal-подкаталогах (`types/{call,func,names,registry,subst,type}`), снаружи не включаются.
#pragma once

#include "types/group.hpp"
#include "types/intrinsics.hpp"
#include "types/type_system.hpp"
#include "types/type/ref_type.hpp"
#include "types/type/type_traversal.hpp"
#include "types/type_data.hpp"
#include "types/type_id.hpp"
#include "types/typekind.hpp"
