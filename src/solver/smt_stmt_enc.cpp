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

/// Директива решателя `z3_unroll(N)` в инварианте цикла: N итераций разворачивания, 0 - не задана.
int invariantUnrollCount(const AstNodeBase* inv) {
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
}

} // namespace

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
    for (const auto& stmt : nodes) {
        if (!stmt) {
            continue;
        }
        switch (stmt->kind()) {
        case ParserToken::Kind::VarDecl:
            if (!encodeVarDeclStmt(stmt.get(), res)) {
                return {};
            }
            break;
        case ParserToken::Kind::AssignOp:
            if (!encodeAssignStmt(stmt.get(), res)) {
                return {};
            }
            break;
        case ParserToken::Kind::ReturnStmt:
            if (!encodeReturnStmt(stmt.get(), res)) {
                return {};
            }
            break;
        case ParserToken::Kind::TrustContract:
            if (!encodeTrustContractStmt(stmt.get(), res, loopInvNode)) {
                return {};
            }
            break;
        case ParserToken::Kind::SemicolonStmt:
            if (!encodeSemicolonStmt(stmt.get(), res)) {
                return {};
            }
            break;
        case ParserToken::Kind::ScopeBlock:
            if (!encodeScopeBlockStmt(stmt.get(), res)) {
                return {};
            }
            break;
        case ParserToken::Kind::IfStmt:
            if (!encodeIfStmt(stmt.get(), res)) {
                return {};
            }
            break;
        case ParserToken::Kind::WhileStmt:
            if (!encodeWhileStmt(stmt.get(), res, loopInvNode)) {
                return {};
            }
            break;
        case ParserToken::Kind::DoWhileStmt:
            if (!encodeDoWhileStmt(stmt.get(), res, loopInvNode)) {
                return {};
            }
            break;
        default:
            report(*stmt, "statement kind is not supported in solver export");
            return {};
        }
    }
    return res;
}

