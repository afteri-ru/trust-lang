// Зона интернирования: КОНТЕЙНЕРЫ и ШАБЛОНЫ — Range/Array/Tensor, native-шаблон, pack,
// record-инстанциация и подстановка элементного параметра.
#include "types/registry/structural_interner.hpp"

#include "diag/base_diags.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/report.hpp"
#include "types/names/storage_key.hpp"
#include "types/names/type_names.hpp"
#include "types/registry/type_name_index.hpp"
#include "types/registry/type_store.hpp"
#include "types/subst/template_subst.hpp"
#include "types/type/data_record_access.hpp"
#include "types/type_read.hpp"
#include "utils/error.hpp"

#include <algorithm>
#include <cstddef>
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
    // Заголовок диапазона принадлежит реализации абстрактного `:Range` (range.src); include
    // подтягивается fallback'ом preprocIncludesOf, структурный узел своих include не несёт.
    return getOrCreateStructuralType("", rangeKind, {TypeKeyChild{elementType}}, std::move(data));
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
        // Абстрактный :Range → элементный тип не выведен (внутренний sentinel Auto).
        elem = m_read.getType(type_generic::Auto);
    }
    return substituteElementParam(elem, templateFuncType);
}

TypeId StructuralInterner::getOrCreateArrayType(TypeId elementType, std::vector<uint64_t> dimensions) {
    EXPECT(isUnqualifiedType(elementType) && "getOrCreateArrayType: element type must not carry type-state flags");
    // Размерности - часть identity, но НЕ TypeId: они попадают в типизированный
    // ArrayShapeTag (structuralKeyTag), а не в имя записи TypeKeyChild.
    // Data=1 в Group::kContainers - конкретный структурный массив (абстрактный `:Array` - Data=0).
    TypeKind arrayKind = makeTypeKind(Group::kContainers, 1);
    ArrayTypeData data{elementType, dimensions, false};
    TypeId id = getOrCreateStructuralType("", arrayKind, {TypeKeyChild{elementType}}, std::move(data));
    // Инклуды контейнера (std::vector/std::array) не зависят от элемента; добавляются при
    // кодогенерации по константности TypeId (resolveCppTypeId), поэтому здесь заголовки не задаём.
    return id;
}

TypeId StructuralInterner::getOrCreateTensorType(TypeId elementType, std::vector<uint64_t> shape, bool sparse) {
    EXPECT(isUnqualifiedType(elementType) && "getOrCreateTensorType: element type must not carry type-state flags");
    // Форма - часть identity, но НЕ TypeId: попадает в типизированный TensorShapeTag
    // (structuralKeyTag), а не в имя записи TypeKeyChild.
    // Data=1 - dense, Data=2 - sparse (конкретные структурные тензоры; абстрактные :Tensor/:SparseTensor - Data=0).
    const TypeKind tensorKind = makeTypeKind(Group::kTensors, sparse ? 2 : 1);
    TensorTypeData data{elementType, shape};
    // Заголовок доставляется stdlib.trust (include/stdlib/tensor.hpp) - запись on-use.
    return getOrCreateStructuralType("", tensorKind, {TypeKeyChild{elementType}}, std::move(data));
}

TypeId StructuralInterner::instantiateTensorMethod(TypeId objType, TypeId templateFuncType) {
    TypeId elem = m_read.store().tensorElementType(objType);
    if (elem == INVALID_TYPE_ID) {
        elem = m_read.getType(type_generic::Auto);
    }
    return substituteElementParam(elem, templateFuncType);
}

TypeId StructuralInterner::getOrCreateNativeTemplateType(TypeId templateOf, std::vector<TypeId> args) {
    EXPECT(templateOf != INVALID_TYPE_ID && "getOrCreateNativeTemplateType: abstract native template id is required");
    EXPECT(m_read.store().isNativeTemplateType(m_read.store().desugaredTypeId(templateOf)) &&
           "getOrCreateNativeTemplateType: templateOf must be an abstract native template");
    for (const TypeId c : args) {
        EXPECT(isUnqualifiedType(c) && "getOrCreateNativeTemplateType: template arg must not carry type-state flags");
    }
    // Конкретная инстанциация: Data=1, children = типовые аргументы. Идентичность — ЯЗЫКОВОЙ ключ
    // (абстрактный шаблон + аргументы), НЕ C++-имя (R37, NativeTemplateTag{templateOf}).
    TypeKind kind = makeTypeKind(Group::kNativeTemplate, 1);
    std::vector<TypeKeyChild> children;
    children.reserve(args.size());
    for (const TypeId arg : args) {
        children.push_back(TypeKeyChild{arg});
    }
    NativeTemplateTypeData data{templateOf, std::move(args)};
    return getOrCreateStructuralType("", kind, std::move(children), std::move(data));
}

