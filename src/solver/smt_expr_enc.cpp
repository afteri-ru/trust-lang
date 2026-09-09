#include "solver/trust_to_smt.hpp"
#include "solver/smt_term_builder.hpp"

#include "session/context.hpp"
#include "ast/ast_nodes.hpp"
#include "types/registry.hpp"
#include "types/typekind.hpp"
#include "types/group.hpp"
#include "utils/error.hpp"
#include "semantic/solver.hpp"

#include <algorithm>
#include <memory>

namespace trust {
namespace solver {

using namespace SmtTermBuilder;

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

int TrustToSmt::exprSign(const AstNodeBase* node) const {
    // 1 - знаковый (kIntegers), 0 - беззнаковый (kUnsigned), -1 - неизвестно/нейтрально.
    // Литералы и узлы с неизвестным типом нейтральны: наследуют знак контекста (другого операнда).
    if (!node) {
        return -1;
    }
    switch (node->kind()) {
    case ParserToken::Kind::Ident: {
        const std::string name(node->text());
        // Параметр функции: точный знак из типа параметра.
        if (auto it = m_paramTypes.find(name); it != m_paramTypes.end()) {
            return m_sorts.isSignedType(it->second) ? 1 : 0;
        }
        // Имя функции в пост-условии = возвращаемое значение: знак результата функции.
        if (m_inPost && name == m_curFuncName) {
            return m_curResultSigned ? 1 : 0;
        }
        return -1; // глобал/неизвестный идентификатор
    }
    case ParserToken::Kind::MathOp:
    case ParserToken::Kind::BitwiseOp: {
        // Знак результата арифметики/битовой операции следует знаку операндов.
        const auto& b = static_cast<const Binary&>(*node);
        const int ls = exprSign(b.m_left.get());
        const int rs = exprSign(b.m_right.get());
        if (ls == 1 || rs == 1) {
            return 1;
        }
        if (ls == 0 || rs == 0) {
            return 0;
        }
        return -1;
    }
    case ParserToken::Kind::CallExpr: {
        // Знак результата вызова - из типа возврата callee (по сигнатуре AST, без скоуп-стека).
        const auto& call = static_cast<const CallExpr&>(*node);
        if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::Ident) {
            if (auto it = m_funcDecls.find(std::string(call.m_callee->text())); it != m_funcDecls.end()) {
                const FuncDecl* fd = it->second;
                if (fd->m_type) {
                    if (auto pt = m_sorts.resolveTypeByName(fd->m_type->text())) {
                        return m_sorts.isSignedType(*pt) ? 1 : 0;
                    }
                }
            }
        }
        return -1;
    }
    case ParserToken::Kind::ArrayAccess: {
        // Знак элемента массива - по типу элемента (resultType узла доступа).
        const auto& b = static_cast<const Binary&>(*node);
        if (b.resultType != INVALID_TYPE_ID) {
            return m_sorts.isSignedType(b.resultType) ? 1 : 0;
        }
        return -1;
    }
    case ParserToken::Kind::IntLiteral:
    case ParserToken::Kind::FloatLiteral:
    default:
        return -1; // литералы и прочее нейтральны
    }
}

std::optional<SmtTerm> TrustToSmt::toTerm(const AstNodeBase* node, const std::optional<SmtSort>& expected,
                                          const std::unordered_map<std::string, SmtTerm>* state) {
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
        report(*node, "expression kind is not supported in solver export");
        return std::nullopt;
    }
}

std::optional<SmtTerm> TrustToSmt::encodeIntLiteralTerm(const AstNodeBase* node, const std::optional<SmtSort>& expected) {
    const auto& lit = static_cast<const Literal&>(*node);
    std::optional<SmtSort> s = expected;
    if (!s || s->kind != SmtSortKind::kBitVec) {
        s = m_sorts.sortOf(lit.typeId);
    }
    // В пост-условии (имя функции = возврат) сорт может быть не выведен у операндов -
    // используем сорт результата функции.
    if ((!s || s->kind != SmtSortKind::kBitVec) && m_curResultSort && m_curResultSort->kind == SmtSortKind::kBitVec) {
        s = m_curResultSort;
    }
    if (!s || s->kind != SmtSortKind::kBitVec) {
        report(lit, "integer literal outside a bit-vector context");
        return std::nullopt;
    }
    return makeConst(std::string(lit.text()), *s, lit.range());
}

