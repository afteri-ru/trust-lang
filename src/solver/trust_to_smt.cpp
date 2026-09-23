#include "solver/trust_to_smt.hpp"
#include "solver/smt_term_builder.hpp"

#include "session/context.hpp"
#include "ast/ast_nodes.hpp"
#include "types/registry.hpp"
#include "types/typekind.hpp"
#include "types/group.hpp"
#include "utils/error.hpp"
#include "semantic/solver.hpp"

#include <memory>

namespace trust {
namespace solver {

using namespace SmtTermBuilder;

TrustToSmt::TrustToSmt(Context& ctx)
: m_ctx(ctx)
, m_sorts(ctx) {
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
