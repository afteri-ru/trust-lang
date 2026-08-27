#pragma once
// include/solver/smt_term_builder.hpp
// Компонент построения SmtTerm: чистые (без состояния TrustToSmt) хелперы конструирования
// термов/сортов SMT-LIB из примитивов. Вынесены из монолитного trust_to_smt.cpp, чтобы
// зона «построение термов» была изолирована и независимо тестируема.

#include "solver/smt_ast.hpp"
#include "ast/token_base.hpp"
#include "ast/ast_nodes.hpp"
#include "location/location.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace trust {
namespace solver {
namespace SmtTermBuilder {

/// Литерал (константа) заданного сорта.
inline SmtTerm makeConst(std::string value, SmtSort sort, MapperRange srcRange = {}) {
    SmtTerm t;
    t.kind = SmtTermKind::kConst;
    t.sort = std::move(sort);
    t.const_value = std::move(value);
    t.srcRange = srcRange;
    return t;
}

/// Именованная переменная заданного сорта.
inline SmtTerm makeNamedVar(std::string name, SmtSort sort, MapperRange srcRange = {}) {
    SmtTerm t;
    t.kind = SmtTermKind::kNamedVar;
    t.sort = std::move(sort);
    t.var_name = std::move(name);
    t.srcRange = srcRange;
    return t;
}

/// Применение функции/оператора. Встроенные операторы (SOLVER_OPERATOR_LIST) → enum SmtOp.
inline SmtTerm makeApp(std::string fun, SmtSort sort, std::vector<std::shared_ptr<SmtTerm>> args, MapperRange srcRange = {}) {
    SmtTerm t;
    t.kind = SmtTermKind::kApp;
    t.sort = std::move(sort);
    t.fun_name = std::move(fun);
    t.args = std::move(args);
    t.srcRange = srcRange;
    t.op = parseSmtOp(t.fun_name);
    return t;
}

/// Сорт Bool (результат логических операторов/сравнений).
inline SmtSort boolSort() {
    SmtSort s;
    s.kind = SmtSortKind::kBool;
    return s;
}

/// Конъюнкция 0+ термов: пусто → true, один → как есть, иначе (and ...).
inline SmtTerm mkAnd(std::vector<SmtTerm> terms, MapperRange srcRange = {}) {
    if (terms.empty()) {
        return makeConst("true", boolSort(), srcRange);
    }
    if (terms.size() == 1) {
        return std::move(terms[0]);
    }
    std::vector<std::shared_ptr<SmtTerm>> args;
    args.reserve(terms.size());
    for (auto& t : terms) {
        args.push_back(std::make_shared<SmtTerm>(std::move(t)));
    }
    return makeApp("and", boolSort(), std::move(args), srcRange);
}

/// Отрицание (not ...).
inline SmtTerm mkNot(std::shared_ptr<SmtTerm> t, MapperRange srcRange = {}) {
    return makeApp("not", boolSort(), {std::move(t)}, srcRange);
}

/// Арифметический/битовый оператор → SMT-имя; "" - неподдерживаемый.
inline std::string_view mapArithOp(std::string_view op, bool bv, bool sign) {
    if (op == "+") {
        return bv ? "bvadd" : "+";
    }
    if (op == "-") {
        return bv ? "bvsub" : "-";
    }
    if (op == "*") {
        return bv ? "bvmul" : "*";
    }
    if (op == "//" || op == "/") {
        return bv ? (sign ? "bvsdiv" : "bvudiv") : "/";
    }
    if (op == "%") {
        return bv ? (sign ? "bvsrem" : "bvurem") : "%";
    }
    if (bv) {
        if (op == ".&.") {
            return "bvand";
        }
        if (op == ".|.") {
            return "bvor";
        }
        if (op == ".^.") {
            return "bvxor";
        }
        if (op == ".~.") {
            return "bvnot";
        }
        if (op == ".>>.") {
            return sign ? "bvashr" : "bvlshr";
        }
        if (op == ".<.") {
            return "bvshl";
        }
    }
    return {};
}

/// Сравнение → SMT-имя; "" - не поддерживается.
inline std::string_view mapCmpOp(std::string_view op, bool bv, bool sign) {
    if (op == ">") {
        return bv ? (sign ? "bvsgt" : "bvugt") : ">";
    }
    if (op == ">=") {
        return bv ? (sign ? "bvsge" : "bvuge") : ">=";
    }
    if (op == "<") {
        return bv ? (sign ? "bvslt" : "bvult") : "<";
    }
    if (op == "<=") {
        return bv ? (sign ? "bvsle" : "bvule") : "<=";
    }
    return {};
}

/// Логический оператор → SMT-имя; "" - неподдерживаемый.
inline std::string_view mapLogicalOp(std::string_view op) {
    if (op == "&&") {
        return "and";
    }
    if (op == "||") {
        return "or";
    }
    if (op == "^^") {
        return "xor";
    }
    return {};
}

/// Сравнение «не равно».
inline bool isNeqOp(std::string_view op) {
    return op == "!=" || op == "~=" || op == "=/=" || op == "!~" || op == "!~~" || op == "!~~~" || op == "!==";
}
/// Сравнение «равно».
inline bool isEqOp(std::string_view op) {
    return op == "==" || op == "===";
}

/// Использует ли сорт массив (в т.ч. рекурсивно в domain/range).
inline bool sortUsesArray(const SmtSort& s) {
    if (s.kind == SmtSortKind::kArray) {
        return true;
    }
    if (s.domain && sortUsesArray(*s.domain)) {
        return true;
    }
    return s.range && sortUsesArray(*s.range);
}

/// Встречается ли квантор/let (kForall/kExists/kLet) в терме (рекурсивно).
inline bool termUsesQuantifier(const SmtTerm& t) {
    if (t.kind == SmtTermKind::kForall || t.kind == SmtTermKind::kExists || t.kind == SmtTermKind::kLet) {
        return true;
    }
    for (const auto& a : t.args) {
        if (a && termUsesQuantifier(*a)) {
            return true;
        }
    }
    if (t.quant_body && termUsesQuantifier(*t.quant_body)) {
        return true;
    }
    if (t.let_body && termUsesQuantifier(*t.let_body)) {
        return true;
    }
    for (const auto& [n, v] : t.let_bindings) {
        (void)n;
        if (v && termUsesQuantifier(*v)) {
            return true;
        }
    }
    return false;
}

/// Встречается ли массивный сорт в терме (рекурсивно по термам/сортам).
inline bool termUsesArray(const SmtTerm& t) {
    if (sortUsesArray(t.sort)) {
        return true;
    }
    for (const auto& a : t.args) {
        if (a && termUsesArray(*a)) {
            return true;
        }
    }
    if (t.quant_body && termUsesArray(*t.quant_body)) {
        return true;
    }
    if (t.let_body && termUsesArray(*t.let_body)) {
        return true;
    }
    for (const auto& [n, v] : t.let_bindings) {
        (void)n;
        if (v && termUsesArray(*v)) {
            return true;
        }
    }
    return false;
}

/// Есть ли у узла trust-контракт в m_trust (например, переменное `y @{ assert: A @} := ...`).
inline bool hasTrustContractOnNode(const AstNodeBase* node) {
    for (const auto& t : node->m_trust) {
        if (t && t->kind() == ParserToken::Kind::TrustContract) {
            return true;
        }
    }
    return false;
}

/// Есть ли в теле (рекурсивно) trust-контракты.
inline bool bodyHasTrustContract(const AstNodeBase* node) {
    if (!node) {
        return false;
    }
    if (node->kind() == ParserToken::Kind::TrustContract) {
        return true;
    }
    if (hasTrustContractOnNode(node)) {
        return true;
    }
    for (const auto& child : node->children()) {
        if (child && bodyHasTrustContract(child.get())) {
            return true;
        }
    }
    return false;
}

/// Разворачивает тело ветки/блока в список statements для кодирования.
inline std::vector<AstNodePtr> stmtsOf(const AstNodePtr& body) {
    if (!body) {
        return {};
    }
    if (body->kind() == ParserToken::Kind::ScopeBlock) {
        const auto& sc = static_cast<const ScopeBlock&>(*body);
        return sc.m_body;
    }
    if (body->kind() == ParserToken::Kind::SemicolonStmt) {
        const auto& ss = static_cast<const SemicolonStmt&>(*body);
        return ss.m_expr ? std::vector<AstNodePtr>{ss.m_expr} : std::vector<AstNodePtr>{};
    }
    return {body};
}

/// Квантор ∀vars: sorts. body.
inline SmtTerm makeForall(std::vector<std::string> vars, std::vector<SmtSort> sorts, SmtTerm body, MapperRange srcRange = {}) {
    SmtTerm t;
    t.kind = SmtTermKind::kForall;
    t.sort = boolSort();
    t.quant_vars = std::move(vars);
    t.quant_var_sorts = std::move(sorts);
    t.quant_body = std::make_shared<SmtTerm>(std::move(body));
    t.srcRange = srcRange;
    return t;
}

/// Квантор ∃vars: sorts. body.
inline SmtTerm makeExists(std::vector<std::string> vars, std::vector<SmtSort> sorts, SmtTerm body, MapperRange srcRange = {}) {
    SmtTerm t;
    t.kind = SmtTermKind::kExists;
    t.sort = boolSort();
    t.quant_vars = std::move(vars);
    t.quant_var_sorts = std::move(sorts);
    t.quant_body = std::make_shared<SmtTerm>(std::move(body));
    t.srcRange = srcRange;
    return t;
}

/// Расширение операнда до целевой ширины (mixed-width BV, 2.5).
inline SmtTerm coerceToWidth(SmtTerm t, const SmtSort& target, bool signedOp) {
    if (t.sort.kind == SmtSortKind::kBitVec && target.kind == SmtSortKind::kBitVec && t.sort.bv_width < target.bv_width) {
        SmtTerm ext;
        ext.kind = SmtTermKind::kApp;
        ext.op = signedOp ? SmtOp::SignExt : SmtOp::ZeroExt;
        ext.fun_name = signedOp ? "sign_extend" : "zero_extend";
        ext.ext_amount = target.bv_width - t.sort.bv_width;
        ext.sort = target;
        ext.srcRange = t.srcRange;
        ext.args = {std::make_shared<SmtTerm>(std::move(t))};
        return ext;
    }
    return t;
}

} // namespace SmtTermBuilder
} // namespace solver
} // namespace trust
