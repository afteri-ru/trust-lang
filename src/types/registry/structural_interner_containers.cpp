// src/types/registry/structural_interner_containers.cpp
// Зона интернирования: КОНТЕЙНЕРЫ и ШАБЛОНЫ — Range/Array/Tensor, native-шаблон, pack,
// record-инстанциация и подстановка элементного параметра.
#include "types/registry/structural_interner.hpp"

#include "types/names/storage_key.hpp"
#include "types/names/type_names.hpp"
#include "types/registry/type_name_index.hpp"
#include "types/registry/type_store.hpp"
#include "types/subst/template_subst.hpp"
#include "types/type_read.hpp"
#include "utils/error.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {

TypeId StructuralInterner::getOrCreateRangeType(TypeId elementType) {
    EXPECT(isUnqualifiedType(elementType) && "getOrCreateRangeType: element type must not carry type-state flags");
    // Data=1 в Group::kRanges - конкретный структурный диапазон (абстрактный `:Range` - Data=0).
    TypeKind rangeKind = makeTypeKind(Group::kRanges, 1);
    TemplateTypeData data{m_read.getType(type_category::Range), {elementType}};
    TypeId id = getOrCreateStructuralType("", rangeKind, {elementType}, std::move(data), "@trust/range.hpp");
    // Рантайм-заголовки: range.hpp самодостаточен, но для toDict/элементов нужны dict.hpp и
    // rational.hpp - пайплайн извлекает только по прямому запросу, поэтому кладём все три.
    if (TypeDescriptor* desc = m_store.writableDescriptor(id)) {
        desc->preprocIncludes = {"@trust/range.hpp", "@trust/dict.hpp", "@trust/rational.hpp"};
    }
    return id;
}

TypeId StructuralInterner::substituteElementParam(TypeId elem, TypeId templateFuncType) {
    const TypeId tmplParam = m_read.getType(type_category::TemplateParam);
    const std::vector<std::pair<TypeId, TypeId>> mapping{{tmplParam, elem}};
    return trust::substituteTypeParams(TypeWriteContext{m_read, *this}, mapping, templateFuncType);
}

TypeId StructuralInterner::instantiateRangeMethod(TypeId objType, TypeId templateFuncType) {
    // Элементный тип: конкретный Range<Elem> → Elem; абстрактный :Range → Any (нет Elem).
    TypeId elem = m_read.store().rangeElementType(objType);
    if (elem == INVALID_TYPE_ID) {
        elem = m_read.getType(type_generic::Any);
    }
    return substituteElementParam(elem, templateFuncType);
}

TypeId StructuralInterner::getOrCreateArrayType(TypeId elementType, std::vector<uint64_t> dimensions) {
    EXPECT(isUnqualifiedType(elementType) && "getOrCreateArrayType: element type must not carry type-state flags");
    // Размерности - часть identity, но НЕ TypeId: они попадают в типизированный
    // ArrayShapeTag (structuralKeyTag), а не в TypeKey::names.
    // Data=1 в Group::kContainers - конкретный структурный массив (абстрактный `:Array` - Data=0).
    TypeKind arrayKind = makeTypeKind(Group::kContainers, 1);
    ArrayTypeData data{elementType, dimensions, false};
    TypeId id = getOrCreateStructuralType("", arrayKind, {elementType}, std::move(data), "");
    // Инклуды контейнера (std::vector/std::array) не зависят от элемента; добавляются при
    // кодогенерации по константности TypeId (resolveCppTypeId), поэтому здесь заголовки не задаём.
    return id;
}

TypeId StructuralInterner::getOrCreateTensorType(TypeId elementType, std::vector<uint64_t> shape, bool sparse) {
    EXPECT(isUnqualifiedType(elementType) && "getOrCreateTensorType: element type must not carry type-state flags");
    // Форма - часть identity, но НЕ TypeId: попадает в типизированный TensorShapeTag
    // (structuralKeyTag), а не в TypeKey::names.
    // Data=1 - dense, Data=2 - sparse (конкретные структурные тензоры; абстрактные :Tensor/:SparseTensor - Data=0).
    const TypeKind tensorKind = makeTypeKind(Group::kTensors, sparse ? 2 : 1);
    TensorTypeData data{elementType, shape};
    // Заголовок доставляется stdlib.trust (include/trust/tensor.hpp) - запись on-use.
    return getOrCreateStructuralType("", tensorKind, {elementType}, std::move(data), "@trust/tensor.hpp");
}

TypeId StructuralInterner::instantiateTensorMethod(TypeId objType, TypeId templateFuncType) {
    TypeId elem = m_read.store().tensorElementType(objType);
    if (elem == INVALID_TYPE_ID) {
        elem = m_read.getType(type_generic::Any);
    }
    return substituteElementParam(elem, templateFuncType);
}

