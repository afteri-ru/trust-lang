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
            elems.emplace_back(std::string(a.text()), clearInferred(a.resultType));
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
                    raw.push_back(clearInferred(et));
                }
            }
            // Узкая разрядность (как у литерала `[...]`): `:Array(1,2,3)` → std::vector<int8_t>.
            elemType = arrayElementJoin(raw);
        }
        if (elemType == INVALID_TYPE_ID) {
            elemType = reg.getType(type_generic::Any);
        }
        elemType = clearInferred(elemType);
        const TypeId arrBase = reg.getOrCreateArrayType(elemType, {static_cast<uint64_t>(dl.m_body.size())});
        if (arrBase != INVALID_TYPE_ID) {
            // Константность (`:Array^`) - kConstFlag-бит в TypeId (withConst), а не поле типа.
            const TypeId arr = isConst ? withConst(arrBase) : arrBase;
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
            const TypeId ct = clearInferred(et);
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
        elemType = clearInferred(explicitTypes.front());
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
    elemType = clearInferred(elemType);
    const TypeId arrType = reg.getOrCreateArrayType(elemType, {static_cast<uint64_t>(node.m_body.size())});
    if (arrType != INVALID_TYPE_ID) {
        node.arrayType = arrType;
        m_actx.setExprType(&node, arrType);
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
        case Group::kRationals:
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

// Тип значения элемента словаря по его узлу (литерал → literalType; иначе resolvedType).
TypeId ExprTyper::dictElementType(const AstNodeBase* valueNode) const {
    if (!valueNode) {
        return INVALID_TYPE_ID;
    }
    if (is_literal_kind(valueNode->kind())) {
        // Тип значения литерала ВЫВЕДЕН из литерала (auto-Bool `0`/`1` продвигается в арифметике).
        return withInferred(literalType(static_cast<const Literal&>(*valueNode), m_actx.ctx().types()));
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
    const TypeId lt = m_actx.resolvedType(*b.m_left);
    const TypeId rt = m_actx.resolvedType(*b.m_right);
    const TypeRegistry& reg = m_actx.ctx().types();

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

    // Продвижение auto-Bool в арифметике: тип, ВЫВЕДЕННЫЙ из литерала 0/1 или из
    // inferred-переменной, продвигается по общим правилам приведения (C++ bool→int → Int32).
    // Явный Bool (:Bool, результат сравнения/логики) в арифметике - ошибка компиляции
    // (нельзя привести к числу). Compare/Logical и простое присваивание '=' не затрагиваются.
    TypeId elt = lt, ert = rt;
    const bool arithmetic = !(b.kind() == ParserToken::Kind::CompareOp || b.kind() == ParserToken::Kind::LogicalOp) &&
                            !(b.kind() == ParserToken::Kind::AssignOp && utils::isPlainAssignOp(b.text()));
    if (arithmetic) {
        const TypeId boolT = reg.getCanonicalTypeId(reg.getType(type::Bool));
        auto promoteBool = [&](const AstNodeBase* operand, TypeId t, TypeId& out) {
            if (t == INVALID_TYPE_ID || reg.getCanonicalTypeId(t) != boolT) {
                return;
            }
            if (typeIsInferred(t)) {
                out = reg.getType(type::Int32); // bool→int (C++ promotion)
            } else {
                m_actx.ctx().diag().report(Severity::Error, operand->range(),
                                           "cannot use Bool value in arithmetic '{}'; use an explicit integer type or a cast", b.text());
            }
        };
        promoteBool(b.m_left.get(), lt, elt);
        promoteBool(b.m_right.get(), rt, ert);
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
                if (!typeIsInferred(s->type) && (isAssignOp || compound)) {
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
                if (typeIsConst(s->type)) {
                    if (!makeConst) {
                        m_actx.ctx().diag().report(Severity::Error, b.range(), "cannot assign to constant variable '{}'", s->name);
                    }
                } else if (makeConst) {
                    // Финальная запись `x^ = ...`: переменная становится константной (бит
                    // kConstFlag на Symbol::type). Декларация при этом остаётся не-const (см.
                    // transpiler::generateVarDeclToFile - const объявления берётся из атрибута узла).
                    s->type = withConst(s->type);
                }
            }
        }
        // Расширение выводимой цели по истории присвоений - только для присваиваний
        // (AssignOp "=", "+=" или составной MathOp "+=").
        if (isAssignOp || compound) {
            TypeId widen = result;
            // Автоматически выведенный Bool, используемый в составной числовой арифметике
            // (например `mult := 1; mult += 1;`), расширяется до максимального Int (Int64):
            // Bool - вырожденное целое, а в однопроходной типизации нет «оператора в цикле»,
            // поэтому расширяем по самому факту составного присваивания. Явный `:Bool` так
            // НЕ расширяется - для него это ошибка (явный тип фиксирован).
            if (!utils::isPlainAssignOp(b.text()) && b.m_left && b.m_left->kind() == ParserToken::Kind::Ident) {
                if (Symbol* s = m_actx.symbols().resolveMutable(b.m_left->text())) {
                    const TypeRegistry& reg = m_actx.ctx().types();
                    if (s->type != INVALID_TYPE_ID && reg.getCanonicalTypeId(s->type) == reg.getType(type::Bool)) {
                        if (typeIsInferred(s->type)) {
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
                const TypeId t = m_actx.resolvedType(*v.m_initializer);
                if (t != INVALID_TYPE_ID) {
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
                    v.inferredType = clearInferred(t);
                    if (Symbol* s = m_actx.symbols().resolveMutable(v.text())) {
                        // Живой тип символа несёт бит «выведен» (для join/продвижения) и, при
                        // константности ('^' → attr::ReadOnly), бит «константность» (kConstFlag) -
                        // источник префикса `const ` в кодогенерации переменной.
                        s->type = v.has_attr(m_actx.ctx().attrs(), attr::ReadOnly) ? withConst(t) : t;
                    }
                } else if (v.m_initializer->kind() != ParserToken::Kind::TypeName) {
                    if (auto aid = m_actx.ctx().types().findType("Any")) {
                        // Инициализатор без выводимого типа (C++-вставка `{% %}`, вызов с
                        // неизвестным результатом, отрицательный литерал) - переменная по природе
                        // std::any. Маркируем тип ЯВНО (Any), чтобы транспилятор НЕ угадывал тихим
                        // fallback на std::any (AGENTS rule 5): INVALID у переменной с инициализатором
                        // в кодогенерации - ошибка вывода. Голый `:T` сюда не попадает - это
                        // невалидная запись `x := :Int32` (диагностируется в analyzeVarDecl).
                        v.inferredType = *aid;
                        if (Symbol* s = m_actx.symbols().resolveMutable(v.text())) {
                            s->type = v.has_attr(m_actx.ctx().attrs(), attr::ReadOnly) ? withConst(*aid) : *aid;
                        }
                    }
                }
            } else {
                // Явно-типизированная: проверить сужение инициализатора в целевой тип.
                auto targetOpt = m_actx.resolveType(*v.m_type);
                if (targetOpt.has_value()) {
                    checkAssignmentNarrowing(v.m_initializer.get(), m_actx.resolvedType(*v.m_initializer), *targetOpt, v.text());
                }
            }
        } else if (v.m_type == nullptr) {
            // Нетипизированное forward-объявление (`x := ...;`): и инициализатора, и типа нет -
            // по природе std::any. Маркируем тип ЯВНО (Any), как и для тип-less инициализаторов,
            // чтобы транспилятор единообразно эмитил `emitTypeName(inferred)` без ветки угадывания.
            if (auto aid = m_actx.ctx().types().findType("Any")) {
                v.inferredType = *aid;
                if (Symbol* s = m_actx.symbols().resolveMutable(v.text())) {
                    s->type = v.has_attr(m_actx.ctx().attrs(), attr::ReadOnly) ? withConst(*aid) : *aid;
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
        // Литерал: кешируем тип (literalType), чтобы resolvedType не пересчитывал его
        // при каждом обращении (маленький лист - но единый кеш типов выражений).
        const TypeId t = literalType(static_cast<const Literal&>(*node), m_actx.ctx().types());
        if (t != INVALID_TYPE_ID) {
            // Тип значения литерала всегда ВЫВЕДЕН из литерала → маркируем битом inferred
            // (auto-Bool `0`/`1` продвигается в арифметике; см. typeBinaryResult).
            const TypeId vt = withInferred(t);
            // Запоминаем TypeId на узле - транспилятор литерала словаря читает его (kind из
            // getKindFromId, C++-имя из реестра), не пересчитывая диапазоны литералов.
            static_cast<Literal&>(*node).typeId = vt;
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
                m_actx.setExprType(node, withInferred(t));
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
    if (!s || !typeIsInferred(s->type)) {
        return;
    }
    // Живой тип расширяется и сохраняет бит «выведен» (переменная остаётся inferred) и, если
    // переменная константна, бит «константность» (kConstFlag) не теряется при join.
    const bool wasConst = typeIsConst(s->type);
    s->type = withInferred(result);
    if (wasConst) {
        s->type = withConst(s->type);
    }
    // Обновить выведенный тип на узле объявления (VarDecl), чтобы декларация использовала
    // финальный join после сброса скоуп-стека (транспилятор читает VarDecl::inferredType).
    if (s->decl && s->decl->kind() == ParserToken::Kind::VarDecl) {
        static_cast<VarDecl*>(s->decl)->inferredType = clearInferred(result);
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