TypeId StructuralInterner::instantiateArrayMethod(TypeId objType, TypeId templateFuncType) {
    // Элементный тип: конкретный Array<Elem> → Elem; абстрактный :Array → Any (нет Elem).
    TypeId elem = m_read.store().arrayElementType(objType);
    if (elem == INVALID_TYPE_ID) {
        elem = m_read.getType(type_generic::Auto);
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
    // Структурная идентичность параметра: якорный child - встроенный TemplateParam (имя в
    // записи TypeKeyChild::name для generic-параметров, интернируемых по имени) + owner/позиция
    // в StructuralTag::TemplateParamTag.
    // `owner` различает одинаковые имена у РАЗНЫХ шаблонов (A<T> != B<T>); depth/index - C++-модель
    // TemplateTypeParmDecl. Data=1 отличает идентификатор от абстрактного :TemplateParam (Data=0).
    TypeKind kind = makeTypeKind(Group::kTemplateParam, 1);
    TemplateParamTypeData data{std::string(name), owner, depth, index};
    return getOrCreateStructuralType("", kind, {TypeKeyChild{m_read.getType(type_category::TemplateParam), std::string(name)}}, std::move(data));
}

TypeId StructuralInterner::getOrCreatePackExpansionType(TypeId pattern) {
    if (pattern == INVALID_TYPE_ID) {
        return INVALID_TYPE_ID;
    }
    // Отдельная группа Group::kPackExpansion (Group::kEllipsis занят runtime-вариадиками).
    const TypeKind kind = makeTypeKind(Group::kPackExpansion, 1);
    const TypeId structuralPattern = getUnqualifiedType(pattern);
    return getOrCreateStructuralType("", kind, {TypeKeyChild{structuralPattern}}, PackExpansionTypeData{structuralPattern});
}

TypeId StructuralInterner::getOrCreateRecordTemplateInstance(TypeId templateId, std::vector<TypeId> args) {
    const TypeId tmpl = m_read.store().desugaredTypeId(templateId);
    const std::vector<TypeId> params = m_read.store().templateParamsOf(tmpl);
    EXPECT(!params.empty() && "getOrCreateRecordTemplateInstance: not a record template");
    EXPECT(params.size() == args.size() && "getOrCreateRecordTemplateInstance: template arity mismatch");
    for (const TypeId a : args) {
        EXPECT(isUnqualifiedType(a) && "getOrCreateRecordTemplateInstance: template arg must not carry type-state flags");
    }
    // R26: защита от циклов и лимит глубины. Инстанциация регистрируется в таблице ПОСЛЕ
    // подстановки типов её полей, поэтому самоссылочное поле по значению запрашивает тот же ключ
    // до регистрации — это цикл. Диагностика цикла выдаётся один раз на ключ (R9); повторные
    // запросы того же ключа другими проходами возвращают INVALID без дубля диагностики.
    const std::pair<TypeId, std::vector<TypeId>> instanceKey{tmpl, args};
    if (m_failedInstantiationCycles.contains(instanceKey)) {
        return INVALID_TYPE_ID;
    }
    for (const InstanceFrame& frame : m_instanceStack) {
        if (frame.templateId == tmpl && frame.args == args) {
            m_failedInstantiationCycles.insert(instanceKey);
            diag::reportWithOptions(m_diag, m_opts, diag::DiagId::ParseError, {},
                                    "recursive instantiation of template '{}': its field type is the template's own instantiation",
                                    m_read.printer().typeNameOf(tmpl));
            return INVALID_TYPE_ID;
        }
    }
    if (m_instanceStack.size() >= kMaxRecordInstanceDepth) {
        diag::reportWithOptions(m_diag, m_opts, diag::DiagId::ParseError, {}, "template instantiation depth limit ({}) exceeded for '{}'",
                                kMaxRecordInstanceDepth, m_read.printer().typeNameOf(tmpl));
        return INVALID_TYPE_ID;
    }
    const InstanceScope instanceScope{m_instanceStack, InstanceFrame{tmpl, args}};
    std::vector<std::pair<TypeId, TypeId>> mapping;
    mapping.reserve(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        mapping.emplace_back(params[i], args[i]);
    }
    // Идентичность инстанциации: kind (группа шаблона, Data=1) + children = {templateId} + args.
    // templateId в детях обязателен: иначе Box<Int32> и Pair<Int32> совпали бы по (kind, children).
    std::vector<TypeKeyChild> children;
    children.reserve(args.size() + 1);
    children.push_back(TypeKeyChild{tmpl});
    for (const TypeId arg : args) {
        children.push_back(TypeKeyChild{arg});
    }

    std::vector<TupleElementData> fields;
    // Копия полей ДО подстановки: substituteTypeParams добавляет дескрипторы в TypeStore.
    // Дескрипторы живут в std::deque (адреса стабильны), копия сохраняет независимость от
    // изменения набора полей во время рекурсии.
    // У объявленного (ещё не определённого) шаблона формы нет - поля пусты.
    const RecordTypeData* trd = m_read.store().getTypeDataAs<RecordTypeData>(tmpl);
    const std::vector<TupleElementData> rawFields = trd != nullptr ? RecordTypeAccess::fieldsOf(*trd) : std::vector<TupleElementData>{};
    fields.reserve(rawFields.size());
    for (const auto& f : rawFields) {
        // Тип поля - QualType: подставляем identity, квалификаторы (const) сохраняем. Доступ и ключ
        // объявления поля (DeclId) — проекция того же объявления шаблона (R14).
        const TypeId substituted = trust::substituteTypeParams(TypeWriteContext{m_read, *this}, mapping, f.type.getTypeId());
        if (substituted == INVALID_TYPE_ID) {
            return INVALID_TYPE_ID; // диагностику выдал вложенный запрос инстанциации (R9/R16)
        }
        fields.push_back(TupleElementData{f.name, f.type.withIdentity(substituted), f.access, f.declId});
    }
    // Базы/методы/имя шаблона - копируем ДО создания инстанциации: `substituteTypeParams` ниже
    // добавляет дескрипторы в TypeStore (deque — адреса стабильны); копии фиксируют исходное
    // объявление шаблона.
    std::vector<BaseClass> tmplBases = m_read.store().basesOf(tmpl); // копия исходного набора
    std::string tmplName;
    if (const TypeDescriptor* tdesc = m_read.store().descriptorOf(tmpl)) {
        tmplName = tdesc->name;
    }
    std::vector<BaseClass> bases;
    bases.reserve(tmplBases.size());
    for (const BaseClass& b : tmplBases) {
        const TypeId baseType = trust::substituteTypeParams(TypeWriteContext{m_read, *this}, mapping, b.type);
        if (baseType == INVALID_TYPE_ID) {
            return INVALID_TYPE_ID; // диагностику выдал вложенный запрос инстанциации (R9/R16)
        }
        bases.push_back(BaseClass{baseType, b.access});
    }

    RecordTypeData data{std::move(fields), {}, tmpl, std::move(args)};
    TypeKind kind = makeTypeKind(typeGroupOf(tmpl), 1);
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
    if (rdata == nullptr || RecordTypeAccess::templateOf(*rdata) == INVALID_TYPE_ID) {
        return id; // не record-специализация
    }
    const TypeDescriptor* tdesc = m_read.store().descriptorOf(RecordTypeAccess::templateOf(*rdata));
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
    const TypeId instanceOfTemplate = RecordTypeAccess::templateOf(*rdata);
    const std::vector<TypeId> params = m_read.store().templateParamsOf(instanceOfTemplate);
    const std::vector<TypeId>& args = RecordTypeAccess::templateArgsOf(*rdata);
    mapping.reserve(std::min(params.size(), args.size()));
    for (std::size_t i = 0; i < params.size() && i < args.size(); ++i) {
        mapping.emplace_back(params[i], args[i]);
    }
    // Методы - те же объявления шаблона: подставляется только identity сигнатуры; параметры/defaults
    // и идентичность объявления (DeclId, ключ целевого C++-суффикса) копируются как есть - сам
    // целевой суффикс хранится у владельца (`target`/DeclCppFacts) и резолвится по DeclId.
    std::map<MethodKey, MethodInfo> methods;
    for (const auto& [key, info] : tdesc->methods) {
        std::vector<CallableSignature> substituted;
        substituted.reserve(info.overloads.size());
        for (const CallableSignature& entry : info.overloads) {
            CallableSignature copy = entry;
            copy.signature = trust::substituteTypeParams(TypeWriteContext{m_read, *this}, mapping, entry.signature);
            substituted.push_back(std::move(copy));
        }
        methods[key] = MethodInfo{std::move(substituted), info.isNative};
    }
    desc->methods = std::move(methods);
    desc->methodAliases = tdesc->methodAliases;
    return id;
}

} // namespace trust