/// Bounded unrolling тела на n итераций со слиянием состояния через ite по cond.
void TrustToSmt::unrollBlock(int n, const SmtTerm& cond, const AstNodeBase* node, const std::vector<AstNodePtr>& body, BlockResult& res) {
    for (int it = 0; it < n; ++it) {
        BlockResult bodyRes = encodeBlock(body, res.state, res.ret);
        for (auto& [v, vterm] : res.state) {
            (void)vterm;
            if (auto fit = bodyRes.state.find(v); fit != bodyRes.state.end()) {
                res.state[v] =
                    makeApp("ite", fit->second.sort,
                            {std::make_shared<SmtTerm>(cond), std::make_shared<SmtTerm>(fit->second), std::make_shared<SmtTerm>(res.state[v])}, node->range());
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
}

bool TrustToSmt::encodeVarDeclStmt(const AstNodeBase* stmt, BlockResult& res) {
    do {
        const auto& v = static_cast<const VarDecl&>(*stmt);
        if (!v.m_initializer) {
            break;
        }
        auto val = toTerm(v.m_initializer.get(), std::nullopt, &res.state);
        if (!val) {
            return false;
        }
        // Инициализатор приводится к типу переменной (объявленному или выведенному): только
        // так SSA-значения сохраняют ширину переменной при merge в циклах/if (иначе
        // литерал Int8 в SSA-merge с Int32-переменной дал бы рассогласование разрядности).
        TypeId varTy = INVALID_TYPE_ID;
        if (v.m_type) {
            if (auto pt = m_sorts.resolveTypeByName(v.m_type->text())) {
                varTy = *pt;
            }
        } else if (v.inferredType != INVALID_TYPE_ID) {
            varTy = v.inferredType;
        }
        if (varTy != INVALID_TYPE_ID) {
            if (auto vs = m_sorts.sortOf(varTy); vs && vs->kind == SmtSortKind::kBitVec && val->sort.kind == SmtSortKind::kBitVec) {
                *val = coerceToWidth(std::move(*val), *vs, m_sorts.isSignedType(varTy));
            }
        }
        res.state[std::string(v.text())] = std::move(*val);
        // Переменный trust-контракт (напр. `y @{ assert --> A @} := expr`, 2.3): после
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
                return false;
            }
            res.asserts.push_back(std::move(*a));
        }
        break;
    } while (false);
    return true;
}

bool TrustToSmt::encodeAssignStmt(const AstNodeBase* stmt, BlockResult& res) {
    do {
        const auto& b = static_cast<const Binary&>(*stmt);
        if (!b.m_left || !b.m_right) {
            break;
        }
        auto val = toTerm(b.m_right.get(), std::nullopt, &res.state);
        if (!val) {
            return false;
        }
        // Присваивание не сужает переменную: RHS приводится к текущей ширине цели (состояние
        // несёт ширину из инициализатора), чтобы обратная запись литерала Int8 не сузила
        // SSA-значение и не рассогласовала ite-merge.
        if (val->sort.kind == SmtSortKind::kBitVec) {
            if (auto it = res.state.find(std::string(b.m_left->text()));
                it != res.state.end() && it->second.sort.kind == SmtSortKind::kBitVec && it->second.sort.bv_width > val->sort.bv_width) {
                *val = coerceToWidth(std::move(*val), it->second.sort, true);
            }
        }
        res.state[std::string(b.m_left->text())] = std::move(*val);
        break;
    } while (false);
    return true;
}

bool TrustToSmt::encodeReturnStmt(const AstNodeBase* stmt, BlockResult& res) {
    do {
        const auto& j = static_cast<const JumpStmt&>(*stmt);
        // Hoist-временная возврата создана СЕМАНТИКОЙ (m_tempDecl): исходное выражение значения
        // переехало в её инициализатор, а m_value — ссылка на временную (__trust_res_N). Для
        // пост-условий VC нужно ИСХОДНОЕ выражение, а не имя temp (temp не существует в SMT-модели).
        const AstNodeBase* valueNode = j.m_value.get();
        if (j.m_tempDecl && j.m_tempDecl->kind() == ParserToken::Kind::VarDecl) {
            const auto* vd = static_cast<const VarDecl*>(j.m_tempDecl.get());
            if (vd->m_initializer) {
                valueNode = vd->m_initializer.get();
            }
        }
        if (valueNode) {
            // Терм возврата приводится к сорту результата функции: `@return 0` в функции
            // :Int32 расширяет литерал Int8 до Int32 (в C++ int8→int32 — неявно), иначе
            // return узкой разрядности ломает ite-merge в if/else.
            std::optional<SmtSort> expectSort;
            if (m_curResultSort && (m_curResultSort->kind == SmtSortKind::kBitVec || m_curResultSort->kind == SmtSortKind::kReal)) {
                expectSort = m_curResultSort;
            }
            auto val = toTerm(valueNode, expectSort, &res.state);
            if (!val) {
                return false;
            }
            if (m_curResultSort && m_curResultSort->kind == SmtSortKind::kBitVec && val->sort.kind == SmtSortKind::kBitVec) {
                *val = coerceToWidth(std::move(*val), *m_curResultSort, m_curResultSigned);
            }
            res.ret = std::move(*val);
        } else {
            res.ret = std::nullopt; // void return - значения нет
        }
        break;
    } while (false);
    return true;
}

bool TrustToSmt::encodeTrustContractStmt(const AstNodeBase* stmt, BlockResult& res, const AstNodeBase*& loopInvNode) {
    do {
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
            return false;
        }
        res.asserts.push_back(std::move(*a));
        break;
    } while (false);
    return true;
}

bool TrustToSmt::encodeSemicolonStmt(const AstNodeBase* stmt, BlockResult& res) {
    do {
        const auto& ss = static_cast<const SemicolonStmt&>(*stmt);
        if (ss.m_expr) {
            BlockResult sub = encodeBlock(stmtsOf(ss.m_expr), res.state, res.ret);
            res.state = std::move(sub.state);
            res.ret = std::move(sub.ret);
            res.asserts.insert(res.asserts.end(), std::make_move_iterator(sub.asserts.begin()), std::make_move_iterator(sub.asserts.end()));
        }
        break;
    } while (false);
    return true;
}

bool TrustToSmt::encodeScopeBlockStmt(const AstNodeBase* stmt, BlockResult& res) {
    do {
        const auto& sc = static_cast<const ScopeBlock&>(*stmt);
        BlockResult sub = encodeBlock(sc.m_body, res.state, res.ret);
        res.state = std::move(sub.state);
        res.ret = std::move(sub.ret);
        res.asserts.insert(res.asserts.end(), std::make_move_iterator(sub.asserts.begin()), std::make_move_iterator(sub.asserts.end()));
        break;
    } while (false);
    return true;
}