std::optional<SmtTerm> TrustToSmt::encodeFloatLiteralTerm(const AstNodeBase* node) {
    const auto& lit = static_cast<const Literal&>(*node);
    auto rs = m_sorts.sortOf(lit.typeId);
    if (!rs || rs->kind != SmtSortKind::kReal) {
        report(lit, "float literal outside a real context");
        return std::nullopt;
    }
    return makeConst(std::string(lit.text()), *rs, lit.range());
}

std::optional<SmtTerm> TrustToSmt::encodeIdentTerm(const AstNodeBase* node, const std::optional<SmtSort>& expected,
                                                   const std::unordered_map<std::string, SmtTerm>* state) {
    const std::string name(node->text());
    if (name == "true" || name == "yes") {
        return makeConst("true", boolSort(), node->range());
    }
    if (name == "false" || name == "no") {
        return makeConst("false", boolSort(), node->range());
    }
    // SSA-значение переменной тела (из переданного состояния) - терм её текущего значения.
    if (state) {
        if (auto it = state->find(name); it != state->end()) {
            return it->second; // копия (терм уже проставлен сорт)
        }
    }
    // Пост-условие: имя функции = возвращаемое значение.
    if (m_inPost && name == m_curFuncName && !m_curParams.empty()) {
        if (!m_buildAxiom && m_curReturn) {
            return *m_curReturn; // тело закодировано: функция = фактический терм возврата
        }
        // Аксиома контракта (m_buildAxiom) или нет тела: имя функции → uninterpreted-вызов.
        if (!m_curResultSort) {
            report(*node, "function result sort unknown");
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
                report(*node, "parameter '{}' sort unknown", p);
                return std::nullopt;
            }
            args.push_back(std::make_shared<SmtTerm>(makeNamedVar(smtName, *ps, node->range())));
        }
        m_symbolMap[name] = SmtSymbolRef{name, node->range()};
        return makeApp(name, *m_curResultSort, std::move(args), node->range());
    }
    // Параметр функции → его уникальное SMT-имя (func_param) с известным сортом.
    if (auto it = m_paramSmtNames.find(name); it != m_paramSmtNames.end()) {
        const auto pt = m_paramTypes.find(name);
        const auto ps = pt != m_paramTypes.end() ? m_sorts.sortOf(pt->second) : std::nullopt;
        if (!ps) {
            report(*node, "parameter '{}' sort unknown", name);
            return std::nullopt;
        }
        return makeNamedVar(it->second, *ps, node->range());
    }
    // Свободное имя (глобал): сорт из контекста (числовой) или Bool (логический контекст).
    SmtSort gsort = boolSort();
    if (expected && (expected->kind == SmtSortKind::kBitVec || expected->kind == SmtSortKind::kReal)) {
        gsort = *expected;
    }
    m_symbolMap[name] = SmtSymbolRef{name, node->range()};
    return makeNamedVar(name, gsort, node->range());
}

