#include "solver/trust_to_smt.hpp"
#include "solver/smt_term_builder.hpp"

#include "diag/context.hpp"
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

TrustToSmt::TrustToSmt(Context& ctx)
: m_ctx(ctx)
, m_sorts(ctx) {
}

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
    case ParserToken::Kind::IntLiteral: {
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
    case ParserToken::Kind::FloatLiteral: {
        const auto& lit = static_cast<const Literal&>(*node);
        auto rs = m_sorts.sortOf(lit.typeId);
        if (!rs || rs->kind != SmtSortKind::kReal) {
            report(lit, "float literal outside a real context");
            return std::nullopt;
        }
        return makeConst(std::string(lit.text()), *rs, lit.range());
    }
    case ParserToken::Kind::Ident: {
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

    case ParserToken::Kind::MathOp:
    case ParserToken::Kind::BitwiseOp:
    case ParserToken::Kind::CompareOp:
    case ParserToken::Kind::LogicalOp: {
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
        // знаковые (bvslt/bvsle/..., bvsdiv/bvsrem, арифметический сдвиг). Знак берём из ТИПОВ
        // ОПЕРАНДОВ (lhsType/rhsType), а не из продвинутого commonType: литерал по умолчанию может
        // быть kUnsigned, и если другой операнд знаковый (Int32) - сравнение остаётся знаковым.
        // Знак оператора: для беззнаковых целых (kUnsigned) - беззнаковые BV-операторы
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
            if (isEqOp(op)) {
                auto l = toTerm(b.m_left.get(), opSort, state);
                auto r = toTerm(b.m_right.get(), opSort, state);
                if (!l || !r) {
                    return std::nullopt;
                }
                *l = coerceToWidth(std::move(*l), *opSort, m_sorts.isSignedType(b.lhsType));
                *r = coerceToWidth(std::move(*r), *opSort, m_sorts.isSignedType(b.rhsType));
                return makeApp("=", boolSort(), {std::make_shared<SmtTerm>(std::move(*l)), std::make_shared<SmtTerm>(std::move(*r))}, b.range());
            }
            if (isNeqOp(op)) {
                auto l = toTerm(b.m_left.get(), opSort, state);
                auto r = toTerm(b.m_right.get(), opSort, state);
                if (!l || !r) {
                    return std::nullopt;
                }
                *l = coerceToWidth(std::move(*l), *opSort, m_sorts.isSignedType(b.lhsType));
                *r = coerceToWidth(std::move(*r), *opSort, m_sorts.isSignedType(b.rhsType));
                auto eq = makeApp("=", boolSort(), {std::make_shared<SmtTerm>(std::move(*l)), std::make_shared<SmtTerm>(std::move(*r))}, b.range());
                return mkNot(std::make_shared<SmtTerm>(std::move(eq)), b.range());
            }
            const auto smtOp = mapCmpOp(op, bv, signedOp);
            if (smtOp.empty()) {
                report(b, "unsupported comparison operator '{}'", op);
                return std::nullopt;
            }
            auto l = toTerm(b.m_left.get(), opSort, state);
            auto r = toTerm(b.m_right.get(), opSort, state);
            if (!l || !r) {
                return std::nullopt;
            }
            *l = coerceToWidth(std::move(*l), *opSort, m_sorts.isSignedType(b.lhsType));
            *r = coerceToWidth(std::move(*r), *opSort, m_sorts.isSignedType(b.rhsType));
            return makeApp(std::string(smtOp), boolSort(), {std::make_shared<SmtTerm>(std::move(*l)), std::make_shared<SmtTerm>(std::move(*r))}, b.range());
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
    case ParserToken::Kind::CallExpr: {
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
    case ParserToken::Kind::ArrayAccess: {
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
    case ParserToken::Kind::ArrayInit: {
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
    case ParserToken::Kind::TrustElem: {
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
    default:
        report(*node, "expression kind is not supported in solver export");
        return std::nullopt;
    }
}

void TrustToSmt::addAssert(std::optional<SmtTerm>&& vc, MapperRange srcRange, bool isolated) {
    if (!vc) {
        return;
    }
    SmtCommand cmd;
    cmd.kind = SmtCommandKind::kAssert;
    cmd.assert_term = std::make_shared<SmtTerm>(std::move(*vc));
    cmd.srcRange = srcRange; // какая trust-конструкция породила VC (контракт функции)
    cmd.isolated = isolated; // VC изолируется push/check-sat/pop; аксиомы/глобальные - нет
    m_script.commands.push_back(std::move(cmd));
}

std::optional<SmtTerm> TrustToSmt::encodeBody(const FuncDecl& f) {
    m_bodyAsserts.clear();
    // Начальное SSA-состояние: параметры → их константы (func_param).
    std::unordered_map<std::string, SmtTerm> state;
    for (const auto& p : m_curParams) {
        const auto pit = m_paramSmtNames.find(p);
        if (pit == m_paramSmtNames.end()) {
            continue;
        }
        const auto pt = m_paramTypes.find(p);
        const auto ps = pt != m_paramTypes.end() ? m_sorts.sortOf(pt->second) : std::nullopt;
        if (!ps) {
            report(f, "parameter '{}' sort unknown", p);
            return std::nullopt;
        }
        const MapperRange pr = m_paramRanges.count(p) ? m_paramRanges[p] : f.range();
        state[p] = makeNamedVar(pit->second, *ps, pr);
    }
    // Начальное состояние функции - для термина `@( old, x @)` (значение на входе).
    m_entryState = state;
    if (!f.m_body) {
        return std::nullopt; // forward/без тела - остаётся uninterpreted
    }
    BlockResult res = encodeBlock(*f.m_body, std::move(state), std::nullopt);
    m_bodyAsserts = std::move(res.asserts);
    return std::move(res.ret);
}

BlockResult TrustToSmt::encodeBlock(const std::vector<AstNodePtr>& nodes, const std::unordered_map<std::string, SmtTerm>& inState,
                                    const std::optional<SmtTerm>& inRet) {
    BlockResult res;
    res.state = inState;
    res.ret = inRet;
    // Инвариант цикла (2.6): AST-узел последнего `@[ I @];` перед @while в этом блоке.
    const AstNodeBase* loopInvNode = nullptr;
    // Если инвариант-контракт несёт директиву решателя `z3_unroll(N)` - это НЕ индукция, а
    // разворачивание на N итераций (приоритетнее глобального `-fsolver-loop-unroll`).
    const auto invariantUnroll = [](const AstNodeBase* inv) -> int {
        if (inv && inv->kind() == ParserToken::Kind::TrustElem) {
            const auto& te = static_cast<const TrustElem&>(*inv);
            if (te.kind == Z3TermKind::Unroll && !te.m_args.empty() && te.m_args[0]) {
                try {
                    return std::stoi(std::string(te.m_args[0]->text()));
                } catch (...) {
                    return 0; // не число - не разворачиваем (диагностика ниже)
                }
            }
        }
        return 0;
    };
    // Bounded unrolling тела на n итераций со слиянием состояния через ite по cond.
    const auto doUnroll = [&](int n, const SmtTerm& cond, const AstNodeBase* node, const std::vector<AstNodePtr>& body) -> void {
        for (int it = 0; it < n; ++it) {
            BlockResult bodyRes = encodeBlock(body, res.state, res.ret);
            for (auto& [v, vterm] : res.state) {
                (void)vterm;
                if (auto fit = bodyRes.state.find(v); fit != bodyRes.state.end()) {
                    res.state[v] = makeApp("ite", fit->second.sort,
                                           {std::make_shared<SmtTerm>(cond), std::make_shared<SmtTerm>(fit->second), std::make_shared<SmtTerm>(res.state[v])},
                                           node->range());
                }
            }
            for (auto& a : bodyRes.asserts) {
                res.asserts.push_back(makeApp("=>", boolSort(), {std::make_shared<SmtTerm>(cond), std::make_shared<SmtTerm>(std::move(a))}, node->range()));
            }
            if (bodyRes.ret) {
                if (res.ret) {
                    res.ret =
                        makeApp("ite", res.ret->sort,
                                {std::make_shared<SmtTerm>(cond), std::make_shared<SmtTerm>(*bodyRes.ret), std::make_shared<SmtTerm>(*res.ret)}, node->range());
                } else {
                    res.ret = bodyRes.ret;
                }
            }
        }
    };
    for (const auto& stmt : nodes) {
        if (!stmt) {
            continue;
        }
        switch (stmt->kind()) {
        case ParserToken::Kind::VarDecl: {
            const auto& v = static_cast<const VarDecl&>(*stmt);
            if (!v.m_initializer) {
                break;
            }
            auto val = toTerm(v.m_initializer.get(), std::nullopt, &res.state);
            if (!val) {
                return {};
            }
            res.state[std::string(v.text())] = std::move(*val);
            // Переменный trust-контракт (напр. `y @{ assert: A @} := expr`, 2.3): после
            // инициализации y проверяется A при y = init (state уже содержит y → init).
            for (const auto& t : v.m_trust) {
                const auto* tc = dynamic_cast<const TrustContract*>(t.get());
                if (!tc || (tc->kind != PropertyKind::Assert && tc->kind != PropertyKind::kUnknown)) {
                    continue;
                }
                if (!tc->m_expr) {
                    continue;
                }
                auto a = toTerm(tc->m_expr.get(), std::nullopt, &res.state);
                if (!a) {
                    return {};
                }
                res.asserts.push_back(std::move(*a));
            }
            break;
        }
        case ParserToken::Kind::AssignOp: {
            const auto& b = static_cast<const Binary&>(*stmt);
            if (!b.m_left || !b.m_right) {
                break;
            }
            auto val = toTerm(b.m_right.get(), std::nullopt, &res.state);
            if (!val) {
                return {};
            }
            res.state[std::string(b.m_left->text())] = std::move(*val);
            break;
        }
        case ParserToken::Kind::ReturnStmt: {
            const auto& j = static_cast<const JumpStmt&>(*stmt);
            if (j.m_value) {
                auto val = toTerm(j.m_value.get(), std::nullopt, &res.state);
                if (!val) {
                    return {};
                }
                res.ret = std::move(*val);
            } else {
                res.ret = std::nullopt; // void return - значения нет
            }
            break;
        }
        case ParserToken::Kind::TrustContract: {
            // Автономный trust-контракт `@{ [kind:] expr @};` в точке тела:
            //   - assert: добавляется в консеквенты (2.3);
            //   - invariant перед циклом: запоминаем AST-узел для следующего @while.
            const auto& tc = static_cast<const TrustContract&>(*stmt);
            if (tc.kind == PropertyKind::Invariant) {
                if (tc.m_expr) {
                    loopInvNode = tc.m_expr.get();
                }
                break;
            }
            if (tc.kind != PropertyKind::Assert && tc.kind != PropertyKind::kUnknown) {
                break;
            }
            if (!tc.m_expr) {
                break;
            }
            auto a = toTerm(tc.m_expr.get(), std::nullopt, &res.state);
            if (!a) {
                return {};
            }
            res.asserts.push_back(std::move(*a));
            break;
        }
        case ParserToken::Kind::SemicolonStmt: {
            const auto& ss = static_cast<const SemicolonStmt&>(*stmt);
            if (ss.m_expr) {
                BlockResult sub = encodeBlock(stmtsOf(ss.m_expr), res.state, res.ret);
                res.state = std::move(sub.state);
                res.ret = std::move(sub.ret);
                res.asserts.insert(res.asserts.end(), std::make_move_iterator(sub.asserts.begin()), std::make_move_iterator(sub.asserts.end()));
            }
            break;
        }
        case ParserToken::Kind::ScopeBlock: {
            const auto& sc = static_cast<const ScopeBlock&>(*stmt);
            BlockResult sub = encodeBlock(sc.m_body, res.state, res.ret);
            res.state = std::move(sub.state);
            res.ret = std::move(sub.ret);
            res.asserts.insert(res.asserts.end(), std::make_move_iterator(sub.asserts.begin()), std::make_move_iterator(sub.asserts.end()));
            break;
        }
        case ParserToken::Kind::IfStmt: {
            const auto& is = static_cast<const IfStmt&>(*stmt);
            auto cond = toTerm(is.m_cond.get(), std::nullopt, &res.state);
            if (!cond) {
                return {};
            }
            // Ветки: (условие, statements). else - отдельно.
            struct Branch {
                SmtTerm cond;
                std::vector<AstNodePtr> stmts;
            };
            std::vector<Branch> branches;
            branches.push_back({*cond, stmtsOf(is.m_body)});
            for (const auto& [c, b] : is.m_elseifs) {
                auto ct = toTerm(c.get(), std::nullopt, &res.state);
                if (!ct) {
                    return {};
                }
                branches.push_back({*ct, stmtsOf(b)});
            }
            const std::vector<AstNodePtr> elseStmts = stmtsOf(is.m_else);

            // Кодируем каждую ветку от копии входного состояния.
            std::vector<BlockResult> brs;
            brs.reserve(branches.size());
            for (const auto& br : branches) {
                BlockResult r = encodeBlock(br.stmts, res.state, std::nullopt);
                brs.push_back(std::move(r));
            }
            const BlockResult elseRes = encodeBlock(elseStmts, res.state, std::nullopt);

            // Возврат: требуем return во ВСЕХ ветках и в else (иначе не тихий fallback - диагноз).
            const bool allReturn = elseRes.ret.has_value() && std::all_of(brs.begin(), brs.end(), [](const BlockResult& r) { return r.ret.has_value(); });
            if (!allReturn) {
                report(is, "if statement with a non-returning branch is not supported in solver export");
                return {};
            }

            // Слияние переменных через ite-цепочку.
            std::unordered_set<std::string> vars;
            for (const auto& r : brs) {
                for (const auto& [n, v] : r.state) {
                    (void)v;
                    vars.insert(n);
                }
            }
            for (const auto& [n, v] : elseRes.state) {
                (void)v;
                vars.insert(n);
            }
            for (const auto& v : vars) {
                // Значение в else-ветке: из elseRes.state, иначе preState (не менялась).
                SmtTerm elseVal;
                bool haveElse = false;
                if (auto it = elseRes.state.find(v); it != elseRes.state.end()) {
                    elseVal = it->second;
                    haveElse = true;
                } else if (auto it = res.state.find(v); it != res.state.end()) {
                    elseVal = it->second;
                    haveElse = true;
                }
                if (!haveElse) {
                    continue; // переменная определена только внутри веток - вне if не видна
                }
                SmtTerm acc = elseVal;
                for (int i = static_cast<int>(brs.size()) - 1; i >= 0; --i) {
                    SmtTerm branchVal;
                    if (auto fit = brs[i].state.find(v); fit != brs[i].state.end()) {
                        branchVal = fit->second;
                    } else if (auto pit = res.state.find(v); pit != res.state.end()) {
                        branchVal = pit->second; // не менялась в ветке → preState
                    } else {
                        branchVal = elseVal;
                    }
                    acc = makeApp("ite", acc.sort,
                                  {std::make_shared<SmtTerm>(branches[i].cond), std::make_shared<SmtTerm>(std::move(branchVal)),
                                   std::make_shared<SmtTerm>(std::move(acc))},
                                  is.range());
                }
                res.state[v] = std::move(acc);
            }
            // Слияние возврата: ite(cond1, ret1, ite(cond2, ret2, ..., elseRet)).
            {
                SmtTerm acc = *elseRes.ret;
                for (int i = static_cast<int>(brs.size()) - 1; i >= 0; --i) {
                    acc = makeApp(
                        "ite", acc.sort,
                        {std::make_shared<SmtTerm>(branches[i].cond), std::make_shared<SmtTerm>(*brs[i].ret), std::make_shared<SmtTerm>(std::move(acc))},
                        is.range());
                }
                res.ret = std::move(acc);
            }
            // Утверждения из веток: оборачиваем охранкой `cond → A` (проверяется только на пути ветки).
            for (std::size_t i = 0; i < brs.size(); ++i) {
                for (auto& a : brs[i].asserts) {
                    res.asserts.push_back(
                        makeApp("=>", boolSort(), {std::make_shared<SmtTerm>(branches[i].cond), std::make_shared<SmtTerm>(std::move(a))}, is.range()));
                }
            }
            for (auto& a : elseRes.asserts) {
                res.asserts.push_back(std::move(a));
            }
            break;
        }
        case ParserToken::Kind::WhileStmt: {
            const auto& w = static_cast<const WhileStmt&>(*stmt);
            auto cond = toTerm(w.m_cond.get(), std::nullopt, &res.state);
            if (!cond) {
                return {};
            }
            if (loopInvNode) {
                const int invUnroll = invariantUnroll(loopInvNode);
                if (invUnroll > 0) {
                    // Директива решателя z3_unroll(N): разворачиваем на N (приоритетнее глобального флага).
                    doUnroll(invUnroll, *cond, &w, stmtsOf(w.m_body));
                } else {
                    // Инвариант (2.6): формализуем индукцию, НЕ разворачивая.
                    //   assert I(начало);   assert (I ∧ cond) → I[тело].
                    // Постусловие (в processFuncContract) доказывается через инвариант.
                    auto I0 = toTerm(loopInvNode, std::nullopt, &res.state);
                    if (!I0) {
                        return {};
                    }
                    res.asserts.push_back(*I0); // инвариант выполняется до цикла
                    BlockResult bodyRes = encodeBlock(stmtsOf(w.m_body), res.state, res.ret);
                    auto Ibody = toTerm(loopInvNode, std::nullopt, &bodyRes.state);
                    if (!Ibody) {
                        return {};
                    }
                    SmtTerm ant = mkAnd({*I0, *cond}, w.range());
                    res.asserts.push_back(
                        makeApp("=>", boolSort(), {std::make_shared<SmtTerm>(std::move(ant)), std::make_shared<SmtTerm>(std::move(*Ibody))}, w.range()));
                    // состояние после цикла (не развёрнуто) - постусловие выводится из инварианта.
                }
            } else {
                // Нет инварианта. Разворачиваем ТОЛЬКО при глобальном `-fsolver-loop-unroll`; иначе -
                // диагностика по `-Wsolver-loop` (по умолчанию warning: цикл не участвует в доказательстве).
                if (semantic::solverLoopUnrollEnabled(m_ctx.opts())) {
                    doUnroll(m_unroll, *cond, &w, stmtsOf(w.m_body));
                } else {
                    switch (semantic::solverLoopModeFromOptions(m_ctx.opts())) {
                    case semantic::SolverLoopMode::kError:
                        report(w, "loop without an invariant cannot be verified; add an invariant or enable unrolling");
                        return {};
                    case semantic::SolverLoopMode::kIgnore:
                        break; // тихо пропустить (цикл не участвует)
                    case semantic::SolverLoopMode::kWarning:
                    default:
                        m_ctx.diag().report(Severity::Warning, w.range(),
                                            "loop without an invariant is not verified by the solver; add an invariant or enable unrolling");
                        break;
                    }
                    // цикл не участвует в доказательстве: состояние не меняется.
                }
            }
            break;
        }
        case ParserToken::Kind::DoWhileStmt: {
            // do { body } while(cond) ≡ body; while(cond){ body; } - тело выполняется минимум один
            // раз. Первая итерация - безусловно (assert'ы без охранки). Последующие - разворачиваем,
            // только если явно разрешено (директива z3_unroll(N) в инварианте ИЛИ глобальный флаг
            // `-fsolver-loop-unroll`); иначе - диагностика по `-Wsolver-loop` (default warning).
            const auto& dw = static_cast<const DoWhileStmt&>(*stmt);
            auto cond = toTerm(dw.m_cond.get(), std::nullopt, &res.state);
            if (!cond) {
                return {};
            }
            BlockResult first = encodeBlock(stmtsOf(dw.m_body), res.state, res.ret);
            res.state = std::move(first.state);
            res.ret = std::move(first.ret);
            res.asserts.insert(res.asserts.end(), std::make_move_iterator(first.asserts.begin()), std::make_move_iterator(first.asserts.end()));
            int unrollN = invariantUnroll(loopInvNode);
            if (unrollN <= 0 && semantic::solverLoopUnrollEnabled(m_ctx.opts())) {
                unrollN = m_unroll;
            }
            if (unrollN > 0) {
                doUnroll(unrollN, *cond, &dw, stmtsOf(dw.m_body));
            } else {
                switch (semantic::solverLoopModeFromOptions(m_ctx.opts())) {
                case semantic::SolverLoopMode::kError:
                    report(*stmt, "do-while without an invariant cannot be verified; add an invariant or enable unrolling");
                    return {};
                case semantic::SolverLoopMode::kIgnore:
                    break; // тихо пропустить последующие итерации
                case semantic::SolverLoopMode::kWarning:
                default:
                    m_ctx.diag().report(Severity::Warning, stmt->range(),
                                        "do-while without an invariant is not verified by the solver; add an invariant or enable unrolling");
                    break;
                }
                // состояние остаётся после первой (безусловной) итерации.
            }
            break;
        }
        default:
            report(*stmt, "statement kind is not supported in solver export");
            return {};
        }
    }
    return res;
}

void TrustToSmt::processFuncContract(const FuncDecl& f) {
    m_curFuncName = f.text();
    m_curParams.clear();
    m_paramTypes.clear();
    if (f.m_params) {
        for (const auto& p : *f.m_params) {
            if (p && p->kind() == ParserToken::Kind::ArgNode) {
                const auto& pd = static_cast<const ArgNode&>(*p);
                const std::string pname(pd.text());
                m_curParams.push_back(pname);
                m_paramRanges[pname] = pd.range();
                // Тип параметра - из аннотации AST + реестр (как транспилятор, без скоуп-стека).
                if (pd.m_type) {
                    if (auto pt = m_sorts.resolveTypeByName(pd.m_type->text())) {
                        m_paramTypes[pname] = *pt;
                    }
                }
            }
        }
    }
    // Тип результата - из аннотации возврата (f.m_type) + реестр.
    const auto resSort = f.m_type ? m_sorts.sortOf(m_sorts.resolveTypeByName(f.m_type->text()).value_or(INVALID_TYPE_ID)) : std::nullopt;
    if (!resSort) {
        report(f, "unsupported function result type for solver export");
        return;
    }
    m_curResultSort = resSort;
    // Знак результата функции (для exprSign/пост-условия): по типу возврата.
    m_curResultSigned = true;
    if (f.m_type) {
        if (auto rt = m_sorts.resolveTypeByName(f.m_type->text())) {
            m_curResultSigned = m_sorts.isSignedType(*rt);
        }
    }
    std::vector<SmtSort> paramSorts;
    paramSorts.reserve(m_curParams.size());
    for (const auto& pname : m_curParams) {
        const auto it = m_paramTypes.find(pname);
        const auto ps = m_sorts.sortOf(it != m_paramTypes.end() ? it->second : INVALID_TYPE_ID);
        if (!ps) {
            report(f, "unsupported parameter type for solver export");
            return;
        }
        paramSorts.push_back(*ps);
    }
    // declare-fun параметров как констант с уникальными именами (func_param; без дублей).
    // Уникальность нужна: одно исходное имя (x) в разных функциях - разные константы, иначе
    // VCs функций пересекаются (ложная несовместность в общем solver).
    m_paramSmtNames.clear();
    for (size_t i = 0; i < m_curParams.size(); ++i) {
        const std::string smtName = m_curFuncName + "_" + m_curParams[i];
        m_paramSmtNames[m_curParams[i]] = smtName;
        if (!m_declared.insert(smtName).second) {
            continue;
        }
        // Символьный маппинг: func_param → trust-имя параметра + его диапазон.
        const MapperRange paramRange = m_paramRanges.count(m_curParams[i]) ? m_paramRanges[m_curParams[i]] : f.range();
        m_symbolMap[smtName] = SmtSymbolRef{m_curParams[i], paramRange};
        SmtCommand cmd;
        cmd.kind = SmtCommandKind::kDeclareFun;
        cmd.fun_name = smtName;
        cmd.fun_result_sort = std::make_shared<SmtSort>(paramSorts[i]);
        cmd.srcRange = paramRange;
        m_script.commands.push_back(std::move(cmd));
    }
    // declare-fun функции (uninterpreted) - нужен для аксиомы контракта и вызовов из др. функций.
    // Для функций с контрактом (m_trust непуст) декларируется всегда; вызовы функций без
    // контракта декларируются в CallExpr.
    if (!f.m_trust.empty() && m_declared.insert(m_curFuncName).second) {
        SmtCommand cmd;
        cmd.kind = SmtCommandKind::kDeclareFun;
        cmd.fun_name = m_curFuncName;
        cmd.fun_arg_sorts = paramSorts;
        cmd.fun_result_sort = std::make_shared<SmtSort>(*resSort);
        cmd.srcRange = f.range();
        m_script.commands.push_back(std::move(cmd));
    }
    // Кодирование тела: если есть фактический возврат, функция заменяется термом возврата (SSA)
    // в собственном VC; declare-fun выше используется аксиомой/вызывающими.
    m_curReturn = encodeBody(f);
    // Символьный маппинг функции: имя → {trust-имя, диапазон контракта} (для имени assert в
    // .smt2.map; символьная запись в .smt2 есть только при declare-fun - см. buildSmt2Map).
    m_symbolMap[m_curFuncName] = SmtSymbolRef{m_curFuncName, f.range()};
    // Разбор trust-конструкций функции на pre/post/assert. Собираем ДВАЖДЫ:
    //   - axiom=false: собственный VC (имя функции в пост-условии → инлайн тела);
    //   - axiom=true : аксиома контракта (имя функции → uninterpreted-вызов, params → bound).
    struct TrustTerms {
        std::vector<SmtTerm> pres;
        std::vector<SmtTerm> posts;
        std::vector<SmtTerm> asserts;
        bool hasPre = false;
        bool bad = false;
    };
    const auto buildTerms = [&](bool axiom) -> TrustTerms {
        TrustTerms r;
        m_buildAxiom = axiom;
        for (const auto& t : f.m_trust) {
            if (!t) {
                continue;
            }
            auto* tc = dynamic_cast<TrustContract*>(t.get());
            if (!tc || !tc->m_expr) {
                continue;
            }
            m_inPost = (tc->kind == PropertyKind::Post);
            auto term = toTerm(tc->m_expr.get(), std::nullopt);
            m_inPost = false;
            if (!term) {
                report(*tc, "unsupported trust condition in solver export");
                r.bad = true;
                break;
            }
            switch (tc->kind) {
            case PropertyKind::Pre:
                r.pres.push_back(std::move(*term));
                r.hasPre = true;
                break;
            case PropertyKind::Post:
                r.posts.push_back(std::move(*term));
                break;
            case PropertyKind::Assert:
            case PropertyKind::kUnknown:
                r.asserts.push_back(std::move(*term));
                break;
            default:
                break;
            }
        }
        m_buildAxiom = false;
        return r;
    };

    const MapperRange vcRange = f.range(); // источник VC - контракт функции
    TrustTerms own = buildTerms(false);
    if (own.bad) {
        return;
    }
    TrustTerms ax = buildTerms(true);
    if (ax.bad) {
        return;
    }

    // Собственный VC: при предусловиях должно выполняться постусловие/утверждения.
    SmtTerm ante = mkAnd(std::move(own.pres), vcRange);
    std::vector<SmtTerm> conseqs = std::move(own.posts);
    conseqs.insert(conseqs.end(), std::make_move_iterator(own.asserts.begin()), std::make_move_iterator(own.asserts.end()));
    // Автономные/переменные утверждения, собранные при кодировании тела (2.3).
    conseqs.insert(conseqs.end(), std::make_move_iterator(m_bodyAsserts.begin()), std::make_move_iterator(m_bodyAsserts.end()));
    if (!conseqs.empty()) {
        SmtTerm cons = mkAnd(std::move(conseqs), vcRange);
        SmtTerm vc;
        if (!own.hasPre) {
            vc = mkNot(std::make_shared<SmtTerm>(std::move(cons)), vcRange);
        } else {
            auto notCons = mkNot(std::make_shared<SmtTerm>(std::move(cons)), vcRange);
            vc = makeApp("and", boolSort(), {std::make_shared<SmtTerm>(std::move(ante)), std::make_shared<SmtTerm>(std::move(notCons))}, vcRange);
        }
        addAssert(std::move(vc), vcRange); // isolated=true (по умолчанию)
    }

    // Аксиома контракта для вызывающих: ∀params. pre → (post ∧ asserts). Глобальная (isolated=false).
    if (!ax.pres.empty() || !ax.posts.empty() || !ax.asserts.empty()) {
        const SmtTerm preA = mkAnd(std::move(ax.pres), vcRange);
        std::vector<SmtTerm> aPosts = std::move(ax.posts);
        aPosts.insert(aPosts.end(), std::make_move_iterator(ax.asserts.begin()), std::make_move_iterator(ax.asserts.end()));
        const SmtTerm postA = mkAnd(std::move(aPosts), vcRange);
        SmtTerm impl;
        if (!ax.hasPre) {
            impl = postA;
        } else {
            impl = makeApp("=>", boolSort(), {std::make_shared<SmtTerm>(preA), std::make_shared<SmtTerm>(postA)}, vcRange);
        }
        // bound-переменные = smt-имена параметров (те же, что используются в pre/post).
        std::vector<std::string> qvars;
        qvars.reserve(m_curParams.size());
        for (const auto& p : m_curParams) {
            const auto pit = m_paramSmtNames.find(p);
            qvars.push_back(pit != m_paramSmtNames.end() ? pit->second : p);
        }
        if (qvars.empty()) {
            addAssert(std::move(impl), vcRange, /*isolated=*/false); // без параметров - без квантора
        } else {
            const SmtTerm forall = makeForall(std::move(qvars), paramSorts, std::move(impl), vcRange);
            addAssert(std::move(forall), vcRange, /*isolated=*/false);
        }
    }
}

void TrustToSmt::processTypeAssert(const Binary& typeDecl) {
    // Тип-утверждение: `MyInt ::= Int32 @{ MyInt > 0 @}`. VC: ∀v:sort. A(v), где v - значение
    // типа (плейсхолдер = имя типа), сорт - из базового типа (RHS).
    if (!typeDecl.m_right) {
        return;
    }
    const std::string typeName(typeDecl.m_left ? typeDecl.m_left->text() : "");
    const auto base = m_sorts.resolveTypeByName(typeDecl.m_right->text());
    const auto baseSort = base ? m_sorts.sortOf(*base) : std::nullopt;
    if (!baseSort) {
        report(typeDecl, "cannot determine base sort for type assertion");
        return;
    }
    for (const auto& t : typeDecl.m_trust) {
        const auto* tc = dynamic_cast<const TrustContract*>(t.get());
        if (!tc || (tc->kind != PropertyKind::Type && tc->kind != PropertyKind::kUnknown)) {
            continue;
        }
        if (!tc->m_expr) {
            continue;
        }
        // A(v): кодируем условие с expected = сорт значения типа; имя типа (MyInt) → bound-переменная.
        auto a = toTerm(tc->m_expr.get(), baseSort);
        if (!a) {
            return;
        }
        // VC: ¬(∀v. A(v)) - нарушаемо → SAT (контрпример), выполнимо → UNSAT. Изолированный.
        const SmtTerm forall = makeForall({typeName}, {*baseSort}, std::move(*a), typeDecl.range());
        const SmtTerm vc = mkNot(std::make_shared<SmtTerm>(forall), typeDecl.range());
        addAssert(std::move(vc), typeDecl.range()); // isolated=true
    }
}

void TrustToSmt::walk(const AstNodeBase* node) {
    if (!node) {
        return;
    }
    if (node->kind() == ParserToken::Kind::FuncDecl) {
        const auto& f = static_cast<const FuncDecl&>(*node);
        if (!f.m_trust.empty() || bodyHasTrustContract(&f)) {
            processFuncContract(f);
        }
    } else if (node->kind() == ParserToken::Kind::TypeDecl) {
        const auto& b = static_cast<const Binary&>(*node);
        if (!b.m_trust.empty()) {
            processTypeAssert(b);
        }
    }
    for (const auto& child : node->children()) {
        walk(child.get());
    }
}

std::optional<SmtScript> TrustToSmt::generate(const std::vector<AstNodePtr>& astNodes) {
    m_script = SmtScript{};
    m_symbolMap.clear();
    m_declared.clear();
    // Пре-скан: карта имя функции → FuncDecl модуля (для интерпроцедурных вызовов и аксиом 2.4).
    m_funcDecls.clear();
    const auto collectFuncs = [&](const AstNodeBase* node, const auto& self) -> void {
        if (!node) {
            return;
        }
        if (node->kind() == ParserToken::Kind::FuncDecl) {
            const auto& f = static_cast<const FuncDecl&>(*node);
            m_funcDecls[std::string(f.text())] = &f;
        }
        for (const auto& child : node->children()) {
            self(child.get(), self);
        }
    };
    for (const auto& n : astNodes) {
        collectFuncs(n.get(), collectFuncs);
    }
    for (const auto& n : astNodes) {
        walk(n.get());
    }
    if (m_ctx.diag().errorCount() > 0) {
        return std::nullopt; // диагностика уже выдана
    }
    if (m_script.commands.empty()) {
        return m_script; // контрактов нет - пустой скрипт (без логики/check-sat)
    }
    // Логика по фичам: массивы → AUFBV (Array+BitVec+кванторы); кванторы → UFBV;
    // иначе → QF_UFBV (фаза 1). Без кванторов в QF_* — иначе решатель отклонит скрипт.
    bool usesQuant = false;
    bool usesArray = false;
    for (const auto& cmd : m_script.commands) {
        for (const auto& s : cmd.fun_arg_sorts) {
            usesArray = usesArray || sortUsesArray(s);
        }
        if (cmd.fun_result_sort && sortUsesArray(*cmd.fun_result_sort)) {
            usesArray = true;
        }
        if (cmd.assert_term) {
            usesQuant = usesQuant || termUsesQuantifier(*cmd.assert_term);
            usesArray = usesArray || termUsesArray(*cmd.assert_term);
        }
        if (cmd.fun_body) {
            usesQuant = usesQuant || termUsesQuantifier(*cmd.fun_body);
            usesArray = usesArray || termUsesArray(*cmd.fun_body);
        }
    }
    m_script.logic = usesArray ? "AUFBV" : (usesQuant ? "UFBV" : "QF_UFBV");
    // Изоляция VC: каждый assert (VC функции) оборачивается push/check-sat/pop, чтобы
    // проверялся отдельно от других VCs (иначе unsat одной функции маскирует sat другой).
    // get-model опущен: не используется кодом, а при unsat даёт z3-ошибку модели.
    std::vector<SmtCommand> out;
    out.reserve(m_script.commands.size() * 2);
    for (auto& cmd : m_script.commands) {
        if (cmd.kind == SmtCommandKind::kAssert && cmd.isolated) {
            SmtCommand p;
            p.kind = SmtCommandKind::kPush;
            p.stack_depth = 1;
            SmtCommand cs;
            cs.kind = SmtCommandKind::kCheckSat;
            SmtCommand pp;
            pp.kind = SmtCommandKind::kPop;
            pp.stack_depth = 1;
            out.push_back(std::move(p));
            out.push_back(std::move(cmd));
            out.push_back(std::move(cs));
            out.push_back(std::move(pp));
        } else {
            out.push_back(std::move(cmd));
        }
    }
    m_script.commands = std::move(out);
    // Символьный маппинг SMT-имя → {trust-имя, диапазон} (для .smt2.map/LSP).
    m_script.symbolMap.assign(m_symbolMap.begin(), m_symbolMap.end());
    return std::move(m_script);
}

} // namespace solver
} // namespace trust
