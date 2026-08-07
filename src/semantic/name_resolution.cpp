#include "semantic/name_resolution.hpp"

#include "semantic/format_check.hpp"
#include "semantic/symbol_table.hpp"
#include "semantic/type_inference.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
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
// Влезает ли десятичный целочисленный литерал в целевой целый тип (по группе/ширине).
// Границы — единый источник `fitsIntegerValue` (type_inference.hpp).
bool intFitsTarget(std::string_view text, TypeKind targetKind) noexcept {
    const Group g = getGroup(targetKind);
    if (g != Group::kIntegers && g != Group::kUnsigned) {
        return true; // не-целая цель (float) — целочисленный литерал считается безопасным
    }
    unsigned long long v = 0;
    if (!parseDecimalUInt(text, v)) {
        return false; // отрицательный/нецелой литерал не типизируем как положительный
    }
    return fitsIntegerValue(g, getData(targetKind), v);
}

// Является ли TypeId универсальным словарём `:Dict` (канонический). Единый предикат для
// детекции словарного операнда в `[]= ... dict` (spread-merge) — сравнение по каноническому id.
bool isDictTypeId(const TypeRegistry& reg, TypeId tid) noexcept {
    if (tid == INVALID_TYPE_ID) {
        return false;
    }
    return reg.getCanonicalTypeId(tid) == reg.getType(type::Dict);
}

// Является ли имя простым (без сигила/квалификатора) — кандидат на нормализацию `x → $x`
// (опция -Wsigil) и на «$x-first» резолв. Сигилы: $ локальная, % нативная, @ макро, \ модуль,
// : тип, . поле. Квалифицированное (::) имя — не простое.
bool isSimpleVarName(std::string_view name) noexcept {
    if (name.empty()) {
        return false;
    }
    const char c = name.front();
    if (c == '$' || c == '%' || c == '@' || c == '\\' || c == ':' || c == '.') {
        return false;
    }
    return name.find("::") == std::string_view::npos;
}

// Человекочитаемое имя ожидаемой категории printf-аргумента (для диагностики).
const char* format_expect_name(format_check::Expect expect) noexcept {
    using format_check::Expect;
    switch (expect) {
    case Expect::Integer:
        return "integer";
    case Expect::Unsigned:
        return "unsigned integer";
    case Expect::Float:
        return "floating point";
    case Expect::StrChar:
        return "string";
    case Expect::Pointer:
        return "pointer";
    }
    return "value";
}
} // namespace

NameResolutionPass::NameResolutionPass(AnalysisContext& actx)
: m_actx(actx) {
}

void NameResolutionPass::addHook(std::unique_ptr<InlineAnalysisHook> hook) {
    if (hook) {
        m_hooks.push_back(std::move(hook));
    }
}

void NameResolutionPass::finalize() {
    for (auto& hook : m_hooks) {
        hook->finalize();
    }
}

// ── Скоупы с уведомлением хуков ──

void NameResolutionPass::enterScope(const AstNodeBase& node) {
    m_actx.symbols().push(&node);
    for (auto& hook : m_hooks) {
        hook->enterScope();
    }
}

void NameResolutionPass::exitScope() {
    for (auto& hook : m_hooks) {
        hook->exitScope();
    }
    m_actx.symbols().pop();
}

// ── Обход ──

void NameResolutionPass::run(std::vector<AstNodePtr>& ast_nodes) {
    for (auto& node : ast_nodes) {
        if (node) {
            analyzeNode(node);
        }
    }
}

// Однопроходный обход: имя должно быть объявлено до использования. Модуль, блоки
// и ScopeBlock открывают вложенный скоуп; объявления регистрируются в текущем скоупе;
// Ident разрешается поиском вверх по стеку. Раскрытие контекст-макросов выполняет
// всегда-подключённый хук ContextMacroExpander (в начале обработки каждого узла).
void NameResolutionPass::analyzeNode(AstNodePtr& self) {
    if (!self) {
        return;
    }

    // Раскрытие контекст-макросов (ContextMacro → Literal/IdentName, квалификатор @::
    // в именах) выполняет всегда-подключённый хук ContextMacroExpander. Он вызывается
    // ДО обработки ядра, чтобы имя объявления было раскрыто до регистрации, а ContextMacro
    // — заменён до резолва. Возврат true означает, что узел заменён хук-ом (ядро его
    // не обрабатывает, но продолжает обход детей).
    bool consumed = false;
    for (auto& hook : m_hooks) {
        if (hook->onNode(self)) {
            consumed = true;
        }
    }

    // Скоуп-контейнеры (модуль/блок) открывают вложенный скоуп на время обхода тела.
    // ЛЮБОЙ блок (в т.ч. цикл while/do-while) создаёт скоуп — это локальность переменных:
    // объявленные в теле цикла видны только внутри него. Детекция цикла (для диагностик
    // деструктуризации) — по creator-скоупа в стеке (isInLoop).
    switch (self->kind()) {
    case ParserToken::Kind::ModuleDecl:
    case ParserToken::Kind::sequence:
    case ParserToken::Kind::ScopeBlock:
    case ParserToken::Kind::WhileStmt:
    case ParserToken::Kind::DoWhileStmt:
        enterScope(*self);
        analyzeChildren(self);
        exitScope();
        return;
    case ParserToken::Kind::FuncDecl: {
        // Имя функции регистрируется в ТЕКУЩЕМ (внешнем) скоупе, затем открывается
        // скоуп функции, в котором видны параметры и тело.
        auto& f = static_cast<FuncDecl&>(*self);
        analyzeFuncDecl(f);
        enterScope(f);
        declareFuncParams(f);
        analyzeChildren(self);
        exitScope();
        return;
    }
    case ParserToken::Kind::DestructureDecl:
        // Деструктуризация `item, dict := ... source;`: первый target объявляется локальной
        // std::any-переменной (первый элемент), источник мутируется pop_front. Цели не обходим
        // общим механизмом (это объявления, не ссылки).
        analyzeDestructure(static_cast<DestructureDecl&>(*self));
        return;
    case ParserToken::Kind::DictLiteral:
        // Литерал словаря: анализируем значения элементов (имена-метки не резолвим).
        analyzeDictLiteral(static_cast<Sequence&>(*self));
        return;
    case ParserToken::Kind::ArrayInit:
        // Литерал массива `[1,2,3,]` / `[1,2,3,]:Int32`: анализ элементов + вывод типа
        // элемента + интернирование структурного Array<Elem> (см. analyzeArrayInit).
        analyzeArrayInit(static_cast<DictLiteralNode&>(*self));
        return;
    case ParserToken::Kind::RangeExpr:
        // Литерал диапазона: резолв/типизация операндов + элементный тип (join).
        analyzeRangeExpr(static_cast<RangeExpr&>(*self));
        return;
    case ParserToken::Kind::MemberAccess:
    case ParserToken::Kind::ArrayAccess:
        // Доступ к элементу словаря: объект анализируется, поле-имя не резолвится,
        // статический индекс проверяется по размерности объекта.
        analyzeAccess(static_cast<Binary&>(*self));
        return;
    default:
        break;
    }

    // Обработка узла по kind (если он не был заменён хук-ом) + полный обход детей.
    if (!consumed) {
        handleNode(self);
    }
    analyzeChildren(self);

    // Пост-порядковая типизация выражения/объявления (после того как дети уже
    // проанализированы и типизированы): вычисляет тип результата выражения и
    // расширяет выводимый (inferred) тип целевой переменной по истории присвоений.
    typeExpr(self.get());
}

// Обход реальных детей через единый источник AstNodeBase::collectChildren (ссылки на
// слоты, чтобы хук мог заменять узлы). Не открывает скоупы — это делает analyzeNode.
void NameResolutionPass::analyzeChildren(AstNodePtr& self) {
    if (!self) {
        return;
    }
    std::vector<AstNodePtr*> slots;
    self->collectChildren(slots);
    for (auto* child : slots) {
        if (child) {
            analyzeNode(*child);
        }
    }
}

