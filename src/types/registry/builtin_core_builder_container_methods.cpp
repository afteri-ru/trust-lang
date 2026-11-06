// Seed-регистрация методов абстрактных контейнеров (:Range/:Array/:Tensor/:Dict).
// Порядок регистрации — часть контракта: он задаёт registry_index встроенных типов.
#include "types/registry/builtin_core_builder.hpp"

#include "types/registry/builtin_seed_zones.hpp"

#include "types/group.hpp"
#include "types/names/type_names.hpp"
#include "types/registry/builtin_registry.hpp"
#include "types/registry/method_table.hpp"
#include "types/registry/named_type_registrar.hpp"
#include "types/registry/structural_interner.hpp"
#include "types/registry/type_store.hpp"
#include "types/type_read.hpp"
#include "utils/error.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {

void BuiltinCoreBuilder::registerBuiltinContainerMethods(BuiltinSeedZones& z) {
    // ids резолвятся по именам: типы уже зарегистрированы, дублировать значения не нужно.
    const TypeId int64Id = z.read.getType(type::Int64);
    const TypeId boolId = z.read.getType(type::Bool);
    const TypeId autoId = z.read.getType(type_generic::Auto);
    const TypeId templateParamId = z.read.getType(type_category::TemplateParam);
    const TypeId rangeId = z.read.getType(type_category::Range);
    const TypeId arrayId = z.read.getType(type::Array);
    const TypeId tensorId = z.read.getType(type::Tensor);
    const TypeId dictId = z.read.getType(type::Dict);

    // Здесь только ЧИСТЫЙ Trust-интерфейс (имя + сигнатура), без C++-имён: привязка к членам
    // trust::Range и include/link приходят из stdlib/builtin/range.src (`count^ := %count ...`).
    // Ключ кодирует константность ('^'); все методы Range константные.
    // Сигнатуры объявлены ОДИН раз на абстрактном `:Range`; ЭЛЕМЕНТ-зависимые слоты помечены
    // типовым параметром T (Group::kTemplateParam) и подставляются (T→Elem) при резолве для
    // конкретного Range<Elem> (C++-модель шаблонов, см. getOrCreateRangeType/instantiateRangeMethod):
    //   start/stop/step → T, at(Int64) → T, contains(T) → Bool.
    // count/size/empty/toDict - конкретные типы (не зависят от элемента). toVector/toArray/toList/
    // reversed возвращают контейнер/диапазон, для которых элементная точность не моделируется → Any.
    // Алиас `length` → `count`: алиас ИНТЕРФЕЙСА (реализацию наследует от цели).
    z.methods.addMethod(rangeId, "count^", z.interner.getOrCreateFunctionType(int64Id, {}), {"length^"});
    z.methods.addMethod(rangeId, "size^", z.interner.getOrCreateFunctionType(int64Id, {}));
    z.methods.addMethod(rangeId, "empty^", z.interner.getOrCreateFunctionType(boolId, {}));
    z.methods.addMethod(rangeId, "start^", z.interner.getOrCreateFunctionType(templateParamId, {}));
    z.methods.addMethod(rangeId, "stop^", z.interner.getOrCreateFunctionType(templateParamId, {}));
    z.methods.addMethod(rangeId, "step^", z.interner.getOrCreateFunctionType(templateParamId, {}));
    z.methods.addMethod(rangeId, "at^", z.interner.getOrCreateFunctionType(templateParamId, {int64Id}));
    z.methods.addMethod(rangeId, "contains^", z.interner.getOrCreateFunctionType(boolId, {templateParamId}));
    z.methods.addMethod(rangeId, "toVector^", z.interner.getOrCreateFunctionType(autoId, {}));
    z.methods.addMethod(rangeId, "toArray^", z.interner.getOrCreateFunctionType(autoId, {}));
    z.methods.addMethod(rangeId, "toList^", z.interner.getOrCreateFunctionType(autoId, {}));
    z.methods.addMethod(rangeId, "toDict^", z.interner.getOrCreateFunctionType(dictId, {}));
    z.methods.addMethod(rangeId, "reversed^", z.interner.getOrCreateFunctionType(autoId, {}));

    // Модель как у `:Range`: сигнатуры объявлены ОДИН раз на абстрактном `:Array`; элемент-зависимые
    // слоты помечены типовым параметром T (Group::kTemplateParam) и подставляются (T→Elem) при
    // резолве для конкретного `Array<Elem>` (instantiateArrayMethod).
    //   at(Int64)→T, first/front/last/back→T, push_back(T)→Void, pop_back()→Void.
    // count/size/length/empty/clear/resize - не зависят от элемента (конкретные типы).
    // Чистый Trust-интерфейс: C++-члены (size/front/back/at/push_back/…) привязывает array.src.
    z.methods.addMethod(arrayId, "count^", z.interner.getOrCreateFunctionType(int64Id, {}), {"length^"});
    z.methods.addMethod(arrayId, "size^", z.interner.getOrCreateFunctionType(int64Id, {}));
    z.methods.addMethod(arrayId, "empty^", z.interner.getOrCreateFunctionType(boolId, {}));
    z.methods.addMethod(arrayId, "at^", z.interner.getOrCreateFunctionType(templateParamId, {int64Id}));
    z.methods.addMethod(arrayId, "first^", z.interner.getOrCreateFunctionType(templateParamId, {}), {"front^"});
    z.methods.addMethod(arrayId, "last^", z.interner.getOrCreateFunctionType(templateParamId, {}), {"back^"});
    z.methods.addMethod(arrayId, "push_back", z.interner.getOrCreateFunctionType(INVALID_TYPE_ID, {templateParamId}));
    z.methods.addMethod(arrayId, "pop_back", z.interner.getOrCreateFunctionType(INVALID_TYPE_ID, {}));
    z.methods.addMethod(arrayId, "clear", z.interner.getOrCreateFunctionType(INVALID_TYPE_ID, {}));
    z.methods.addMethod(arrayId, "resize", z.interner.getOrCreateFunctionType(INVALID_TYPE_ID, {int64Id}));

    // Как у Array/Range: сигнатуры объявлены ОДИН раз на абстрактном `:Tensor`;
    // элемент-зависимые слоты помечены T и подставляются instantiateTensorMethod.
    // Привязка к членам trust::Tensor — в провайдере tensor.src.
    z.methods.addMethod(tensorId, "rank^", z.interner.getOrCreateFunctionType(int64Id, {}));
    z.methods.addMethod(tensorId, "size^", z.interner.getOrCreateFunctionType(int64Id, {}));
    z.methods.addMethod(tensorId, "shape^", z.interner.getOrCreateFunctionType(z.interner.getOrCreateArrayType(int64Id), {}));
    z.methods.addMethod(tensorId, "at^", z.interner.getOrCreateFunctionType(templateParamId, {int64Id}));

    // Метод универсального словаря Dict (trust::Dict): size() → Int64 (рантайм Dict::size()).
    // Чистый Trust-интерфейс: C++-член и привязка приходят из stdlib/builtin/dict.src.
    z.methods.addMethod(dictId, "size^", z.interner.getOrCreateFunctionType(int64Id, {}));
}

} // namespace trust