TypeId StructuralInterner::getOrCreateNativeTemplateType(std::string_view cppTemplate, std::vector<TypeId> args, std::string_view preprocInclude) {
    for (const TypeId c : args) {
        EXPECT(isUnqualifiedType(c) && "getOrCreateNativeTemplateType: template arg must not carry type-state flags");
    }
    // Конкретная инстанциация: Data=1, children = типовые аргументы. C++-имя шаблона - часть
    // идентичности; оно вычисляется в TypeKey::tag из NativeTemplateTypeData (structuralKeyTag).
    TypeKind kind = makeTypeKind(Group::kNativeTemplate, 1);
    NativeTemplateTypeData data{std::string(cppTemplate), args};
    return getOrCreateStructuralType("", kind, args, std::move(data), preprocInclude);
}

TypeId StructuralInterner::instantiateArrayMethod(TypeId objType, TypeId templateFuncType) {
    // Элементный тип: конкретный Array<Elem> → Elem; абстрактный :Array → Any (нет Elem).
    TypeId elem = m_read.store().arrayElementType(objType);
    if (elem == INVALID_TYPE_ID) {
        elem = m_read.getType(type_generic::Any);
    }
    // Единая подстановка типового параметра T→Elem (как instantiateRangeMethod).
    return substituteElementParam(elem, templateFuncType);
}

TypeId StructuralInterner::instantiateNativeTemplateMethod(TypeId objType, TypeId templateFuncType) {
    const TypeId canonical = m_read.store().desugaredTypeId(objType);
    if (!m_read.store().isNativeTemplateType(canonical)) {
        return templateFuncType;
    }
    const std::vector<TypeId>& args = m_read.store().nativeTemplateArgs(canonical);
    // Для одного типового параметра (Iterator<T>) подстановка однозначна. Multi-arg
    // native templates не имеют различимых TemplateParam TypeId — это явно
    // неподдерживаемый сценарий, а не silent fallback.
    if (args.size() != 1) {
        return INVALID_TYPE_ID;
    }
    const TypeId tmplParam = m_read.getType(type_category::TemplateParam);
    TemplateBindings bindings{{tmplParam, false, {args.front()}}};
    return trust::substituteTemplateParams(TypeWriteContext{m_read, *this}, bindings, templateFuncType);
}

TypeId StructuralInterner::getOrCreateTemplateParamType(std::string_view name, TypeId owner, uint32_t depth, uint32_t index) {
    // Структурная идентичность параметра: якорный child - встроенный TemplateParam
    // (getOrCreateStructuralType требует names.size()==children.size(), имя остаётся в TypeKey::names
    // для generic-параметров, интернируемых по имени) + owner/позиция в StructuralTag::TemplateParamTag.
    // `owner` различает одинаковые имена у РАЗНЫХ шаблонов (A<T> != B<T>); depth/index - C++-модель
    // TemplateTypeParmDecl. Data=1 отличает идентификатор от абстрактного :TemplateParam (Data=0).
    TypeKind kind = makeTypeKind(Group::kTemplateParam, 1);
    TemplateParamTypeData data{std::string(name), owner, depth, index};
    return getOrCreateStructuralType("", kind, {m_read.getType(type_category::TemplateParam)}, std::move(data), "", {std::string(name)});
}

TypeId StructuralInterner::getOrCreatePackExpansionType(TypeId pattern) {
    if (pattern == INVALID_TYPE_ID) {
        return INVALID_TYPE_ID;
    }
    // Отдельная группа Group::kPackExpansion (Group::kEllipsis занят runtime-вариадиками).
    const TypeKind kind = makeTypeKind(Group::kPackExpansion, 1);
    const TypeId structuralPattern = getUnqualifiedType(pattern);
    return getOrCreateStructuralType("", kind, {structuralPattern}, PackExpansionTypeData{structuralPattern});
}