bool TrustToSmt::encodeIfStmt(const AstNodeBase* stmt, BlockResult& res) {
    do {
        const auto& is = static_cast<const IfStmt&>(*stmt);
        auto cond = toTerm(is.m_cond.get(), std::nullopt, &res.state);
        if (!cond) {
            return false;
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
                return false;
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
            return false;
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
                acc = makeApp(
                    "ite", acc.sort,
                    {std::make_shared<SmtTerm>(branches[i].cond), std::make_shared<SmtTerm>(std::move(branchVal)), std::make_shared<SmtTerm>(std::move(acc))},
                    is.range());
            }
            res.state[v] = std::move(acc);
        }
        // Слияние возврата: ite(cond1, ret1, ite(cond2, ret2, ..., elseRet)).
        {
            SmtTerm acc = *elseRes.ret;
            for (int i = static_cast<int>(brs.size()) - 1; i >= 0; --i) {
                acc = makeApp("ite", acc.sort,
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
    } while (false);
    return true;
}

bool TrustToSmt::encodeWhileStmt(const AstNodeBase* stmt, BlockResult& res, const AstNodeBase* loopInvNode) {
    do {
        const auto& w = static_cast<const WhileStmt&>(*stmt);
        auto cond = toTerm(w.m_cond.get(), std::nullopt, &res.state);
        if (!cond) {
            return false;
        }
        if (loopInvNode) {
            const int invUnroll = invariantUnrollCount(loopInvNode);
            if (invUnroll > 0) {
                // Директива решателя z3_unroll(N): разворачиваем на N (приоритетнее глобального флага).
                unrollBlock(invUnroll, *cond, &w, stmtsOf(w.m_body), res);
            } else {
                // Инвариант (2.6): формализуем индукцию, НЕ разворачивая.
                //   assert I(начало);   assert (I ∧ cond) → I[тело].
                // Постусловие (в processFuncContract) доказывается через инвариант.
                auto I0 = toTerm(loopInvNode, std::nullopt, &res.state);
                if (!I0) {
                    return false;
                }
                res.asserts.push_back(*I0); // инвариант выполняется до цикла
                BlockResult bodyRes = encodeBlock(stmtsOf(w.m_body), res.state, res.ret);
                auto Ibody = toTerm(loopInvNode, std::nullopt, &bodyRes.state);
                if (!Ibody) {
                    return false;
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
                unrollBlock(m_unroll, *cond, &w, stmtsOf(w.m_body), res);
            } else {
                switch (semantic::solverLoopModeFromOptions(m_ctx.opts())) {
                case semantic::SolverLoopMode::kError:
                    report(w, "loop without an invariant cannot be verified; add an invariant or enable unrolling");
                    return false;
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
    } while (false);
    return true;
}

bool TrustToSmt::encodeDoWhileStmt(const AstNodeBase* stmt, BlockResult& res, const AstNodeBase* loopInvNode) {
    do {
        // do { body } while(cond) ≡ body; while(cond){ body; } - тело выполняется минимум один
        // раз. Первая итерация - безусловно (assert'ы без охранки). Последующие - разворачиваем,
        // только если явно разрешено (директива z3_unroll(N) в инварианте ИЛИ глобальный флаг
        // `-fsolver-loop-unroll`); иначе - диагностика по `-Wsolver-loop` (default warning).
        const auto& dw = static_cast<const DoWhileStmt&>(*stmt);
        auto cond = toTerm(dw.m_cond.get(), std::nullopt, &res.state);
        if (!cond) {
            return false;
        }
        BlockResult first = encodeBlock(stmtsOf(dw.m_body), res.state, res.ret);
        res.state = std::move(first.state);
        res.ret = std::move(first.ret);
        res.asserts.insert(res.asserts.end(), std::make_move_iterator(first.asserts.begin()), std::make_move_iterator(first.asserts.end()));
        int unrollN = invariantUnrollCount(loopInvNode);
        if (unrollN <= 0 && semantic::solverLoopUnrollEnabled(m_ctx.opts())) {
            unrollN = m_unroll;
        }
        if (unrollN > 0) {
            unrollBlock(unrollN, *cond, &dw, stmtsOf(dw.m_body), res);
        } else {
            switch (semantic::solverLoopModeFromOptions(m_ctx.opts())) {
            case semantic::SolverLoopMode::kError:
                report(*stmt, "do-while without an invariant cannot be verified; add an invariant or enable unrolling");
                return false;
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
    } while (false);
    return true;
}

} // namespace solver
} // namespace trust