std::optional<SmtTerm> TrustToSmt::encodeOperatorTerm(const AstNodeBase* node, const std::optional<SmtSort>& expected,
                                                      const std::unordered_map<std::string, SmtTerm>* state) {
    const auto& b = static_cast<const Binary&>(*node);
    const std::string_view op = b.text();
    // Унарный минус: MathOp "-" с m_left==nullptr.
    if (!b.m_left && b.kind() == ParserToken::Kind::MathOp && op == "-") {
        auto rhs = toTerm(b.m_right.get(), expected, state);
        if (!rhs) {
            return std::nullopt;
        }
        const SmtSort negSort = rhs->sort;
        return makeApp("bvneg", negSort, {std::make_shared<SmtTerm>(std::move(*rhs))}, b.range());
    }
    if (!b.m_left || !b.m_right) {
        report(b, "unsupported unary operator '{}'", op);
        return std::nullopt;
    }
    // Сорт операндов: для Compare/Logical - общий тип операндов (commonType/lhsType);
    // для MathOp/BitwiseOp - тип РЕЗУЛЬТАТА (продвинутый; напр. Int8+Int32 → Int32, mixed-width 2.5).
    const TypeId opType = [&]() -> TypeId {
        if (b.kind() == ParserToken::Kind::CompareOp || b.kind() == ParserToken::Kind::LogicalOp) {
            return b.commonType != INVALID_TYPE_ID ? b.commonType : b.lhsType;
        }
        return b.resultType != INVALID_TYPE_ID ? b.resultType : (b.lhsType != INVALID_TYPE_ID ? b.lhsType : b.rhsType);
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
        auto r = m_sorts.sortOf(b.rhsType);
        if (r && (r->kind == SmtSortKind::kBitVec || r->kind == SmtSortKind::kReal)) {
            opSort = r;
        }
    }
    const bool bv = opSort && opSort->kind == SmtSortKind::kBitVec;

    // Знак оператора: для беззнаковых целых (kUnsigned) используются беззнаковые BV-операторы
    // (bvult/bvule/bvugt/bvuge, bvudiv/bvurem, логический сдвиг); для знаковых (kIntegers) -
    // знаковые (bvslt/bvsle/..., bvsdiv/bvsrem, арифметический сдвиг). Знак выводим РЕКУРСИВНО
    // из типов операндов-переменных (литералы/INVALID - нейтральны, наследуют знак контекста;
    // узел сравнения даёт INVALID/Bool для вложенных выражений - полагаться на него нельзя).
    const int lSign = exprSign(b.m_left.get());
    const int rSign = exprSign(b.m_right.get());
    bool signedOp = (lSign == 1 || rSign == 1) ? true : (lSign == 0 || rSign == 0) ? false : true;

    if (b.kind() == ParserToken::Kind::LogicalOp) {
        const auto smtOp = mapLogicalOp(op);
        if (smtOp.empty()) {
            report(b, "unsupported logical operator '{}'", op);
            return std::nullopt;
        }
        auto l = toTerm(b.m_left.get(), std::nullopt, state);
        auto r = toTerm(b.m_right.get(), std::nullopt, state);
        if (!l || !r) {
            return std::nullopt;
        }
        return makeApp(std::string(smtOp), boolSort(), {std::make_shared<SmtTerm>(std::move(*l)), std::make_shared<SmtTerm>(std::move(*r))}, b.range());
    }
    if (b.kind() == ParserToken::Kind::CompareOp) {
        const bool isEq = isEqOp(op);
        const bool isNeq = isNeqOp(op);
        std::string cmpOp = "=";
        if (!isEq && !isNeq) {
            cmpOp = mapCmpOp(op, bv, signedOp);
            if (cmpOp.empty()) {
                report(b, "unsupported comparison operator '{}'", op);
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
        *l = coerceToWidth(std::move(*l), target, m_sorts.isSignedType(b.lhsType));
        *r = coerceToWidth(std::move(*r), target, m_sorts.isSignedType(b.rhsType));
        auto cmp = makeApp(cmpOp, boolSort(), {std::make_shared<SmtTerm>(std::move(*l)), std::make_shared<SmtTerm>(std::move(*r))}, b.range());
        return isNeq ? mkNot(std::make_shared<SmtTerm>(std::move(cmp)), b.range()) : cmp;
    }
    // MathOp / BitwiseOp.
    const auto smtOp = mapArithOp(op, bv, signedOp);
    if (smtOp.empty()) {
        report(b, "unsupported arithmetic/bitwise operator '{}'", op);
        return std::nullopt;
    }
    if (!opSort) {
        report(b, "cannot determine operand sort for '{}'", op);
        return std::nullopt;
    }
    auto l = toTerm(b.m_left.get(), opSort, state);
    auto r = toTerm(b.m_right.get(), opSort, state);
    if (!l || !r) {
        return std::nullopt;
    }
    // Mixed-width BV (2.5): расширение операндов до общего сорта результата.
    *l = coerceToWidth(std::move(*l), *opSort, m_sorts.isSignedType(b.lhsType));
    *r = coerceToWidth(std::move(*r), *opSort, m_sorts.isSignedType(b.rhsType));
    return makeApp(std::string(smtOp), *opSort, {std::make_shared<SmtTerm>(std::move(*l)), std::make_shared<SmtTerm>(std::move(*r))}, b.range());
}

std::optional<SmtTerm> TrustToSmt::encodeCallTerm(const AstNodeBase* node, const std::unordered_map<std::string, SmtTerm>* state) {
    // Интерпроцедурный вызов (2.4): `(f args)` с сортами из сигнатуры callee; callee -
    // uninterpreted declare-fun; контракт callee добавляется как аксиома (см. processFuncContract).
    const auto& call = static_cast<const CallExpr&>(*node);
    if (!call.m_callee || call.m_callee->kind() != ParserToken::Kind::Ident) {
        report(*node, "unsupported callee in solver export");
        return std::nullopt;
    }
    const std::string callee(call.m_callee->text());
    const FuncDecl* fd = nullptr;
    if (auto it = m_funcDecls.find(callee); it != m_funcDecls.end()) {
        fd = it->second;
    }
    if (!fd) {
        report(*node, "called function '{}' is not declared in module", callee);
        return std::nullopt;
    }
    // Сигнатура callee из AST-аннотаций (без скоуп-стека): сорта параметров и возврата.
    std::vector<SmtSort> paramSorts;
    if (fd->m_params) {
        for (const auto& p : *fd->m_params) {
            if (!p || p->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            const auto& pd = static_cast<const ArgNode&>(*p);
            std::optional<SmtSort> ps;
            if (pd.m_type) {
                if (auto pt = m_sorts.resolveTypeByName(pd.m_type->text())) {
                    ps = m_sorts.sortOf(*pt);
                }
            }
            if (!ps) {
                ps = m_sorts.sortOf(pd.resultType);
            }
            if (!ps) {
                report(*node, "called function '{}' parameter sort unknown", callee);
                return std::nullopt;
            }
            paramSorts.push_back(*ps);
        }
    }
    std::optional<SmtSort> resSort;
    if (fd->m_type) {
        if (auto rt = m_sorts.resolveTypeByName(fd->m_type->text())) {
            resSort = m_sorts.sortOf(*rt);
        }
    }
    if (!resSort) {
        report(*node, "called function '{}' result sort unknown", callee);
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
        m_script.commands.push_back(std::move(cmd));
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
            args.push_back(std::make_shared<SmtTerm>(std::move(*t)));
        }
    }
    return makeApp(callee, *resSort, std::move(args), node->range());
}

std::optional<SmtTerm> TrustToSmt::encodeArrayAccessTerm(const AstNodeBase* node, const std::unordered_map<std::string, SmtTerm>* state) {
    // Массив[i] → (select arr idx) (2.8). Сорт результата = элемент массива (resultType).
    const auto& b = static_cast<const Binary&>(*node);
    if (!b.m_left || !b.m_right) {
        report(*node, "array access missing operand");
        return std::nullopt;
    }
    auto arr = toTerm(b.m_left.get(), std::nullopt, state);
    auto idx = toTerm(b.m_right.get(), std::nullopt, state);
    if (!arr || !idx) {
        return std::nullopt;
    }
    std::optional<SmtSort> elemSort;
    if (b.resultType != INVALID_TYPE_ID) {
        elemSort = m_sorts.sortOf(b.resultType);
    }
    if (!elemSort && arr->sort.kind == SmtSortKind::kArray && arr->sort.range) {
        elemSort = *arr->sort.range;
    }
    if (!elemSort) {
        report(*node, "cannot determine element sort for array access");
        return std::nullopt;
    }
    // Индекс приводится к домену массива (2.8): домен (BitVec 64), индекс может быть уже.
    if (arr->sort.kind == SmtSortKind::kArray && arr->sort.domain) {
        *idx = coerceToWidth(std::move(*idx), *arr->sort.domain, false);
    }
    return makeApp("select", *elemSort, {std::make_shared<SmtTerm>(std::move(*arr)), std::make_shared<SmtTerm>(std::move(*idx))}, node->range());
}

std::optional<SmtTerm> TrustToSmt::encodeArrayInitTerm(const AstNodeBase* node, const std::unordered_map<std::string, SmtTerm>* state) {
    // Литерал массива `[v0, v1, ...]` → store-цепочка от нулевого массива (2.8).
    const auto& d = static_cast<const DictLiteralNode&>(*node);
    std::optional<SmtSort> arrSort;
    if (d.arrayType != INVALID_TYPE_ID) {
        arrSort = m_sorts.sortOf(d.arrayType);
    }
    if (!arrSort) {
        report(*node, "cannot determine array sort for array literal");
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
        m_script.commands.push_back(std::move(cmd));
    }
    SmtTerm arr = makeNamedVar(arrName, *arrSort, node->range());
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
            idx = makeConst(std::string(a.text()), idxSort, node->range());
        } else {
            idx = makeConst(std::to_string(autoIdx), idxSort, node->range());
        }
        auto val = toTerm(a.m_value.get(), arrSort->range ? std::optional<SmtSort>(*arrSort->range) : std::nullopt, state);
        if (!val) {
            return std::nullopt;
        }
        *idx = coerceToWidth(std::move(*idx), idxSort, false);
        arr = makeApp("store", *arrSort,
                      {std::make_shared<SmtTerm>(std::move(arr)), std::make_shared<SmtTerm>(std::move(*idx)), std::make_shared<SmtTerm>(std::move(*val))},
                      node->range());
        ++autoIdx;
    }
    return arr;
}

