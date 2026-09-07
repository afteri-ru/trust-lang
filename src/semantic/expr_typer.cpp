// Generated: src/semantic/expr_typer.cpp
#include "semantic/expr_typer.hpp"
#include "semantic/name_resolution.hpp"
#include "semantic/analysis_common.hpp"
#include "semantic/format_check.hpp"
#include "semantic/solver.hpp"
#include "semantic/symbol_table.hpp"
#include "semantic/type_inference.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/promotion.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "utils/operators.hpp"
#include "utils/strings.hpp"
#include <algorithm>
#include <format>
#include <string>

namespace trust {

namespace {

// ЕДИНСТВЕННЫЙ репортёр невалидной постфиксной аннотации литерала `literal :Type`. Вызывается
// только из авторитетного прохода typeExpr; провизионные читатели (dictElementType/resolvedType)
// решают тип через annotatedLiteralType, но ошибку НЕ дублируют. ann - резолвленная аннотация
// (nullopt/INVALID ⇒ «unknown type», тексты сообщений берутся из решателя/литерала).
void reportLiteralAnnotProblem(trust::Context& ctx, const Literal& lit, std::optional<TypeId> ann, const TypeRegistry& reg, LiteralAnnotProblem problem) {
    switch (problem) {
    case LiteralAnnotProblem::UnknownType:
        ctx.diag().report(Severity::Error, lit.typeAnnotation->range(), "unknown type annotation '{}' on a literal", lit.typeAnnotation->text());
        break;
    case LiteralAnnotProblem::RationalExpected:
        ctx.diag().report(Severity::Error, lit.range(), "a rational literal 'num\\den' must be typed 'Rational' (found '{}')",
                          reg.getFullTypeName(reg.getCanonicalTypeId(*ann)));
        break;
    case LiteralAnnotProblem::BoolNot01:
        ctx.diag().report(Severity::Error, lit.range(), "boolean literal must be 0 or 1 (found '{}')", lit.text());
        break;
    case LiteralAnnotProblem::IntOverflow:
        ctx.diag().report(Severity::Error, lit.range(), "integer literal '{}' does not fit the annotated type '{}'", lit.text(),
                          reg.getFullTypeName(reg.getCanonicalTypeId(*ann)));
        break;
    case LiteralAnnotProblem::NonNumeric:
        ctx.diag().report(Severity::Error, lit.range(), "cannot annotate a numeric literal with non-numeric type '{}'",
                          reg.getFullTypeName(reg.getCanonicalTypeId(*ann)));
        break;
    case LiteralAnnotProblem::None:
        break;
    }
}

} // namespace

// Анализ литерала словаря. Контракт: все элементы m_body - ArgNode (имя в text(), значение в
// m_value), строятся из канонических пар грамматики `args` (term_to_ast::visit_DICT).
// Значение анализируем полностью (резолв/типизация); имя-метку НЕ резолвим как переменную и
// НЕ регистрируем в таблице символов. Тип значения сохраняем на элементе (ArgNode::resultType
// из resolvedType) - единый источник для кодогенерации TypedValue (не только Literal::typeId).
void ExprTyper::analyzeDictLiteral(Sequence& dict_node) {
    // Enum/Variant-объявление (ПОСТФИКС `(...):Enum`/`(...):Variant`): это правая часть `::=`,
    // обрабатывается analyzeTypeDecl (analyzeEnumDecl/analyzeVariantDecl); как обычный словарь НЕ
    // анализируется (иначе голые члены `B` резолвились бы как переменные). Префикс `:Enum(...)`
    // - НЕ объявление (type-call): голые аргументы = значения, резолвятся как обычно.
    const auto& dl0 = static_cast<const DictLiteralNode&>(dict_node);
    if (!dl0.prefix && dl0.m_type) {
        const std::string ann = std::string(dl0.m_type->text());
        if (ann == "Enum" || ann == "Variant") {
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
        a.resultType = m_actx.resolvedType(*a.m_value);
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
        target = m_actx.resolveType(*dl.m_type).value_or(INVALID_TYPE_ID);
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
            if (auto ann = m_actx.resolveType(*dl.arrayElementAnnotation); ann) {
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
                    et = m_actx.resolvedType(*a.m_value);
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
        const TypeId arrBase = reg.getOrCreateArrayType(elemType, {static_cast<uint64_t>(dl.m_body.size())});
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
    std::vector<TypeId> raw;           // сырые (clearInferred) типы элементов
    std::vector<TypeId> explicitTypes; // типы из явных аннотаций элементов (`2:Int8`)
    bool hasArrayElement = false;      // хотя бы один элемент - массив (многомерный литерал)
    TypeId firstArrayElemType = INVALID_TYPE_ID;
    for (auto& el : node.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        auto& a = static_cast<ArgNode&>(*el);
        TypeId et = INVALID_TYPE_ID;
        if (a.m_value) {
            m_core.analyzeNode(a.m_value);
            et = m_actx.resolvedType(*a.m_value);
        }
        // Явная аннотация элемента (`2:Int8`) имеет приоритет над выведенным типом.
        if (a.m_type) {
            if (auto ann = m_actx.resolveType(*a.m_type); ann) {
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
        if (auto ann = m_actx.resolveType(*node.m_type); ann) {
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
    const TypeId arrType = reg.getOrCreateArrayType(elemType, {static_cast<uint64_t>(node.m_body.size())});
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
    const TypeId arr = reg.getOrCreateArrayType(targetElem, {static_cast<uint64_t>(node.m_body.size())});
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
        TypeId t = m_actx.resolvedType(*child);
        // Явная аннотация типа операнда (`stop:Type`, напр. `0..100:Rational`) имеет приоритет
        // над выведенным типом: грамматика кладёт её в m_type терма-операнда, конвертер - в
        // RangeExpr::operandTypes. Аннотация `:Rational` делает элементный тип Rational.
        if (i < range_node.operandTypes.size() && range_node.operandTypes[i]) {
            if (auto ann = m_actx.resolveType(*range_node.operandTypes[i]); ann) {
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

// Тип значения элемента словаря по его узлу (литерал → единый решатель/текст; иначе resolvedType).
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
            const LiteralAnnotResult r = annotatedLiteralType(lit, m_actx.resolveType(*lit.typeAnnotation), m_actx.ctx().types());
            return (r.type != INVALID_TYPE_ID) ? setFlag(r.type, SymbolFlag::Inferred) : INVALID_TYPE_ID;
        }
        // Тип значения литерала - по его тексту (минимальный знаковый Int / Float / ...).
        return setFlag(literalType(lit, m_actx.ctx().types()), SymbolFlag::Inferred);
    }
    return m_actx.resolvedType(*valueNode);
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

// -- Типизация выражений (post-order) --

TypeId ExprTyper::typeBinaryResult(Binary& b) {
    if (!b.m_left || !b.m_right) {
        b.lhsType = b.rhsType = b.resultType = b.commonType = INVALID_TYPE_ID;
        m_actx.setExprType(&b, INVALID_TYPE_ID);
        return INVALID_TYPE_ID;
    }
    // Чистая запись `x = <expr>` (AssignOp "=" с LHS-Ident): LHS - ЦЕЛЬ перезаписи, текущее
    // значение НЕ читается → тип цели берём из символа напрямую (НЕ через resolvedType, чтобы не
    // дать ложного «чтения неинициализированной»: `x := _; x = 5;` валидно - цель перезаписывается).
    // Составные `+=`/`op=` и остальные бинарные операции LHS читают (нужно текущее значение) →
    // resolvedType (если цель неинициализирована - это ошибка чтения до инициализации).
    TypeId lt = INVALID_TYPE_ID;
    const bool plainIdentAssign =
        (b.kind() == ParserToken::Kind::AssignOp && utils::isPlainAssignOp(b.text()) && b.m_left && b.m_left->kind() == ParserToken::Kind::Ident);
    if (plainIdentAssign) {
        if (const Symbol* s = m_actx.symbols().resolve(b.m_left->text())) {
            lt = clearFlag(s->type, SymbolFlag::Uninit);
        }
    } else {
        lt = m_actx.resolvedType(*b.m_left);
    }
    const TypeId rt = m_actx.resolvedType(*b.m_right);
    const TypeRegistry& reg = m_actx.ctx().types();

    // Swap `:=:` - интринсик обмена/перемещения. `a :=: b` → std::swap(a, b) и возвращает &a
    // (типы должны быть совместимы, не обязательно ссылки); `var :=: _` → std::move(var)
    // (перемещение значения в discard). Результат - тип левого операнда.
    if (utils::isSwapOp(b.text())) {
        // Форма `var :=: _`: правая часть - идентификатор `_` (None) → std::move(var).
        const bool isMoveDiscard = b.m_right && b.m_right->kind() == ParserToken::Kind::Ident && b.m_right->text() == "_";
        if (!isMoveDiscard) {
            const TypeId lc = (lt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(lt) : INVALID_TYPE_ID;
            const TypeId rc = (rt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(rt) : INVALID_TYPE_ID;
            if (lc == INVALID_TYPE_ID || rc == INVALID_TYPE_ID || lc != rc) {
                // Имя типа с видом ссылки (getFullTypeName даёт только pointee для ссылочных):
                // различает unique/shared/weak/value в диагностике (`a :=: b` для unique и shared).
                const auto typeDisplay = [&](TypeId tid) {
                    if (tid == INVALID_TYPE_ID) {
                        return std::string("?");
                    }
                    std::string n(reg.getFullTypeName(tid));
                    const RefType rt2 = getRefType(getKindFromId(tid));
                    if (rt2 != RefType::kValue) {
                        n = std::string(refTypeName(rt2)) + "<" + n + ">";
                    }
                    return n;
                };
                m_actx.ctx().diag().report(Severity::Error, b.range(),
                                           "swap ':=:' requires operands of compatible types, got '{}' and '{}' (values can be swapped via '*lhs :=: *rhs')",
                                           typeDisplay(lt), typeDisplay(rt));
            }
        }
        b.lhsType = lt;
        b.rhsType = rt;
        b.resultType = b.commonType = lt;
        m_actx.setExprType(&b, lt);
        return lt;
    }

    // Ссылочные типы НЕ участвуют в арифметике и сравнении (в языке отсутствует класс ссылочной
    // арифметики; сырые указатели - только для интеграции с C++). Использование незахваченной
    // ссылки в выражении как значения - нарушение контракта: ошибка + fixit `*<name>` (локер).
    if (b.kind() == ParserToken::Kind::MathOp || b.kind() == ParserToken::Kind::CompareOp) {
        const bool lRef = isRefTypeId(lt);
        const bool rRef = isRefTypeId(rt);
        if (lRef || rRef) {
            const AstNodeBase* bad = lRef ? b.m_left.get() : b.m_right.get();
            const char* opname = (b.kind() == ParserToken::Kind::MathOp) ? "arithmetic" : "comparison";
            auto* entry = m_actx.ctx().diag().report(
                Severity::Error, b.range(), "{} '{}' on a reference is not allowed (no pointer/reference arithmetic); use '*{}' (locker) to access the value",
                opname, b.text(), bad ? bad->text() : "");
            if (entry != nullptr && bad && !bad->range().isInvalid()) {
                m_actx.ctx().diag().fixit(entry, bad->range(), "*" + std::string(bad->text()));
            }
            b.lhsType = lt;
            b.rhsType = rt;
            b.resultType = b.commonType = INVALID_TYPE_ID;
            m_actx.setExprType(&b, INVALID_TYPE_ID);
            return INVALID_TYPE_ID;
        }
    }

    // Типобезопасность enum: сравнение (<,>,<=,>=,==,!=) допустимо только между однотипными
    // enum; неявное приведение enum к его типу значений или иному типу запрещено (работа
    // с enum идёт ТОЛЬКО через имя типа - осознанное решение, см. MEMORY.md).
    if (b.kind() == ParserToken::Kind::CompareOp) {
        const bool lEnum = isEnumType(lt, reg);
        const bool rEnum = isEnumType(rt, reg);
        if (lEnum || rEnum) {
            const TypeId lc = (lt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(lt) : INVALID_TYPE_ID;
            const TypeId rc = (rt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(rt) : INVALID_TYPE_ID;
            if (!(lEnum && rEnum && lc == rc)) {
                m_actx.ctx().diag().report(Severity::Error, b.range(), "type-safe enum comparison requires both operands to be the same enum type");
                b.resultType = b.commonType = INVALID_TYPE_ID;
                m_actx.setExprType(&b, INVALID_TYPE_ID);
                return INVALID_TYPE_ID;
            }
        }
    }

    // Продвижение auto-Bool в арифметике: Bool НЕтипизированной переменной (напр. из
    // `b := 1 :Bool` / сравнения) продвигается по правилам C++ bool→int → Int32. Буквальные
    // 0/1 выводятся как Int8 (не Bool). Явный Bool (:Bool, результат сравнения/логики)
    // в арифметике - ошибка компиляции (нельзя привести к числу). Compare/Logical и простое
    // присваивание '=' не затрагиваются.
    TypeId elt = lt, ert = rt;
    const bool arithmetic = !(b.kind() == ParserToken::Kind::CompareOp || b.kind() == ParserToken::Kind::LogicalOp) &&
                            !(b.kind() == ParserToken::Kind::AssignOp && utils::isPlainAssignOp(b.text()));
    if (arithmetic) {
        const TypeId boolT = reg.getCanonicalTypeId(reg.getType(type::Bool));
        auto promoteBool = [&](const AstNodeBase* operand, TypeId t, TypeId& out) {
            if (t == INVALID_TYPE_ID || reg.getCanonicalTypeId(t) != boolT) {
                return;
            }
            if (testFlag(t, SymbolFlag::Inferred)) {
                out = reg.getType(type::Int32); // bool→int (C++ promotion)
            } else {
                m_actx.ctx().diag().report(Severity::Error, operand->range(),
                                           "cannot use Bool value in arithmetic '{}'; use an explicit integer type or a cast", b.text());
            }
        };
        promoteBool(b.m_left.get(), lt, elt);
        promoteBool(b.m_right.get(), rt, ert);
    }

    // BigInteger/Rational (kArbitraryPrecision): смешивание с float (Numbers) или
    // целочисленное деление `//` - ошибки (нужен явный каст / не поддерживается).
    if (arithmetic) {
        const TypeId lc = (lt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(lt) : INVALID_TYPE_ID;
        const TypeId rc = (rt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(rt) : INVALID_TYPE_ID;
        const Group lg = (lc != INVALID_TYPE_ID) ? getGroup(getKindFromId(lc)) : Group::kAny;
        const Group rg = (rc != INVALID_TYPE_ID) ? getGroup(getKindFromId(rc)) : Group::kAny;
        const bool lAP = lg == Group::kArbitraryPrecision;
        const bool rAP = rg == Group::kArbitraryPrecision;
        if (lAP || rAP) {
            if (utils::isIntDivOp(b.text())) {
                m_actx.ctx().diag().report(Severity::Error, b.range(), "integer division '{}' is not supported for BigInteger/Rational", b.text());
            } else if ((lAP && rg == Group::kNumbers) || (rAP && lg == Group::kNumbers)) {
                m_actx.ctx().diag().report(Severity::Error, b.range(), "cannot mix BigInteger/Rational with a floating-point number without an explicit cast");
            }
        }
    }

    // Простое присвоение "=" → тип RHS; составное/арифметика → тип результата (lhs op rhs).
    const TypeId result =
        (b.kind() == ParserToken::Kind::AssignOp && utils::isPlainAssignOp(b.text())) ? rt : resultTypeBinary(b.kind(), b.text(), elt, ert, reg);
    b.lhsType = lt;
    b.rhsType = rt;
    b.resultType = result;
    b.commonType = result;
    // Присваивание в переменную доверенного типа: помечаем узел ссылкой на декларацию типа
    // (источник trust-условий для проверки после присваивания; переживает таблицу символов).
    if (b.kind() == ParserToken::Kind::AssignOp) {
        b.m_typeDecl = m_core.trustTypeDeclOf(lt);
    }
    // Общий тип операндов для any_cast: арифметика → result; Compare/Logical (результат Bool)
    // → продвинутый конкретный операнд, если ровно один операнд std::any.
    if (b.kind() == ParserToken::Kind::CompareOp || b.kind() == ParserToken::Kind::LogicalOp) {
        if (isAnyType(lt, reg)) {
            b.commonType = promoteSingleNumeric(reg, rt);
        } else if (isAnyType(rt, reg)) {
            b.commonType = promoteSingleNumeric(reg, lt);
        } else {
            b.commonType = INVALID_TYPE_ID; // оба конкретные - any_cast не нужен
        }
    }
    m_actx.setExprType(&b, result);
    return result;
}

void ExprTyper::typeExpr(AstNodeBase* node) {
    if (!node) {
        return;
    }
    if (is_binary_expr_kind(node->kind())) {
        auto& b = static_cast<Binary&>(*node);
        // AppendStmt (`X []= v`) - append к контейнеру, не обычное бинарное выражение:
        // типы ложатся специально (lhsType=тип контейнера, rhsType/resultType=тип значения),
        // сужение/расширение целевой переменной не применяются (append не меняет тип цели).
        if (b.kind() == ParserToken::Kind::AppendStmt) {
            const TypeId lt = b.m_left ? m_actx.resolvedType(*b.m_left) : INVALID_TYPE_ID;
            const TypeId rt = b.m_right ? m_actx.resolvedType(*b.m_right) : INVALID_TYPE_ID;
            // Spread-merge `X []= ... dict`: правая часть - маркер распаковки (Ellipsis),
            // единичным элементом НЕ является, тип результата не выводится (INVALID).
            const bool spread = b.m_right && b.m_right->kind() == ParserToken::Kind::Ellipsis;
            b.lhsType = lt;
            b.rhsType = spread ? INVALID_TYPE_ID : rt;
            b.resultType = spread ? INVALID_TYPE_ID : rt;
            b.commonType = spread ? INVALID_TYPE_ID : rt;
            // Строковые контейнеры: ширина RHS должна быть совместима с единичным элементом
            // контейнера (StrChar ↔ std::string, StrWide ↔ std::wstring). Потерянное сужение
            // (широкая строка в узкий контейнер) - ошибка; обратное (char→wide) кодогенерация
            // сама расширяет узкий литерал в wide.
            const TypeRegistry& reg = m_actx.ctx().types();
            const TypeId strChar = reg.getType(type::StrChar);
            const TypeId strWide = reg.getType(type::StrWide);
            const TypeId ltC = (lt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(lt) : INVALID_TYPE_ID;
            const TypeId rtC = (rt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(rt) : INVALID_TYPE_ID;
            if (spread) {
                // Распаковка `[]= ...` допустима только для контейнера-словаря (merge/extend).
                if (ltC != INVALID_TYPE_ID && ltC != reg.getType(type::Dict)) {
                    m_actx.ctx().diag().report(Severity::Error, b.range(), "spread append '[]= ...' is only supported for a dictionary container");
                }
                // Вложенный LHS (`d['x'] []= ...`, `d[0] []= ...`, `d.field []= ...`) - отложено.
                if (b.m_left && (b.m_left->kind() == ParserToken::Kind::ArrayAccess || b.m_left->kind() == ParserToken::Kind::MemberAccess)) {
                    m_actx.ctx().diag().report(Severity::Error, b.m_left->range(),
                                               "вложенный append '[]=' пока не реализован: append допустим только к простому контейнеру");
                }
                m_actx.setExprType(&b, INVALID_TYPE_ID);
                return;
            }

            if (ltC == strChar && rtC == strWide) {
                m_actx.ctx().diag().report(
                    Severity::Error, b.range(),
                    "append '[]=': wide string cannot be appended to a narrow string container; use matching quotes (narrow '...' or wide \"...\")");
            }
            // Вложенный LHS (`d['x'] []= v`, `d[0] []= v`, `d.field []= v`) - отложено.
            if (b.m_left && (b.m_left->kind() == ParserToken::Kind::ArrayAccess || b.m_left->kind() == ParserToken::Kind::MemberAccess)) {
                m_actx.ctx().diag().report(Severity::Error, b.m_left->range(),
                                           "вложенный append '[]=' пока не реализован: append допустим только к простому контейнеру");
            }
            m_actx.setExprType(&b, rt);
            return;
        }
        const TypeId result = typeBinaryResult(b);
        const bool isAssignOp = (b.kind() == ParserToken::Kind::AssignOp);
        // Составное присваивание ("+=", "//=") - оператор с текстом, оканчивающимся на '=';
        // простые операторы ("//", "+") не расширяют целевую переменную.
        const bool compound = utils::isCompoundAssignOp(b.text());
        // Сужение в ЯВНО-типизированную цель (inferred-цели расширяются ниже).
        if (b.m_left && b.m_left->kind() == ParserToken::Kind::Ident) {
            if (Symbol* s = m_actx.symbols().resolveMutable(b.m_left->text())) {
                if (!testFlag(s->type, SymbolFlag::Inferred) && (isAssignOp || compound)) {
                    const TypeId assigned = utils::isPlainAssignOp(b.text()) ? b.rhsType : result;
                    checkAssignmentNarrowing(b.m_right.get(), assigned, s->type, s->name);
                }
            }
        }
        // Константность (kConstFlag): запись в константную переменную - ошибка; LHS с `^`
        // (attr::ReadOnly на узле Ident) - финальная запись, делающая переменную константой
        // (became-const: `x := 42; x^ += 1;` → x неизменяема со значением 44).
        if (b.m_left && b.m_left->kind() == ParserToken::Kind::Ident && (isAssignOp || compound)) {
            if (Symbol* s = m_actx.symbols().resolveMutable(b.m_left->text())) {
                const bool makeConst = b.m_left->as_attr() && b.m_left->as_attr()->has_attr(m_actx.ctx().attrs(), attr::ReadOnly);
                if (testFlag(s->type, SymbolFlag::Const)) {
                    if (!makeConst) {
                        m_actx.ctx().diag().report(Severity::Error, b.range(), "cannot assign to constant variable '{}'", s->name);
                    }
                } else if (makeConst) {
                    // Финальная запись `x^ = ...`: переменная становится константной (бит
                    // kConstFlag на Symbol::type). Декларация при этом остаётся не-const (см.
                    // transpiler::generateVarDeclToFile - const объявления берётся из атрибута узла).
                    s->type = setFlag(s->type, SymbolFlag::Const);
                }
                // Признак инициализации - ортогональный бит kUninitFlag на Symbol::type (как
                // пер-переменная константность; НЕ то же, что m_initializer). `_` здесь -
                // ФОРМАЛЬНЫЙ инициализатор (`x = _;`), который СБРАСЫВАЕТ «инициализирована»
                // (значение более не определено); обычная запись значения (`x = <expr>` /
                // `x += …`) признак снимает.
                s->type = isNoneMarker(b.m_right.get()) ? setFlag(s->type, SymbolFlag::Uninit) : clearFlag(s->type, SymbolFlag::Uninit);
            }
        }
        // Расширение выводимой цели по истории присвоений - только для присваиваний
        // (AssignOp "=", "+=" или составной MathOp "+=").
        if (isAssignOp || compound) {
            TypeId widen = result;
            // Автоматически выведенный Bool (неТипизированная переменная, напр.
            // `b := 1 :Bool; b += 1;`), используемый в составной числовой арифметике,
            // расширяется до максимального Int (Int64): Bool - вырожденное целое, а в
            // однопроходной типизации нет «оператора в цикле», поэтому расширяем по самому
            // факту составного присваивания. Явный `:Bool` (в т.ч. из голого `1`/`0`, которые
            // теперь Int8) так НЕ расширяется - для него это ошибка (явный тип фиксирован).
            if (!utils::isPlainAssignOp(b.text()) && b.m_left && b.m_left->kind() == ParserToken::Kind::Ident) {
                if (Symbol* s = m_actx.symbols().resolveMutable(b.m_left->text())) {
                    const TypeRegistry& reg = m_actx.ctx().types();
                    if (s->type != INVALID_TYPE_ID && reg.getCanonicalTypeId(s->type) == reg.getType(type::Bool)) {
                        if (testFlag(s->type, SymbolFlag::Inferred)) {
                            widen = reg.getType(type::Int64);
                        } else {
                            m_actx.ctx().diag().report(Severity::Error, b.range(),
                                                       "explicitly typed Bool cannot be widened to Int64 by numeric compound assignment '{}'; "
                                                       "use an inferred int variable or an explicit integer type",
                                                       b.text());
                        }
                    }
                }
            }
            widenInferredTarget(b.m_left.get(), widen);
        }
        return;
    }
    switch (node->kind()) {
    case ParserToken::Kind::VarDecl: {
        // Нетипизированная переменная (inferred): тип из типа инициализатора.
        auto& v = static_cast<VarDecl&>(*node);
        if (v.m_initializer) {
            if (v.m_type == nullptr) {
                TypeId t = m_actx.resolvedType(*v.m_initializer);
                if (t != INVALID_TYPE_ID) {
                    // Ссылочная переменная без явного типа (`&& x := 5` / `&* u := 10` / `&? w := & x`):
                    // вид ссылки - из reftype-атрибута (префиксный сигл перед именем), pointee выводится
                    // из инициализатора. Если тип инициализатора УЖЕ ссылочный - он должен совпадать
                    // с видом переменной (иначе - ошибка); если value - оборачиваем в вид переменной.
                    const AttrPool& attrs = m_actx.ctx().attrs();
                    if (const auto rid = attrs.lookup(attr::Reftype); rid.has_value() && v.has_attr(*rid)) {
                        if (const auto* rargs = v.attr_args(*rid); rargs && !rargs->empty()) {
                            if (const auto rk = refTypeFromString(rargs->front())) {
                                TypeRegistry& treg = m_actx.ctx().types();
                                const bool nativeMarker = (*rk == RefType::kPtr || *rk == RefType::kRef);
                                // Нативная ссылка `%& expr` (NativeRefMakeExpr) - контекст (`T&` или `T*`)
                                // задаёт ЛЕВЫЙ маркер декларации: тип переменной = вид маркера над pointee
                                // инициализатора; вид результата `%& expr` кладём в m_resultType
                                // (kRef для ссылки / kPtr для указателя) - единый источник для кодогена.
                                if (nativeMarker && v.m_initializer->kind() == ParserToken::Kind::NativeRefMakeExpr) {
                                    const auto& nat = static_cast<const NativeRefMakeExpr&>(*v.m_initializer);
                                    if (!nat.m_body.empty()) {
                                        const TypeId opType = m_actx.resolvedType(*nat.m_body[0]);
                                        if (opType != INVALID_TYPE_ID) {
                                            t = treg.applyRefType(clearFlag(opType, SymbolFlag::Inferred), *rk);
                                            static_cast<NativeRefMakeExpr&>(*v.m_initializer).m_resultType = t;
                                        }
                                    }
                                } else {
                                    const RefType deduced = getRefType(getKindFromId(t));
                                    if (deduced == RefType::kValue) {
                                        t = treg.applyRefType(t, *rk); // value → оборачиваем в вид переменной
                                    } else if (deduced != *rk) {
                                        m_actx.ctx().diag().report(Severity::Error, v.range(),
                                                                   "reference kind mismatch: variable '{}' is '{}' but its deduced type is '{}'", v.text(),
                                                                   refTypeName(*rk), treg.getFullTypeName(t));
                                    }
                                }
                            }
                        }
                    }
                    // Тип с trust-условиями (пред/пост/утверждения) НЕ может быть выведен
                    // автоматически - только явная аннотация типа (`x :MyInt := ...`). Признак -
                    // бит trust в TypeId (typeIsTrusted). См. types/MEMORY.md.
                    if (typeIsTrusted(t)) {
                        const TypeRegistry& treg = m_actx.ctx().types();
                        const std::string tn = treg.getFullTypeName(t);
                        m_actx.ctx().diag().report(
                            Severity::Error, v.range(),
                            "type '{}' carries trust conditions and cannot be auto-deduced; annotate the variable type explicitly (e.g. '{} :{} := ...')", tn,
                            v.text(), tn);
                        return;
                    }
                    // Транспилятору нужен структурный тип (кодогенерация не различает inferred).
                    v.inferredType = clearFlag(t, SymbolFlag::Inferred);
                    if (Symbol* s = m_actx.symbols().resolveMutable(v.text())) {
                        // Живой тип символа несёт бит «выведен» (для join/продвижения) и, при
                        // константности ('^' → attr::ReadOnly), бит «константность» (kConstFlag) -
                        // источник префикса `const ` в кодогенерации переменной.
                        s->type = v.has_attr(m_actx.ctx().attrs(), attr::ReadOnly) ? setFlag(t, SymbolFlag::Const) : t;
                    }
                } else if (v.m_initializer->kind() != ParserToken::Kind::TypeName) {
                    // Нетипизированная `x := _;` («объявить без значения», маркер `_`) - НЕ std::any
                    // по умолчанию: тип выводится МОНОТОННО по последующим записям (первая запись
                    // задаёт категорию; несовместимая последующая категория - Error, см. widenInferredTarget).
                    // Чтение до записи уже Error через Uninit-бит; VarDecl::inferredType заполняет
                    // widenInferredTarget на первой записи (конкретный C++-тип, не std::any).
                    if (isNoneMarker(v.m_initializer.get())) {
                        if (Symbol* s = m_actx.symbols().resolveMutable(v.text()); s && s->storage != Storage::Local) {
                            // Глобальная/модульная/статическая нетипизированная `:= _` (вне потока
                            // функции): тип неоткуда вывести (нет единого «потока записей» скоупа
                            // функции) - требуется явная аннотация. Ошибка блокирует конвейер до
                            // кодогена; отдельного «cannot infer» не выдаём (см. finishUntypedUnderscoreDecls,
                            // работающий только для Storage::Local).
                            m_actx.ctx().diag().report(Severity::Error, v.range(),
                                                       "non-local '{} := _' requires an explicit type annotation (e.g. '{} :Any := _'); "
                                                       "the type cannot be inferred from assignments outside a function body",
                                                       v.text(), v.text());
                        } else {
                            // Локальная `x := _;`: отложенный вывод. НЕ ставим Any. Символ несёт
                            // Inferred+Uninit на INVALID-структуре (тип заполнит первая запись);
                            // чтение до записи даст Error «read before it is initialized».
                            if (s) {
                                TypeId st = INVALID_TYPE_ID;
                                if (v.has_attr(m_actx.ctx().attrs(), attr::ReadOnly)) {
                                    st = setFlag(st, SymbolFlag::Const);
                                }
                                st = setFlag(st, SymbolFlag::Inferred);
                                st = setFlag(st, SymbolFlag::Uninit);
                                s->type = st;
                            }
                            // v.inferredType остаётся INVALID; заполнит widenInferredTarget на первой
                            // записи (транспилятор читает VarDecl::inferredType).
                        }
                    } else if (auto aid = m_actx.ctx().types().findType("Any")) {
                        // Инициализатор без выводимого типа (C++-вставка `{% %}`, вызов с
                        // неизвестным результатом, отрицательный литерал) - переменная по природе
                        // std::any. Маркируем тип ЯВНО (Any), чтобы транспилятор НЕ угадывал тихим
                        // fallback на std::any (AGENTS rule 5): INVALID у переменной с инициализатором
                        // в кодогенерации - ошибка вывода. Голый `:T` сюда не попадает - это
                        // невалидная запись `x := :Int32` (диагностируется в analyzeVarDecl).
                        v.inferredType = *aid;
                        if (Symbol* s = m_actx.symbols().resolveMutable(v.text())) {
                            s->type = v.has_attr(m_actx.ctx().attrs(), attr::ReadOnly) ? setFlag(*aid, SymbolFlag::Const) : *aid;
                        }
                    }
                }
            } else {
                // Явно-типизированная: проверить сужение инициализатора в целевой тип.
                auto targetOpt = m_actx.resolveType(*v.m_type);
                if (targetOpt.has_value()) {
                    // Литерал массива `[1,2,3,]` в типизированную Array-цель (`vector<Int32>`):
                    // коэрция элемента к типу элемента цели (`std::vector<int32_t>{1,2,3}`), иначе
                    // узкая разрядность литерала (int8) не сконвертируется в целевую.
                    if (v.m_initializer && v.m_initializer->kind() == ParserToken::Kind::ArrayInit) {
                        coerceArrayInitToTarget(static_cast<DictLiteralNode&>(*v.m_initializer), *targetOpt);
                    }
                    // Полный целевой тип С УЧЁТОМ reftype-атрибута (`@[reftype(...)@]` применяется в
                    // analyzeVarDecl к Symbol::type, а resolveType(*m_type) даёт только базовый тип без
                    // reftype). Для контракта value-vs-reference важен полный тип (Symbol::type) -
                    // иначе цель-слабая ссылка выглядела бы value-типом и копирование ссылки
                    // ошибочно запрещалось. Символический сигл (`:&? Int32`) reftype уже несёт.
                    TypeId targetFull = *targetOpt;
                    if (Symbol* ts = m_actx.symbols().resolveMutable(v.text())) {
                        if (ts->type != INVALID_TYPE_ID) {
                            targetFull = ts->type;
                        }
                    }
                    checkAssignmentNarrowing(v.m_initializer.get(), m_actx.resolvedType(*v.m_initializer), targetFull, v.text());
                }
            }
        } else if (v.m_type == nullptr) {
            // Нетипизированное forward-объявление (`x := ...;`): и инициализатора, и типа нет -
            // по природе std::any. Маркируем тип ЯВНО (Any), как и для тип-less инициализаторов,
            // чтобы транспилятор единообразно эмитил `emitTypeName(inferred)` без ветки угадывания.
            if (auto aid = m_actx.ctx().types().findType("Any")) {
                v.inferredType = *aid;
                if (Symbol* s = m_actx.symbols().resolveMutable(v.text())) {
                    s->type = v.has_attr(m_actx.ctx().attrs(), attr::ReadOnly) ? setFlag(*aid, SymbolFlag::Const) : *aid;
                }
            }
        }
        // Биндинг оператора `with`: ссылочный тип (Shared/Weak) без захвата `*`/`*^` - предупреждение.
        // Инициализатор `*ref` (RefTakeExpr) - это корректный захват; голый `ref`/`obj.member` копирует
        // ссылку, а не блокирует доступ - вероятная ошибка.
        if (v.m_inWith && v.m_initializer && v.m_initializer->kind() != ParserToken::Kind::RefTakeExpr) {
            const TypeId it = m_actx.resolvedType(*v.m_initializer);
            if (it != INVALID_TYPE_ID) {
                const RefType rt = getRefType(getKindFromId(it));
                if (rt == RefType::kShared || rt == RefType::kWeak) {
                    // -Wwith-ref-without-capture (default: warning; управляется -W/-Wno).
                    const Severity sev = m_actx.ctx().opts().get(semantic::DiagId::WithRefWithoutCapture);
                    if (sev != Severity::Ignore) {
                        auto* entry =
                            m_actx.ctx().diag().report(sev, v.range(), semantic::DiagId::WithRefWithoutCapture,
                                                       "with binding '{}' copies a reference without capture; use '*{}' to lock access", v.text(), v.text());
                        // Fix-it: обернуть инициализатор в '*': `with(v = *ref)` (для простых ссылок).
                        m_actx.ctx().diag().fixit(entry, v.m_initializer->range(), "*" + std::string(v.m_initializer->text()));
                    }
                }
            }
        }
        // Нативная ссылка с `%& expr`-инициализатором: вид результата (kRef/kPtr) из аннотации
        // типа (`r : %&/%* Type` - основной путь, нативный тип обязателен) или из reftype-атрибута
        // переменной (legacy/запасной) → m_resultType инициализатора (для кодогена `(...)`/`&(...)`).
        // Здесь (пост-порядок, после типизации инициализатора) перезаписывает дефолт kRef, когда
        // цель - нативный указатель (`%* name`). Покрывает и типизированные, и нетипизированные.
        if (v.m_initializer && v.m_initializer->kind() == ParserToken::Kind::NativeRefMakeExpr) {
            const auto& nat = static_cast<const NativeRefMakeExpr&>(*v.m_initializer);
            if (!nat.m_body.empty()) {
                const AttrPool& attrs = m_actx.ctx().attrs();
                const TypeId opType = m_actx.resolvedType(*nat.m_body[0]);
                // D2 (const-correctness, общее правило для всех ссылок): запрещено брать
                // НЕ-константную нативную ссылку с константного объекта (`p : %* Int32 := %& constObj`).
                const bool targetConst = v.has_attr(attrs, attr::ReadOnly);
                if (opType != INVALID_TYPE_ID && testFlag(opType, SymbolFlag::Const) && !targetConst) {
                    m_actx.ctx().diag().report(
                        Severity::Error, nat.range(),
                        "cannot take a non-const native reference to a const object '{}'; make the reference const (@[readonly@] / '^') or remove constness",
                        nat.m_body[0]->text());
                }
                std::optional<RefType> target;
                if (v.m_type) {
                    target = refTypeFromTypeSigil(v.m_type->text());
                }
                if (!target.has_value() || *target == RefType::kShared || *target == RefType::kWeak || *target == RefType::kUnique) {
                    if (auto rid = attrs.lookup(attr::Reftype); rid.has_value() && v.has_attr(*rid)) {
                        if (const auto* args = v.attr_args(*rid); args && !args->empty()) {
                            target = refTypeFromString(args->front());
                        }
                    }
                }
                if (target.has_value() && (*target == RefType::kPtr || *target == RefType::kRef || *target == RefType::kRref || *target == RefType::kPtrPtr)) {
                    if (opType != INVALID_TYPE_ID) {
                        static_cast<NativeRefMakeExpr&>(*v.m_initializer).m_resultType =
                            m_actx.ctx().types().applyRefType(clearFlag(opType, SymbolFlag::Inferred), *target);
                    }
                }
            }
        }

        break;
    }
    case ParserToken::Kind::IntLiteral:
    case ParserToken::Kind::FloatLiteral:
    case ParserToken::Kind::StrChar:
    case ParserToken::Kind::StrWide:
    case ParserToken::Kind::RationalLiteral: {
        Literal& lit = static_cast<Literal&>(*node);
        const TypeRegistry& reg = m_actx.ctx().types();
        // Литерал: тип задаёт постфиксная аннотация `literal :Type` (0 :Bool, 5 :Rational,
        // 5 :BigInteger, 256 :Int16, 1.5 :Float32; RationalLiteral `num\den` - только :Rational)
        // либо выводится по виду/тексту (literalType). Единые правила и единственный репортёр
        // ошибок - annotatedLiteralType / reportLiteralAnnotProblem.
        if (lit.typeAnnotation) {
            const auto ann = m_actx.resolveType(*lit.typeAnnotation);
            const LiteralAnnotResult r = annotatedLiteralType(lit, ann, reg);
            if (r.problem == LiteralAnnotProblem::None) {
                lit.typeId = setFlag(r.type, SymbolFlag::Inferred);
                m_actx.setExprType(node, lit.typeId);
            } else {
                // Единственный репортёр невалидной аннотации (typeExpr - авторитетный проход);
                // кешируем INVALID, чтобы resolvedType не возвращал текст-тип/не пересчитывал.
                reportLiteralAnnotProblem(m_actx.ctx(), lit, ann, reg, r.problem);
                m_actx.setExprType(node, INVALID_TYPE_ID);
            }
            break;
        }
        // Кешируем выведенный тип (literalType), чтобы resolvedType не пересчитывал его; тип с
        // битом inferred (для join/расширения; см. typeBinaryResult). Транспилятор литерала
        // словаря читает lit.typeId, не пересчитывая диапазоны.
        const TypeId t = literalType(lit, reg);
        if (t != INVALID_TYPE_ID) {
            const TypeId vt = setFlag(t, SymbolFlag::Inferred);
            lit.typeId = vt;
            m_actx.setExprType(node, vt);
        }
        break;
    }
    case ParserToken::Kind::CallExpr: {
        // Компиляйт-тайм проверка аргументов на соответствие printf-формату для функций
        // с атрибутом @[format("printf", ...)]. Вызывается пост-порядково, когда типы
        // аргументов уже вычислены и доступны через resolvedType.
        auto& call = static_cast<CallExpr&>(*node);
        // Строка-формат `"{}"(args)` / `'{}'(args)`: callee - строковый литерал. Результат -
        // строка той же ширины (StrWide/StrChar), как у литерала. + компиляйт-тайм проверка.
        if (call.m_callee && (call.m_callee->kind() == ParserToken::Kind::StrWide || call.m_callee->kind() == ParserToken::Kind::StrChar)) {
            const bool wide = call.m_callee->kind() == ParserToken::Kind::StrWide;
            const TypeId t = m_actx.ctx().types().getType(wide ? type::StrWide : type::StrChar);
            if (t != INVALID_TYPE_ID) {
                m_actx.setExprType(node, setFlag(t, SymbolFlag::Inferred));
            }
            checkFormatStringArgs(call);
            break;
        }
        // printf-формат (атрибут @[format]) - проверка типов аргументов (пост-порядково).
        checkFormatArgs(call);
        // Обычный вызов пользовательской функции: типизируем результат возвращаемым типом
        // сигнатуры (как handleMethodCall для методов). Это чинит `p := f(10)` → int32_t
        // (ранее результат вызова был std::any). Метод-вызов obj.method(args) обрабатывается
        // отдельно (analyzeAccess/handleMethodCall). Если тип не резолвится - остаётся Any.
        if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::Ident) {
            if (const Symbol* s = m_core.resolveSimple(call.m_callee.get(), call.m_callee->text())) {
                if (s->decl && s->decl->kind() == ParserToken::Kind::FuncDecl) {
                    if (const auto* fd = m_actx.ctx().types().getTypeDataAs<FunctionTypeData>(s->type)) {
                        m_actx.setExprType(node, fd->returnType);
                    }
                }
            }
        }
        // Чтение значения в АРГУМЕНТЕ вызова: вариативные/слабо-типизированные цели (напр. `@print`)
        // не проверяют типы аргументов, поэтому чтение неинициализированной переменной-аргумента не
        // пришло бы через типизацию аргумента. Принудительно резолвим каждый аргумент-Ident как
        // value-read (репорт-дедуп на узле внутри resolvedType). Составные аргументы уже прочитаны
        // при своей типизации (вложенные операнды) — здесь только верхний слой-Ident.
        if (call.m_args) {
            for (const auto& a : *call.m_args) {
                if (a && a->kind() == ParserToken::Kind::Ident) {
                    const TypeId _argRead = m_actx.resolvedType(*a);
                    (void)_argRead; // [[nodiscard]]; результат не нужен — важна проверка чтения
                }
            }
        }
        break;
    }
    case ParserToken::Kind::RefMakeExpr: {
        // `& expr` - взятие ссылки/заимствование (address-of). Допустимо ТОЛЬКО для переменной,
        // объявленной как ссылочная; weak (слабая ссылка) получается ТОЛЬКО из shared (`& shared_var`).
        // unique → ошибка; не-ссылочная переменная → ошибка. Результат - weak-тип pointee.
        RefMakeExpr& ref = static_cast<RefMakeExpr&>(*node);
        if (ref.m_body.empty()) {
            break;
        }
        const AstNodeBase* operand = ref.m_body[0].get();
        TypeRegistry& treg = m_actx.ctx().types();
        const TypeId opType = m_actx.resolvedType(*operand);
        if (opType == INVALID_TYPE_ID) {
            break;
        }
        const RefType rt = getRefType(getKindFromId(opType));
        if (rt == RefType::kShared) {
            const TypeId pointee = treg.getPointeeType(opType);
            if (pointee != INVALID_TYPE_ID) {
                const TypeId weak = clearFlag(treg.applyRefType(pointee, RefType::kWeak), SymbolFlag::Inferred);
                ref.m_resultType = weak; // для кодогенерации (локальные символы недоступны)
                m_actx.setExprType(node, weak);
            }
        } else if (rt == RefType::kUnique) {
            m_actx.ctx().diag().report(
                Severity::Error, node->range(),
                "operator '&' cannot take a weak reference of a unique variable '{}'; weak references are only obtainable from a shared reference",
                treg.getFullTypeName(opType));
        } else {
            m_actx.ctx().diag().report(Severity::Error, node->range(), "operator '&' (address-of/borrow) requires a reference variable (shared), got '{}'",
                                       treg.getFullTypeName(opType));
        }
        break;
    }
    case ParserToken::Kind::RefTakeExpr: {
        // `*ref` / `*^ref` (take): разыменование ссылочного операнда - прямой доступ к данным
        // (семантика std::reference_wrapper). Результат - тип pointee. Вид ссылки операнда
        // сохраняется в RefTakeExpr::m_opRefKind для кодогенерации (транспилятору локальные
        // символы недоступны): shared/weak → *(ref.lock()), unique/ptr → *ref.
        // Операнд - единственный ребёнок RefTakeExpr (m_body[0]).
        RefTakeExpr& ref = static_cast<RefTakeExpr&>(*node);
        if (ref.m_body.empty()) {
            break;
        }
        const AstNodeBase* operand = ref.m_body[0].get();
        TypeRegistry& treg = m_actx.ctx().types();
        const TypeId opType = m_actx.resolvedType(*operand);
        if (opType == INVALID_TYPE_ID) {
            break;
        }
        const RefType rt = getRefType(getKindFromId(opType));
        if (rt == RefType::kShared || rt == RefType::kWeak || rt == RefType::kUnique || rt == RefType::kPtr) {
            ref.m_opRefKind = rt; // для кодогенерации (unique/ptr - прямой доступ, без guard'а)
            const TypeId pointee = treg.getPointeeType(opType);
            if (pointee != INVALID_TYPE_ID) {
                m_actx.setExprType(node, clearFlag(pointee, SymbolFlag::Inferred));
            }
        } else {
            m_actx.ctx().diag().report(Severity::Error, node->range(), "operator '*' (dereference) requires a reference (shared/weak/unique/ptr), got '{}'",
                                       treg.getFullTypeName(opType));
        }
        // -Wnative-ref: разименование нативной (сырой) ссылки/указателя (`*` - общий оператор
        // для всех видов, но для нативных выводится диагностика о его использовании).
        if (rt == RefType::kPtr || rt == RefType::kRef) {
            m_actx.ctx().report(node->range(), semantic::DiagId::NativeRef, "native (raw) C++ dereference operator is used");
        }
        break;
    }
    case ParserToken::Kind::NativeRefMakeExpr: {
        // Нативный (сырой) C++ оператор `%& var` (address-of): результат - нативная ссылка.
        // Конкретный контекст (`T& name = var` / `T* ptr = &var`) задаёт левый оператор
        // создания/присваивания: VarDecl-семантика перезаписывает m_resultType видом kPtr, когда
        // цель - нативный указатель. Здесь дефолт - kRef (ссылка).
        NativeRefMakeExpr& nat = static_cast<NativeRefMakeExpr&>(*node);
        if (nat.m_body.empty()) {
            break;
        }
        const TypeId opType = m_actx.resolvedType(*nat.m_body[0]);
        if (opType == INVALID_TYPE_ID) {
            break;
        }
        TypeRegistry& treg = m_actx.ctx().types();
        // D8: запрещено брать нативную ссылку из умной (shared/weak/unique) - смешение ссылок.
        const RefType opRef = getRefType(getKindFromId(opType));
        if (opRef == RefType::kShared || opRef == RefType::kWeak || opRef == RefType::kUnique) {
            m_actx.ctx().diag().report(
                Severity::Error, node->range(),
                "cannot take a native reference to a smart reference (shared/weak/unique); native references only point to plain lvalue variables");
            break;
        }
        TypeId res = treg.applyRefType(clearFlag(opType, SymbolFlag::Inferred), RefType::kRef);
        nat.m_resultType = res; // для кодогенерации (локальные символы недоступны)
        m_actx.setExprType(node, res);
        m_actx.ctx().report(node->range(), semantic::DiagId::NativeRef, "native (raw) C++ {} operator is used",
                            getRefType(getKindFromId(res)) == RefType::kPtr ? "address/pointer" : "reference");
        break;
    }
    case ParserToken::Kind::NativeRefTakeExpr: {
        // Нативное разименование `%* ref` - допустимо ТОЛЬКО для нативного указателя (%&, kPtr).
        // Результат - тип pointee. Константность в типе операнда.
        NativeRefTakeExpr& nat = static_cast<NativeRefTakeExpr&>(*node);
        if (nat.m_body.empty()) {
            break;
        }
        const TypeId opType = m_actx.resolvedType(*nat.m_body[0]);
        if (opType == INVALID_TYPE_ID) {
            break;
        }
        TypeRegistry& treg = m_actx.ctx().types();
        const RefType rt = getRefType(getKindFromId(opType));
        if (rt != RefType::kPtr) {
            m_actx.ctx().diag().report(Severity::Error, node->range(), "operator '%*' (native dereference) requires a native pointer '%&' (kPtr), got '{}'",
                                       treg.getFullTypeName(opType));
            break;
        }
        const TypeId pointee = treg.getPointeeType(opType);
        if (pointee != INVALID_TYPE_ID) {
            m_actx.setExprType(node, clearFlag(pointee, SymbolFlag::Inferred));
        }
        m_actx.ctx().report(node->range(), semantic::DiagId::NativeRef, "native (raw) C++ dereference operator is used");
        break;
    }
    default:
        break;
    }
}

// -- Компиляйт-тайм проверка printf-формата (атрибут @[format("printf", ...)]) --
void ExprTyper::checkFormatArgs(CallExpr& call) {
    if (!call.m_callee || call.m_callee->kind() != ParserToken::Kind::Ident) {
        return;
    }
    const Symbol* sym = m_core.resolveSimple(call.m_callee.get(), call.m_callee->text());
    if (!sym || !sym->decl || sym->decl->kind() != ParserToken::Kind::FuncDecl) {
        return;
    }
    const auto& f = static_cast<const FuncDecl&>(*sym->decl);
    const AttrPool& attrs = m_actx.ctx().attrs();
    auto fmt_id = attrs.lookup(attr::Format);
    if (!fmt_id.has_value() || !f.has_attr(*fmt_id)) {
        return;
    }
    const std::vector<std::string>* fargs = f.attr_args(*fmt_id);
    // @[format("printf", string_index, first_to_check)] - ровно три параметра.
    if (!fargs || fargs->size() != 3 || fargs->at(0) != "printf") {
        return; // поддерживается только printf-архетип; параметры валидирует matches_params
    }
    int stringIdx = 0;
    int firstToCheck = 0;
    try {
        stringIdx = std::stoi(fargs->at(1));
        firstToCheck = std::stoi(fargs->at(2));
    } catch (...) {
        return;
    }
    if (stringIdx < 1 || firstToCheck < 1 || !call.m_args || static_cast<int>(call.m_args->size()) < stringIdx) {
        return;
    }
    // Формат-строка - аргумент stringIdx-1 (индексы 1-based); обязана быть строковым литералом.
    const auto& fmtArg = (*call.m_args)[stringIdx - 1];
    if (!fmtArg || fmtArg->kind() != ParserToken::Kind::StrChar) {
        m_actx.ctx().report(fmtArg ? fmtArg->range() : call.range(), semantic::DiagId::Format, "format string is not a string literal");
        return;
    }
    const std::string fmt(fmtArg->text());
    std::vector<format_check::Conversion> convs;
    if (!format_check::parse_printf_format(fmt, convs)) {
        m_actx.ctx().report(fmtArg->range(), semantic::DiagId::Format, "invalid printf format string '{}'", fmt);
        return;
    }
    const TypeRegistry& reg = m_actx.ctx().types();
    for (std::size_t j = 0; j < convs.size(); ++j) {
        const int argPos = firstToCheck - 1 + static_cast<int>(j);
        if (argPos >= static_cast<int>(call.m_args->size())) {
            m_actx.ctx().report(call.range(), semantic::DiagId::Format,
                                "format string requires more arguments than provided (missing argument for conversion '%{}')", std::string(1, convs[j].conv));
            return;
        }
        const auto& arg = (*call.m_args)[argPos];
        if (!arg) {
            continue;
        }
        const TypeId argType = m_actx.resolvedType(*arg);
        if (argType == INVALID_TYPE_ID) {
            continue;
        }
        bool ok = format_check::arg_matches_expect(reg, argType, convs[j].expect);
        // %s: StrChar-ЛИТЕРАЛ уже const char* в C++ (emitExpr → "..."), поэтому допустим,
        // хотя тип StrChar (std::string). Переменная StrChar требует .c_str() → CString.
        if (!ok && convs[j].expect == format_check::Expect::StrChar && reg.getCanonicalTypeId(argType) == reg.getType(type::StrChar) &&
            arg->kind() == ParserToken::Kind::StrChar) {
            ok = true;
        }
        if (!ok) {
            const TypeId c = reg.getCanonicalTypeId(argType);
            std::string typeName = (c != INVALID_TYPE_ID) ? std::string(reg.getFullTypeName(c)) : "?";
            m_actx.ctx().report(arg->range(), semantic::DiagId::Format, "format argument {} expects {} (conversion '{}') but argument has type '{}'",
                                argPos + 1, format_expect_name(convs[j].expect), std::string(1, convs[j].conv), typeName);
        }
    }
}

// -- Компиляйт-тайм проверка строки-формата `"{}"(args)` / `'{}'(args)` --
// callee - строковый литерал (StrWide/StrChar). Сверяем число плейсхолдеров `{}` с числом
// аргументов ({{ / }} - литеральные скобки, аргумент не потребляют) и баланс фигурных скобок.
void ExprTyper::checkFormatStringArgs(CallExpr& call) {
    const auto* fmtNode = call.m_callee.get();
    if (!fmtNode || (fmtNode->kind() != ParserToken::Kind::StrChar && fmtNode->kind() != ParserToken::Kind::StrWide)) {
        return;
    }
    const std::string fmt(fmtNode->text());
    const size_t nArgs = call.m_args ? call.m_args->size() : 0;
    size_t placeholders = 0;
    int depth = 0;
    for (size_t i = 0; i < fmt.size(); ++i) {
        const char c = fmt[i];
        if (c == '{') {
            if (i + 1 < fmt.size() && fmt[i + 1] == '{') { // `{{` - литеральная скобка
                ++i;
                continue;
            }
            ++placeholders;
            ++depth;
            // Явный индекс `{N}` / `{N:spec}`: сверяем N с числом аргументов (иначе
            // std::format сгенерирует сломанный C++, а не понятную диагностику).
            size_t j = i + 1;
            if (j < fmt.size() && fmt[j] >= '0' && fmt[j] <= '9') {
                size_t idx = 0;
                while (j < fmt.size() && fmt[j] >= '0' && fmt[j] <= '9') {
                    idx = idx * 10 + static_cast<size_t>(fmt[j] - '0');
                    ++j;
                }
                if (idx >= nArgs) {
                    m_actx.ctx().report(fmtNode->range(), semantic::DiagId::Format,
                                        "format string '{}' references argument index {} but only {} argument(s) provided", fmt, idx, nArgs);
                }
            }
        } else if (c == '}') {
            if (i + 1 < fmt.size() && fmt[i + 1] == '}') { // `}}` - литеральная скобка
                ++i;
                continue;
            }
            --depth;
        }
    }
    if (depth != 0) {
        m_actx.ctx().report(fmtNode->range(), semantic::DiagId::Format, "unbalanced braces in format string '{}'", fmt);
        return;
    }
    if (placeholders != nArgs) {
        m_actx.ctx().report(fmtNode->range(), semantic::DiagId::Format, "format string '{}' has {} placeholder(s) but {} argument(s) provided", fmt,
                            placeholders, nArgs);
    }
}

void ExprTyper::widenInferredTarget(const AstNodeBase* lhs, TypeId result) {
    if (!lhs || lhs->kind() != ParserToken::Kind::Ident || result == INVALID_TYPE_ID) {
        return;
    }
    Symbol* s = m_actx.symbols().resolveMutable(lhs->text());
    if (!s || !testFlag(s->type, SymbolFlag::Inferred)) {
        return;
    }
    // Нетипизированная `x := _;` (deferred-вывод по записям): первая запись задаёт категорию типа.
    // Последующая запись НЕСОВМЕСТИМОЙ категории (напр. число потом строка) не может быть выведена
    // монотонно → явная Error с требованием аннотации `x:Any := _` (никакого тихого std::any-fallback).
    // Совместимые категории (числовые промоции / одинаковый тип) ведут тип как у обычного inferred.
    if (s->decl && s->decl->kind() == ParserToken::Kind::VarDecl) {
        const auto* vd = static_cast<const VarDecl*>(s->decl);
        if (!vd->m_type && vd->m_initializer && isNoneMarker(vd->m_initializer.get())) {
            const TypeId cur = structuralType(s->type);
            const TypeId nw = structuralType(result);
            if (cur != INVALID_TYPE_ID && nw != INVALID_TYPE_ID && cur != nw) {
                const TypeRegistry& reg = m_actx.ctx().types();
                const TypeId cc = reg.getCanonicalTypeId(cur);
                const TypeId nc = reg.getCanonicalTypeId(nw);
                const bool compatible = (cc == nc) || (isArithmeticGroup(getGroup(getKindFromId(cc))) && isArithmeticGroup(getGroup(getKindFromId(nc))));
                if (!compatible) {
                    std::string disp{lhs->text()};
                    if (!disp.empty() && disp.front() == '$') {
                        disp.erase(0, 1); // DSL-сигил: показываем имя без служебного '$'
                    }
                    m_actx.ctx().diag().report(Severity::Error, lhs->range(),
                                               "cannot assign type '{}' to '{}' whose type was inferred as '{}' from an earlier assignment to the untyped "
                                               "'{} := _' declaration; use explicit '{} :Any := _' to allow values of different types",
                                               reg.getFullTypeName(nc), disp, reg.getFullTypeName(cc), disp, disp);
                    return; // тип категории не перетираем; ошибка останавливает конвейер
                }
            }
        }
    }
    // Живой тип расширяется и сохраняет бит «выведен» (переменная остаётся inferred) и, если
    // переменная константна, бит «константность» (kConstFlag) не теряется при join.
    const bool wasConst = testFlag(s->type, SymbolFlag::Const);
    s->type = setFlag(result, SymbolFlag::Inferred);
    if (wasConst) {
        s->type = setFlag(s->type, SymbolFlag::Const);
    }
    // Обновить выведенный тип на узле объявления (VarDecl), чтобы декларация использовала
    // финальный join после сброса скоуп-стека (транспилятор читает VarDecl::inferredType).
    if (s->decl && s->decl->kind() == ParserToken::Kind::VarDecl) {
        static_cast<VarDecl*>(s->decl)->inferredType = clearFlag(result, SymbolFlag::Inferred);
    }
}

void ExprTyper::checkAssignmentNarrowing(const AstNodeBase* valueNode, TypeId sourceType, TypeId targetType, std::string_view targetName) {
    (void)targetName;
    if (valueNode == nullptr || sourceType == INVALID_TYPE_ID || targetType == INVALID_TYPE_ID) {
        return;
    }
    const TypeRegistry& reg = m_actx.ctx().types();
    const TypeId sc = reg.getCanonicalTypeId(sourceType);
    const TypeId dc = reg.getCanonicalTypeId(targetType);
    const TypeKind sKind = getKindFromId(sc);
    const TypeKind dKind = getKindFromId(dc);
    const Group sg = getGroup(sKind);
    const Group dg = getGroup(dKind);
    // Контракт value-vs-reference (см. types/REFType.md «двухосевая модель»): источник или цель -
    // ссылочный тип. Правила:
    //   * цель-ссылка + источник-значение: создание владеющей обёртки (Shared/Unique) - ВАЛИДНО
    //     (`x : &Int32 := 5`); Weak из значения невозможен (нужна существующая shared-ссылка);
    //   * источник-ссылка + цель-значение: копирование ссылки в значение - нарушение контракта
    //     (диагностика + fixit `*<name>` - локер);
    //   * обе ссылки: оси владения (shared/weak vs unique) и pointee-типы обязаны совпадать.
    const RefType srt = getRefType(sKind);
    const RefType drt = getRefType(dKind);
    const bool sRef = srt != RefType::kValue;
    const bool dRef = drt != RefType::kValue;
    if (sRef || dRef) {
        const auto refAxis = [](RefType r) {
            if (r == RefType::kShared || r == RefType::kWeak) {
                return 1; // shared-ось (Weak строится из Shared)
            }
            if (r == RefType::kUnique) {
                return 2; // unique-ось (отдельное владение)
            }
            return 0; // value
        };
        std::string srcName(reg.getFullTypeName(sc));
        std::string dstName(reg.getFullTypeName(dc));
        if (dRef && !sRef) {
            if (drt == RefType::kWeak) {
                m_actx.ctx().diag().report(
                    Severity::Error, valueNode->range(),
                    "cannot initialize a weak reference from a value '{}'; weak requires an existing shared reference (use '& <shared_var>')", srcName);
            }
            return; // shared/unique из значения - валидно (владеющая обёртка), числовое сужение неприменимо
        }
        if (sRef && !dRef) {
            auto* entry = m_actx.ctx().diag().report(Severity::Error, valueNode->range(),
                                                     "cannot copy a reference '{}' into a value variable '{}'; use '*{}' (locker) to access the value", srcName,
                                                     targetName, valueNode->text());
            if (entry != nullptr && !valueNode->range().isInvalid()) {
                m_actx.ctx().diag().fixit(entry, valueNode->range(), "*" + std::string(valueNode->text()));
            }
            return;
        }
        // Обе ссылки: запрещено смешение нативных (сырых) и умных (shared/weak/unique) ссылок
        // (D8): нельзя положить нативную ссылку в умную и наоборот - разная идеология владения.
        const auto isNativeRefKind = [](RefType r) { return r == RefType::kPtr || r == RefType::kRef || r == RefType::kRref || r == RefType::kPtrPtr; };
        const auto isSmartRefKind = [](RefType r) { return r == RefType::kShared || r == RefType::kWeak || r == RefType::kUnique; };
        if (isNativeRefKind(srt) != isNativeRefKind(drt) && (isNativeRefKind(srt) || isNativeRefKind(drt))) {
            m_actx.ctx().diag().report(Severity::Error, valueNode->range(), "cannot mix native and smart references: cannot assign '{}' to '{}'", srcName,
                                       dstName);
            return;
        }
        // Совпадение осей владения (shared/weak vs unique).
        if (refAxis(srt) != refAxis(drt)) {
            m_actx.ctx().diag().report(Severity::Error, valueNode->range(),
                                       "reference ownership mismatch: cannot assign '{}' to '{}' (shared/weak and unique are different ownership axes)",
                                       srcName, dstName);
            return;
        }
        // И совпадение pointee-типов (канонические).
        const TypeId spo = reg.getPointeeType(sc);
        const TypeId dpo = reg.getPointeeType(dc);
        if (spo != INVALID_TYPE_ID && dpo != INVALID_TYPE_ID && reg.getCanonicalTypeId(spo) != reg.getCanonicalTypeId(dpo)) {
            m_actx.ctx().diag().report(Severity::Error, valueNode->range(), "reference pointee mismatch: cannot assign '{}' to '{}'", srcName, dstName);
        }
        return;
    }
    // Сверхразрядный целочисленный литерал (BigInteger-источник): валиден как BigInteger.
    // В фиксированную целую цель (`x:Int64 := <huge>`) не влезает → ошибка переполнения.
    const TypeId bi = reg.getCanonicalTypeId(reg.getType(type::BigInteger));
    if (sc == bi && valueNode->kind() == ParserToken::Kind::IntLiteral) {
        const Literal& intLit = static_cast<const Literal&>(*valueNode);
        // Влезает ли текст литерала в знаковый Int64 (единый предикат intFitsTarget, Int64-вид).
        const TypeKind int64Kind = getKindFromId(reg.getCanonicalTypeId(reg.getType(type::Int64)));
        if (!intFitsTarget(intLit.text(), int64Kind)) {
            // Цель BigInteger - ок (литерал остаётся BigInteger); фикс. целое/беззнаковое - ошибка.
            if (dg == Group::kIntegers || dg == Group::kUnsigned) {
                m_actx.ctx().diag().report(Severity::Error, valueNode->range(),
                                           "integer literal '{}' exceeds the range of Integer (Int64); use BigInteger or a Double (floating point)",
                                           intLit.text());
            }
            return;
        }
    }
    // Строки: сужение StrWide (широкая) → StrChar (узкая) - всегда ошибка (значение литерала
    // не влияет: любой "…" уже широкий; безопасного сужения и строкового cast нет).
    if (sg == Group::kStrWide && dg == Group::kStrChar) {
        std::string srcName(reg.getFullTypeName(sc));
        m_actx.ctx().diag().report(Severity::Error, valueNode->range(),
                                   "value of type '{}' cannot be narrowed to 'StrChar' (wide string into narrow); use single-quoted '…' for a narrow string",
                                   srcName);
        return;
    }
    // Проверка только для целых групп одной категории: сужение по ширине.
    const bool sNum = (sg == Group::kIntegers || sg == Group::kUnsigned);
    const bool dNum = (dg == Group::kIntegers || dg == Group::kUnsigned);
    if (!sNum || !dNum || sg != dg) {
        return;
    }
    const uint8_t sw = getData(sKind);
    const uint8_t dw = getData(dKind);
    if (sw <= dw) {
        return; // не сужение (шире или тот же размер)
    }
    // Литерал, влезающий в целевой тип → безопасное сужение (без диагностики).
    if (valueNode->kind() == ParserToken::Kind::IntLiteral && intFitsTarget(valueNode->text(), dKind)) {
        return;
    }
    // Сужение (переменная/неизвестное шире цели) → ошибка + fixit «use cast :Type(expr)».
    std::string dstName(reg.getFullTypeName(dc));
    std::string srcName(reg.getFullTypeName(sc));
    auto* entry = m_actx.ctx().diag().report(Severity::Error, valueNode->range(), "value of type '{}' cannot be narrowed to '{}' (use cast :{}(expr))", srcName,
                                             dstName, dstName);
    if (entry != nullptr && !valueNode->range().isInvalid()) {
        std::string replacement = std::format(":{}({})", dstName, valueNode->text());
        m_actx.ctx().diag().fixit(entry, valueNode->range(), replacement);
    }
}

} // namespace trust
