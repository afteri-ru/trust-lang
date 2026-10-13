#include "solver/trust_to_smt.hpp"
#include "solver/vc/smt_term_builder.hpp"

#include "session/context.hpp"
#include "ast/ast_nodes.hpp"
#include "types/type_system.hpp"
#include "types/typekind.hpp"
#include "types/group.hpp"
#include "utils/error.hpp"

#include <algorithm>
#include <memory>

namespace trust {
namespace solver {

namespace sb = SmtTermBuilder;

namespace {
// Сорт для операндов сравнения: заявленный сорт операции, но не уже фактической ширины
// операндов (coerceToWidth расширяет только узкие операнды). Сравнение Int8-операнда с
// Int32-параметром иначе дало бы рассогласование разрядности в z3.
SmtSort widenOperandSort(const SmtSort& sort, const std::optional<SmtTerm>& l, const std::optional<SmtTerm>& r) {
    SmtSort t = sort;
    if (t.kind != SmtSortKind::kBitVec) {
        return t;
    }
    if (l && l->sort.kind == SmtSortKind::kBitVec) {
        t.bv_width = std::max(t.bv_width, l->sort.bv_width);
    }
    if (r && r->sort.kind == SmtSortKind::kBitVec) {
        t.bv_width = std::max(t.bv_width, r->sort.bv_width);
    }
    return t;
}

} // namespace

std::optional<SmtTerm> TrustToSmt::toTerm(const AstNodeBase* node, const std::optional<SmtSort>& expected, const VcValues* state) {
    if (!node) {
        return std::nullopt;
    }
    switch (node->kind()) {
    case ParserToken::Kind::IntLiteral:
        return encodeIntLiteralTerm(node, expected);
    case ParserToken::Kind::FloatLiteral:
        return encodeFloatLiteralTerm(node);
    case ParserToken::Kind::Ident:
        return encodeIdentTerm(node, expected, state);
    case ParserToken::Kind::MathOp:
    case ParserToken::Kind::BitwiseOp:
    case ParserToken::Kind::CompareOp:
    case ParserToken::Kind::LogicalOp:
        return encodeOperatorTerm(node, expected, state);
    case ParserToken::Kind::CallExpr:
        return encodeCallTerm(node, state);
    case ParserToken::Kind::ArrayAccess:
        return encodeArrayAccessTerm(node, state);
    case ParserToken::Kind::ArrayInit:
        return encodeArrayInitTerm(node, state);
    case ParserToken::Kind::TrustElem:
        return encodeTrustElemTerm(node, expected, state);
    default:
        reportOutsideEnvelope(*node, "expression kind is not modelable");
        return std::nullopt;
    }
}

std::optional<SmtTerm> TrustToSmt::encodeIntLiteralTerm(const AstNodeBase* node, const std::optional<SmtSort>& expected) {
    const auto& lit = static_cast<const Literal&>(*node);
    std::optional<SmtSort> s = expected;
    if (!s || s->kind != SmtSortKind::kBitVec) {
        s = m_sorts.sortOf(lit.typeState.getTypeId());
    }
    // В пост-условии (имя функции = возврат) сорт может быть не выведен у операндов -
    // используем сорт результата функции.
    if ((!s || s->kind != SmtSortKind::kBitVec) && m_curResultSort && m_curResultSort->kind == SmtSortKind::kBitVec) {
        s = m_curResultSort;
    }
    if (!s || s->kind != SmtSortKind::kBitVec) {
        reportOutsideEnvelope(lit, "integer literal outside a bit-vector context");
        return std::nullopt;
    }
    return sb::makeConst(std::string(lit.text()), *s, lit.range());
}

std::optional<SmtTerm> TrustToSmt::encodeFloatLiteralTerm(const AstNodeBase* node) {
    const auto& lit = static_cast<const Literal&>(*node);
    auto rs = m_sorts.sortOf(lit.typeState.getTypeId());
    if (!rs || rs->kind != SmtSortKind::kReal) {
        reportOutsideEnvelope(lit, "float literal outside a real context");
        return std::nullopt;
    }
    return sb::makeConst(std::string(lit.text()), *rs, lit.range());
}

std::optional<SmtTerm> TrustToSmt::encodeIdentTerm(const AstNodeBase* node, const std::optional<SmtSort>& expected, const VcValues* state) {
    const std::string name(requiredText(*node));
    if (name == "true" || name == "yes") {
        return sb::makeConst("true", sb::boolSort(), node->range());
    }
    if (name == "false" || name == "no") {
        return sb::makeConst("false", sb::boolSort(), node->range());
    }
    // Значение МЕСТА тела: точный поиск по `resolvedDecl` (одноимённые места разных ветвей и
    // скоупов не смешиваются).
    if (state != nullptr && node->kind() == ParserToken::Kind::Ident) {
        const auto& ident = static_cast<const IdentName&>(*node);
        if (ident.resolvedDecl != nullptr) {
            if (auto id = placeIdOf(ident.resolvedDecl)) {
                const auto it = state->find(*id);
                if (it != state->end()) {
                    return it->second; // копия (терм уже проставлен сорт)
                }
            }
        }
    }
    // Правило затенения: инициализатор объявления читается ДО записи нового значения
    // (`s := s + 1`), поэтому значением имени служит предыдущее место с тем же текстом.
    // Порядок слотов — порядок регистрации (параметры → тело), выбор детерминирован.
    if (state != nullptr && m_curSlotNodes != nullptr) {
        for (std::size_t index = 0; index < m_curSlotNodes->size(); ++index) {
            const AstNodeBase* place = (*m_curSlotNodes)[index];
            if (place == nullptr || requiredText(*place) != name) {
                continue;
            }
            const auto it = state->find(flow::PlaceId{.value = static_cast<std::uint32_t>(index)});
            if (it != state->end()) {
                return it->second;
            }
        }
    }
    // Место объявлено в функции, но значения нет: чтение неинициализированного (или потерянного
    // при слиянии ветвей) — внутренняя ошибка кодирования, а не свободная константа.
    if (state != nullptr && node->kind() == ParserToken::Kind::Ident) {
        const auto& ident = static_cast<const IdentName&>(*node);
        if (ident.resolvedDecl != nullptr && m_localPlaces.contains(ident.resolvedDecl)) {
            report(*node, "read of place '{}' without a value (uninitialized or dropped on merge)", name);
            return std::nullopt;
        }
    }
    // Связка квантора (`z3_forall`/`z3_exists`): у bound-переменной нет AST-объявления.
    if (auto it = m_boundNames.find(name); it != m_boundNames.end()) {
        return it->second;
    }
    // Пост-условие: имя функции = возвращаемое значение.
    if (m_inPost && name == m_curFuncName && !m_curParams.empty()) {
        if (!m_buildAxiom) {
            if (m_curReturn) {
                return *m_curReturn; // собственный VC: функция = фактический терм возврата
            }
            // void-путь: результата нет, постусловие ссылаться на него не может.
            reportOutsideEnvelope(*node, "postcondition references the result of a void return path");
            return std::nullopt;
        }
        // Аксиома контракта (m_buildAxiom): имя функции → uninterpreted-вызов.
        if (!m_curResultSort) {
            reportOutsideEnvelope(*node, "function result sort unknown");
            return std::nullopt;
        }
        std::vector<std::shared_ptr<SmtTerm>> args;
        args.reserve(m_curParams.size());
        for (const auto& p : m_curParams) {
            const auto pit = m_paramSmtNames.find(p);
            const std::string smtName = pit != m_paramSmtNames.end() ? pit->second : p;
            const auto pt = m_paramTypes.find(p);
            const auto ps = pt != m_paramTypes.end() ? m_sorts.sortOf(pt->second) : std::nullopt;
            if (!ps) {
                reportOutsideEnvelope(*node, "parameter '{}' sort unknown", p);
                return std::nullopt;
            }
            args.push_back(std::make_shared<SmtTerm>(sb::makeNamedVar(smtName, *ps, node->range())));
        }
        m_symbolMap[name] = SmtSymbolRef{name, node->range()};
        return sb::makeApp(name, *m_curResultSort, std::move(args), node->range());
    }
    // Параметр функции → его уникальное SMT-имя (func_param) с известным сортом.
    if (auto it = m_paramSmtNames.find(name); it != m_paramSmtNames.end()) {
        const auto pt = m_paramTypes.find(name);
        const auto ps = pt != m_paramTypes.end() ? m_sorts.sortOf(pt->second) : std::nullopt;
        if (!ps) {
            reportOutsideEnvelope(*node, "parameter '{}' sort unknown", name);
            return std::nullopt;
        }
        return sb::makeNamedVar(it->second, *ps, node->range());
    }
    // Свободное имя (глобал): сорт из контекста (числовой) или Bool (логический контекст).
    SmtSort gsort = sb::boolSort();
    if (expected && (expected->kind == SmtSortKind::kBitVec || expected->kind == SmtSortKind::kReal)) {
        gsort = *expected;
    }
    m_symbolMap[name] = SmtSymbolRef{name, node->range()};
    return sb::makeNamedVar(name, gsort, node->range());
}

std::optional<SmtTerm> TrustToSmt::encodeOperatorTerm(const AstNodeBase* node, const std::optional<SmtSort>& expected, const VcValues* state) {
    const auto& b = static_cast<const Binary&>(*node);
    const std::string_view op = binaryOpSpelling(b.m_op);
    // Унарный минус: MathOp "-" с m_left==nullptr.
    if (!b.m_left && b.kind() == ParserToken::Kind::MathOp && op == "-") {
        auto rhs = toTerm(b.m_right.get(), expected, state);
        if (!rhs) {
            return std::nullopt;
        }
        const SmtSort negSort = rhs->sort;
        return sb::makeApp("bvneg", negSort, {std::make_shared<SmtTerm>(std::move(*rhs))}, b.range());
    }
    if (!b.m_left || !b.m_right) {
        reportOutsideEnvelope(b, "unary operator '{}' is not modelable", op);
        return std::nullopt;
    }
    // Сорт операндов: для Compare/Logical - общий тип операндов (commonType/lhsType);
    // для MathOp/BitwiseOp - тип РЕЗУЛЬТАТА (продвинутый; напр. Int8+Int32 → Int32, mixed-width 2.5).
    const TypeId opType = [&]() -> TypeId {
        if (b.kind() == ParserToken::Kind::CompareOp || b.kind() == ParserToken::Kind::LogicalOp) {
            return b.commonType.getTypeId() != INVALID_TYPE_ID ? b.commonType.getTypeId() : b.lhsType.getTypeId();
        }
        return b.resultType.getTypeId() != INVALID_TYPE_ID ? b.resultType.getTypeId()
                                                           : (b.lhsType.getTypeId() != INVALID_TYPE_ID ? b.lhsType.getTypeId() : b.rhsType.getTypeId());
    }();
    auto opSort = m_sorts.sortOf(opType);
    // Пост-условие: имя функции = возврат - берём сорт результата функции.
    if ((!opSort || (opSort->kind != SmtSortKind::kBitVec && opSort->kind != SmtSortKind::kReal)) && m_inPost && m_curResultSort) {
        opSort = m_curResultSort;
    }
    // Не определён из аннотаций типа (напр. self-reference `y` в переменном утверждении, или
    // локальная переменная в теле) - выводим из фактического терма левого операнда (сорт из
    // state/параметра/глобала).
    if ((!opSort || (opSort->kind != SmtSortKind::kBitVec && opSort->kind != SmtSortKind::kReal)) && b.m_left) {
        auto ltry = toTerm(b.m_left.get(), std::nullopt, state);
        if (ltry && (ltry->sort.kind == SmtSortKind::kBitVec || ltry->sort.kind == SmtSortKind::kReal)) {
            opSort = ltry->sort;
        }
    }
    // Последний резерв: сорт правого операнда. НЕ раньше m_inPost/левого операнда: литерал по
    // умолчанию имеет узкий тип (напр. `5` → Int8 → (_ BitVec 8)) и перекрыл бы корректный сорт
    // результата функции/переменной (иначе сравнение Int32 с литералом даёт sort mismatch в z3).
    if ((!opSort || (opSort->kind != SmtSortKind::kBitVec && opSort->kind != SmtSortKind::kReal)) && b.m_right) {
        auto r = m_sorts.sortOf(b.rhsType.getTypeId());
        if (r && (r->kind == SmtSortKind::kBitVec || r->kind == SmtSortKind::kReal)) {
            opSort = r;
        }
    }
    const bool bv = opSort && opSort->kind == SmtSortKind::kBitVec;

    // Знак оператора: беззнаковые целые (kUnsigned) → беззнаковые BV-операторы, знаковые
    // (kIntegers) → знаковые. Источник знака — первый факт типа, который ДЕЙСТВИТЕЛЬНО
    // целочисленный (`resultType`/`commonType`/`lhsType`/`rhsType`); не-целые типы (Bool,
    // функциональный тип имени функции в пост-условии) знаком не обладают и пропускаются.
    std::optional<bool> signOfOp;
    for (const TypeId candidate : {opType, b.lhsType.getTypeId(), b.rhsType.getTypeId()}) {
        if (auto sign = m_sorts.integerSignOf(candidate)) {
            signOfOp = sign;
            break;
        }
    }
    if (!signOfOp.has_value() && m_inPost && m_curResultSort) {
        signOfOp = m_curResultSigned;
    }
    const bool signedOp = signOfOp.value_or(true);

    if (b.kind() == ParserToken::Kind::LogicalOp) {
        const auto smtOp = sb::mapLogicalOp(op);
        if (smtOp.empty()) {
            reportOutsideEnvelope(b, "logical operator '{}' is not modelable", op);
            return std::nullopt;
        }
        auto l = toTerm(b.m_left.get(), std::nullopt, state);
        auto r = toTerm(b.m_right.get(), std::nullopt, state);
        if (!l || !r) {
            return std::nullopt;
        }
        return sb::makeApp(std::string(smtOp), sb::boolSort(), {std::make_shared<SmtTerm>(std::move(*l)), std::make_shared<SmtTerm>(std::move(*r))}, b.range());
    }
    if (b.kind() == ParserToken::Kind::CompareOp) {
        const bool isEq = sb::isEqOp(op);
        const bool isNeq = sb::isNeqOp(op);
        std::string cmpOp = "=";
        if (!isEq && !isNeq) {
            cmpOp = sb::mapCmpOp(op, bv, signedOp);
            if (cmpOp.empty()) {
                reportOutsideEnvelope(b, "comparison operator '{}' is not modelable", op);
                return std::nullopt;
            }
        }
        auto l = toTerm(b.m_left.get(), opSort, state);
        auto r = toTerm(b.m_right.get(), opSort, state);
        if (!l || !r) {
            return std::nullopt;
        }
        // Оба операнда приводим к общему сорту операции (не уже фактической ширины операнда).
        const SmtSort target = opSort ? widenOperandSort(*opSort, l, r) : (l ? l->sort : r->sort);
        *l = sb::coerceToWidth(std::move(*l), target, m_sorts.isSignedType(b.lhsType.getTypeId()));
        *r = sb::coerceToWidth(std::move(*r), target, m_sorts.isSignedType(b.rhsType.getTypeId()));
        auto cmp = sb::makeApp(cmpOp, sb::boolSort(), {std::make_shared<SmtTerm>(std::move(*l)), std::make_shared<SmtTerm>(std::move(*r))}, b.range());
        return isNeq ? sb::mkNot(std::make_shared<SmtTerm>(std::move(cmp)), b.range()) : cmp;
    }
    // MathOp / BitwiseOp.
    const auto smtOp = sb::mapArithOp(op, bv, signedOp);
    if (smtOp.empty()) {
        reportOutsideEnvelope(b, "arithmetic/bitwise operator '{}' is not modelable", op);
        return std::nullopt;
    }
    if (!opSort) {
        reportOutsideEnvelope(b, "cannot determine operand sort for '{}'", op);
        return std::nullopt;
    }
    auto l = toTerm(b.m_left.get(), opSort, state);
    auto r = toTerm(b.m_right.get(), opSort, state);
    if (!l || !r) {
        return std::nullopt;
    }
    // Mixed-width BV (2.5): расширение операндов до общего сорта результата.
    *l = sb::coerceToWidth(std::move(*l), *opSort, m_sorts.isSignedType(b.lhsType.getTypeId()));
    *r = sb::coerceToWidth(std::move(*r), *opSort, m_sorts.isSignedType(b.rhsType.getTypeId()));
    return sb::makeApp(std::string(smtOp), *opSort, {std::make_shared<SmtTerm>(std::move(*l)), std::make_shared<SmtTerm>(std::move(*r))}, b.range());
}

std::optional<SmtTerm> TrustToSmt::encodeCallTerm(const AstNodeBase* node, const VcValues* state) {
    // Результат вызова уже вычислен в точке вызова (элемент MIR `Call`): вызов — действие с
    // побочными эффектами, его нельзя кодировать повторно внутри выражения.
    if (state != nullptr) {
        if (auto id = placeIdOf(node)) {
            if (auto it = state->find(*id); it != state->end()) {
                return it->second;
            }
        }
    }
    // Интерпроцедурный вызов: `(f args)` с сортами из декларации callee; callee —
    // uninterpreted declare-fun; контракт callee добавляется как аксиома (processFuncContract).
    // Callee — МАТЕРИАЛИЗОВАННЫЙ семантикой факт (`IdentName::resolvedDecl`), а не поиск по имени:
    // тот же факт использует MIR-адаптер для `MirOp::Call::place`.
    const auto& call = static_cast<const CallExpr&>(*node);
    if (!call.m_callee || call.m_callee->kind() != ParserToken::Kind::Ident) {
        reportOutsideEnvelope(*node, "callee form is not modelable");
        return std::nullopt;
    }
    const auto* calleeIdent = call.m_callee->as<IdentName>();
    const auto* calleeDecl = calleeIdent != nullptr ? calleeIdent->resolvedDecl : nullptr;
    if (calleeDecl == nullptr || !calleeDecl->is<FuncDecl>()) {
        reportOutsideEnvelope(*node, "callee is not a resolved function declaration");
        return std::nullopt;
    }
    const FuncDecl* fd = static_cast<const FuncDecl*>(calleeDecl);
    const std::string callee(fd->text());
    // Нативный вызов: модели (аксиомы) нативной функции в этом цикле нет, поэтому тело,
    // зависящее от её результата, недоказуемо. Явная граница области проверки вместо
    // молчаливого uninterpreted-символа (который давал ложный «контрпример»).
    if (fd->m_isNativeImport || fd->m_isNativeTemplateImport) {
        reportOutsideEnvelope(*node, "native call '{}' has no model", callee);
        return std::nullopt;
    }
    // Функция без контракта не имеет аксиомы: её тело в VC не участвует, а uninterpreted-символ
    // сделал бы результат доказательства произвольным. Явная граница области проверки.
    if (fd->m_trust.empty()) {
        reportOutsideEnvelope(*node, "call to '{}' has no contract (result is not modeled)", callee);
        return std::nullopt;
    }
    // Сигнатура callee — из материализованных фактов (`ArgNode`/`FuncDecl::m_type`).
    std::vector<SmtSort> paramSorts;
    std::vector<TypeId> paramTypes;
    if (fd->m_params) {
        for (const auto& p : *fd->m_params) {
            if (!p || p->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            const auto& pd = static_cast<const ArgNode&>(*p);
            const TypeId paramType = resolvedTypeOf(pd.m_type.get(), pd.resultType.getTypeId());
            const auto ps = m_sorts.sortOf(paramType);
            if (!ps) {
                reportOutsideEnvelope(*node, "called function '{}' parameter sort unknown", callee);
                return std::nullopt;
            }
            paramTypes.push_back(paramType);
            paramSorts.push_back(*ps);
        }
    }
    const auto resSort = m_sorts.sortOf(resolvedTypeOf(fd->m_type.get()));
    if (!resSort) {
        reportOutsideEnvelope(*node, "called function '{}' result sort unknown", callee);
        return std::nullopt;
    }
    // declare-fun callee (uninterpreted), если ещё не объявлен (аксиому добавляет processFuncContract).
    if (m_declared.insert(callee).second) {
        SmtCommand cmd;
        cmd.kind = SmtCommandKind::kDeclareFun;
        cmd.fun_name = callee;
        cmd.fun_arg_sorts = paramSorts;
        cmd.fun_result_sort = std::make_shared<SmtSort>(*resSort);
        cmd.srcRange = fd->range();
        m_plan.declarations.push_back(std::move(cmd));
        m_symbolMap[callee] = SmtSymbolRef{callee, fd->range()};
    }
    // Аргументы: каждый toTerm с expected = сорт параметра callee.
    std::vector<std::shared_ptr<SmtTerm>> args;
    if (call.m_args) {
        for (size_t i = 0; i < call.m_args->size(); ++i) {
            const auto& a = (*call.m_args)[i];
            if (!a) {
                continue;
            }
            std::optional<SmtSort> exp;
            if (i < paramSorts.size()) {
                exp = paramSorts[i];
            }
            auto t = toTerm(a.get(), exp, state);
            if (!t) {
                return std::nullopt;
            }
            // Аргумент приводится к типу параметра (расширение разрядности), иначе приложение
            // uninterpreted-функции не совпадёт по сорту с declare-fun.
            if (exp.has_value() && exp->kind == SmtSortKind::kBitVec && t->sort.kind == SmtSortKind::kBitVec) {
                const bool signedArg = i < paramTypes.size() && paramTypes[i] != INVALID_TYPE_ID ? m_sorts.isSignedType(paramTypes[i]) : true;
                *t = sb::coerceToWidth(std::move(*t), *exp, signedArg);
            }
            args.push_back(std::make_shared<SmtTerm>(std::move(*t)));
        }
    }
    return sb::makeApp(callee, *resSort, std::move(args), node->range());
}

std::optional<SmtTerm> TrustToSmt::encodeArrayAccessTerm(const AstNodeBase* node, const VcValues* state) {
    // Массив[i] → (select arr idx) (2.8). Сорт результата = элемент массива (resultType).
    const auto& b = static_cast<const Binary&>(*node);
    if (!b.m_left || !b.m_right) {
        reportOutsideEnvelope(*node, "array access form is not modelable");
        return std::nullopt;
    }
    auto arr = toTerm(b.m_left.get(), std::nullopt, state);
    auto idx = toTerm(b.m_right.get(), std::nullopt, state);
    if (!arr || !idx) {
        return std::nullopt;
    }
    std::optional<SmtSort> elemSort;
    if (b.resultType.getTypeId() != INVALID_TYPE_ID) {
        elemSort = m_sorts.sortOf(b.resultType.getTypeId());
    }
    if (!elemSort && arr->sort.kind == SmtSortKind::kArray && arr->sort.range) {
        elemSort = *arr->sort.range;
    }
    if (!elemSort) {
        reportOutsideEnvelope(*node, "cannot determine element sort for array access");
        return std::nullopt;
    }
    // Индекс приводится к домену массива (2.8): домен (BitVec 64), индекс может быть уже.
    if (arr->sort.kind == SmtSortKind::kArray && arr->sort.domain) {
        *idx = sb::coerceToWidth(std::move(*idx), *arr->sort.domain, false);
    }
    return sb::makeApp("select", *elemSort, {std::make_shared<SmtTerm>(std::move(*arr)), std::make_shared<SmtTerm>(std::move(*idx))}, node->range());
}

std::optional<SmtTerm> TrustToSmt::encodeArrayInitTerm(const AstNodeBase* node, const VcValues* state) {
    // Литерал массива `[v0, v1, ...]` → store-цепочка от нулевого массива (2.8).
    const auto& d = static_cast<const DictLiteralNode&>(*node);
    std::optional<SmtSort> arrSort;
    if (d.arrayType.getTypeId() != INVALID_TYPE_ID) {
        arrSort = m_sorts.sortOf(d.arrayType.getTypeId());
    }
    if (!arrSort) {
        reportOutsideEnvelope(*node, "cannot determine array sort for array literal");
        return std::nullopt;
    }
    // Начальный массив: declare-fun константа (z3 4.8 не поддерживает (as const ...));
    // элементы кладутся store-цепочкой. select из константы - произвольное значение.
    const std::string arrName = "__arr" + std::to_string(m_arrCounter++);
    if (m_declared.insert(arrName).second) {
        SmtCommand cmd;
        cmd.kind = SmtCommandKind::kDeclareFun;
        cmd.fun_name = arrName;
        cmd.fun_arg_sorts = {};
        cmd.fun_result_sort = std::make_shared<SmtSort>(*arrSort);
        cmd.srcRange = node->range();
        m_plan.declarations.push_back(std::move(cmd));
    }
    SmtTerm arr = sb::makeNamedVar(arrName, *arrSort, node->range());
    SmtSort idxSort;
    if (arrSort->domain) {
        idxSort = *arrSort->domain;
    } else {
        idxSort.kind = SmtSortKind::kBitVec;
        idxSort.bv_width = 64;
    }
    size_t autoIdx = 0;
    for (const auto& el : d.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        const auto& a = static_cast<const ArgNode&>(*el);
        if (!a.m_value) {
            continue;
        }
        // Индекс: метка элемента (text) или автоинкремент по позиции.
        std::optional<SmtTerm> idx;
        if (!a.text().empty()) {
            idx = sb::makeConst(std::string(a.text()), idxSort, node->range());
        } else {
            idx = sb::makeConst(std::to_string(autoIdx), idxSort, node->range());
        }
        auto val = toTerm(a.m_value.get(), arrSort->range ? std::optional<SmtSort>(*arrSort->range) : std::nullopt, state);
        if (!val) {
            return std::nullopt;
        }
        *idx = sb::coerceToWidth(std::move(*idx), idxSort, false);
        arr = sb::makeApp("store", *arrSort,
                      {std::make_shared<SmtTerm>(std::move(arr)), std::make_shared<SmtTerm>(std::move(*idx)), std::make_shared<SmtTerm>(std::move(*val))},
                      node->range());
        ++autoIdx;
    }
    return arr;
}

std::optional<SmtTerm> TrustToSmt::encodeTrustElemTerm(const AstNodeBase* node, const std::optional<SmtSort>& expected, const VcValues* state) {
    // Термин решателя `@( term, args... @)` внутри выражения контракта: result/old/forall/exists.
    // fresh/length (и прочее) - вне фаз: выдаётся диагностика (не тихий пропуск).
    const auto& te = static_cast<const TrustElem&>(*node);
    switch (te.kind) {
    case Z3TermKind::Result: {
        // Результат функции (в пост-условии/теле). Если тело закодировано - фактический терм
        // возврата; иначе функция как uninterpreted-вызов от параметров (как имя функции).
        if (!m_buildAxiom && m_curReturn) {
            return *m_curReturn;
        }
        if (!m_curResultSort) {
            reportOutsideEnvelope(*node, "function result sort unknown");
            return std::nullopt;
        }
        if (m_curParams.empty()) {
            return sb::makeNamedVar(m_curFuncName, *m_curResultSort, node->range());
        }
        std::vector<std::shared_ptr<SmtTerm>> args;
        args.reserve(m_curParams.size());
        for (const auto& p : m_curParams) {
            const auto pit = m_paramSmtNames.find(p);
            const std::string smtName = pit != m_paramSmtNames.end() ? pit->second : p;
            const auto pt = m_paramTypes.find(p);
            const auto ps = pt != m_paramTypes.end() ? m_sorts.sortOf(pt->second) : std::nullopt;
            if (!ps) {
                reportOutsideEnvelope(*node, "parameter '{}' sort unknown", p);
                return std::nullopt;
            }
            args.push_back(std::make_shared<SmtTerm>(sb::makeNamedVar(smtName, *ps, node->range())));
        }
        return sb::makeApp(m_curFuncName, *m_curResultSort, std::move(args), node->range());
    }
    case Z3TermKind::Old: {
        // Старое (входное) значение аргумента: кодируется в начальном состоянии функции.
        if (te.m_args.empty() || !te.m_args[0]) {
            report(*node, "old expects an argument");
            return std::nullopt;
        }
        return toTerm(te.m_args[0].get(), expected, &m_entryState);
    }
    case Z3TermKind::Forall:
    case Z3TermKind::Exists: {
        if (te.m_args.size() < 2 || !te.m_args[0] || !te.m_args[1]) {
            report(*node, "quantifier expects (var, body)");
            return std::nullopt;
        }
        const std::string varName(requiredText(*te.m_args[0]));
        // Сорт связки - из типа ранее объявленной переменной (разрешён semantic в m_boundVarType);
        // НЕ выводится эвристикой. Если тип не установлен - ошибка.
        if (te.m_boundVarType == INVALID_TYPE_ID) {
            report(*te.m_args[0], "quantifier bound variable '{}' has no resolved type (must be a variable declared earlier)", varName);
            return std::nullopt;
        }
        auto varSort = m_sorts.sortOf(te.m_boundVarType);
        if (!varSort) {
            reportOutsideEnvelope(*te.m_args[0], "cannot determine sort of quantifier bound variable '{}'", varName);
            return std::nullopt;
        }
        // Связываем переменную квантора: имя → named var сорта (перекрывает параметр/глобал с тем
        // же именем - это связка, не свободная переменная). У bound-переменной нет AST-объявления,
        // поэтому связки хранятся отдельно от карты мест.
        const auto savedBound = m_boundNames;
        m_boundNames[varName] = sb::makeNamedVar(varName, *varSort, te.m_args[0]->range());
        auto body = toTerm(te.m_args[1].get(), expected, state);
        m_boundNames = savedBound;
        if (!body) {
            return std::nullopt;
        }
        if (te.kind == Z3TermKind::Forall) {
            return sb::makeForall({varName}, {*varSort}, std::move(*body), node->range());
        }
        return sb::makeExists({varName}, {*varSort}, std::move(*body), node->range());
    }
    default:
        reportOutsideEnvelope(*node, "solver term '{}' is not modelable", z3TermName(te.kind));
        return std::nullopt;
    }
}

} // namespace solver
} // namespace trust