std::optional<SmtTerm> TrustToSmt::encodeTrustElemTerm(const AstNodeBase* node, const std::optional<SmtSort>& expected,
                                                       const std::unordered_map<std::string, SmtTerm>* state) {
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
            report(*node, "function result sort unknown");
            return std::nullopt;
        }
        if (m_curParams.empty()) {
            return makeNamedVar(m_curFuncName, *m_curResultSort, node->range());
        }
        std::vector<std::shared_ptr<SmtTerm>> args;
        args.reserve(m_curParams.size());
        for (const auto& p : m_curParams) {
            const auto pit = m_paramSmtNames.find(p);
            const std::string smtName = pit != m_paramSmtNames.end() ? pit->second : p;
            const auto pt = m_paramTypes.find(p);
            const auto ps = pt != m_paramTypes.end() ? m_sorts.sortOf(pt->second) : std::nullopt;
            if (!ps) {
                report(*node, "parameter '{}' sort unknown", p);
                return std::nullopt;
            }
            args.push_back(std::make_shared<SmtTerm>(makeNamedVar(smtName, *ps, node->range())));
        }
        return makeApp(m_curFuncName, *m_curResultSort, std::move(args), node->range());
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
        const std::string varName(te.m_args[0]->text());
        // Сорт связки - из типа ранее объявленной переменной (разрешён semantic в m_boundVarType);
        // НЕ выводится эвристикой. Если тип не установлен - ошибка.
        if (te.m_boundVarType == INVALID_TYPE_ID) {
            report(*te.m_args[0], "quantifier bound variable '{}' has no resolved type (must be a variable declared earlier)", varName);
            return std::nullopt;
        }
        auto varSort = m_sorts.sortOf(te.m_boundVarType);
        if (!varSort) {
            report(*te.m_args[0], "cannot determine sort of quantifier bound variable '{}'", varName);
            return std::nullopt;
        }
        // Связываем переменную квантора: в состоянии var → named var сорта (перекрывает
        // параметр/глобал с тем же именем - это связка, не свободная переменная).
        std::unordered_map<std::string, SmtTerm> qstate;
        if (state) {
            qstate = *state;
        }
        qstate[varName] = makeNamedVar(varName, *varSort, te.m_args[0]->range());
        auto body = toTerm(te.m_args[1].get(), expected, &qstate);
        if (!body) {
            return std::nullopt;
        }
        if (te.kind == Z3TermKind::Forall) {
            return makeForall({varName}, {*varSort}, std::move(*body), node->range());
        }
        return makeExists({varName}, {*varSort}, std::move(*body), node->range());
    }
    default:
        report(*node, "solver term '{}' is not supported in solver export", z3TermName(te.kind));
        return std::nullopt;
    }
}

} // namespace solver
} // namespace trust