TypeId StructuralInterner::getOrCreateRecordTemplateInstance(TypeId templateId, std::vector<TypeId> args) {
    const TypeId tmpl = m_read.store().desugaredTypeId(templateId);
    const std::vector<TypeId> params = m_read.store().recordTemplateParams(tmpl);
    EXPECT(!params.empty() && "getOrCreateRecordTemplateInstance: not a record template");
    EXPECT(params.size() == args.size() && "getOrCreateRecordTemplateInstance: template arity mismatch");
    for (const TypeId a : args) {
        EXPECT(isUnqualifiedType(a) && "getOrCreateRecordTemplateInstance: template arg must not carry type-state flags");
    }
    std::vector<std::pair<TypeId, TypeId>> mapping;
    mapping.reserve(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        mapping.emplace_back(params[i], args[i]);
    }
    // Идентичность инстанциации: kind (группа шаблона, Data=1) + children = {templateId} + args.
    // templateId в детях обязателен: иначе Box<Int32> и Pair<Int32> совпали бы по (kind, children).
    std::vector<TypeId> children;
    children.reserve(args.size() + 1);
    children.push_back(tmpl);
    children.insert(children.end(), args.begin(), args.end());

    std::vector<TupleElementData> fields;
    // Копия полей ДО подстановки: substituteTypeParams добавляет дескрипторы в TypeStore.
    // Дескрипторы живут в std::deque (адреса стабильны), копия сохраняет независимость от
    // изменения набора полей во время рекурсии.
    // У объявленного (ещё не определённого) шаблона данных нет - поля пусты (self-инстанциация
    // внутри собственного тела; анализатор схлопывает её на сам шаблон).
    const RecordTypeData* trd = m_read.store().getTypeDataAs<RecordTypeData>(tmpl);
    const std::vector<TupleElementData> rawFields = trd != nullptr ? trd->fields : std::vector<TupleElementData>{};
    fields.reserve(rawFields.size());
    for (const auto& f : rawFields) {
        // Тип поля - QualType: подставляем identity, квалификаторы (const) сохраняем.
        const TypeId substituted = trust::substituteTypeParams(TypeWriteContext{m_read, *this}, mapping, f.type.getTypeId());
        fields.push_back(TupleElementData{f.name, f.type.withIdentity(substituted)});
    }
    // Базы/методы/имя шаблона - копируем ДО создания инстанциации: `substituteTypeParams` ниже
    // добавляет дескрипторы в TypeStore (deque — адреса стабильны); копии фиксируют исходное
    // объявление шаблона.
    std::vector<TypeId> tmplBases = m_read.store().baseClasses(tmpl); // копия исходного набора
    std::string tmplName;
    if (const TypeDescriptor* tdesc = m_read.store().descriptorOf(tmpl)) {
        tmplName = tdesc->name;
    }
    std::vector<TypeId> bases;
    bases.reserve(tmplBases.size());
    for (const TypeId b : tmplBases) {
        bases.push_back(trust::substituteTypeParams(TypeWriteContext{m_read, *this}, mapping, b));
    }

    RecordTypeData data{std::move(fields)};
    data.templateOf = tmpl;
    data.templateArgs = std::move(args);
    TypeKind kind = makeTypeKind(getGroup(getTypeKind(tmpl)), 1);
    const TypeId id = getOrCreateStructuralType("", kind, std::move(children), std::move(data));
    TypeDescriptor* desc = m_store.writableDescriptor(id);
    if (desc == nullptr) {
        return id; // инстанциация - пользовательский структурный тип; сюда попасть не должны
    }
    // desc НЕ держим через вызовы реестра - заполняем локальными копиями (реестр уже не мутируем).
    desc->name = tmplName; // имя для диагностик (в TypeNameIndex не регистрируется)
    desc->baseClasses = std::move(bases);
    // Каноническая record-специализация (templateId, args): неявная инстанциация
    // (TemplateSpecializationKind); явная специализация будет выставляться здесь на этапе S5.
    desc->specializationKind = TemplateSpecializationKind::kImplicitInstantiation;
    // Методы/алиасы специализации НЕ копируются энергично (C++-модель неявной инстанциации):
    // они материализуются по требованию при первом доступе и мемоизируются в `desc->methods`
    // (`StructuralInterner::materializeRecordInstanceMembers`, хук в MethodTable::findMethodInfo).
    return id;
}

TypeId StructuralInterner::materializeRecordInstanceMembers(TypeId id) {
    if (id == INVALID_TYPE_ID || !m_read.store().isRecordType(id)) {
        return id;
    }
    const RecordTypeData* rdata = m_read.store().getTypeDataAs<RecordTypeData>(m_read.store().desugaredTypeId(id));
    if (rdata == nullptr || rdata->templateOf == INVALID_TYPE_ID) {
        return id; // не record-специализация
    }
    const TypeDescriptor* tdesc = m_read.store().descriptorOf(rdata->templateOf);
    if (tdesc == nullptr) {
        return id; // шаблон не найден
    }
    TypeDescriptor* desc = m_store.writableDescriptor(id);
    if (desc == nullptr) {
        return id;
    }
    if (!desc->methods.empty()) {
        return id; // memo: члены уже материализованы (повторный доступ не дублирует)
    }
    std::vector<std::pair<TypeId, TypeId>> mapping;
    const std::vector<TypeId> params = m_read.store().recordTemplateParams(rdata->templateOf);
    const std::vector<TypeId>& args = rdata->templateArgs;
    mapping.reserve(std::min(params.size(), args.size()));
    for (std::size_t i = 0; i < params.size() && i < args.size(); ++i) {
        mapping.emplace_back(params[i], args[i]);
    }
    // Методы - те же объявления шаблона: подставляется только identity сигнатуры; параметры/defaults
    // и C++-суффиксы (уже с binder-контекстом шаблона) копируются как есть.
    std::map<std::string, std::vector<CallableSignature>> methods;
    for (const auto& [key, entries] : tdesc->methods) {
        std::vector<CallableSignature> substituted;
        substituted.reserve(entries.size());
        for (const CallableSignature& entry : entries) {
            CallableSignature copy = entry;
            copy.signature = trust::substituteTypeParams(TypeWriteContext{m_read, *this}, mapping, entry.signature);
            substituted.push_back(std::move(copy));
        }
        methods[key] = std::move(substituted);
    }
    desc->methods = std::move(methods);
    desc->methodAliases = tdesc->methodAliases;
    return id;
}

} // namespace trust
