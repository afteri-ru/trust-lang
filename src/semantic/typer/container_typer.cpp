// src/semantic/typer/container_typer.cpp
// Типизация контейнерных литералов и выражений: Dict/Array/Tensor/Range (фасад - `ExprTyper`).
#include "types/type_read.hpp"
#include "semantic/core/ast_view.hpp"
#include "semantic/typer/literal_annotation.hpp"
#include "semantic/typer/container_typer.hpp"
#include "semantic/core/name_resolution.hpp"
#include "semantic/typer/ellipsis.hpp"
#include "semantic/verify/format_check.hpp"
#include "semantic/solver.hpp"
#include "symbols/symbol_table.hpp"
#include "semantic/typer/type_inference.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/token.hpp"
#include "ast/token_type.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "semantic/diag.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "utils/strings.hpp"
#include <algorithm>
#include <format>
#include <string>

namespace trust {

namespace {

bool isTensorTypeId(const TypeReadModel& reg, TypeId id) {
    if (id == INVALID_TYPE_ID) {
        return false;
    }
    const TypeId c = reg.store().desugaredTypeId(id);
    return reg.store().isTensorType(c) || c == reg.getType(type::Tensor) || c == reg.getType(type::SparseTensor);
}

bool isNestedArrayLiteral(const AstNodeBase* node) {
    return node != nullptr && (node->kind() == ParserToken::Kind::ArrayInit || node->kind() == ParserToken::Kind::DictLiteral);
}

// Собирает типы ЛИСТЬЕВ (скалярных значений) тензорного литерала, включая вложенные
// `[[...],[...]]`. Чисел/типов больше, чем нужно для dtype: порядок обхода row-major.
void collectTensorLeaves(const DictLiteralNode& node, std::vector<TypeId>& raw, int64_t& count) {
    for (const auto& el : node.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        const auto& a = static_cast<const ArgNode&>(*el);
        if (!a.m_value) {
            continue;
        }
        if (isNestedArrayLiteral(a.m_value.get())) {
            collectTensorLeaves(static_cast<const DictLiteralNode&>(*a.m_value), raw, count);
            continue;
        }
        if (a.resultType.getTypeId() != INVALID_TYPE_ID) {
            raw.push_back(a.resultType.getTypeId());
        }
        ++count;
    }
}

int64_t shapeProduct(const std::vector<uint64_t>& shape) {
    int64_t n = 1;
    for (const uint64_t d : shape) {
        n *= static_cast<int64_t>(d);
    }
    return n;
}

} // namespace

// Анализ литерала словаря. Контракт: все элементы m_body - ArgNode (имя в text(), значение в
// m_value), строятся из канонических пар грамматики `args` (term_to_ast::visit_DICT).
// Значение анализируем полностью (резолв/типизация); имя-метку НЕ резолвим как переменную и
// НЕ регистрируем в таблице символов. Тип значения сохраняем на элементе (ArgNode::resultType
// из exprType) - единый источник для кодогенерации TypedValue (не только Literal::typeState).
void ContainerTyper::analyzeDictLiteral(Sequence& dict_node) {
    // Enum/Variant-объявление (ПОСТФИКС `(...):Enum`/`(...):Variant`): это правая часть `::=`,
    // обрабатывается analyzeTypeDecl (analyzeEnumDecl/analyzeVariantDecl); как обычный словарь НЕ
    // анализируется (иначе голые члены `B` резолвились бы как переменные). Префикс `:Enum(...)`
    // - НЕ объявление (type-call): голые аргументы = значения, резолвятся как обычно.
    const auto& dl0 = static_cast<const DictLiteralNode&>(dict_node);
    if (!dl0.prefix && dl0.m_type) {
        const std::string ann = std::string(requiredText(*dl0.m_type));
        if (ann == type_category::Enum || ann == type_category::Variant) {
            return;
        }
    }
    for (auto& el : dict_node.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        auto& a = static_cast<ArgNode&>(*el);
        // Аннотация элемента (`true` → `1:Bool`) принадлежит ArgNode, а не значению: резолвим
        // её здесь, чтобы факт был материализован для кодогена (иначе тип узла не разрешён).
        if (a.m_type) {
            (void)m_actx.types().resolveTypeRef(*a.m_type);
        }
        if (!a.m_value) {
            continue;
        }
        m_core.analyzeNode(a.m_value);
        a.resultType = m_actx.facts().exprState(*a.m_value);
    }
    // Тип литерала определяется по АННОТАЦИИ m_type через реестр типов (никаких строк/enum):
    //   - если аннотация резолвится в тип Tuple → kind узла меняется на Tuple, а тип выражения -
    //     на интернированный структурный кортеж (TupleTypeData; источник `t.name`/`t.0`);
    //   - иначе (аннотация-каст/конструктор или её нет) - тип литерала = резолвленной аннотации
    //     (скаляр/класс) или универсального словаря Dict.
    auto& dl = static_cast<DictLiteralNode&>(dict_node);
    TypeSystem& reg = m_actx.ctx().types();
    // Тип операнда одиночной конструкции/каста (`:T(x)`): нужен кодогенерации, чтобы отличить
    // явную конверсию Any (any_to) от проверяемого numeric-каста (checked_cast).
    dl.castSourceType = QualType{};
    if (dl.m_body.size() == 1 && dl.m_body.front() && dl.m_body.front()->kind() == ParserToken::Kind::ArgNode) {
        const auto& only = static_cast<const ArgNode&>(*dl.m_body.front());
        if (only.m_value) {
            dl.castSourceType = only.resultType;
        }
    }
    TypeId target = INVALID_TYPE_ID;
    if (dl.m_type) {
        target = m_actx.types().resolveTypeRef(*dl.m_type).value_or(INVALID_TYPE_ID);
    }
    const TypeId tupleId = reg.read().getType(type_category::Tuple);
    // Именованные аргументы конструктора record/native-типа пока не привязываются к полям:
    // молчаливый порядок исходника изменил бы инициализацию. Явная диагностика вместо
    // неверной генерации; позиционная форма остаётся доступной.
    if (target != INVALID_TYPE_ID &&
        (reg.read().store().isRecordType(target) || reg.read().store().isNativeClassType(target) || reg.read().store().isNativeTemplateType(target))) {
        for (const auto& el : dl.m_body) {
            if (el && el->kind() == ParserToken::Kind::ArgNode && !static_cast<const ArgNode&>(*el).text().empty()) {
                m_actx.ctx().diag().report(Severity::Error, el->range(), "named constructor arguments are not implemented for type '{}'",
                                           reg.read().printer().getTypeName(target));
                m_actx.facts().setExprType(&dl, INVALID_TYPE_ID);
                return;
            }
        }
    }
    // Тензорная value-конструкция `:Tensor[2,2](1,2,3,4)` / `:Tensor[3](1,2,3):Float32`:
    // dims сохраняются в IdentType (см. term_to_ast), dtype - из хвостовой аннотации или
    // join значений. Абстрактный `:Tensor` без dims/dtype достраивается в analyzeTensorInit.
    if (dl.prefix && dl.m_type && dl.m_type->kind() == ParserToken::Kind::TypeName) {
        const auto* it = dynamic_cast<const IdentType*>(dl.m_type.get());
        const auto baseTid = m_actx.types().lookupTypeName(requiredText(*dl.m_type));
        if (it != nullptr && baseTid.has_value() && isTensorTypeId(reg, *baseTid)) {
            const bool sparse = reg.read().store().desugaredTypeId(*baseTid) == reg.read().getType(type::SparseTensor);
            std::vector<uint64_t> dims;
            if (it->dims()) {
                for (const auto& d : *it->dims()) {
                    if (!d || d->kind() != ParserToken::Kind::IntLiteral) {
                        m_actx.ctx().diag().report(Severity::Error, d ? d->range() : dl.range(), "tensor shape dimensions must be integer literals");
                        m_actx.facts().setExprType(&dl, INVALID_TYPE_ID);
                        return;
                    }
                    unsigned long long v = 0;
                    try {
                        v = std::stoull(stripDigitSeparators(requiredText(*d)), nullptr, 0);
                    } catch (...) {
                        m_actx.ctx().diag().report(Severity::Error, d->range(), "tensor dimension '{}' is out of range", std::string(requiredText(*d)));
                        m_actx.facts().setExprType(&dl, INVALID_TYPE_ID);
                        return;
                    }
                    dims.push_back(v);
                }
            }
            TypeId elem = INVALID_TYPE_ID;
            if (dl.arrayElementAnnotation) {
                elem = m_actx.types().resolveTypeRef(*dl.arrayElementAnnotation).value_or(INVALID_TYPE_ID);
            }
            if (elem == INVALID_TYPE_ID) {
                std::vector<TypeId> raw;
                int64_t count = 0;
                collectTensorLeaves(dl, raw, count);
                elem = arrayElementJoin(raw);
            }
            const TypeId targetTensor = (elem != INVALID_TYPE_ID) ? reg.interner().getOrCreateTensorType(elem, dims, sparse) : *baseTid;
            analyzeTensorInit(dl, targetTensor);
            return;
        }
    }
    if (isTensorTypeId(reg, target)) {
        analyzeTensorInit(dl, target);
        return;
    }
    if (target != INVALID_TYPE_ID && reg.read().store().desugaredTypeId(target) == tupleId) {
        dl.setKind(ParserToken::Kind::Tuple);
        std::vector<std::pair<std::string, QualType>> elems;
        elems.reserve(dl.m_body.size());
        for (const auto& el : dl.m_body) {
            if (!el || el->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            const auto& a = static_cast<const ArgNode&>(*el);
            elems.emplace_back(std::string(a.text()), a.resultType);
        }
        const TypeId t = reg.interner().getOrCreateTupleType(std::move(elems));
        if (t != INVALID_TYPE_ID) {
            m_actx.facts().setExprType(&dict_node, t);
        }
        return;
    }
    // Конструкция шаблон-типа-массива `:vector<Int32>(...)` (нативный шаблон → интернированный
    // Array<Elem>): элемент уже известен из шаблона - фиксируем arrayType для emitArrayLiteral
    // (`std::vector<Elem>{...}`), не выводя элемент по значениям.
    if (target != INVALID_TYPE_ID && reg.read().store().isArrayType(target)) {
        dl.arrayType = QualType::fromTypeId(target);
        m_actx.facts().setExprType(&dict_node, target);
        return;
    }
    // Конструкция массива `:Array(...)` / `:Array^(...)`: префиксная форма с аннотацией `:Array`.
    // Элементный тип выводится из значений (join), как у литерала `[...]` (analyzeArrayInit);
    // интернируем структурный Array<Elem> (тип сохраняем в DictLiteralNode::arrayType).
    // Константность контейнера `:Array^` (attr::ReadOnly на m_type) → isConst (std::array).
    const TypeId arrayId = reg.read().getType(type::Array);
    if (target != INVALID_TYPE_ID && reg.read().store().desugaredTypeId(target) == arrayId) {
        bool isConst = false;
        if (const AstNodeBase* mtype = dl.m_type.get()) {
            if (const AstNodeAttr* a = mtype->as_attr()) {
                isConst = a->has_attr(m_actx.ctx().attrs(), attr::ReadOnly);
            }
        }
        // Элементный тип: хвостовая аннотация `:Array(...):Elem` (arrayElementAnnotation)
        // приоритетнее; иначе join значений.
        TypeId elemType = INVALID_TYPE_ID;
        if (dl.arrayElementAnnotation) {
            if (auto ann = m_actx.types().resolveTypeRef(*dl.arrayElementAnnotation); ann) {
                elemType = *ann;
            }
        }
        if (elemType == INVALID_TYPE_ID) {
            std::vector<TypeId> raw;
            for (const auto& el : dl.m_body) {
                if (!el || el->kind() != ParserToken::Kind::ArgNode) {
                    continue;
                }
                const auto& a = static_cast<const ArgNode&>(*el);
                TypeId et = a.resultType.getTypeId();
                if (et == INVALID_TYPE_ID && a.m_value) {
                    et = m_actx.facts().exprType(*a.m_value);
                }
                if (et != INVALID_TYPE_ID) {
                    raw.push_back(et);
                }
            }
            // Узкая разрядность (как у литерала `[...]`): `:Array(1,2,3)` → std::vector<int8_t>.
            elemType = arrayElementJoin(raw);
        }
        if (elemType == INVALID_TYPE_ID) {
            elemType = reg.read().getType(type_generic::Any);
        }
        elemType = getUnqualifiedType(elemType);
        // FILLING `... expr ...` в конструкции `:Array(...)`: число позиций неизвестно (в цели
        // размерность не задана) - оставляем dims пустой, кодогенерация выдаст диагностику.
        const EllipsisInfo ell = scanArrayEllipsis(dl);
        const std::vector<uint64_t> dims =
            (ell.form != EllipsisForm::None) ? std::vector<uint64_t>{} : std::vector<uint64_t>{static_cast<uint64_t>(dl.m_body.size())};
        const TypeId arrBase = reg.interner().getOrCreateArrayType(elemType, dims);
        if (arrBase != INVALID_TYPE_ID) {
            // Константность (`:Array^`) - state-квалификатор QualType (withConst), а не поле типа.
            const QualType arr = isConst ? QualType::fromTypeId(arrBase).withConst() : QualType::fromTypeId(arrBase);
            dl.arrayType = arr;
            m_actx.facts().setExprState(&dict_node, arr);
        }
        return;
    }
    const TypeId litType = (target != INVALID_TYPE_ID) ? target : reg.read().getType(type::Dict);
    if (litType != INVALID_TYPE_ID) {
        m_actx.facts().setExprType(&dict_node, litType);
    }
}

// Анализ литерала массива `[1,2:Int8,3,]` / `[1,2,3,]:Int32` / `[[1,2,],[3,4,],]` (вложенный).
// Элементы - ArgNode (имя пустое, значение в m_value, явный тип в m_type). Тип элемента: явная
// аннотация `]:Type` (приоритет), затем явная аннотация элемента (`2:Int8`), затем общий тип
// (если все элементы одного канонического типа - напр. вложенные Array<Elem> для многомерных),
// затем join'элементных типов. Результат - интернированный структурный массив `Array<Elem>`
// (ArrayTypeData); тип сохраняется на узле (DictLiteralNode::arrayType) и в кеше выражений.
// Вложенные массивы-литералы строят многомерный `Array<Array<...>>` (анализ работает;
// «не реализовано» - только на кодогенерации тензора).
void ContainerTyper::analyzeArrayInit(DictLiteralNode& node) {
    {
        TypeSystem& reg0 = m_actx.ctx().types();
        if (node.m_type) {
            const auto ann = m_actx.types().resolveTypeRef(*node.m_type);
            if (ann.has_value() && isTensorTypeId(reg0, *ann)) {
                analyzeTensorInit(node, *ann);
                return;
            }
        }
    }
    TypeSystem& reg = m_actx.ctx().types();
    // Семейство «замыкающего» многоточия (`... expr ...` / `...`): структурные правила проверяются
    // здесь (не зависят от типа-цели); capacity/типы и МАТЕРИАЛИЗАЦИЯ - в coerceArrayInitToTarget.
    const EllipsisInfo ell = scanArrayEllipsis(node);
    if (!validateEllipsis(ell, node, m_actx, "элементы массива")) {
        discardEllipsisElements(node.m_body, true);
    }
    std::vector<TypeId> raw;           // сырые (clearInferred) типы элементов
    std::vector<TypeId> explicitTypes; // типы из явных аннотаций элементов (`2:Int8`)
    bool hasArrayElement = false;      // хотя бы один элемент - массив (многомерный литерал)
    TypeId firstArrayElemType = INVALID_TYPE_ID;
    for (auto& el : node.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        auto& a = static_cast<ArgNode&>(*el);
        // Элемент-многоточие (`... expr ...` / `...`): НЕ явный элемент (число позиций задаёт
        // тип-цель). Операнд Fill всё же участвует в выводе элементного типа (join).
        if (a.m_value && (a.m_value->kind() == ParserToken::Kind::Filling || a.m_value->kind() == ParserToken::Kind::Ellipsis)) {
            if (a.m_value->kind() == ParserToken::Kind::Filling) {
                m_core.analyzeNode(a.m_value);
                const TypeId ft = m_actx.facts().exprType(*a.m_value);
                a.resultType = QualType::fromTypeId(ft);
                if (ft != INVALID_TYPE_ID) {
                    raw.push_back(getUnqualifiedType(ft));
                }
            }
            continue;
        }
        TypeId et = INVALID_TYPE_ID;
        if (a.m_value) {
            m_core.analyzeNode(a.m_value);
            et = m_actx.facts().exprType(*a.m_value);
        }
        // Явная аннотация элемента (`2:Int8`) имеет приоритет над выведенным типом.
        if (a.m_type) {
            if (auto ann = m_actx.types().resolveTypeRef(*a.m_type); ann) {
                et = *ann;
                explicitTypes.push_back(et);
            }
        }
        a.resultType = QualType::fromTypeId(et);
        if (et != INVALID_TYPE_ID) {
            const TypeId ct = getUnqualifiedType(et);
            raw.push_back(ct);
            // Элемент - массив (вложенный литерал/многомерность): детектируем независимо от типа.
            if (reg.read().store().isArrayType(ct)) {
                hasArrayElement = true;
                if (firstArrayElemType == INVALID_TYPE_ID) {
                    firstArrayElemType = ct;
                }
            }
        }
    }
    // Тип элемента: явная аннотация `]:Type` приоритетнее; затем явные аннотации элементов
    // (`2:Int8` типизирует весь массив); затем общий тип (узкая разрядность, arrayElementJoin).
    TypeId elemType = INVALID_TYPE_ID;
    if (node.m_type) {
        if (auto ann = m_actx.types().resolveTypeRef(*node.m_type); ann) {
            elemType = *ann;
        }
    }
    if (elemType == INVALID_TYPE_ID && !explicitTypes.empty()) {
        elemType = getUnqualifiedType(explicitTypes.front());
    }
    if (elemType == INVALID_TYPE_ID) {
        elemType = arrayElementJoin(raw);
    }
    // Многомерный литерал: если есть элементы-массивы, а выведенный элементный тип не массив
    // (гетерогенные внутренние массивы, напр. `[[1,2],[3,4]]`), берём тип первого массива-элемента -
    // так внешний тип становится Array<Array<...>> и кодогенерация выдаёт «не реализовано».
    if (elemType == INVALID_TYPE_ID && hasArrayElement && firstArrayElemType != INVALID_TYPE_ID) {
        elemType = firstArrayElemType;
    }
    if (elemType == INVALID_TYPE_ID) {
        elemType = reg.read().getType(type_generic::Any);
    }
    elemType = getUnqualifiedType(elemType);
    // Многоточие: число позиций задаёт тип-цель - до его применения размерность неизвестна
    // (разворот/материализацию делает coerceArrayInitToTarget).
    const std::vector<uint64_t> dims =
        (ell.form != EllipsisForm::None) ? std::vector<uint64_t>{} : std::vector<uint64_t>{static_cast<uint64_t>(node.m_body.size())};
    const TypeId arrType = reg.interner().getOrCreateArrayType(elemType, dims);
    if (arrType != INVALID_TYPE_ID) {
        node.arrayType = QualType::fromTypeId(arrType);
        m_actx.facts().setExprType(&node, arrType);
    }
}

// Анализ тензорного литерала/конструкции: `[1,2,3,]:Tensor`, `[...]:Float32` в тензорной
// цели, `:Tensor[2,2](:Float32)(1,2,3,4)` и `:Tensor[3](1,2,3)`. Тип элемента: цель →
// хвостовая аннотация → join типов листьев. Форма: цель → вывод из вложенности → [count].
void ContainerTyper::analyzeTensorInit(DictLiteralNode& node, TypeId target) {
    TypeSystem& reg = m_actx.ctx().types();
    const bool sparse = reg.read().store().isSparseTensorType(target) || reg.read().store().desugaredTypeId(target) == reg.read().getType(type::SparseTensor);

    // Анализ значений (вложенные литералы анализируются рекурсивно своими путями).
    for (auto& el : node.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        auto& a = static_cast<ArgNode&>(*el);
        if (a.m_value) {
            m_core.analyzeNode(a.m_value);
            a.resultType = m_actx.facts().exprState(*a.m_value);
        }
        if (a.m_type) {
            if (auto ann = m_actx.types().resolveTypeRef(*a.m_type); ann.has_value()) {
                a.resultType = QualType::fromTypeId(*ann);
            }
        }
    }

    // Элементный тип: цель → хвостовая аннотация `:Tensor(...):Elem` → join листьев.
    TypeId elem = reg.read().store().tensorElementType(target);
    if (elem == reg.read().getType(type_generic::Any)) {
        elem = INVALID_TYPE_ID; // dtype выводится из инициализатора
    }
    if (elem == INVALID_TYPE_ID && node.arrayElementAnnotation) {
        elem = m_actx.types().resolveTypeRef(*node.arrayElementAnnotation).value_or(INVALID_TYPE_ID);
    }
    // Литерал `[...]:Elem` в тензорной цели: `Elem` лежит в m_type (не arrayElementAnnotation),
    // если это не тензорная аннотация.
    if (elem == INVALID_TYPE_ID && node.m_type) {
        const auto ann = m_actx.types().resolveTypeRef(*node.m_type);
        if (ann.has_value() && !isTensorTypeId(reg, *ann)) {
            elem = *ann;
        }
    }
    std::vector<TypeId> raw;
    int64_t count = 0;
    collectTensorLeaves(node, raw, count);
    if (elem == INVALID_TYPE_ID) {
        elem = arrayElementJoin(raw);
    }
    if (elem == INVALID_TYPE_ID) {
        m_actx.ctx().diag().report(Severity::Error, node.range(),
                                   "cannot infer the element type of the tensor initializer; add an explicit element type annotation");
        m_actx.facts().setExprType(&node, INVALID_TYPE_ID);
        return;
    }
    elem = getUnqualifiedType(elem);

    // Форма: цель → вывод из вложенности. `...` требует известного размера и rank==1.
    std::vector<uint64_t> shape = reg.read().store().tensorDimensions(target);
    const EllipsisInfo ell = scanArrayEllipsis(node);
    if (ell.form != EllipsisForm::None) {
        if (shape.size() != 1) {
            m_actx.ctx().diag().report(Severity::Error, node.range(), "tensor filler '...' requires a known one-dimensional shape");
            m_actx.facts().setExprType(&node, INVALID_TYPE_ID);
            return;
        }
        if (!validateEllipsis(ell, node, m_actx, "tensor elements")) {
            discardEllipsisElements(node.m_body, true);
            m_actx.facts().setExprType(&node, INVALID_TYPE_ID);
            return;
        }
        if (!expandArrayEllipsis(node, m_actx, ell, shape.front(), elem)) {
            m_actx.facts().setExprType(&node, INVALID_TYPE_ID);
            return;
        }
        raw.clear();
        count = 0;
        collectTensorLeaves(node, raw, count);
    }
    if (shape.empty()) {
        // Вывод формы из вложенных литералов; нерегулярная вложенность - ошибка.
        std::optional<std::vector<uint64_t>> inferred;
        const auto infer = [&](const auto& self, const DictLiteralNode& n) -> std::optional<std::vector<uint64_t>> {
            uint64_t width = 0;
            std::optional<std::vector<uint64_t>> childShape;
            bool anyArray = false;
            bool anyScalar = false;
            for (const auto& el : n.m_body) {
                if (!el || el->kind() != ParserToken::Kind::ArgNode) {
                    continue;
                }
                const auto& a = static_cast<const ArgNode&>(*el);
                if (!a.m_value) {
                    continue;
                }
                ++width;
                if (isNestedArrayLiteral(a.m_value.get())) {
                    anyArray = true;
                    auto sub = self(self, static_cast<const DictLiteralNode&>(*a.m_value));
                    if (!sub) {
                        return std::nullopt;
                    }
                    if (!childShape) {
                        childShape = std::move(sub);
                    } else if (*childShape != *sub) {
                        return std::nullopt;
                    }
                } else {
                    anyScalar = true;
                }
            }
            if (anyArray && anyScalar) {
                return std::nullopt;
            }
            std::vector<uint64_t> out;
            out.push_back(width);
            if (childShape) {
                out.insert(out.end(), childShape->begin(), childShape->end());
            }
            return out;
        };
        inferred = infer(infer, node);
        if (!inferred) {
            m_actx.ctx().diag().report(Severity::Error, node.range(), "tensor initializer is not rectangular");
            m_actx.facts().setExprType(&node, INVALID_TYPE_ID);
            return;
        }
        shape = std::move(*inferred);
    }

    const int64_t expected = shapeProduct(shape);
    if (expected != count) {
        m_actx.ctx().diag().report(Severity::Error, node.range(), "tensor initializer has {} element(s), but the declared shape requires {}", count, expected);
        m_actx.facts().setExprType(&node, INVALID_TYPE_ID);
        return;
    }
    const TypeId tensorType = reg.interner().getOrCreateTensorType(elem, shape, sparse);
    if (tensorType == INVALID_TYPE_ID) {
        m_actx.facts().setExprType(&node, INVALID_TYPE_ID);
        return;
    }
    node.tensorType = QualType::fromTypeId(tensorType);
    m_actx.facts().setExprType(&node, tensorType);
}

// Литерал без явной аннотации выводит узкую разрядность элемента (int8) - присвоение в
// `vector<Int32>` дало бы `std::vector<int8_t>{...}` и несобираемый C++. Переинтернируем литерал
// с типом элемента цели (только расширение/равенство - сужение оставляет checkAssignmentNarrowing).
void ContainerTyper::coerceArrayInitToTarget(DictLiteralNode& node, TypeId targetType) {
    TypeSystem& reg = m_actx.ctx().types();
    if (isTensorTypeId(reg, targetType)) {
        analyzeTensorInit(node, targetType);
        return;
    }
    if (!reg.read().store().isArrayType(targetType)) {
        return;
    }
    const TypeId targetElem = reg.read().store().arrayElementType(targetType);
    if (targetElem == INVALID_TYPE_ID) {
        return;
    }
    // Многоточие (`... expr ...` / `...`): число позиций задаёт тип-цель (dims) - здесь оно известно,
    // поэтому именно здесь список МАТЕРИАЛИЗУЕТСЯ (разворот в конкретные элементы; кодоген
    // многоточия не видит).
    const EllipsisInfo ell = scanArrayEllipsis(node);
    std::vector<uint64_t> dims;
    if (ell.form != EllipsisForm::None) {
        const auto& targetDims = reg.read().store().arrayDimensions(targetType);
        if (targetDims.empty()) {
            return; // размер неизвестен (диагностику выдаст пост-проход семантики)
        }
        if (!validateEllipsis(ell, node, m_actx, "элементы массива")) {
            discardEllipsisElements(node.m_body, true);
            return;
        }
        if (!expandArrayEllipsis(node, m_actx, ell, targetDims.front(), targetElem)) {
            return; // диагностика выдана (многоточие уже нейтрализовано)
        }
        dims = targetDims;
    } else {
        dims = {static_cast<uint64_t>(node.m_body.size())};
    }
    const TypeId srcElem = (node.arrayType.getTypeId() != INVALID_TYPE_ID) ? reg.read().store().arrayElementType(node.arrayType.getTypeId()) : INVALID_TYPE_ID;
    // Коэрция только при расширении/равенстве элемента; сужение - на совести checkAssignmentNarrowing.
    if (srcElem != INVALID_TYPE_ID && reg.read().store().desugaredTypeId(srcElem) != reg.read().store().desugaredTypeId(targetElem)) {
        const Group sg = getGroup(getTypeKind(srcElem));
        const Group dg = getGroup(getTypeKind(targetElem));
        const bool sNum = (sg == Group::kIntegers || sg == Group::kUnsigned);
        const bool dNum = (dg == Group::kIntegers || dg == Group::kUnsigned);
        if (!sNum || !dNum || sg != dg || getData(getTypeKind(srcElem)) > getData(getTypeKind(targetElem))) {
            return; // не-числовое или сужение - не коэрцируем
        }
    }
    const TypeId arr = reg.interner().getOrCreateArrayType(targetElem, dims);
    if (arr != INVALID_TYPE_ID) {
        node.arrayType = QualType::fromTypeId(arr);
        m_actx.facts().setExprType(&node, arr);
    }
}

// Общий тип элементов массива с сохранением узкой разрядности (в отличие от naturalRuntimeType,
// который для словарей/диапазонов расширяет целые до Int64). Для литералов массивов это даёт
// `[1,2,3,]` → Int8, `[100,300,]` → Int16, `[1.5,2.5,]` → Float64. Если все элементы одного
// канонического типа - берём его; строки не смешиваются с числами; несовместимое → INVALID (Any).
TypeId ContainerTyper::arrayElementJoin(const std::vector<TypeId>& elementTypes) const {
    const TypeReadModel& reg = m_actx.ctx().types();
    if (elementTypes.empty()) {
        return INVALID_TYPE_ID;
    }
    // Все одного канонического типа → он (в т.ч. вложенные однотипные Array<Elem>).
    const TypeId first = reg.store().desugaredTypeId(elementTypes[0]);
    bool allSame = true;
    for (const TypeId t : elementTypes) {
        if (reg.store().desugaredTypeId(t) != first) {
            allSame = false;
            break;
        }
    }
    if (allSame) {
        return first;
    }
    // Числовое продвижение / строки по битовой структуре TypeKind (без строковых имён).
    bool hasStrChar = false, hasStrWide = false, hasOther = false, hasFloat = false, hasInt = false;
    uint8_t maxWidth = 0;
    TypeId widestNumeric = INVALID_TYPE_ID;
    for (const TypeId t : elementTypes) {
        const TypeId c = reg.store().desugaredTypeId(t);
        const Group g = getGroup(getTypeKind(c));
        switch (g) {
        case Group::kLogical:
            hasInt = true; // Bool - вырожденное целое
            break;
        case Group::kIntegers:
        case Group::kUnsigned:
            hasInt = true;
            if (getData(getTypeKind(c)) > maxWidth) {
                maxWidth = getData(getTypeKind(c));
                widestNumeric = c;
            }
            break;
        case Group::kNumbers:
            hasFloat = true;
            if (getData(getTypeKind(c)) > maxWidth) {
                maxWidth = getData(getTypeKind(c));
                widestNumeric = c;
            }
            break;
        case Group::kStrChar:
            hasStrChar = true;
            break;
        case Group::kStrWide:
            hasStrWide = true;
            break;
        default:
            hasOther = true;
            break;
        }
    }
    const bool hasStr = hasStrChar || hasStrWide;
    if (hasStr) {
        // строки не смешиваются с числами/прочим
        if (hasInt || hasFloat || hasOther) {
            return INVALID_TYPE_ID;
        }
        return hasStrWide ? reg.store().desugaredTypeId(reg.getType(type::StrWide)) : reg.store().desugaredTypeId(reg.getType(type::StrChar));
    }
    if (hasOther) {
        return INVALID_TYPE_ID;
    }
    if (hasFloat) {
        // Наибольший float (Float64 при наличии); иначе Double.
        return (widestNumeric != INVALID_TYPE_ID && getGroup(getTypeKind(widestNumeric)) == Group::kNumbers)
                   ? widestNumeric
                   : reg.store().desugaredTypeId(reg.getType(type::Double));
    }
    if (hasInt) {
        // Наибольшая целая разрядность (Int8/16/32/64, UInt); только Bool → Int8.
        return widestNumeric != INVALID_TYPE_ID ? widestNumeric : reg.store().desugaredTypeId(reg.getType(type::Int8));
    }
    return INVALID_TYPE_ID;
}

// Анализ литерала диапазона `start..stop` / `start..stop..step`. Универсальный тип `:Range`
// (как `:Dict`): тип ВЫРАЖЕНИЯ - `:Range`, а элементный тип (Int/Rational/Float/Any) выводится
// join'ом типов start/stop/step и параметризует `trust::Range<Elem>` при кодогенерации.
// Элементы должны быть арифметическими (Int/UInt/Float/Rational/Bool) или Any; строки и прочие
// - диагностика. Элементный тип: Rational при любом рациональном операнде, иначе Double при
// любом float, иначе Int64/UInt64/Bool; несовместимое/неизвестное → INVALID (Any).
void ContainerTyper::analyzeRangeExpr(RangeExpr& range_node) {
    TypeSystem& reg = m_actx.ctx().types();
    std::vector<TypeId> types;
    types.reserve(range_node.m_body.size());
    for (std::size_t i = 0; i < range_node.m_body.size(); ++i) {
        auto& child = range_node.m_body[i];
        if (!child) {
            continue;
        }
        m_core.analyzeNode(child);
        TypeId t = m_actx.facts().exprType(*child);
        // Явная аннотация типа операнда (`stop:Type`, напр. `0..100:Rational`) имеет приоритет
        // над выведенным типом: грамматика кладёт её в m_type терма-операнда, конвертер - в
        // RangeExpr::operandTypes. Аннотация `:Rational` делает элементный тип Rational.
        if (i < range_node.operandTypes.size() && range_node.operandTypes[i]) {
            if (auto ann = m_actx.types().resolveTypeRef(*range_node.operandTypes[i]); ann) {
                t = *ann;
            }
        }
        types.push_back(t);
    }
    // Валидация + join элементных типов (рациональный операнд всегда даёт Rational).
    bool hasRational = false, hasDouble = false, hasInt = false, hasUInt = false, hasBool = false;
    bool bad = false;
    for (const TypeId t : types) {
        const TypeId ct = reg.read().store().desugaredTypeId(t);
        const Group g = getGroup(getTypeKind(ct));
        switch (g) {
        case Group::kArbitraryPrecision:
            hasRational = true;
            break;
        case Group::kNumbers:
            hasDouble = true;
            break;
        case Group::kIntegers:
            hasInt = true;
            break;
        case Group::kUnsigned:
            hasUInt = true;
            break;
        case Group::kLogical:
            hasBool = true;
            break;
        case Group::kAny:
        case Group::kTemplateParam:
            break; // универсальный диапазон (Any)
        default:
            bad = true;
            break;
        }
    }
    if (bad) {
        m_actx.ctx().diag().report(Severity::Error, range_node.range(), "range operands must be arithmetic (Int/Float/Rational) or Any");
    }
    if (hasRational) {
        range_node.elementType = QualType::fromTypeId(reg.read().store().desugaredTypeId(reg.read().getType(type::Rational)));
    } else if (hasDouble) {
        range_node.elementType = QualType::fromTypeId(reg.read().store().desugaredTypeId(reg.read().getType(type::Double)));
    } else if (hasInt) {
        range_node.elementType = QualType::fromTypeId(reg.read().store().desugaredTypeId(reg.read().getType(type::Int64)));
    } else if (hasUInt) {
        range_node.elementType = QualType::fromTypeId(reg.read().store().desugaredTypeId(reg.read().getType(type::UInt64)));
    } else if (hasBool) {
        range_node.elementType = QualType::fromTypeId(reg.read().store().desugaredTypeId(reg.read().getType(type::Bool)));
    } else {
        range_node.elementType = QualType{}; // Any
    }
    // Тип выражения - параметризованный структурный Range<Elem> (элементный тип Elem):
    // литерал `1..10` → Range<Int64>, `0..100:Rational` → Range<Rational>, иначе Range<Any>.
    TypeId elemT = range_node.elementType.getTypeId();
    if (elemT == INVALID_TYPE_ID) {
        elemT = reg.read().getType(type_generic::Any);
    }
    const TypeId rangeT = reg.interner().getOrCreateRangeType(elemT);
    if (rangeT != INVALID_TYPE_ID) {
        m_actx.facts().setExprType(&range_node, rangeT);
    }
}

// Статическая размерность объекта: для литерала словаря - число элементов; для переменной -
// свойство dims символа (из инициализатора-литерала). -1 = неизвестна.
int64_t ContainerTyper::dictSizeOf(const AstNodeBase* obj) const {
    if (!obj) {
        return -1;
    }
    if (is_collection_literal_kind(obj->kind())) {
        return static_cast<int64_t>(static_cast<const Sequence&>(*obj).m_body.size());
    }
    if (obj->kind() == ParserToken::Kind::Ident) {
        if (const Symbol* s = m_actx.symbols().resolve(requiredText(*obj))) {
            return s->dims;
        }
    }
    return -1;
}

// Тип значения элемента словаря по его узлу (литерал → единый решатель/текст; иначе exprType).
QualType ContainerTyper::dictElementType(const AstNodeBase* valueNode) const {
    if (!valueNode) {
        return QualType{};
    }
    if (is_literal_kind(valueNode->kind())) {
        const auto& lit = static_cast<const Literal&>(*valueNode);
        // Авторитетная типизация + диагностика литерала — в typeExpr (annotatedLiteralType +
        // (этот узел всегда обходится затем); здесь — ТОЛЬКО чистый выбор типа через единый
        // решатель annotatedLiteralType (без репорта, без fallback на «сырой» номинал аннотации:
        // невалидная аннотация → INVALID, а не Bool/Int8 по имени типа).
        if (lit.typeAnnotation) {
            const LiteralAnnotResult r = annotatedLiteralType(lit, m_actx.types().resolveTypeRef(*lit.typeAnnotation), m_actx.ctx().types());
            return (r.type != INVALID_TYPE_ID) ? QualType::fromTypeId(r.type).withInferred() : QualType{};
        }
        // Тип значения литерала - по его тексту (минимальный знаковый Int / Float / ...).
        return QualType::fromTypeId(literalType(lit, m_actx.ctx().types())).withInferred();
    }
    return m_actx.facts().exprState(*valueNode);
}

// Типы элементов словаря-источника ПО ИНДЕКСУ (для вывода типов целей деструктуризации,
// аналогично кортежу): литерал → тип каждого элемента m_body; переменная → dictFieldTypes.
// Возвращаются СЫРЫЕ типы (с битом inferred у литералов) - по нему определяется нетипизированный
// словарь. Неизвестный/неприменимый элемент → INVALID_TYPE_ID. Пустой вектор - типы недоступны.
std::vector<QualType> ContainerTyper::dictElementTypes(const AstNodeBase* src) const {
    std::vector<QualType> result;
    if (!src) {
        return result;
    }
    if (is_collection_literal_kind(src->kind())) {
        const auto& dl = static_cast<const Sequence&>(*src);
        for (const auto& el : dl.m_body) {
            std::string name;
            const AstNodeBase* valueNode = nullptr;
            collectionElementNameValue(el.get(), name, valueNode);
            (void)name;
            result.push_back(dictElementType(valueNode));
        }
        return result;
    }
    if (src->kind() == ParserToken::Kind::Ident) {
        if (const Symbol* s = m_actx.symbols().resolve(requiredText(*src))) {
            for (const auto& [name, ft] : s->dictFieldTypes) {
                (void)name;
                result.push_back(ft);
            }
        }
    }
    return result;
}

// «Естественный» runtime-тип элемента словаря (как хранит Dict): integers → Int64,
// unsigned → UInt64, numbers(float) → Double, logical → Bool, StrChar/StrWide → соответствующий.
// Нечисловой/неизвестный → INVALID_TYPE_ID (→ Any). Классификация - по битовой структуре TypeKind
// (getTypeKind/getGroup), а НЕ по строковым именам типов.
TypeId ContainerTyper::naturalRuntimeType(TypeId nominal) const {
    const TypeReadModel& reg = m_actx.ctx().types();
    if (nominal == INVALID_TYPE_ID) {
        return INVALID_TYPE_ID;
    }
    const Group g = getGroup(getTypeKind(reg.store().desugaredTypeId(nominal)));
    switch (g) {
    case Group::kIntegers:
        return reg.store().desugaredTypeId(reg.getType(type::Int64));
    case Group::kUnsigned:
        return reg.store().desugaredTypeId(reg.getType(type::UInt64));
    case Group::kNumbers:
        return reg.store().desugaredTypeId(reg.getType(type::Double));
    case Group::kLogical:
        return reg.store().desugaredTypeId(reg.getType(type::Bool));
    case Group::kStrChar:
        return reg.store().desugaredTypeId(reg.getType(type::StrChar));
    case Group::kStrWide:
        return reg.store().desugaredTypeId(reg.getType(type::StrWide));
    default:
        return INVALID_TYPE_ID;
    }
}

// JOIN (максимальный) элементных runtime-типов для widening В ЦИКЛЕ: Bool+Int → Int64,
// любой float → Double, однородные строки → Str; несовместимое/неизвестное → INVALID (Any).
// Классификация - по Group (TypeKind), без строковых имён типов.
TypeId ContainerTyper::joinElementTypes(const std::vector<TypeId>& naturalized) const {
    const TypeReadModel& reg = m_actx.ctx().types();
    bool hasDouble = false, hasInt = false, hasBool = false, hasUInt = false, hasStrChar = false, hasStrWide = false;
    for (const TypeId et : naturalized) {
        const Group g = getGroup(getTypeKind(reg.store().desugaredTypeId(et)));
        switch (g) {
        case Group::kIntegers:
            hasInt = true;
            break;
        case Group::kUnsigned:
            hasUInt = true;
            break;
        case Group::kNumbers:
            hasDouble = true;
            break;
        case Group::kLogical:
            hasBool = true;
            break;
        case Group::kStrChar:
            hasStrChar = true;
            break;
        case Group::kStrWide:
            hasStrWide = true;
            break;
        default:
            return INVALID_TYPE_ID; // неизвестный элемент → Any
        }
    }
    const bool hasNumeric = hasInt || hasBool || hasUInt || hasDouble;
    if (hasStrChar || hasStrWide) {
        // строки не смешиваются с числами
        if (hasNumeric) {
            return INVALID_TYPE_ID;
        }
        return hasStrWide ? reg.store().desugaredTypeId(reg.getType(type::StrWide)) : reg.store().desugaredTypeId(reg.getType(type::StrChar));
    }
    if (hasDouble) {
        return reg.store().desugaredTypeId(reg.getType(type::Double));
    }
    if (hasInt) {
        return reg.store().desugaredTypeId(reg.getType(type::Int64));
    }
    if (hasUInt) {
        return reg.store().desugaredTypeId(reg.getType(type::UInt64));
    }
    if (hasBool) {
        return reg.store().desugaredTypeId(reg.getType(type::Bool));
    }
    return INVALID_TYPE_ID; // пусто
}

// Тип поля объекта по ключу доступа (имя/статический индекс для MemberAccess, индекс для
// ArrayAccess). INVALID - тип неизвестен (гетерогенный/динамический) → Any.
TypeId ContainerTyper::dictFieldTypeOf(const Binary& access) const {
    const AstNodeBase* obj = access.m_left.get();
    if (!obj) {
        return INVALID_TYPE_ID;
    }
    std::vector<std::pair<std::string, TypeId>> fields;
    if (is_collection_literal_kind(obj->kind())) {
        const auto& dl = static_cast<const Sequence&>(*obj);
        for (const auto& el : dl.m_body) {
            std::string n;
            const AstNodeBase* v = nullptr;
            collectionElementNameValue(el.get(), n, v);
            fields.emplace_back(n, v ? dictElementType(v).getTypeId() : INVALID_TYPE_ID);
        }
    } else if (obj->kind() == ParserToken::Kind::Ident) {
        if (const Symbol* s = m_actx.symbols().resolve(requiredText(*obj))) {
            fields.reserve(s->dictFieldTypes.size());
            for (const auto& [n, st] : s->dictFieldTypes) {
                fields.emplace_back(n, st.getTypeId());
            }
        }
    }
    if (fields.empty()) {
        return INVALID_TYPE_ID;
    }
    if (access.kind() == ParserToken::Kind::MemberAccess && access.m_right) {
        if (access.m_right->kind() == ParserToken::Kind::IntLiteral) {
            unsigned long long idx = 0;
            if (parseDecimalUInt(requiredText(*access.m_right), idx) && idx < fields.size()) {
                return fields[idx].second;
            }
        } else {
            const std::string name = std::string(requiredText(*access.m_right));
            for (const auto& [n, t] : fields) {
                if (n == name) {
                    return t;
                }
            }
        }
    } else if (access.kind() == ParserToken::Kind::ArrayAccess && access.m_right && access.m_right->kind() == ParserToken::Kind::IntLiteral) {
        unsigned long long idx = 0;
        if (parseDecimalUInt(requiredText(*access.m_right), idx) && idx < fields.size()) {
            return fields[idx].second;
        }
    }
    return INVALID_TYPE_ID;
}

} // namespace trust