// Анализ литерала словаря. Контракт: все элементы m_body — ArgNode (имя в text(), значение в
// m_value), строятся из канонических пар грамматики `args` (term_to_ast::visit_DICT).
// Значение анализируем полностью (резолв/типизация); имя-метку НЕ резолвим как переменную и
// НЕ регистрируем в таблице символов. Тип значения сохраняем на элементе (ArgNode::resultType
// из resolvedType) — единый источник для кодогенерации TypedValue (не только Literal::typeId).
void NameResolutionPass::analyzeDictLiteral(Sequence& dict_node) {
    // Enum/Variant-объявление (ПОСТФИКС `(...):Enum`/`(...):Variant`): это правая часть `::=`,
    // обрабатывается analyzeTypeDecl (analyzeEnumDecl/analyzeVariantDecl); как обычный словарь НЕ
    // анализируется (иначе голые члены `B` резолвились бы как переменные). Префикс `:Enum(...)`
    // — НЕ объявление (type-call): голые аргументы = значения, резолвятся как обычно.
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
        analyzeNode(a.m_value);
        a.resultType = m_actx.resolvedType(*a.m_value);
    }
    // Тип литерала определяется по АННОТАЦИИ m_type через реестр типов (никаких строк/enum):
    //   - если аннотация резолвится в тип Tuple → kind узла меняется на Tuple, а тип выражения —
    //     на интернированный структурный кортеж (TupleTypeData; источник `t.name`/`t.0`);
    //   - иначе (аннотация-каст/конструктор или её нет) — тип литерала = резолвленной аннотации
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
            // Константность (`:Array^`) — kConstFlag-бит в TypeId (withConst), а не поле типа.
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
// Элементы — ArgNode (имя пустое, значение в m_value, явный тип в m_type). Тип элемента: явная
// аннотация `]:Type` (приоритет), затем явная аннотация элемента (`2:Int8`), затем общий тип
// (если все элементы одного канонического типа — напр. вложенные Array<Elem> для многомерных),
// затем join'элементных типов. Результат — интернированный структурный массив `Array<Elem>`
// (ArrayTypeData); тип сохраняется на узле (DictLiteralNode::arrayType) и в кеше выражений.
// Вложенные массивы-литералы строят многомерный `Array<Array<...>>` (анализ работает;
// «не реализовано» — только на кодогенерации тензора).
void NameResolutionPass::analyzeArrayInit(DictLiteralNode& node) {
    TypeRegistry& reg = m_actx.ctx().types();
    std::vector<TypeId> raw;           // сырые (clearInferred) типы элементов
    std::vector<TypeId> explicitTypes; // типы из явных аннотаций элементов (`2:Int8`)
    bool hasArrayElement = false;      // хотя бы один элемент — массив (многомерный литерал)
    TypeId firstArrayElemType = INVALID_TYPE_ID;
    for (auto& el : node.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        auto& a = static_cast<ArgNode&>(*el);
        TypeId et = INVALID_TYPE_ID;
        if (a.m_value) {
            analyzeNode(a.m_value);
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
            // Элемент — массив (вложенный литерал/многомерность): детектируем независимо от типа.
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
    // (гетерогенные внутренние массивы, напр. `[[1,2],[3,4]]`), берём тип первого массива-элемента —
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
// канонического типа — берём его; строки не смешиваются с числами; несовместимое → INVALID (Any).
TypeId NameResolutionPass::arrayElementJoin(const std::vector<TypeId>& elementTypes) const {
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
            hasInt = true; // Bool — вырожденное целое
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
// (как `:Dict`): тип ВЫРАЖЕНИЯ — `:Range`, а элементный тип (Int/Rational/Float/Any) выводится
// join'ом типов start/stop/step и параметризует `trust::Range<Elem>` при кодогенерации.
// Элементы должны быть арифметическими (Int/UInt/Float/Rational/Bool) или Any; строки и прочие
// — диагностика. Элементный тип: Rational при любом рациональном операнде, иначе Double при
// любом float, иначе Int64/UInt64/Bool; несовместимое/неизвестное → INVALID (Any).
void NameResolutionPass::analyzeRangeExpr(RangeExpr& range_node) {
    TypeRegistry& reg = m_actx.ctx().types();
    std::vector<TypeId> types;
    types.reserve(range_node.m_body.size());
    for (std::size_t i = 0; i < range_node.m_body.size(); ++i) {
        auto& child = range_node.m_body[i];
        if (!child) {
            continue;
        }
        analyzeNode(child);
        TypeId t = m_actx.resolvedType(*child);
        // Явная аннотация типа операнда (`stop:Type`, напр. `0..100:Rational`) имеет приоритет
        // над выведенным типом: грамматика кладёт её в m_type терма-операнда, конвертер — в
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
    // Тип выражения — параметризованный структурный Range<Elem> (элементный тип Elem):
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

// Статическая размерность объекта: для литерала словаря — число элементов; для переменной —
// свойство dims символа (из инициализатора-литерала). -1 = неизвестна.
int64_t NameResolutionPass::dictSizeOf(const AstNodeBase* obj) const {
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

// Имя и значение элемента коллекции из m_body. Элемент — ArgNode (dict/enum/variant) или
// общий узел (ArrayInit/прочее). Для ArgNode: имя=text(), значение=m_value; иначе элемент сам
// является значением (позиционный). Единый источник чтения элемента для семантики.
static void collectionElementNameValue(const AstNodeBase* el, std::string& name, const AstNodeBase*& value) {
    name.clear();
    value = el;
    if (!el) {
        return;
    }
    if (el->kind() == ParserToken::Kind::ArgNode) {
        const auto& a = static_cast<const ArgNode&>(*el);
        name = std::string(a.text());
        value = a.m_value.get();
    }
}

// Извлечение члена enum/variant из ArgNode: имя, значение (null — безнарный), явный тип.
// Безнарный член `HIGH` (имя="" и значение-Ident) — имя лежит в значении (Ident), значение
// отбрасывается (это имя члена, а не значение). Тип члена — напрямую из ArgNode.m_type.
struct EnumVariantMember {
    std::string name;
    AstNodePtr value; // nullptr — безнарный (нет значения)
    AstNodePtr type;  // явный тип (nullptr — нет)
};
static EnumVariantMember enumVariantMember(const ArgNode& a) {
    EnumVariantMember m;
    m.name = std::string(a.text());
    m.value = a.m_value;
    m.type = a.m_type;
    if (m.name.empty() && m.value && m.value->kind() == ParserToken::Kind::Ident) {
        m.name = std::string(m.value->text());
        m.value = nullptr;
    }
    return m;
}

// Тип значения элемента словаря по его узлу (литерал → literalType; иначе resolvedType).
TypeId NameResolutionPass::dictElementType(const AstNodeBase* valueNode) const {
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
// Возвращаются СЫРЫЕ типы (с битом inferred у литералов) — по нему определяется нетипизированный
// словарь. Неизвестный/неприменимый элемент → INVALID_TYPE_ID. Пустой вектор — типы недоступны.
std::vector<TypeId> NameResolutionPass::dictElementTypes(const AstNodeBase* src) const {
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
// Нечисловой/неизвестный → INVALID_TYPE_ID (→ Any). Классификация — по битовой структуре TypeKind
// (getKindFromId/getGroup), а НЕ по строковым именам типов.
TypeId NameResolutionPass::naturalRuntimeType(TypeId nominal) const {
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
// Классификация — по Group (TypeKind), без строковых имён типов.
TypeId NameResolutionPass::joinElementTypes(const std::vector<TypeId>& naturalized) const {
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
// ArrayAccess). INVALID — тип неизвестен (гетерогенный/динамический) → Any.
TypeId NameResolutionPass::dictFieldTypeOf(const Binary& access) const {
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

// Доступ к элементу словаря. MemberAccess (имя `d.two` или статический индекс `d.1`) и
// ArrayAccess (динамический индекс `d[1]`). Объект анализируется; имя поля справа от '.'
// НЕ резолвится как переменная; статический индекс проверяется по размерности объекта.
void NameResolutionPass::analyzeAccess(Binary& n) {
    if (n.m_left) {
        analyzeNode(n.m_left);
    }
    // Вызов метода на объекте: obj.method(args) — MemberAccess(left=obj, right=CallExpr).
    if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && n.m_right->kind() == ParserToken::Kind::CallExpr) {
        handleMethodCall(n);
        return;
    }
    // Доступ к кортежу `t.name` / `t.0` / `t[idx]`: левый операнд — структурный Tuple-тип.
    {
        const TypeRegistry& treg = m_actx.ctx().types();
        const TypeId leftT = n.m_left ? treg.getCanonicalTypeId(m_actx.resolvedType(*n.m_left)) : INVALID_TYPE_ID;
        if (leftT != INVALID_TYPE_ID && treg.isTypeDataKind(leftT, TypeDataKind::kTuple)) {
            resolveTupleAccess(n, leftT);
            return;
        }
        // Доступ к элементу массива `a[i]` / `a.0`: левый операнд — структурный Array-тип.
        if (leftT != INVALID_TYPE_ID && treg.isArrayType(leftT)) {
            resolveArrayAccess(n, leftT);
            return;
        }
        // Доступ к члену enum через имя типа: `Color.RED` → тип enum. Осознанное решение:
        // члены не несут методов, вся работа идёт через имя типа (см. MEMORY.md).
        if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && leftT != INVALID_TYPE_ID && isEnumType(leftT, treg)) {
            const auto* ed = treg.getTypeDataAs<EnumTypeData>(leftT);
            if (ed) {
                const std::string mname = std::string(n.m_right->text());
                bool found = false;
                for (const auto& m : ed->members) {
                    if (m.name == mname) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    m_actx.ctx().diag().report(Severity::Error, n.range(), "enum '{}' has no member '{}'", treg.getFullTypeName(leftT), mname);
                    n.resultType = n.commonType = INVALID_TYPE_ID;
                    m_actx.setExprType(&n, INVALID_TYPE_ID);
                } else {
                    n.resultType = n.commonType = leftT;
                    m_actx.setExprType(&n, leftT);
                }
                return;
            }
        }
        // Доступ к члену Variant через имя типа: `Value.RED` → тип ЭТОГО члена (гетерогенный:
        // у каждого члена свой тип). Работа идёт только через имя типа (как и для enum).
        if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && leftT != INVALID_TYPE_ID && isVariantType(leftT, treg)) {
            const auto* vd = treg.getTypeDataAs<VariantTypeData>(leftT);
            if (vd) {
                const std::string mname = std::string(n.m_right->text());
                TypeId mtype = INVALID_TYPE_ID;
                for (const auto& m : vd->members) {
                    if (m.name == mname) {
                        mtype = m.type;
                        break;
                    }
                }
                if (mtype == INVALID_TYPE_ID) {
                    m_actx.ctx().diag().report(Severity::Error, n.range(), "variant '{}' has no member '{}'", treg.getFullTypeName(leftT), mname);
                    n.resultType = n.commonType = INVALID_TYPE_ID;
                    m_actx.setExprType(&n, INVALID_TYPE_ID);
                } else {
                    n.resultType = n.commonType = mtype;
                    m_actx.setExprType(&n, mtype);
                }
                return;
            }
        }
    }
    if (n.kind() == ParserToken::Kind::ArrayAccess) {
        // Динамический индекс — обычное выражение (резолв/типизация).
        if (n.m_right) {
            analyzeNode(n.m_right);
        }
    } else {
        // MemberAccess: m_right — имя поля или статический индекс (литерал). Не резолвим.
        if (n.m_right && n.m_right->kind() == ParserToken::Kind::IntLiteral) {
            // Статический индекс `d.1`: проверка по статической размерности объекта.
            const int64_t size = dictSizeOf(n.m_left.get());
            if (size < 0) {
                m_actx.ctx().diag().report(Severity::Error, n.range(), "static dict index requires a compile-time known size (use d[expr] for dynamic access)");
            } else {
                unsigned long long idx = 0;
                if (parseDecimalUInt(n.m_right->text(), idx) && static_cast<int64_t>(idx) >= size) {
                    // Ошибка на доступе к индексу: каретка на всём `d.N` (Clang-style).
                    m_actx.ctx().diag().report(Severity::Error, n.range(), "static dict index {} out of range (size {})", idx, size);
                }
            }
        }
    }
    // Тип поля: конкретный (из Dims литерала: `d.two` → Int8/...) или Any (гетерогенный/
    // неизвестный). Сохраняем на узле (транспилятор/каст читают тип результата).
    const TypeId t = dictFieldTypeOf(n);
    n.resultType = n.commonType = t;
    m_actx.setExprType(&n, t);
}

// ── Доступ к элементу кортежа: t.name / t.0 / t[idx] ──
// Левый операнд — структурный Tuple-тип (TupleTypeData). Резолвим имя/статический индекс в
// списке элементов; тип результата = тип элемента. Для динамического индекса `t[expr]`
// (без константы) — статически нерезолвимо (std::get требует константу) → диагностика.
void NameResolutionPass::resolveTupleAccess(Binary& n, TypeId tupleType) {
    const TypeRegistry& reg = m_actx.ctx().types();
    const auto* td = reg.getTypeDataAs<TupleTypeData>(tupleType);
    EXPECT(td && "resolveTupleAccess: not a structural tuple type");
    int64_t index = -1;
    const bool isMember = n.kind() == ParserToken::Kind::MemberAccess;
    if (n.m_right && n.m_right->kind() == ParserToken::Kind::IntLiteral) {
        unsigned long long v = 0;
        if (parseDecimalUInt(n.m_right->text(), v)) {
            index = static_cast<int64_t>(v);
        }
    } else if (isMember && n.m_right) {
        const std::string name = std::string(n.m_right->text());
        for (size_t i = 0; i < td->elements.size(); ++i) {
            if (td->elements[i].name == name) {
                index = static_cast<int64_t>(i);
                break;
            }
        }
        if (index < 0) {
            m_actx.ctx().diag().report(Severity::Error, n.range(), "tuple has no field '{}'", name);
            m_actx.setExprType(&n, INVALID_TYPE_ID);
            return;
        }
    }
    if (index < 0 || index >= static_cast<int64_t>(td->elements.size())) {
        if (isMember) {
            m_actx.ctx().diag().report(Severity::Error, n.range(), "tuple index {} out of range (size {})", index, td->elements.size());
        } else {
            m_actx.ctx().diag().report(Severity::Error, n.range(), "tuple dynamic index is not supported: std::get requires a compile-time constant index");
        }
        m_actx.setExprType(&n, INVALID_TYPE_ID);
        return;
    }
    const TypeId et = td->elements[static_cast<size_t>(index)].type;
    n.resultType = n.commonType = et;
    n.tupleIndex = index; // транспилятор: std::get<index>(obj)
    m_actx.setExprType(&n, et);
}

// Доступ к элементу массива `a[i]` / `a.0`: левый операнд — структурный Array-тип.
// Тип результата = элементный тип массива (ArrayTypeData::elementType). Статический индекс
// (литерал) проверяется по известной размерности массива. Индекс-выражение анализируется.
void NameResolutionPass::resolveArrayAccess(Binary& n, TypeId arrayType) {
    const TypeRegistry& reg = m_actx.ctx().types();
    const TypeId et = reg.arrayElementType(arrayType);
    // Индекс — выражение: анализируем/типизируем (для `a[expr]`).
    if (n.m_right) {
        analyzeNode(n.m_right);
    }
    // Статический индекс: проверка границы по известной размерности (dims.front()).
    const auto& dims = reg.arrayDimensions(arrayType);
    if (!dims.empty() && n.m_right && n.m_right->kind() == ParserToken::Kind::IntLiteral) {
        unsigned long long idx = 0;
        if (parseDecimalUInt(n.m_right->text(), idx) && idx >= dims.front()) {
            m_actx.ctx().diag().report(Severity::Error, n.range(), "array index {} out of range (size {})", idx, dims.front());
            n.resultType = n.commonType = INVALID_TYPE_ID;
            m_actx.setExprType(&n, INVALID_TYPE_ID);
            return;
        }
    }
    n.resultType = n.commonType = et;
    n.lhsType = arrayType; // транспилятор: определяет, что это доступ к массиву (std::vector::at)
    m_actx.setExprType(&n, et);
}

// ── Вызов метода на объекте: obj.method(args) ──
// По типу объекта ищет метод в реестре типов (TypeRegistry::findMethod), проверяет наличие и
// количество аргументов по сигнатуре, типизирует результат возвращаемым типом. Метод — это
// функциональный тип (метод и функция — одно и то же), поэтому проверка аргументов идёт по
// FunctionTypeData::paramTypes единым путём с функциями. Проверка происходит ДО генерации C++.
void NameResolutionPass::handleMethodCall(Binary& n) {
    const auto& call = static_cast<const CallExpr&>(*n.m_right);
    const std::string mname = call.m_callee ? std::string(call.m_callee->text()) : std::string();
    // Не-const: instantiateRangeMethod интернирует функциональный тип (мутирует реестр).
    TypeRegistry& reg = m_actx.ctx().types();
    const TypeId objType = n.m_left ? reg.getCanonicalTypeId(m_actx.resolvedType(*n.m_left)) : INVALID_TYPE_ID;
    if (objType == INVALID_TYPE_ID) {
        // Тип объекта неизвестен (напр. Any) — не можем проверить метод; типизируем как Any.
        n.resultType = n.commonType = m_actx.ctx().types().getType(type_generic::Any);
        m_actx.setExprType(&n, n.resultType);
        return;
    }
    // Тип объекта сохраняем в lhsType (для кодгена: нативное имя метода через findMethodInfo и
    // const_cast<const T&>). Кодген не может восстановить его сам (скоуп-стек сброшен к глобальному).
    n.lhsType = objType;
    const auto methodInfo = reg.findMethodInfo(objType, mname);
    if (!methodInfo) {
        const std::string tname = std::string(reg.getFullTypeName(objType));
        m_actx.ctx().diag().report(Severity::Error, n.range(), "type '{}' has no method '{}'", tname, mname);
        m_actx.setExprType(&n, INVALID_TYPE_ID);
        return;
    }
    // Интернированная сигнатура метода (TypeId). Нативность/константность для кодгена — из
    // methodInfo->key (полный ключ с '%'/'^'); const-вызов (`obj.method^()`) кодген определяет по
    // attr::ReadOnly на вызове (см. convertAttrsToNode/CallExpr).
    TypeId funcType = methodInfo->funcType;
    // Параметризованный Range<Elem> (и абстрактный `:Range`): методы объявлены на `:Range` с
    // типовым параметром T (Group::kTemplateParam); подставляем T→Elem, чтобы `$a.at(0)` и
    // `$a.start()` возвращали ЭЛЕМЕНТНЫЙ тип (Int64/Rational/...), а не типовой параметр.
    if (reg.isRangeType(objType) || reg.getCanonicalTypeId(objType) == reg.getType(type_category::Range)) {
        funcType = reg.instantiateRangeMethod(objType, funcType);
    }
    // Параметризованный Array<Elem>: методы объявлены на `:Array` с T; подставляем T→Elem,
    // чтобы `a.at(0)`/`a.first()` возвращали ЭЛЕМЕНТНЫЙ тип (как instantiateRangeMethod).
    if (reg.isArrayType(objType) || reg.getCanonicalTypeId(objType) == reg.getType(type::Array)) {
        funcType = reg.instantiateArrayMethod(objType, funcType);
    }
    const auto* fd = reg.getTypeDataAs<FunctionTypeData>(funcType);
    EXPECT(fd && "handleMethodCall: method signature is not a function type");
    const size_t nargs = call.m_args ? call.m_args->size() : 0;
    if (nargs != fd->paramTypes.size()) {
        m_actx.ctx().diag().report(Severity::Error, call.range(), "method '{}' of type '{}' expects {} argument(s), got {}", mname,
                                   reg.getFullTypeName(objType), fd->paramTypes.size(), nargs);
        m_actx.setExprType(&n, INVALID_TYPE_ID);
        return;
    }
    n.resultType = n.commonType = fd->returnType;
    m_actx.setExprType(&n, fd->returnType);
}

// Обработка по kind (объявления, типы, Ident, ContextMacro); полный обход детей — в
// analyzeNode через analyzeChildren, поэтому здесь рекурсия в детей не нужна.
void NameResolutionPass::handleNode(AstNodePtr& self) {
    switch (self->kind()) {
    case ParserToken::Kind::VarDecl:
        analyzeVarDecl(static_cast<VarDecl&>(*self));
        break;
    case ParserToken::Kind::TypeDecl:
        analyzeTypeDecl(static_cast<Binary&>(*self));
        break;
    case ParserToken::Kind::Ident:
        // Квалификатор @:: foo уже раскрыт хук-ом ContextMacroExpander (в analyzeNode);
        // здесь только резолвим имя.
        lookupOrError(*self);
        break;
    case ParserToken::Kind::EmbedExpr: {
        // Опция -Wembed (default Warning): предупреждение за сам факт использования C++-вставки
        // {% ... %} независимо от имён внутри. `-Wembed=ignore` подавляет вывод. Вызывается
        // ровно один раз на узел (обход семантики), в отличие от кодогенерации (рекурсия через emitExpr).
        const auto& embed = static_cast<const AstNodeAttr&>(*self);
        m_actx.ctx().report(embed.range(), OptKind::Embed, "C++ code embedding {{% ... %}} is used");
        // C++-вставка ({% ... %}): trust-имена, на которые ссылается вставка ($name/@name),
        // проверяются на доступность в таблице символов; отсутствующие — предупреждение.
        for (const auto& nm : utils::extract_embed_names(embed.text())) {
            const Symbol* found = m_actx.symbols().resolve(nm);
            // Квалифицированное имя (ns::x): таблица — плоский стек скоупов, поэтому полное
            // имя не находится; проверяем по последнему сегменту (грубая проверка доступности).
            if (!found) {
                const auto pos = nm.rfind("::");
                if (pos != std::string::npos) {
                    found = m_actx.symbols().resolve(nm.substr(pos + 2));
                }
            }
            if (!found) {
                m_actx.ctx().diag().report(Severity::Warning, embed.range(), "embed references name '{}' not declared in trust code", nm);
            }
        }
        break;
    }
    case ParserToken::Kind::AppendStmt: {
        // Учёт `[]=` в статическом размере словаря: `d []= v` увеличивает известный размер
        // (dims) целевого словаря — чтобы статическая проверка `d.N` далее по тексту видела
        // выросший размер (после двух append размер 3 → 5). LHS — простой Ident (вложенный
        // отклонён в typeExpr); dims >= 0 означает «словарь с известным размером».
        //
        // Spread-merge `d []= ... dict` (RHS — Ellipsis): добавляются ВСЕ элементы словаря-
        // операнда, поэтому dims растёт на число элементов операнда, а типы полей переносятся
        // в dictFieldTypes цели. Без `...` — одиночный элемент (прежнее поведение: dims += 1,
        // типы полей не регистрируются — сохранение «Any» для добавленных позиционных).
        auto& append = static_cast<Binary&>(*self);
        if (append.m_left && append.m_left->kind() == ParserToken::Kind::Ident) {
            if (Symbol* s = resolveSimple(append.m_left.get(), append.m_left->text())) {
                if (s->dims < 0) {
                    break; // статический размер цели неизвестен — отслеживать нечего
                }
                const AstNodeBase* rhs = append.m_right.get();
                if (rhs && rhs->kind() == ParserToken::Kind::Ellipsis) {
                    const auto& ell = static_cast<const Sequence&>(*rhs);
                    const AstNodeBase* operand = ell.m_body.empty() ? nullptr : ell.m_body[0].get();
                    if (operand && operand->kind() == ParserToken::Kind::DictLiteral) {
                        // Компиляционно известный словарь-литерал: каждый элемент — новый элемент.
                        const auto& dl = static_cast<const Sequence&>(*operand);
                        for (const auto& el : dl.m_body) {
                            if (!el) {
                                continue;
                            }
                            std::string fname;
                            const AstNodeBase* val = nullptr;
                            collectionElementNameValue(el.get(), fname, val);
                            s->dims += 1;
                            s->dictFieldTypes.emplace_back(fname, val ? dictElementType(val) : INVALID_TYPE_ID);
                        }
                    } else if (operand && operand->kind() == ParserToken::Kind::Ident) {
                        // Словарь-переменная: переносим её известный размер и типы полей.
                        // Простое имя могло быть объявлено как локальная `$x` (опция -Wsigil).
                        if (const Symbol* src = resolveSimpleRead(operand->text())) {
                            if (isDictTypeId(m_actx.ctx().types(), src->type)) {
                                if (src->dims >= 0) {
                                    s->dims += src->dims;
                                }
                                s->dictFieldTypes.insert(s->dictFieldTypes.end(), src->dictFieldTypes.begin(), src->dictFieldTypes.end());
                            }
                        }
                    }
                    // Прочий dict-операнд (выражение): статический размер неизвестен — не меняем.
                    break;
                }
                // Одиночный элемент (не spread): размер +1 и регистрация типа поля по позиции,
                // чтобы dictFieldTypes оставался выровнен по dims (инвариант: число записей
                // dictFieldTypes == известный размер). Для литерала тип выводится, для
                // переменной/выражения (до анализа) — Any (INVALID).
                s->dims += 1;
                s->dictFieldTypes.emplace_back("", rhs ? dictElementType(rhs) : INVALID_TYPE_ID);
            }
        }
        break;
    }
    default:
        break; // прочие kinds обрабатываются только обходом детей
    }
}

// ── Объявления ──

void NameResolutionPass::analyzeVarDecl(VarDecl& var_node) {
    std::string var_name{var_node.text()};
    MapperRange var_range = var_node.range();

    // Резолв необязательной аннотации типа.
    TypeId var_type = INVALID_TYPE_ID;
    if (var_node.m_type) {
        auto type_id = m_actx.resolveType(*var_node.m_type);
        if (type_id.has_value()) {
            var_type = *type_id;
        } else {
            m_actx.ctx().diag().report(Severity::Error, var_node.m_type->range(), "unknown type '{}'", var_node.m_type->text());
        }
    }

    AstNodePtr init_node = var_node.m_initializer;

    // В `:=` справа должно быть ЗНАЧЕНИЕ, а не тип-имя: `x := :Int32` невалидно. Тип объявляется
    // через `::=` (`x ::= :Int32` → тип-алиас). Голый `:T` — TypeName; конструкция `:T(a)` — единый
    // узел DictLiteralNode (это выражение-значение, не затрагивается).
    if (init_node && init_node->kind() == ParserToken::Kind::TypeName) {
        m_actx.ctx().diag().report(Severity::Error, var_range, "cannot assign a type '{}' to a value variable '{}'; use '::=' to declare a type alias",
                                   init_node->text(), var_name);
        return;
    }

    // Предварительное (forward) объявление `x:Type := ...;` — инициализатора нет.
    // Для нативного имени (%...) тип обязателен: имя напрямую транслируется в C++.
    if (!init_node && !var_name.empty() && var_name[0] == '%' && !var_node.m_type) {
        m_actx.ctx().diag().report(Severity::Error, var_range, "native variable '{}' must have a type in a forward declaration", var_name);
        return;
    }

    // Имя без сигила в локальном скоупе: нормализуем в локальную переменную с '$' префиксом
    // ($x) и предупреждаем (опция -Wsigil, default Warning). Локальный скоуп = внутри функции
    // (в стеке скоупов есть FuncDecl); уровень модуля/глобальный — НЕ локальный. Единый хелпер
    // normalizeLocalSigil используется и declareDestructureTarget (унификация sigil-логики).
    const bool isLocal = isInLocalScope();
    var_name = normalizeLocalSigil(var_node, var_node.nameRange(), isLocal);

    Symbol sym;
    sym.name = var_name;
    // Константность ('^' → attr::ReadOnly) и вид ссылки (@[reftype(...)]) — ортогональные
    // квалификаторы, применяемые единым хелпером (applyRefAttrs). Константность в типе даёт
    // `const T` в C++ (getCppTypeName) и попадает в прототипы функций. Для нетипизированной
    // переменной (var_type == INVALID) бит const выставляется позже, в typeExpr, когда тип
    // выводится из инициализатора.
    var_type = applyRefAttrs(var_type, var_node, var_range);
    sym.type = var_type;
    // Признак «тип выведен» (inferred) закодирован битом в TypeId (withInferred) и
    // выставляется в typeExpr, когда тип выводится из инициализатора. Явная аннотация
    // `x:Type :=` даёт структурный тип (без бита) → фиксированный.
    sym.decl = &var_node;

    // Месторасположение (физическая память): TLS → ThreadLocal; имя с '::' → Static
    // (переменная в области имён); локальный скоуп → Local (стек); иначе Global.
    if (var_node.has_attr(m_actx.ctx().attrs(), attr::ThreadLocal)) {
        sym.storage = Storage::ThreadLocal;
    } else if (var_name.find("::") != std::string::npos) {
        sym.storage = Storage::Static;
    } else if (isLocal) {
        sym.storage = Storage::Local;
    }

    // Регистрация в текущем скоупе (дубликат — ошибка). Forward-объявление (init == nullptr)
    // может быть завершено последующим определением того же имени (declareOrComplete → Completed).
    if (m_actx.symbols().declareOrComplete(sym) == DeclResult::Duplicate) {
        m_actx.ctx().diag().report(Severity::Error, var_range, "duplicate declaration '{}'", var_name);
        return;
    }
    for (auto& hook : m_hooks) {
        hook->onDeclare(sym);
    }

    // Статическая размерность из инициализатора-словаря (для статической проверки `d.1`)
    // и типы полей (для вывода типа `d.two`/`d.1`/`d[0]`). Копируются на символ переменной
    // (свойство Dims переменной; см. архитектуру). Иммутабельный случай.
    if (init_node && init_node->kind() == ParserToken::Kind::DictLiteral) {
        if (Symbol* s = m_actx.symbols().resolveMutable(var_name)) {
            const auto& dl = static_cast<const Sequence&>(*init_node);
            s->dims = static_cast<int64_t>(dl.m_body.size());
            for (const auto& el : dl.m_body) {
                if (!el) {
                    continue;
                }
                std::string fname;
                const AstNodeBase* valueNode = nullptr;
                collectionElementNameValue(el.get(), fname, valueNode);
                s->dictFieldTypes.emplace_back(fname, valueNode ? dictElementType(valueNode) : INVALID_TYPE_ID);
            }
        }
    }

    // Инициализатор обходится общим механизмом (analyzeNode → children()).
}

void NameResolutionPass::analyzeTypeDecl(Binary& binary_node) {
    auto* left = binary_node.m_left.get();
    if (!left || left->kind() != ParserToken::Kind::Ident) {
        m_actx.ctx().diag().report(Severity::Error, binary_node.range(), "type declaration must have a name on the left");
        return;
    }

    std::string type_name = std::string(left->text());

    auto* right = binary_node.m_right.get();
    if (!right) {
        m_actx.ctx().diag().report(Severity::Error, binary_node.range(), "type '{}' must have a definition", type_name);
        return;
    }

    // Enum/Variant-объявление (ПОСТФИКС `(...):Enum`/`(...):Variant`, НЕ префикс `:Enum(...)`):
    // правая часть — DictLiteral с аннотацией «Enum»/«Variant». Голые члены = безнарные (валидны).
    if (right->kind() == ParserToken::Kind::DictLiteral) {
        const auto& dl = static_cast<const DictLiteralNode&>(*right);
        if (!dl.prefix && dl.m_type && dl.m_type->text() == "Enum") {
            analyzeEnumDecl(binary_node);
            return;
        }
        if (!dl.prefix && dl.m_type && dl.m_type->text() == "Variant") {
            analyzeVariantDecl(binary_node);
            return;
        }
    }

    // Определяем базовый TypeId правой части (имя типа: алиас или встроенный).
    TypeId base_id = INVALID_TYPE_ID;
    if (right->kind() == ParserToken::Kind::TypeName) {
        // y ::= Int; — alias на существующий тип.
        base_id = m_actx.resolveType(*right).value_or(INVALID_TYPE_ID);
        if (base_id == INVALID_TYPE_ID) {
            m_actx.ctx().diag().report(Severity::Error, right->range(), "type '{}' not found", right->text());
            return;
        }
    } else if (right->kind() == ParserToken::Kind::Ident) {
        // y ::= MyInt; — правая часть — имя ТИПА (пользовательский алиас). Оператор '::='
        // создаёт ТОЛЬКО типы: ссылка на переменную справа — ошибка (не «алиас на переменную»).
        const Symbol* vs = m_actx.symbols().resolve(right->text());
        if (!vs) {
            m_actx.ctx().diag().report(Severity::Error, right->range(), "undefined name '{}'", right->text());
            return;
        }
        if (vs->decl->kind() != ParserToken::Kind::TypeDecl) {
            m_actx.ctx().diag().report(Severity::Error, right->range(), "'::=' right side must be a type, '{}' is not a type", right->text());
            return;
        }
        base_id = vs->type;
        if (base_id == INVALID_TYPE_ID) {
            m_actx.ctx().diag().report(Severity::Error, right->range(), "type of '{}' is not resolved", right->text());
            return;
        }
    } else {
        m_actx.ctx().diag().report(Severity::Error, right->range(), "unsupported type alias definition");
        return;
    }

    // Регистрация алиаса в реестре типов (метаданные TypeId).
    TypeId alias_id = m_actx.ctx().types().registerType(type_name, base_id, {}, right->range());
    if (alias_id == INVALID_TYPE_ID) {
        return; // дубликат — диагностику сформировал реестр
    }

    // Биндинг имени алиаса в текущем скоупе (shadowing/коллизии через скоуп-стек).
    Symbol as;
    as.name = type_name;
    as.type = alias_id;
    as.decl = &binary_node;
    if (!m_actx.symbols().declare(as)) {
        m_actx.ctx().diag().report(Severity::Error, left->range(), "duplicate declaration '{}'", type_name);
        return;
    }
    for (auto& hook : m_hooks) {
        hook->onDeclare(as);
    }
}

// ── Единый сбор членов `(name=value / name:Type=value / bare name)` из DictLiteral RHS ──
// Контракт: элементы m_body — ArgNode (имя в text(), явный тип в m_type, значение в m_value),
// строятся term_to_ast::appendDictElementsFromArgs. Чтение (имя/тип/значение) — НАПРЯМУЮ из
// ArgNode, без обёрток и без разворачивания. Значение члена Variant — AST-выражение (источник —
// ArgNode.m_value); в реестре — только разрешённый тип члена.

// ── Объявление enum-типа (`Color ::= :Enum(RED=1, GREEN=2,)` / `(RED=1, GREEN=2,):Enum`) ──
// TypeDecl(Binary): left = имя типа, right = DictLiteral с аннотацией m_type «Enum»; элементы
// m_body — ArgNode (имя, явный тип, значение). Регистрирует enum-тип, вычисляет единый тип
// значений (по общим правилам, предупреждение WidenAny при повышении до Any), биндит имя и
// регистрирует классические методы.
void NameResolutionPass::analyzeEnumDecl(Binary& binary_node) {
    const std::string enum_name = std::string(binary_node.m_left->text());
    TypeRegistry& reg = m_actx.ctx().types();
    const MapperRange decl_range = binary_node.range();

    auto* right = binary_node.m_right.get();
    EXPECT(right && right->kind() == ParserToken::Kind::DictLiteral && "analyzeEnumDecl: RHS must be Enum-annotated DictLiteral");
    auto& dict = static_cast<DictLiteralNode&>(*right);

    // ── Члены: (имя, значение|null, явный тип|null) — напрямую из элементов m_body (ArgNode).
    const auto& body = dict.m_body;
    const auto isMember = [](const AstNodePtr& el) { return el && el->kind() == ParserToken::Kind::ArgNode; };

    size_t memberCount = 0;
    for (const auto& el : body) {
        if (isMember(el)) {
            ++memberCount;
        }
    }
    if (memberCount == 0) {
        m_actx.ctx().diag().report(Severity::Error, decl_range, "enum '{}' must have at least one member", enum_name);
        return;
    }

    std::vector<EnumMemberData> md;
    md.reserve(memberCount);
    TypeId valueType = INVALID_TYPE_ID;

    // ── Проход 1: тип — из ЯВНЫХ аннотаций члена (`A:Rational`); иначе из значений ──
    bool haveExplicitType = false;
    for (const auto& el : body) {
        if (isMember(el) && static_cast<const ArgNode&>(*el).m_type) {
            haveExplicitType = true;
            break;
        }
    }
    if (haveExplicitType) {
        for (const auto& el : body) {
            if (!isMember(el)) {
                continue;
            }
            const auto& a = static_cast<const ArgNode&>(*el);
            const AstNodePtr ta = a.m_type;
            if (!ta) {
                continue;
            }
            auto tid = m_actx.resolveType(*ta);
            if (!tid.has_value()) {
                m_actx.ctx().diag().report(Severity::Error, ta->range(), "enum '{}': unknown member type", enum_name);
                continue;
            }
            const TypeId c = reg.getCanonicalTypeId(*tid);
            if (valueType == INVALID_TYPE_ID) {
                valueType = c;
            } else if (valueType != c) {
                m_actx.ctx().diag().report(Severity::Error, ta->range(), "enum '{}' member types differ ('{}' vs '{}')", enum_name,
                                           reg.getFullTypeName(valueType), reg.getFullTypeName(c));
                return;
            }
        }
        if (valueType == INVALID_TYPE_ID) {
            valueType = reg.getType(type::Int64);
        }
    } else {
        // Тип из явных значений (resolvedType + join); если явных нет — минимальный Int по числу членов.
        std::vector<TypeId> explicitTypes;
        for (const auto& el : body) {
            if (!isMember(el)) {
                continue;
            }
            const AstNodePtr v = enumVariantMember(static_cast<const ArgNode&>(*el)).value;
            if (v) {
                explicitTypes.push_back(m_actx.resolvedType(*v));
            }
        }
        if (explicitTypes.empty()) {
            valueType = intTypeForLiteral(reg, memberCount - 1);
        } else {
            TypeId common = INVALID_TYPE_ID;
            bool allSame = true;
            for (const TypeId vt : explicitTypes) {
                const TypeId c = (vt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(vt) : INVALID_TYPE_ID;
                if (common == INVALID_TYPE_ID) {
                    common = c;
                } else if (c != INVALID_TYPE_ID && common != c) {
                    allSame = false;
                }
            }
            if (allSame && common != INVALID_TYPE_ID) {
                valueType = common;
            } else {
                std::vector<TypeId> nat;
                nat.reserve(explicitTypes.size());
                for (const TypeId vt : explicitTypes) {
                    nat.push_back(naturalRuntimeType(vt));
                }
                valueType = joinElementTypes(nat);
                if (valueType == INVALID_TYPE_ID) {
                    valueType = reg.getType(type_generic::Any);
                    m_actx.ctx().report(decl_range, OptKind::WidenAny, "enum '{}' members have incompatible value types; value type widened to Any", enum_name);
                }
            }
        }
    }
    // valueType всегда разрешён выше (тип из аннотаций / значений / JOIN → Any с предупреждением
    // WidenAny). Ветка INVALID здесь невозможна — молча не подменяем, а ловим инвариантом.
    EXPECT(valueType != INVALID_TYPE_ID && "analyzeEnumDecl: value type must be resolved");

    // ── Проход 2: значения членов (автоинкремент для целого типа, иначе ординал) ──
    const bool integerVT = getGroup(getKindFromId(reg.getCanonicalTypeId(valueType))) == Group::kIntegers;
    unsigned long long cur = 0;
    bool haveValue = false;
    size_t ordinal = 0;
    for (const auto& el : body) {
        if (!isMember(el)) {
            continue;
        }
        const EnumVariantMember m = enumVariantMember(static_cast<const ArgNode&>(*el));
        const AstNodePtr v = m.value;
        std::string vstr;
        if (v) {
            vstr = v->text();
            if (integerVT) {
                unsigned long long parsed = 0;
                if (parseDecimalUInt(v->text(), parsed)) {
                    cur = parsed;
                    haveValue = true;
                }
            }
        } else if (integerVT) {
            // Автоинкремент: безнарный член = предыдущее значение + 1 (первый = 0).
            cur = haveValue ? (cur + 1) : 0;
            haveValue = true;
            vstr = std::to_string(cur);
        } else {
            // Не-целый тип: безнарный член = ординал (позиция).
            vstr = std::to_string(ordinal);
        }
        md.push_back(EnumMemberData{m.name, std::move(vstr)});
        ++ordinal;
    }

    // ── Регистрация enum-типа в реестре (EnumTypeData; дубликат → диагностика реестра).
    const TypeId enum_id = reg.registerEnumType(enum_name, valueType, std::move(md), decl_range);
    if (enum_id == INVALID_TYPE_ID) {
        return;
    }

    // ── Биндинг имени enum-типа в текущем скоупе (shadowing через скоуп-стек).
    Symbol es;
    es.name = enum_name;
    es.type = enum_id;
    es.decl = &binary_node;
    if (!m_actx.symbols().declare(es)) {
        m_actx.ctx().diag().report(Severity::Error, decl_range, "duplicate declaration '{}'", enum_name);
        return;
    }
    for (auto& hook : m_hooks) {
        hook->onDeclare(es);
    }

    // ── Классические тип-уровневые методы (осознанное решение: работа ТОЛЬКО через тип).
    // count() -> Int64; fromName(name: StrChar) -> Enum; fromValue(value: Value) -> Enum.
    const TypeId int64Id = reg.getType(type::Int64);
    const TypeId strCharId = reg.getType(type::StrChar);
    auto ftype = [&](TypeId ret, std::vector<TypeId> args) { return reg.getOrCreateFunctionType(ret, std::move(args)); };
    reg.addMethod(enum_id, "count", ftype(int64Id, {}));
    reg.addMethod(enum_id, "fromName", ftype(enum_id, {strCharId}));
    reg.addMethod(enum_id, "fromValue", ftype(enum_id, {valueType}));
}

// ── Объявление Variant-типа (`Value ::= :Variant(RED:Int64=0, GREEN='g',)`) ──
// TypeDecl(Binary): left = имя типа, right = DictLiteral с аннотацией m_type «Variant»; элементы
// m_body — Binary(AssignOp) (left=имя или пусто для бесзначённого, right=значение). Тип каждого
// члена — СВОЙ (гетерогенный): выводится из значения (resolvedType), ординальный член без значения
// → минимальный знаковый Int по позиции. Регистрирует Variant-тип, биндит имя, методы (count).
void NameResolutionPass::analyzeVariantDecl(Binary& binary_node) {
    const std::string variant_name = std::string(binary_node.m_left->text());
    TypeRegistry& reg = m_actx.ctx().types();
    const MapperRange decl_range = binary_node.range();

    auto* right = binary_node.m_right.get();
    EXPECT(right && right->kind() == ParserToken::Kind::DictLiteral && "analyzeVariantDecl: RHS must be Variant-annotated DictLiteral");
    auto& dict = static_cast<DictLiteralNode&>(*right);

    std::vector<VariantMemberData> members;
    // Единый сбор из m_body (ArgNode): тип члена — из ЯВНОЙ аннотации (m.type), иначе из
    // значения (m.value), иначе минимальный знаковый Int по позиции.
    size_t ordinal = 0;
    for (const auto& el : dict.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        const EnumVariantMember m = enumVariantMember(static_cast<const ArgNode&>(*el));
        TypeId mtype = INVALID_TYPE_ID;
        if (m.type) {
            auto tid = m_actx.resolveType(*m.type);
            if (tid.has_value()) {
                mtype = reg.getCanonicalTypeId(*tid);
            } else {
                // Явная аннотация типа члена не резолвится — ОШИБКА (симметрично enum), а не
                // тихий fallback на тип из значения/ординал: ниже член всё же получает тип,
                // но ошибка уже зафиксирована.
                m_actx.ctx().diag().report(Severity::Error, m.type->range(), "variant '{}': unknown member type", variant_name);
            }
        }
        if (mtype == INVALID_TYPE_ID && m.value) {
            mtype = m_actx.resolvedType(*m.value); // тип из значения
        }
        if (mtype == INVALID_TYPE_ID) {
            mtype = intTypeForLiteral(reg, ordinal);
        }
        members.push_back(VariantMemberData{m.name, mtype});
        ++ordinal;
    }
    if (members.empty()) {
        m_actx.ctx().diag().report(Severity::Error, decl_range, "variant '{}' must have at least one member", variant_name);
        return;
    }

    const TypeId variant_id = reg.registerVariantType(variant_name, std::move(members), decl_range);
    if (variant_id == INVALID_TYPE_ID) {
        return;
    }

    // Биндинг имени Variant-типа в скоупе.
    Symbol es;
    es.name = variant_name;
    es.type = variant_id;
    es.decl = &binary_node;
    if (!m_actx.symbols().declare(es)) {
        m_actx.ctx().diag().report(Severity::Error, decl_range, "duplicate declaration '{}'", variant_name);
        return;
    }
    for (auto& hook : m_hooks) {
        hook->onDeclare(es);
    }

    // Классический метод count() -> Int64 (работа с variant идёт через имя типа).
    reg.addMethod(variant_id, "count", reg.getOrCreateFunctionType(reg.getType(type::Int64), {}));
}

void NameResolutionPass::analyzeFuncDecl(FuncDecl& func_node) {
    std::string func_name{func_node.text()};
    MapperRange func_range = func_node.range();

    // Нативная функция (%...) транслируется в C++ напрямую → в forward-объявлении
    // тип возврата обязателен (без типа вернули бы голую декларацию без типа).
    if (!func_node.m_body.has_value() && !func_name.empty() && func_name[0] == '%' && !func_node.m_type) {
        m_actx.ctx().diag().report(Severity::Error, func_range, "native function '{}' must have a return type in a forward declaration", func_name);
        return;
    }

    // Регистрация имени функции с функциональным типом сигнатуры (return + параметры).
    Symbol sym;
    sym.name = func_name;
    sym.type = m_actx.buildFuncType(func_node);
    sym.decl = &func_node;

    // Регистрация в текущем скоупе (дубликат — ошибка). Forward-объявление (без тела) может
    // быть завершено последующим определением того же имени (declareOrComplete → Completed).
    if (m_actx.symbols().declareOrComplete(sym) == DeclResult::Duplicate) {
        m_actx.ctx().diag().report(Severity::Error, func_range, "duplicate declaration '{}'", func_name);
        return;
    }
    for (auto& hook : m_hooks) {
        hook->onDeclare(sym);
    }
}

// Регистрация параметров в текущем (функционном) скоупе — вызывается из analyzeNode
// ВНУТРИ enterScope() скоупа функции, чтобы имена в теле функции резолвились.
void NameResolutionPass::declareFuncParams(FuncDecl& func_node) {
    if (!func_node.m_params) {
        return;
    }
    for (const auto& p : *func_node.m_params) {
        if (!p || p->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        auto& pd = static_cast<ArgNode&>(*p);
        Symbol ps;
        ps.name = std::string(pd.text());
        TypeId ptype = (pd.m_type) ? m_actx.resolveType(*pd.m_type).value_or(INVALID_TYPE_ID) : INVALID_TYPE_ID;
        // Константность и вид ссылки параметра — из атрибутов узла ТИПА параметра
        // (`fmt: @[reftype(ptr)]@ StrChar^`): reftype → RefType, ReadOnly → const.
        if (pd.m_type && pd.m_type->as_attr()) {
            ptype = applyRefAttrs(ptype, *pd.m_type->as_attr(), pd.m_type->range());
        }
        ps.type = ptype;
        ps.decl = &pd;
        ps.storage = Storage::Local; // параметры функции — стек
        m_actx.symbols().declare(ps);
        for (auto& hook : m_hooks) {
            hook->onDeclare(ps);
        }
    }
}

// Общий подсчёт слотов-элементов и валидация rest-цели для деструктуризации (spread-словаря и
// кортежа). Единый источник идентичного цикла в analyzeDestructure / analyzeDestructureTuple.
bool NameResolutionPass::collectDestructureSlots(const DestructureDecl& node, size_t& elementSlots, bool& hasRest) {
    elementSlots = 0;
    hasRest = false;
    const size_t cnt = node.m_targets.size();
    for (size_t i = 0; i < cnt; ++i) {
        auto* t = node.m_targets[i].get();
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        const bool isRest = i < node.m_targetIsRest.size() && node.m_targetIsRest[i];
        if (isRest) {
            hasRest = true;
            if (i + 1 != cnt) {
                m_actx.ctx().diag().report(Severity::Error, t->range(), "rest target '...' must be the last destructuring target");
                return false; // фатально: rest не последняя — цели не разбираем
            }
        } else {
            ++elementSlots;
        }
    }
    return true;
}

// Деструктуризация `t1, ..., tN := [... ]source;`.
// Без маркера — ТОЧНАЯ привязка (Python/Rust/Go/C++/Haskell): каждая цель — один элемент; для
// статически-известного размера число целей == числу элементов. Суффикс `...` у цели (`rest...`,
// C++-pack) — «остаток»: связывает оставшиеся элементы; `_...` — извлечь, остаток отбросить;
// одиночный `_` — пропустить ровно один элемент. Спред (`... source`, Dict) — цели извлекаются
// pop_front (точная привязка) / rest = остаток; кортеж (без `...`) — цели std::get<N> с проверкой арности.
void NameResolutionPass::analyzeDestructure(DestructureDecl& node) {
    if (node.m_source) {
        analyzeNode(node.m_source);
    }
    if (node.m_targets.empty()) {
        return;
    }
    // Кортеж (структурный источник, НЕ spread): цели = элементы по индексу.
    if (!node.m_isSpread) {
        const TypeId src = node.m_source ? m_actx.resolvedType(*node.m_source) : INVALID_TYPE_ID;
        const bool isTuple = src != INVALID_TYPE_ID && m_actx.ctx().types().getTypeDataAs<TupleTypeData>(src) != nullptr;
        if (isTuple) {
            analyzeDestructureTuple(node, src);
            return;
        }
        m_actx.ctx().diag().report(Severity::Error, node.m_source ? node.m_source->range() : node.range(),
                                   "destructuring requires a tuple source (or use '...' for a dictionary spread)");
        return;
    }
    // spread (коллекция Dict). Проверка типа источника: допустим только словарь (Dict) — иначе
    // pop_front на не-коллекции упал бы лишь на этапе C++-компиляции (тихий fallback в семантике).
    const TypeRegistry& reg = m_actx.ctx().types();
    const TypeId srcType = node.m_source ? m_actx.resolvedType(*node.m_source) : INVALID_TYPE_ID;
    if (!isDictTypeId(reg, srcType)) {
        m_actx.ctx().diag().report(Severity::Error, node.m_source ? node.m_source->range() : node.range(),
                                   "spread destructuring source must be a dictionary (Dict), got a non-collection type");
        return;
    }
    const size_t cnt = node.m_targets.size();
    // Число целей-элементов (НЕ rest) и наличие rest-маркера (`rest...` / `_...`).
    size_t elementTargets = 0;
    bool hasRest = false;
    if (!collectDestructureSlots(node, elementTargets, hasRest)) {
        return; // rest не последняя — Error репортнут
    }
    // Статическая арность: без rest — elementTargets == размер (точная привязка); с rest —
    // elementTargets <= размер (остаток поглощается rest).
    const int64_t size = dictSizeOf(node.m_source.get());
    if (size >= 0) {
        if (hasRest) {
            if (static_cast<int64_t>(elementTargets) > size) {
                m_actx.ctx().diag().report(Severity::Error, node.m_source->range(),
                                           "destructuring arity mismatch: {} element target(s) before rest but dictionary has {} element(s)", elementTargets,
                                           size);
                return;
            }
        } else if (static_cast<int64_t>(elementTargets) != size) {
            m_actx.ctx().diag().report(Severity::Error, node.m_source->range(),
                                       "destructuring arity mismatch: {} target(s) but dictionary has {} element(s); add '...' rest or adjust targets",
                                       elementTargets, size);
            return;
        }
    }
    // Типизация целей: per-element (как в кортеже) — каждая цель получает runtime-тип своего
    // элемента (Int8..Int64 → Int64, Float → Double, Bool, StrChar...). ВНУТРИ ЦИКЛА тип расширяется
    // до МАКСИМАЛЬНОГО среди элементов (Bool/Int8 → Integer, float → Double) — элемент перечитывается
    // и может меняться; Any — только если тип не выводим (внешний источник / несовместимые категории).
    node.m_targetTypes.assign(cnt, INVALID_TYPE_ID);
    node.m_targetDeclaredTypes.assign(cnt, INVALID_TYPE_ID);
    node.m_inLoop = isInLoop();
    const std::vector<TypeId> elemTypes = dictElementTypes(node.m_source.get());
    std::vector<TypeId> naturalized;
    naturalized.reserve(elemTypes.size());
    for (const TypeId et : elemTypes) {
        naturalized.push_back(naturalRuntimeType(et));
    }
    const TypeId joined = node.m_inLoop ? joinElementTypes(naturalized) : INVALID_TYPE_ID;
    bool untyped = elemTypes.empty();
    for (const TypeId et : elemTypes) {
        if (et != INVALID_TYPE_ID && typeIsInferred(et)) {
            untyped = true;
            break;
        }
    }
    if (node.m_inLoop && untyped) {
        if (joined != INVALID_TYPE_ID) {
            m_actx.ctx().report(node.range(), OptKind::WidenAny, "destructured element type is not specified; widened to the maximum element type");
        } else {
            m_actx.ctx().report(node.range(), OptKind::WidenAny, "destructured element type is not inferable; widened to the generic type (Any)");
        }
    }
    size_t elemIdx = 0;
    for (size_t i = 0; i < cnt; ++i) {
        auto* t = node.m_targets[i].get();
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        const bool isRest = i < node.m_targetIsRest.size() && node.m_targetIsRest[i];
        if (node.m_isAssign) {
            // Присваивание в существующие цели: резолв + проверка const. Тип any_cast — natural
            // runtime тип ЭЛЕМЕНТА (соответствует хранению Dict), а не цель; значение присваивается
            // в существующую переменную с её собственным типом (C++-конверсия).
            // rest в присваивании: допустима только мутация-идиома (rest == источнику); прочее
            // переиспользование — Error (иначе кодген молча присвоил бы Dict в несовместимую цель).
            if (isRest && !restTargetNameAllowed(static_cast<HasText&>(*t), /*isSpreadDict=*/true, node.m_source.get())) {
                continue;
            }
            node.m_targetTypes[i] = isRest ? INVALID_TYPE_ID : (elemIdx < naturalized.size() ? naturalized[elemIdx] : INVALID_TYPE_ID);
            assignDestructureTarget(node, i, static_cast<HasText&>(*t), isRest);
        } else {
            // Объявление: m_targetTypes[i] — тип any_cast = natural runtime тип ЭЛЕМЕНТА (как хранит
            // Dict); declaredType — тип объявляемой переменной (явная аннотация `a:Int32` фиксирует
            // его, иначе — тот же выведенный). Разделение важно: Dict хранит int как int64_t, поэтому
            // any_cast<int32_t> по аннотации Int32 упал бы на элементе, хранимом как int64_t.
            if (isRest) {
                // Шаг 1: переиспользование имени rest-цели. Допустима только мутация-идиома
                // (rest == источнику); прочее переиспользование — Error (иначе кодген молча дал бы
                // C++-redefinition). При конфликте цель не связываем.
                if (!restTargetNameAllowed(static_cast<HasText&>(*t), /*isSpreadDict=*/true, node.m_source.get())) {
                    continue;
                }
                // Шаг 2: аннотация типа на rest-цели запрещена — кодген фиксирует rest как Dict и
                // аннотацию молча игнорировал бы. Явная диагностика вместо тихого игнора.
                if (i < node.m_targetTypeNodes.size() && node.m_targetTypeNodes[i]) {
                    m_actx.ctx().diag().report(Severity::Error, t->range(), "type annotation on a rest target '{}...' is not supported; rest type is inferred",
                                               t->text());
                    continue;
                }
            }
            const TypeId inferred =
                isRest ? INVALID_TYPE_ID : (node.m_inLoop ? joined : (elemIdx < naturalized.size() ? naturalized[elemIdx] : INVALID_TYPE_ID));
            // Вне цикла тип цели не расширяется (per-element типизация): если тип конкретного
            // элемента не выводится (naturalRuntimeType → INVALID), цель молча становится std::any.
            // Симметрично цикловому предупреждению WidenAny — явная диагностика вместо тихого
            // fallback на Any (AGENTS rule 5 «no silent fallback»).
            if (!node.m_inLoop && !isRest && inferred == INVALID_TYPE_ID && t->text() != "_") {
                m_actx.ctx().report(t->range(), OptKind::WidenAny,
                                    "destructured element type for target '{}' is not inferable; widened to the generic type (Any)", t->text());
            }
            const TypeId declaredType = explicitTargetType(node, i, inferred);
            node.m_targetTypes[i] = inferred;                                        // any_cast<T> = как хранит Dict
            node.m_targetDeclaredTypes[i] = isRest ? INVALID_TYPE_ID : declaredType; // тип переменной
            declareDestructureTarget(static_cast<HasText&>(*t), isRest, declaredType);
        }
        if (!isRest) {
            ++elemIdx; // и `_`, и именованная цель занимают один элемент (индекс)
        }
    }
}

void NameResolutionPass::analyzeDestructureTuple(DestructureDecl& node, TypeId tupleType) {
    const auto* td = m_actx.ctx().types().getTypeDataAs<TupleTypeData>(tupleType);
    const size_t elemCount = td ? td->elements.size() : 0;
    node.m_sourceArity = elemCount; // для кортеж-rest в кодогенерации (скоуп сброшен к моменту codegen)
    const size_t cnt = node.m_targets.size();
    // Слоты-элементы (связывание + skip `_`) и наличие rest-цели (`rest...` / `_...`).
    size_t slots = 0;
    bool hasRest = false;
    if (!collectDestructureSlots(node, slots, hasRest)) {
        return; // rest не последняя — Error репортнут
    }
    // Арность: без rest — слоты == число элементов; с rest — слоты <= числа элементов (остаток).
    if (hasRest) {
        if (slots > elemCount) {
            m_actx.ctx().diag().report(Severity::Error, node.range(),
                                       "destructuring arity mismatch: {} element target(s) before rest but tuple has {} element(s)", slots, elemCount);
            return;
        }
    } else if (slots != elemCount) {
        m_actx.ctx().diag().report(Severity::Error, node.range(), "destructuring arity mismatch: {} target(s) but tuple has {} element(s)", slots, elemCount);
        return;
    }
    node.m_targetTypes.assign(cnt, INVALID_TYPE_ID);
    size_t idx = 0;
    for (size_t i = 0; i < cnt; ++i) {
        auto& t = node.m_targets[i];
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        const bool isRest = i < node.m_targetIsRest.size() && node.m_targetIsRest[i];
        auto& h = static_cast<HasText&>(*t);
        if (h.text() == "_" && !isRest) {
            ++idx; // skip-элемент занимает индекс, но не связывается
            continue;
        }
        if (node.m_isAssign) {
            // Присваивание в существующие цели (std::get): резолв + проверка const.
            assignDestructureTarget(node, i, h, isRest);
            if (!isRest) {
                ++idx;
            }
            continue;
        }
        if (isRest) {
            // rest: `_...` — отброс (ничего не связываем); именованный `rest...` — остаток кортежа
            // (C++-тип выводится в кодогенерации через make_tuple; семантический тип — исходный кортеж).
            if (h.text() == "_") {
                continue;
            }
            // Шаг 1: rest кортежа не может переиспользовать существующую переменную (в т.ч. сам
            // источник): кортежный rest — НЕ мутация (в отличие от spread-словаря), иначе кодген
            // молча дал бы `auto c_t = std::make_tuple(std::get<2>(c_t)...)` (переобъявление/UB).
            if (!restTargetNameAllowed(h, /*isSpreadDict=*/false, node.m_source.get())) {
                continue;
            }
            // Шаг 2: аннотация типа на rest-цели запрещена — кодген кортежа эмитит `auto` и
            // аннотацию молча игнорировал бы. Явная диагностика вместо тихого игнора.
            if (i < node.m_targetTypeNodes.size() && node.m_targetTypeNodes[i]) {
                m_actx.ctx().diag().report(Severity::Error, t->range(), "type annotation on a rest target '{}...' is not supported; rest type is inferred",
                                           t->text());
                continue;
            }
            const TypeId restType = explicitTargetType(node, i, tupleType);
            node.m_targetTypes[i] = restType;
            declareDestructureTarget(h, /*isRest=*/true, restType);
            continue;
        }
        const TypeId elemType = (td && idx < td->elements.size()) ? td->elements[idx].type : INVALID_TYPE_ID;
        const TypeId explicitType = explicitTargetType(node, i, INVALID_TYPE_ID);
        node.m_targetTypes[i] = explicitType; // INVALID → кодген кортежа эмитит `auto`
        declareDestructureTarget(h, /*isRest=*/false, (explicitType != INVALID_TYPE_ID) ? explicitType : elemType);
        ++idx;
    }
}

// True, если текущий скоуп — локальный (в стеке скоупов есть FuncDecl). Единый предикат для
// sigil-нормализации имён (analyzeVarDecl / declareDestructureTarget).
bool NameResolutionPass::isInLocalScope() const {
    bool isLocal = false;
    m_actx.symbols().forEachScope([&](const SymbolTable::Scope& s) {
        if (s.creator && s.creator->kind() == ParserToken::Kind::FuncDecl) {
            isLocal = true;
        }
    });
    return isLocal;
}

// True, если текущий узел находится ВНУТРИ тела цикла (в стеке скоупов есть скоуп, созданный
// WhileStmt/DoWhileStmt). Циклы создают скоуп на время обхода тела (см. analyzeNode).
bool NameResolutionPass::isInLoop() const {
    bool inLoop = false;
    m_actx.symbols().forEachScope([&](const SymbolTable::Scope& s) {
        if (s.creator && (s.creator->kind() == ParserToken::Kind::WhileStmt || s.creator->kind() == ParserToken::Kind::DoWhileStmt)) {
            inLoop = true;
        }
    });
    return inLoop;
}

// Нормализация bare-имени в локальном скоупе (`x` → `$x`, опция -Wsigil): правит текст узла
// и репортит предупреждение. Возвращает имя для символа (с сигилом при нормализации).
std::string NameResolutionPass::normalizeLocalSigil(HasText& node, MapperRange range, bool isLocal) {
    std::string name{node.text()};
    if (isLocal && isSimpleVarName(name)) {
        const std::string sigil = "$" + name;
        node.set_text(sigil);
        // Быстрый фикс: заменить bare-имя на сигнальное `$name`. Отчёт через
        // diag().report(...) (возвращает DiagnosticEntry*) вместо ctx().report(...)
        // (discard), чтобы прикрепить fixit к диагностике.
        auto sev = m_actx.ctx().opts().severity(OptKind::NoSigil);
        if (sev.has_value()) {
            auto* entry = m_actx.ctx().diag().report(*sev, range, OptKind::NoSigil, "creating a local variable '${}'", name);
            if (entry != nullptr && !range.isInvalid()) {
                m_actx.ctx().diag().fixit(entry, range, sigil);
            }
        }
        return sigil;
    }
    return name;
}

// Каноническое имя цели деструктуризации (сигил-нормализация БЕЗ мутации узла и без
// предупреждения): bare-имя в локальном скоупе → "$" + имя. Совпадает с логикой
// declareDestructureTarget / normalizeLocalSigil (для проверки rest-переиспользования).
std::string NameResolutionPass::canonicalTargetName(const HasText& t) const {
    std::string name{t.text()};
    if (isInLocalScope() && isSimpleVarName(name)) {
        return "$" + name;
    }
    return name;
}

// Проверка переиспользования имени именованной rest-цели (`rest...`). Для словаря допустима
// мутация-идиома (rest-цель == самому источнику: pop'ы идут прямо в источник, объявление не
// создаётся — см. declareDestructureTarget). Переиспользование ЛЮБОЙ другой существующей
// переменной — Error: без этой проверки кодген молча сгенерировал бы C++-redefinition
// (`trust::Dict c_x = ...` поверх уже объявленного c_x) без диагностики. Для кортежа rest
// никогда не мутация, поэтому переиспользование (в т.ч. самого источника) всегда Error.
bool NameResolutionPass::restTargetNameAllowed(HasText& t, bool isSpreadDict, const AstNodeBase* source) {
    const std::string name = canonicalTargetName(t);
    if (name.empty() || name == "_") {
        return true;
    }
    if (!m_actx.symbols().resolve(name)) {
        return true; // имя свободно — можно объявлять заново
    }
    bool isSourceReuse = false;
    if (isSpreadDict && source && source->kind() == ParserToken::Kind::Ident) {
        isSourceReuse = (canonicalTargetName(static_cast<const HasText&>(*source)) == name);
    }
    if (isSourceReuse) {
        return true; // мутация-идиома `item, dict... := ... dict`
    }
    if (isSpreadDict) {
        m_actx.ctx().diag().report(Severity::Error, t.range(),
                                   "rest target '{}...' reuses an existing variable; only the source itself may be reused (mutation idiom)", name);
    } else {
        m_actx.ctx().diag().report(Severity::Error, t.range(),
                                   "tuple rest target '{}...' cannot reuse an existing variable; tuple rest is not a mutation (unlike dictionary spread)",
                                   name);
    }
    return false;
}

// Объявление одной цели деструктуризации. `_` — skip. isRest + уже объявленное имя (= источник)
// → «остаток» через мутацию pop_front, отдельного объявления нет.
void NameResolutionPass::declareDestructureTarget(HasText& t, bool isRest, TypeId type) {
    std::string name{t.text()};
    if (name == "_") {
        return; // skip: элемент потребляется, переменная не создаётся
    }
    const MapperRange range = t.range();
    // Нормализация сигила (единый хелпер с analyzeVarDecl): bare-имя в локальном скоупе → $name.
    const bool isLocal = isInLocalScope();
    name = normalizeLocalSigil(t, range, isLocal);
    // «Остаток» (isRest): если имя уже объявлено (== источник) — это мутация pop_front, не объявляем.
    if (isRest && m_actx.symbols().resolve(name)) {
        return;
    }
    Symbol sym;
    sym.name = name;
    sym.type = (type != INVALID_TYPE_ID) ? type : (isRest ? m_actx.ctx().types().getType(type::Dict) : m_actx.ctx().types().getType(type_generic::Any));
    sym.decl = &t;
    sym.storage = Storage::Local;
    if (m_actx.symbols().declareOrComplete(sym) == DeclResult::Duplicate) {
        m_actx.ctx().diag().report(Severity::Error, range, "duplicate declaration '{}'", name);
        return;
    }
    for (auto& hook : m_hooks) {
        hook->onDeclare(sym);
    }
}

// Явный тип цели из аннотации (`a:Int32`, node.m_targetTypeNodes[i]); INVALID — аннотации нет
// (возвращается fallback) или тип не резолвится (диагностируется).
TypeId NameResolutionPass::explicitTargetType(const DestructureDecl& node, size_t i, TypeId fallback) {
    if (i >= node.m_targetTypeNodes.size() || !node.m_targetTypeNodes[i]) {
        return fallback;
    }
    auto resolved = m_actx.resolveType(*node.m_targetTypeNodes[i]);
    if (!resolved) {
        m_actx.ctx().diag().report(Severity::Error, node.m_targetTypeNodes[i]->range(), "unknown type '{}'", node.m_targetTypeNodes[i]->text());
        return INVALID_TYPE_ID;
    }
    return *resolved;
}

// Цель деструктуризации-ПРИСВАИВАНИЯ (`a, b = ... source`): резолв существующей переменной,
// проверка на константность; объявление не создаётся. Тип any_cast задаёт вызывающий (цикл кладёт
// natural runtime тип элемента в node.m_targetTypes[i]). `_` — skip; rest == источник — мутация
// pop_front (присвоения нет).
void NameResolutionPass::assignDestructureTarget(DestructureDecl& node, size_t i, HasText& t, bool isRest) {
    (void)node;
    (void)i;
    std::string name{t.text()};
    if (name == "_") {
        return; // skip: элемент потребляется, переменная не связывается
    }
    const MapperRange range = t.range();
    const bool isLocal = isInLocalScope();
    name = normalizeLocalSigil(t, range, isLocal);
    // «Остаток» (isRest): если имя уже объявлено (== источник) — это мутация pop_front, присвоения нет.
    if (isRest && m_actx.symbols().resolve(name)) {
        return;
    }
    Symbol* s = m_actx.symbols().resolveMutable(name);
    if (!s) {
        m_actx.ctx().diag().report(Severity::Error, range, "destructuring assignment target '{}' is not declared (use ':=' to create a variable)", name);
        return;
    }
    if (typeIsConst(s->type)) {
        m_actx.ctx().diag().report(Severity::Error, range, "cannot assign to constant destructuring target '{}'", name);
        return;
    }
}

// ── Применение ортогональных квалификаторов типа (const + вид ссылки) ──
// Единый источник для переменных (analyzeVarDecl) и параметров (declareFuncParams).
TypeId NameResolutionPass::applyRefAttrs(TypeId base, const AstNodeAttr& node, MapperRange range) {
    if (base == INVALID_TYPE_ID) {
        return base;
    }
    const AttrPool& attrs = m_actx.ctx().attrs();
    // Константность ('^' → attr::ReadOnly): бит kConstFlag → `const T` в C++ (getCppTypeName).
    if (node.has_attr(attrs, attr::ReadOnly)) {
        base = withConst(base);
    }
    // Вид ссылки (@[reftype("ptr")]) — плоский enum RefType. Первая ссылка — fast-path бит,
    // вложенность — составной узел (единый источник: TypeRegistry::applyRefType).
    auto reftype_id = attrs.lookup(attr::Reftype);
    if (reftype_id.has_value() && node.has_attr(*reftype_id)) {
        const std::vector<std::string>* rargs = node.attr_args(*reftype_id);
        if (!rargs || rargs->empty()) {
            m_actx.ctx().diag().report(Severity::Error, range, "attribute 'reftype' requires a reference-kind parameter, e.g. @[reftype(\"ptr\")]");
        } else {
            auto refkind = refTypeFromString(rargs->front());
            if (!refkind) {
                m_actx.ctx().diag().report(Severity::Error, range, "unknown reference kind '{}'", rargs->front());
            } else {
                base = m_actx.ctx().types().applyRefType(base, *refkind);
            }
        }
    }
    return base;
}

// ── Разрешение имён ──

const Symbol* NameResolutionPass::lookupOrError(AstNodeBase& node) {
    const std::string name(node.text());
    const Symbol* sym = resolveSimple(&node, name);
    // Зарегистрированные runtime-символы (например %trust::trust__abort__) — известные
    // нативные функции из публичного runtime-заголовка (trust/assert.hpp); «undefined name»
    // для них не выдаём (транспилер эмитит вызов по имени, объявление даёт заголовок).
    const bool isRuntime = !sym && m_actx.isRegisteredRuntimeSymbol(name);
    for (auto& hook : m_hooks) {
        hook->onResolve(node, sym);
    }
    if (!sym && !isRuntime) {
        m_actx.ctx().diag().report(Severity::Error, node.range(), "undefined name '{}'", name);
    }
    return sym;
}

// Единый алгоритм разрешения простого имени с правилами вывода сигилов.
// Порядок для bare-имени `x`: `$x` (локальная) → `x` (глобал/параметр) → `%x` (нативная функция).
// Для `$`-имени `$x`: сначала `$x`; если нет — bare `x` (параметр/локальная без сигила): `n` и
// `$n` — одно локальное имя. При попадании на `$x`/`%x` текст узла-ссылки (node) нормализуется
// на эту форму, чтобы манглинг (name_to_cpp срезает `$`/`%` → c_x / x) совпал с объявлением.
// Квалифицированные/сигилные/нативные имена, найденные напрямую, резолвятся как есть.
// node может быть nullptr (тогда текст не меняется).
Symbol* NameResolutionPass::resolveSimple(AstNodeBase* node, std::string_view name) {
    const bool simple = isSimpleVarName(name);
    // bare x: сначала локальная $x (при попадании — нормализуем текст узла на $x).
    if (simple) {
        const std::string sigil_name = "$" + std::string(name);
        if (Symbol* s = m_actx.symbols().resolveMutable(sigil_name)) {
            if (node && node->kind() == ParserToken::Kind::Ident) {
                static_cast<HasText&>(*node).set_text(sigil_name);
            }
            return s;
        }
    }
    // как есть (глобальная/параметр/квалифицированная/сигилная).
    if (Symbol* s = m_actx.symbols().resolveMutable(name)) {
        return s;
    }
    // Правила вывода сигилов:
    //  - bare x → нативная функция %x (напр. %fib вызывается как fib);
    //  - $x → локальная/параметр без сигила (n и $n — одно локальное имя).
    if (simple) {
        const std::string native_name = "%" + std::string(name);
        if (Symbol* s = m_actx.symbols().resolveMutable(native_name)) {
            if (node && node->kind() == ParserToken::Kind::Ident) {
                static_cast<HasText&>(*node).set_text(native_name);
            }
            return s;
        }
    } else if (!name.empty() && name.front() == '$') {
        if (Symbol* s = m_actx.symbols().resolveMutable(name.substr(1))) {
            return s;
        }
    }
    return nullptr;
}

const Symbol* NameResolutionPass::resolveSimpleRead(std::string_view name) const {
    const bool simple = isSimpleVarName(name);
    if (simple) {
        const std::string sigil_name = "$" + std::string(name);
        if (const Symbol* s = m_actx.symbols().resolve(sigil_name)) {
            return s;
        }
    }
    if (const Symbol* s = m_actx.symbols().resolve(name)) {
        return s;
    }
    if (simple) {
        const std::string native_name = "%" + std::string(name);
        if (const Symbol* s = m_actx.symbols().resolve(native_name)) {
            return s;
        }
    } else if (!name.empty() && name.front() == '$') {
        if (const Symbol* s = m_actx.symbols().resolve(name.substr(1))) {
            return s;
        }
    }
    return nullptr;
}

// ── Типизация выражений (post-order) ──

TypeId NameResolutionPass::typeBinaryResult(Binary& b) {
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
    // с enum идёт ТОЛЬКО через имя типа — осознанное решение, см. MEMORY.md).
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
    // Явный Bool (:Bool, результат сравнения/логики) в арифметике — ошибка компиляции
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
    // Общий тип операндов для any_cast: арифметика → result; Compare/Logical (результат Bool)
    // → продвинутый конкретный операнд, если ровно один операнд std::any.
    if (b.kind() == ParserToken::Kind::CompareOp || b.kind() == ParserToken::Kind::LogicalOp) {
        if (isAnyType(lt, reg)) {
            b.commonType = promoteSingleNumeric(reg, rt);
        } else if (isAnyType(rt, reg)) {
            b.commonType = promoteSingleNumeric(reg, lt);
        } else {
            b.commonType = INVALID_TYPE_ID; // оба конкретные — any_cast не нужен
        }
    }
    m_actx.setExprType(&b, result);
    return result;
}

void NameResolutionPass::typeExpr(AstNodeBase* node) {
    if (!node) {
        return;
    }
    if (is_binary_expr_kind(node->kind())) {
        auto& b = static_cast<Binary&>(*node);
        // AppendStmt (`X []= v`) — append к контейнеру, не обычное бинарное выражение:
        // типы ложатся специально (lhsType=тип контейнера, rhsType/resultType=тип значения),
        // сужение/расширение целевой переменной не применяются (append не меняет тип цели).
        if (b.kind() == ParserToken::Kind::AppendStmt) {
            const TypeId lt = b.m_left ? m_actx.resolvedType(*b.m_left) : INVALID_TYPE_ID;
            const TypeId rt = b.m_right ? m_actx.resolvedType(*b.m_right) : INVALID_TYPE_ID;
            // Spread-merge `X []= ... dict`: правая часть — маркер распаковки (Ellipsis),
            // единичным элементом НЕ является, тип результата не выводится (INVALID).
            const bool spread = b.m_right && b.m_right->kind() == ParserToken::Kind::Ellipsis;
            b.lhsType = lt;
            b.rhsType = spread ? INVALID_TYPE_ID : rt;
            b.resultType = spread ? INVALID_TYPE_ID : rt;
            b.commonType = spread ? INVALID_TYPE_ID : rt;
            // Строковые контейнеры: ширина RHS должна быть совместима с единичным элементом
            // контейнера (StrChar ↔ std::string, StrWide ↔ std::wstring). Потерянное сужение
            // (широкая строка в узкий контейнер) — ошибка; обратное (char→wide) кодогенерация
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
                // Вложенный LHS (`d['x'] []= ...`, `d[0] []= ...`, `d.field []= ...`) — отложено.
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
            // Вложенный LHS (`d['x'] []= v`, `d[0] []= v`, `d.field []= v`) — отложено.
            if (b.m_left && (b.m_left->kind() == ParserToken::Kind::ArrayAccess || b.m_left->kind() == ParserToken::Kind::MemberAccess)) {
                m_actx.ctx().diag().report(Severity::Error, b.m_left->range(),
                                           "вложенный append '[]=' пока не реализован: append допустим только к простому контейнеру");
            }
            m_actx.setExprType(&b, rt);
            return;
        }
        const TypeId result = typeBinaryResult(b);
        const bool isAssignOp = (b.kind() == ParserToken::Kind::AssignOp);
        // Составное присваивание ("+=", "//=") — оператор с текстом, оканчивающимся на '=';
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
        // Константность (kConstFlag): запись в константную переменную — ошибка; LHS с `^`
        // (attr::ReadOnly на узле Ident) — финальная запись, делающая переменную константой
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
                    // transpiler::generateVarDeclToFile — const объявления берётся из атрибута узла).
                    s->type = withConst(s->type);
                }
            }
        }
        // Расширение выводимой цели по истории присвоений — только для присваиваний
        // (AssignOp "=", "+=" или составной MathOp "+=").
        if (isAssignOp || compound) {
            TypeId widen = result;
            // Автоматически выведенный Bool, используемый в составной числовой арифметике
            // (например `mult := 1; mult += 1;`), расширяется до максимального Int (Int64):
            // Bool — вырожденное целое, а в однопроходной типизации нет «оператора в цикле»,
            // поэтому расширяем по самому факту составного присваивания. Явный `:Bool` так
            // НЕ расширяется — для него это ошибка (явный тип фиксирован).
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
                    // Транспилятору нужен структурный тип (кодогенерация не различает inferred).
                    v.inferredType = clearInferred(t);
                    if (Symbol* s = m_actx.symbols().resolveMutable(v.text())) {
                        // Живой тип символа несёт бит «выведен» (для join/продвижения) и, при
                        // константности ('^' → attr::ReadOnly), бит «константность» (kConstFlag) —
                        // источник префикса `const ` в кодогенерации переменной.
                        s->type = v.has_attr(m_actx.ctx().attrs(), attr::ReadOnly) ? withConst(t) : t;
                    }
                } else if (v.m_initializer->kind() != ParserToken::Kind::TypeName) {
                    if (auto aid = m_actx.ctx().types().findType("Any")) {
                        // Инициализатор без выводимого типа (C++-вставка `{% %}`, вызов с
                        // неизвестным результатом, отрицательный литерал) — переменная по природе
                        // std::any. Маркируем тип ЯВНО (Any), чтобы транспилятор НЕ угадывал тихим
                        // fallback на std::any (AGENTS rule 5): INVALID у переменной с инициализатором
                        // в кодогенерации — ошибка вывода. Голый `:T` сюда не попадает — это
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
            // Нетипизированное forward-объявление (`x := ...;`): и инициализатора, и типа нет —
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
        // при каждом обращении (маленький лист — но единый кеш типов выражений).
        const TypeId t = literalType(static_cast<const Literal&>(*node), m_actx.ctx().types());
        if (t != INVALID_TYPE_ID) {
            // Тип значения литерала всегда ВЫВЕДЕН из литерала → маркируем битом inferred
            // (auto-Bool `0`/`1` продвигается в арифметике; см. typeBinaryResult).
            const TypeId vt = withInferred(t);
            // Запоминаем TypeId на узле — транспилятор литерала словаря читает его (kind из
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
        // Строка-формат `"{}"(args)` / `'{}'(args)`: callee — строковый литерал. Результат —
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
        // printf-формат (атрибут @[format]) — проверка типов аргументов (пост-порядково).
        checkFormatArgs(call);
        // Обычный вызов пользовательской функции: типизируем результат возвращаемым типом
        // сигнатуры (как handleMethodCall для методов). Это чинит `p := f(10)` → int32_t
        // (ранее результат вызова был std::any). Метод-вызов obj.method(args) обрабатывается
        // отдельно (analyzeAccess/handleMethodCall). Если тип не резолвится — остаётся Any.
        if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::Ident) {
            if (const Symbol* s = resolveSimple(call.m_callee.get(), call.m_callee->text())) {
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

// ── Компиляйт-тайм проверка printf-формата (атрибут @[format("printf", ...)]) ──
void NameResolutionPass::checkFormatArgs(CallExpr& call) {
    if (!call.m_callee || call.m_callee->kind() != ParserToken::Kind::Ident) {
        return;
    }
    const Symbol* sym = resolveSimple(call.m_callee.get(), call.m_callee->text());
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
    // @[format("printf", string_index, first_to_check)] — ровно три параметра.
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
    // Формат-строка — аргумент stringIdx-1 (индексы 1-based); обязана быть строковым литералом.
    const auto& fmtArg = (*call.m_args)[stringIdx - 1];
    if (!fmtArg || fmtArg->kind() != ParserToken::Kind::StrChar) {
        m_actx.ctx().report(fmtArg ? fmtArg->range() : call.range(), OptKind::Format, "format string is not a string literal");
        return;
    }
    const std::string fmt(fmtArg->text());
    std::vector<format_check::Conversion> convs;
    if (!format_check::parse_printf_format(fmt, convs)) {
        m_actx.ctx().report(fmtArg->range(), OptKind::Format, "invalid printf format string '{}'", fmt);
        return;
    }
    const TypeRegistry& reg = m_actx.ctx().types();
    for (std::size_t j = 0; j < convs.size(); ++j) {
        const int argPos = firstToCheck - 1 + static_cast<int>(j);
        if (argPos >= static_cast<int>(call.m_args->size())) {
            m_actx.ctx().report(call.range(), OptKind::Format, "format string requires more arguments than provided (missing argument for conversion '%{}')",
                                std::string(1, convs[j].conv));
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
            m_actx.ctx().report(arg->range(), OptKind::Format, "format argument {} expects {} (conversion '{}') but argument has type '{}'", argPos + 1,
                                format_expect_name(convs[j].expect), std::string(1, convs[j].conv), typeName);
        }
    }
}

// ── Компиляйт-тайм проверка строки-формата `"{}"(args)` / `'{}'(args)` ──
// callee — строковый литерал (StrWide/StrChar). Сверяем число плейсхолдеров `{}` с числом
// аргументов ({{ / }} — литеральные скобки, аргумент не потребляют) и баланс фигурных скобок.
void NameResolutionPass::checkFormatStringArgs(CallExpr& call) {
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
            if (i + 1 < fmt.size() && fmt[i + 1] == '{') { // `{{` — литеральная скобка
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
                    m_actx.ctx().report(fmtNode->range(), OptKind::Format, "format string '{}' references argument index {} but only {} argument(s) provided",
                                        fmt, idx, nArgs);
                }
            }
        } else if (c == '}') {
            if (i + 1 < fmt.size() && fmt[i + 1] == '}') { // `}}` — литеральная скобка
                ++i;
                continue;
            }
            --depth;
        }
    }
    if (depth != 0) {
        m_actx.ctx().report(fmtNode->range(), OptKind::Format, "unbalanced braces in format string '{}'", fmt);
        return;
    }
    if (placeholders != nArgs) {
        m_actx.ctx().report(fmtNode->range(), OptKind::Format, "format string '{}' has {} placeholder(s) but {} argument(s) provided", fmt, placeholders,
                            nArgs);
    }
}

void NameResolutionPass::widenInferredTarget(const AstNodeBase* lhs, TypeId result) {
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

void NameResolutionPass::checkAssignmentNarrowing(const AstNodeBase* valueNode, TypeId sourceType, TypeId targetType, std::string_view targetName) {
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
    // Строки: сужение StrWide (широкая) → StrChar (узкая) — всегда ошибка (значение литерала
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
