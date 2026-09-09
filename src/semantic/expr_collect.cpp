// Generated: src/semantic/expr_typer.cpp
#include "semantic/expr_typer.hpp"
#include "semantic/name_resolution.hpp"
#include "semantic/analysis_common.hpp"
#include "semantic/ellipsis.hpp"
#include "semantic/format_check.hpp"
#include "semantic/solver.hpp"
#include "analysis/symbol_table.hpp"
#include "semantic/type_inference.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "semantic/ref_kind.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/promotion.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "utils/strings.hpp"
#include <algorithm>
#include <format>
#include <string>

namespace trust {

// Анализ литерала словаря. Контракт: все элементы m_body - ArgNode (имя в text(), значение в
// m_value), строятся из канонических пар грамматики `args` (term_to_ast::visit_DICT).
// Значение анализируем полностью (резолв/типизация); имя-метку НЕ резолвим как переменную и
// НЕ регистрируем в таблице символов. Тип значения сохраняем на элементе (ArgNode::resultType
// из exprType) - единый источник для кодогенерации TypedValue (не только Literal::typeId).
void ExprTyper::analyzeDictLiteral(Sequence& dict_node) {
    // Enum/Variant-объявление (ПОСТФИКС `(...):Enum`/`(...):Variant`): это правая часть `::=`,
    // обрабатывается analyzeTypeDecl (analyzeEnumDecl/analyzeVariantDecl); как обычный словарь НЕ
    // анализируется (иначе голые члены `B` резолвились бы как переменные). Префикс `:Enum(...)`
    // - НЕ объявление (type-call): голые аргументы = значения, резолвятся как обычно.
    const auto& dl0 = static_cast<const DictLiteralNode&>(dict_node);
    if (!dl0.prefix && dl0.m_type) {
        const std::string ann = std::string(dl0.m_type->text());
        if (ann == type_category::Enum || ann == type_category::Variant) {
            return;
        }
    }
    for (auto& el : dict_node.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        auto& a = static_cast<ArgNode&>(*el);
        if (!a.m_value) {
            continue;
        }
        m_core.analyzeNode(a.m_value);
        a.resultType = m_actx.exprType(*a.m_value);
    }
    // Тип литерала определяется по АННОТАЦИИ m_type через реестр типов (никаких строк/enum):
    //   - если аннотация резолвится в тип Tuple → kind узла меняется на Tuple, а тип выражения -
    //     на интернированный структурный кортеж (TupleTypeData; источник `t.name`/`t.0`);
    //   - иначе (аннотация-каст/конструктор или её нет) - тип литерала = резолвленной аннотации
    //     (скаляр/класс) или универсального словаря Dict.
    auto& dl = static_cast<DictLiteralNode&>(dict_node);
    TypeRegistry& reg = m_actx.ctx().types();
    TypeId target = INVALID_TYPE_ID;
    if (dl.m_type) {
        target = m_actx.resolveTypeRef(*dl.m_type).value_or(INVALID_TYPE_ID);
    }
    const TypeId tupleId = reg.getType(type_category::Tuple);
    if (target != INVALID_TYPE_ID && reg.getCanonicalTypeId(target) == tupleId) {
        dl.setKind(ParserToken::Kind::Tuple);
        std::vector<std::pair<std::string, TypeId>> elems;
        elems.reserve(dl.m_body.size());
        for (const auto& el : dl.m_body) {
            if (!el || el->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            const auto& a = static_cast<const ArgNode&>(*el);
            elems.emplace_back(std::string(a.text()), clearFlag(a.resultType, SymbolFlag::Inferred));
        }
        const TypeId t = reg.getOrCreateTupleType(std::move(elems));
        if (t != INVALID_TYPE_ID) {
            m_actx.setExprType(&dict_node, t);
        }
        return;
    }
    // Конструкция шаблон-типа-массива `:vector<Int32>(...)` (нативный шаблон → интернированный
    // Array<Elem>): элемент уже известен из шаблона - фиксируем arrayType для emitArrayLiteral
    // (`std::vector<Elem>{...}`), не выводя элемент по значениям.
    if (target != INVALID_TYPE_ID && reg.isArrayType(target)) {
        dl.arrayType = target;
        m_actx.setExprType(&dict_node, target);
        return;
    }
    // Конструкция массива `:Array(...)` / `:Array^(...)`: префиксная форма с аннотацией `:Array`.
    // Элементный тип выводится из значений (join), как у литерала `[...]` (analyzeArrayInit);
    // интернируем структурный Array<Elem> (тип сохраняем в DictLiteralNode::arrayType).
    // Константность контейнера `:Array^` (attr::ReadOnly на m_type) → isConst (std::array).
    const TypeId arrayId = reg.getType(type::Array);
    if (target != INVALID_TYPE_ID && reg.getCanonicalTypeId(target) == arrayId) {
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
            if (auto ann = m_actx.resolveTypeRef(*dl.arrayElementAnnotation); ann) {
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
                TypeId et = a.resultType;
                if (et == INVALID_TYPE_ID && a.m_value) {
                    et = m_actx.exprType(*a.m_value);
                }
                if (et != INVALID_TYPE_ID) {
                    raw.push_back(clearFlag(et, SymbolFlag::Inferred));
                }
            }
            // Узкая разрядность (как у литерала `[...]`): `:Array(1,2,3)` → std::vector<int8_t>.
            elemType = arrayElementJoin(raw);
        }
        if (elemType == INVALID_TYPE_ID) {
            elemType = reg.getType(type_generic::Any);
        }
        elemType = clearFlag(elemType, SymbolFlag::Inferred);
        // FILLING `... expr ...` в конструкции `:Array(...)`: число позиций неизвестно (в цели
        // размерность не задана) - оставляем dims пустой, кодогенерация выдаст диагностику.
        const EllipsisInfo ell = scanArrayEllipsis(dl);
        const std::vector<uint64_t> dims =
            (ell.form != EllipsisForm::None) ? std::vector<uint64_t>{} : std::vector<uint64_t>{static_cast<uint64_t>(dl.m_body.size())};
        const TypeId arrBase = reg.getOrCreateArrayType(elemType, dims);
        if (arrBase != INVALID_TYPE_ID) {
            // Константность (`:Array^`) - kConstFlag-бит в TypeId (withConst), а не поле типа.
            const TypeId arr = isConst ? setFlag(arrBase, SymbolFlag::Const) : arrBase;
            dl.arrayType = arr;
            m_actx.setExprType(&dict_node, arr);
        }
        return;
    }
    const TypeId litType = (target != INVALID_TYPE_ID) ? target : reg.getType(type::Dict);
    if (litType != INVALID_TYPE_ID) {
        m_actx.setExprType(&dict_node, litType);
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
void ExprTyper::analyzeArrayInit(DictLiteralNode& node) {
    TypeRegistry& reg = m_actx.ctx().types();
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
                const TypeId ft = m_actx.exprType(*a.m_value);
                a.resultType = ft;
                if (ft != INVALID_TYPE_ID) {
                    raw.push_back(clearFlag(ft, SymbolFlag::Inferred));
                }
            }
            continue;
        }
        TypeId et = INVALID_TYPE_ID;
        if (a.m_value) {
            m_core.analyzeNode(a.m_value);
            et = m_actx.exprType(*a.m_value);
        }
        // Явная аннотация элемента (`2:Int8`) имеет приоритет над выведенным типом.
        if (a.m_type) {
            if (auto ann = m_actx.resolveTypeRef(*a.m_type); ann) {
                et = *ann;
                explicitTypes.push_back(et);
            }
        }
        a.resultType = et;
        if (et != INVALID_TYPE_ID) {
            const TypeId ct = clearFlag(et, SymbolFlag::Inferred);
            raw.push_back(ct);
            // Элемент - массив (вложенный литерал/многомерность): детектируем независимо от типа.
            if (reg.isArrayType(ct)) {
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
        if (auto ann = m_actx.resolveTypeRef(*node.m_type); ann) {
            elemType = *ann;
        }
    }
    if (elemType == INVALID_TYPE_ID && !explicitTypes.empty()) {
        elemType = clearFlag(explicitTypes.front(), SymbolFlag::Inferred);
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
        elemType = reg.getType(type_generic::Any);
    }
    elemType = clearFlag(elemType, SymbolFlag::Inferred);
    // Многоточие: число позиций задаёт тип-цель - до его применения размерность неизвестна
    // (разворот/материализацию делает coerceArrayInitToTarget).
    const std::vector<uint64_t> dims =
        (ell.form != EllipsisForm::None) ? std::vector<uint64_t>{} : std::vector<uint64_t>{static_cast<uint64_t>(node.m_body.size())};
    const TypeId arrType = reg.getOrCreateArrayType(elemType, dims);
    if (arrType != INVALID_TYPE_ID) {
        node.arrayType = arrType;
        m_actx.setExprType(&node, arrType);
    }
}

// Коэрция элемента литерала массива `[1,2,3,]` к типу элемента Array-цели (`v: vector<Int32>`).
// Литерал без явной аннотации выводит узкую разрядность элемента (int8) - присвоение в
// `vector<Int32>` дало бы `std::vector<int8_t>{...}` и несобираемый C++. Переинтернируем литерал
// с типом элемента цели (только расширение/равенство - сужение оставляет checkAssignmentNarrowing).
void ExprTyper::coerceArrayInitToTarget(DictLiteralNode& node, TypeId targetType) {
    TypeRegistry& reg = m_actx.ctx().types();
    if (!reg.isArrayType(targetType)) {
        return;
    }
    const TypeId targetElem = reg.arrayElementType(targetType);
    if (targetElem == INVALID_TYPE_ID) {
        return;
    }
    // Многоточие (`... expr ...` / `...`): число позиций задаёт тип-цель (dims) - здесь оно известно,
    // поэтому именно здесь список МАТЕРИАЛИЗУЕТСЯ (разворот в конкретные элементы; кодоген
    // многоточия не видит).
    const EllipsisInfo ell = scanArrayEllipsis(node);
    std::vector<uint64_t> dims;
    if (ell.form != EllipsisForm::None) {
        const auto& targetDims = reg.arrayDimensions(targetType);
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
    const TypeId srcElem = (node.arrayType != INVALID_TYPE_ID) ? reg.arrayElementType(node.arrayType) : INVALID_TYPE_ID;
    // Коэрция только при расширении/равенстве элемента; сужение - на совести checkAssignmentNarrowing.
    if (srcElem != INVALID_TYPE_ID && reg.getCanonicalTypeId(srcElem) != reg.getCanonicalTypeId(targetElem)) {
        const Group sg = getGroup(getKindFromId(srcElem));
        const Group dg = getGroup(getKindFromId(targetElem));
        const bool sNum = (sg == Group::kIntegers || sg == Group::kUnsigned);
        const bool dNum = (dg == Group::kIntegers || dg == Group::kUnsigned);
        if (!sNum || !dNum || sg != dg || getData(getKindFromId(srcElem)) > getData(getKindFromId(targetElem))) {
            return; // не-числовое или сужение - не коэрцируем
        }
    }
    const TypeId arr = reg.getOrCreateArrayType(targetElem, dims);
    if (arr != INVALID_TYPE_ID) {
        node.arrayType = arr;
        m_actx.setExprType(&node, arr);
    }
}

// Общий тип элементов массива с сохранением узкой разрядности (в отличие от naturalRuntimeType,
// который для словарей/диапазонов расширяет целые до Int64). Для литералов массивов это даёт
// `[1,2,3,]` → Int8, `[100,300,]` → Int16, `[1.5,2.5,]` → Float64. Если все элементы одного
// канонического типа - берём его; строки не смешиваются с числами; несовместимое → INVALID (Any).
TypeId ExprTyper::arrayElementJoin(const std::vector<TypeId>& elementTypes) const {
    const TypeRegistry& reg = m_actx.ctx().types();
    if (elementTypes.empty()) {
        return INVALID_TYPE_ID;
    }
    // Все одного канонического типа → он (в т.ч. вложенные однотипные Array<Elem>).
    const TypeId first = reg.getCanonicalTypeId(elementTypes[0]);
    bool allSame = true;
    for (const TypeId t : elementTypes) {
        if (reg.getCanonicalTypeId(t) != first) {
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
        const TypeId c = reg.getCanonicalTypeId(t);
        const Group g = getGroup(getKindFromId(c));
        switch (g) {
        case Group::kLogical:
            hasInt = true; // Bool - вырожденное целое
            break;
        case Group::kIntegers:
        case Group::kUnsigned:
            hasInt = true;
            if (getData(getKindFromId(c)) > maxWidth) {
                maxWidth = getData(getKindFromId(c));
                widestNumeric = c;
            }
            break;
        case Group::kNumbers:
            hasFloat = true;
            if (getData(getKindFromId(c)) > maxWidth) {
                maxWidth = getData(getKindFromId(c));
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
        return hasStrWide ? reg.getCanonicalTypeId(reg.getType(type::StrWide)) : reg.getCanonicalTypeId(reg.getType(type::StrChar));
    }
    if (hasOther) {
        return INVALID_TYPE_ID;
    }
    if (hasFloat) {
        // Наибольший float (Float64 при наличии); иначе Double.
        return (widestNumeric != INVALID_TYPE_ID && getGroup(getKindFromId(widestNumeric)) == Group::kNumbers)
                   ? widestNumeric
                   : reg.getCanonicalTypeId(reg.getType(type::Double));
    }
    if (hasInt) {
        // Наибольшая целая разрядность (Int8/16/32/64, UInt); только Bool → Int8.
        return widestNumeric != INVALID_TYPE_ID ? widestNumeric : reg.getCanonicalTypeId(reg.getType(type::Int8));
    }
    return INVALID_TYPE_ID;
}

// Анализ литерала диапазона `start..stop` / `start..stop..step`. Универсальный тип `:Range`
// (как `:Dict`): тип ВЫРАЖЕНИЯ - `:Range`, а элементный тип (Int/Rational/Float/Any) выводится
// join'ом типов start/stop/step и параметризует `trust::Range<Elem>` при кодогенерации.
// Элементы должны быть арифметическими (Int/UInt/Float/Rational/Bool) или Any; строки и прочие
// - диагностика. Элементный тип: Rational при любом рациональном операнде, иначе Double при
// любом float, иначе Int64/UInt64/Bool; несовместимое/неизвестное → INVALID (Any).
void ExprTyper::analyzeRangeExpr(RangeExpr& range_node) {
    TypeRegistry& reg = m_actx.ctx().types();
    std::vector<TypeId> types;
    types.reserve(range_node.m_body.size());
    for (std::size_t i = 0; i < range_node.m_body.size(); ++i) {
        auto& child = range_node.m_body[i];
        if (!child) {
            continue;
        }
        m_core.analyzeNode(child);
        TypeId t = m_actx.exprType(*child);
        // Явная аннотация типа операнда (`stop:Type`, напр. `0..100:Rational`) имеет приоритет
        // над выведенным типом: грамматика кладёт её в m_type терма-операнда, конвертер - в
        // RangeExpr::operandTypes. Аннотация `:Rational` делает элементный тип Rational.
        if (i < range_node.operandTypes.size() && range_node.operandTypes[i]) {
            if (auto ann = m_actx.resolveTypeRef(*range_node.operandTypes[i]); ann) {
                t = *ann;
            }
        }
        types.push_back(t);
    }
    // Валидация + join элементных типов (рациональный операнд всегда даёт Rational).
    bool hasRational = false, hasDouble = false, hasInt = false, hasUInt = false, hasBool = false;
    bool bad = false;
    for (const TypeId t : types) {
        const TypeId ct = reg.getCanonicalTypeId(t);
        const Group g = getGroup(getKindFromId(ct));
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
        range_node.elementType = reg.getCanonicalTypeId(reg.getType(type::Rational));
    } else if (hasDouble) {
        range_node.elementType = reg.getCanonicalTypeId(reg.getType(type::Double));
    } else if (hasInt) {
        range_node.elementType = reg.getCanonicalTypeId(reg.getType(type::Int64));
    } else if (hasUInt) {
        range_node.elementType = reg.getCanonicalTypeId(reg.getType(type::UInt64));
    } else if (hasBool) {
        range_node.elementType = reg.getCanonicalTypeId(reg.getType(type::Bool));
    } else {
        range_node.elementType = INVALID_TYPE_ID; // Any
    }
    // Тип выражения - параметризованный структурный Range<Elem> (элементный тип Elem):
    // литерал `1..10` → Range<Int64>, `0..100:Rational` → Range<Rational>, иначе Range<Any>.
    TypeId elemT = range_node.elementType;
    if (elemT == INVALID_TYPE_ID) {
        elemT = reg.getType(type_generic::Any);
    }
    const TypeId rangeT = reg.getOrCreateRangeType(elemT);
    if (rangeT != INVALID_TYPE_ID) {
        m_actx.setExprType(&range_node, rangeT);
    }
}

// Статическая размерность объекта: для литерала словаря - число элементов; для переменной -
// свойство dims символа (из инициализатора-литерала). -1 = неизвестна.
int64_t ExprTyper::dictSizeOf(const AstNodeBase* obj) const {
    if (!obj) {
        return -1;
    }
    if (is_collection_literal_kind(obj->kind())) {
        return static_cast<int64_t>(static_cast<const Sequence&>(*obj).m_body.size());
    }
    if (obj->kind() == ParserToken::Kind::Ident) {
        if (const Symbol* s = m_actx.symbols().resolve(obj->text())) {
            return s->dims;
        }
    }
    return -1;
}

// Тип значения элемента словаря по его узлу (литерал → единый решатель/текст; иначе exprType).
TypeId ExprTyper::dictElementType(const AstNodeBase* valueNode) const {
    if (!valueNode) {
        return INVALID_TYPE_ID;
    }
    if (is_literal_kind(valueNode->kind())) {
        const auto& lit = static_cast<const Literal&>(*valueNode);
        // Авторитетная типизация + диагностика литерала — в typeExpr (annotatedLiteralType +
        // (этот узел всегда обходится затем); здесь — ТОЛЬКО чистый выбор типа через единый
        // решатель annotatedLiteralType (без репорта, без fallback на «сырой» номинал аннотации:
        // невалидная аннотация → INVALID, а не Bool/Int8 по имени типа).
        if (lit.typeAnnotation) {
            const LiteralAnnotResult r = annotatedLiteralType(lit, m_actx.resolveTypeRef(*lit.typeAnnotation), m_actx.ctx().types());
            return (r.type != INVALID_TYPE_ID) ? setFlag(r.type, SymbolFlag::Inferred) : INVALID_TYPE_ID;
        }
        // Тип значения литерала - по его тексту (минимальный знаковый Int / Float / ...).
        return setFlag(literalType(lit, m_actx.ctx().types()), SymbolFlag::Inferred);
    }
    return m_actx.exprType(*valueNode);
}

// Типы элементов словаря-источника ПО ИНДЕКСУ (для вывода типов целей деструктуризации,
// аналогично кортежу): литерал → тип каждого элемента m_body; переменная → dictFieldTypes.
// Возвращаются СЫРЫЕ типы (с битом inferred у литералов) - по нему определяется нетипизированный
// словарь. Неизвестный/неприменимый элемент → INVALID_TYPE_ID. Пустой вектор - типы недоступны.
std::vector<TypeId> ExprTyper::dictElementTypes(const AstNodeBase* src) const {
    std::vector<TypeId> result;
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
        if (const Symbol* s = m_actx.symbols().resolve(src->text())) {
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
// (getKindFromId/getGroup), а НЕ по строковым именам типов.
TypeId ExprTyper::naturalRuntimeType(TypeId nominal) const {
    const TypeRegistry& reg = m_actx.ctx().types();
    if (nominal == INVALID_TYPE_ID) {
        return INVALID_TYPE_ID;
    }
    const Group g = getGroup(getKindFromId(reg.getCanonicalTypeId(nominal)));
    switch (g) {
    case Group::kIntegers:
        return reg.getCanonicalTypeId(reg.getType(type::Int64));
    case Group::kUnsigned:
        return reg.getCanonicalTypeId(reg.getType(type::UInt64));
    case Group::kNumbers:
        return reg.getCanonicalTypeId(reg.getType(type::Double));
    case Group::kLogical:
        return reg.getCanonicalTypeId(reg.getType(type::Bool));
    case Group::kStrChar:
        return reg.getCanonicalTypeId(reg.getType(type::StrChar));
    case Group::kStrWide:
        return reg.getCanonicalTypeId(reg.getType(type::StrWide));
    default:
        return INVALID_TYPE_ID;
    }
}

// JOIN (максимальный) элементных runtime-типов для widening В ЦИКЛЕ: Bool+Int → Int64,
// любой float → Double, однородные строки → Str; несовместимое/неизвестное → INVALID (Any).
// Классификация - по Group (TypeKind), без строковых имён типов.
TypeId ExprTyper::joinElementTypes(const std::vector<TypeId>& naturalized) const {
    const TypeRegistry& reg = m_actx.ctx().types();
    bool hasDouble = false, hasInt = false, hasBool = false, hasUInt = false, hasStrChar = false, hasStrWide = false;
    for (const TypeId et : naturalized) {
        const Group g = getGroup(getKindFromId(reg.getCanonicalTypeId(et)));
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
        return hasStrWide ? reg.getCanonicalTypeId(reg.getType(type::StrWide)) : reg.getCanonicalTypeId(reg.getType(type::StrChar));
    }
    if (hasDouble) {
        return reg.getCanonicalTypeId(reg.getType(type::Double));
    }
    if (hasInt) {
        return reg.getCanonicalTypeId(reg.getType(type::Int64));
    }
    if (hasUInt) {
        return reg.getCanonicalTypeId(reg.getType(type::UInt64));
    }
    if (hasBool) {
        return reg.getCanonicalTypeId(reg.getType(type::Bool));
    }
    return INVALID_TYPE_ID; // пусто
}

// Тип поля объекта по ключу доступа (имя/статический индекс для MemberAccess, индекс для
// ArrayAccess). INVALID - тип неизвестен (гетерогенный/динамический) → Any.
TypeId ExprTyper::dictFieldTypeOf(const Binary& access) const {
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
            fields.emplace_back(n, v ? dictElementType(v) : INVALID_TYPE_ID);
        }
    } else if (obj->kind() == ParserToken::Kind::Ident) {
        if (const Symbol* s = m_actx.symbols().resolve(obj->text())) {
            fields = s->dictFieldTypes;
        }
    }
    if (fields.empty()) {
        return INVALID_TYPE_ID;
    }
    if (access.kind() == ParserToken::Kind::MemberAccess && access.m_right) {
        if (access.m_right->kind() == ParserToken::Kind::IntLiteral) {
            unsigned long long idx = 0;
            if (parseDecimalUInt(access.m_right->text(), idx) && idx < fields.size()) {
                return fields[idx].second;
            }
        } else {
            const std::string name = std::string(access.m_right->text());
            for (const auto& [n, t] : fields) {
                if (n == name) {
                    return t;
                }
            }
        }
    } else if (access.kind() == ParserToken::Kind::ArrayAccess && access.m_right && access.m_right->kind() == ParserToken::Kind::IntLiteral) {
        unsigned long long idx = 0;
        if (parseDecimalUInt(access.m_right->text(), idx) && idx < fields.size()) {
            return fields[idx].second;
        }
    }
    return INVALID_TYPE_ID;
}

// Многоточие в аргументах ВЫЗОВА ФУНКЦИИ (`... expr ...` / `...`): резолв сигнатуры + ЕДИНЫЙ
// примитив семьи многоточия (semantic/ellipsis) - тот же, что у вызовов методов и массивов.
void ExprTyper::analyzeCallFilling(CallExpr& call) {
    const EllipsisInfo info = scanCallEllipsis(call);
    if (info.form == EllipsisForm::None) {
        return;
    }
    // Структурные правила (единственное, последнее) - ДО резолва сигнатуры: форма проверяется
    // независимо от вызываемого.
    if (!validateEllipsis(info, call, m_actx, "аргументы вызова")) {
        discardEllipsisElements(*call.m_args, false);
        return;
    }
    // Сигнатура: именованная функция или значение функционального типа.
    const Symbol* sym = nullptr;
    if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::Ident) {
        sym = m_core.resolveSimple(call.m_callee.get(), call.m_callee->text());
    }
    // Сигнатура: для ПЕРЕГРУЖЕННОГО имени - выбранная семантикой (call.resolvedSignature);
    // иначе - единственная сигнатура символа.
    const FunctionTypeData* fd = nullptr;
    if (call.resolvedSignature != INVALID_TYPE_ID) {
        fd = m_actx.ctx().types().getTypeDataAs<FunctionTypeData>(call.resolvedSignature);
    } else if (sym && sym->type != INVALID_TYPE_ID) {
        fd = m_actx.ctx().types().getTypeDataAs<FunctionTypeData>(sym->type);
    }
    if (fd == nullptr) {
        m_actx.ctx().diag().report(Severity::Error, call.range(), "аргументы вызова: число параметров вызываемой функции неизвестно");
        discardEllipsisElements(*call.m_args, false);
        return;
    }
    (void)expandCallEllipsis(call, m_actx, info, fd->paramTypes, fd->variadicType != INVALID_TYPE_ID);
}

} // namespace trust
