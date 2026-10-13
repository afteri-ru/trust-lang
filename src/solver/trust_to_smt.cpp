#include "solver/trust_to_smt.hpp"
#include "solver/impl/smt_assembler.hpp"
#include "solver/vc/smt_term_builder.hpp"

#include "diag/registry.hpp"
#include "session/context.hpp"
#include "ast/ast_nodes.hpp"
#include "types/type_system.hpp"
#include "types/typekind.hpp"
#include "types/group.hpp"
#include "utils/error.hpp"

#include <memory>

namespace trust {
namespace solver {

namespace sb = SmtTermBuilder;

namespace {
// Пер-компонентная регистрация диагностик solver (см. diag/registry.hpp): severity-опция
// `-Wverify-envelope` (код вне области проверки не проверялся).
struct SolverDiagnosticsRegistrar {
    SolverDiagnosticsRegistrar() {
        registerDiagnostics([](Options& opts) { SOLVER_DIAG_LIST(SOLVER_DG_ADD_OPT) });
    }
};
const SolverDiagnosticsRegistrar kSolverDiagnosticsRegistrar;
} // namespace

TrustToSmt::TrustToSmt(Context& ctx, analysis::BehavioralModes modes)
: m_ctx(ctx)
, m_modes(modes)
, m_sorts(ctx) {
}

std::optional<flow::PlaceId> TrustToSmt::placeIdOf(const AstNodeBase* node) const {
    if (node == nullptr || m_curPlaces == nullptr) {
        return std::nullopt;
    }
    return m_curPlaces->find(node);
}

flow::PlaceId TrustToSmt::slotOf(const AstNodeBase* node) const {
    const auto id = placeIdOf(node);
    EXPECT(id.has_value() && "slotOf: node is not registered as a place of the current function");
    return *id;
}

const AstNodeBase* TrustToSmt::slotNode(flow::PlaceId id) const {
    EXPECT(m_curSlotNodes != nullptr && id.value < m_curSlotNodes->size() && "slotNode: place id out of range");
    return (*m_curSlotNodes)[id.value];
}

void TrustToSmt::addGoal(std::optional<SmtTerm>&& vc, MapperRange srcRange, std::string owner, VcObligationKind kind) {
    if (!vc) {
        return;
    }
    m_plan.goals.push_back(VcGoal{std::move(*vc), srcRange, std::move(owner), kind});
}

void TrustToSmt::addAxiom(std::optional<SmtTerm>&& vc, MapperRange srcRange, std::string owner) {
    if (!vc) {
        return;
    }
    m_plan.axioms.push_back(VcAxiom{std::move(*vc), srcRange, std::move(owner)});
}

void TrustToSmt::processFuncContract(const FuncDecl& f, const flow::MirFunction* body) {
    // Таблица мест принадлежит исполнителю тела: между функциями указатели недействительны.
    m_curPlaces = nullptr;
    m_curSlotNodes = nullptr;
    m_entryState.clear();
    m_curFuncName = f.text();
    m_curParams.clear();
    m_paramTypes.clear();
    m_paramRanges.clear();
    if (f.m_params) {
        for (const auto& p : *f.m_params) {
            if (p && p->kind() == ParserToken::Kind::ArgNode) {
                const auto& pd = static_cast<const ArgNode&>(*p);
                const std::string pname(pd.text());
                m_curParams.push_back(pname);
                m_paramRanges[pname] = pd.range();
                const TypeId paramType = resolvedTypeOf(pd.m_type.get(), pd.resultType.getTypeId());
                if (paramType != INVALID_TYPE_ID) {
                    m_paramTypes[pname] = paramType;
                }
            }
        }
    }
    const TypeId resultType = resolvedTypeOf(f.m_type.get());
    const auto resSort = resultType != INVALID_TYPE_ID ? m_sorts.sortOf(resultType) : std::nullopt;
    if (!resSort) {
        reportOutsideEnvelope(f, "function result type is not modelable");
        return;
    }
    m_curResultSort = resSort;
    m_curResultSigned = m_sorts.isSignedType(resultType);
    std::vector<SmtSort> paramSorts;
    paramSorts.reserve(m_curParams.size());
    for (const auto& pname : m_curParams) {
        const auto it = m_paramTypes.find(pname);
        const auto ps = m_sorts.sortOf(it != m_paramTypes.end() ? it->second : INVALID_TYPE_ID);
        if (!ps) {
            reportOutsideEnvelope(f, "parameter type is not modelable");
            return;
        }
        paramSorts.push_back(*ps);
    }
    m_paramSmtNames.clear();
    for (size_t i = 0; i < m_curParams.size(); ++i) {
        const std::string smtName = m_curFuncName + "_" + m_curParams[i];
        m_paramSmtNames[m_curParams[i]] = smtName;
        if (!m_declared.insert(smtName).second) {
            continue;
        }
        const MapperRange paramRange = m_paramRanges.count(m_curParams[i]) ? m_paramRanges[m_curParams[i]] : f.range();
        m_symbolMap[smtName] = SmtSymbolRef{m_curParams[i], paramRange};
        SmtCommand cmd;
        cmd.kind = SmtCommandKind::kDeclareFun;
        cmd.fun_name = smtName;
        cmd.fun_result_sort = std::make_shared<SmtSort>(paramSorts[i]);
        cmd.srcRange = paramRange;
        m_plan.declarations.push_back(std::move(cmd));
    }
    if (!f.m_trust.empty() && m_declared.insert(m_curFuncName).second) {
        SmtCommand cmd;
        cmd.kind = SmtCommandKind::kDeclareFun;
        cmd.fun_name = m_curFuncName;
        cmd.fun_arg_sorts = paramSorts;
        cmd.fun_result_sort = std::make_shared<SmtSort>(*resSort);
        cmd.srcRange = f.range();
        m_plan.declarations.push_back(std::move(cmd));
    }
    m_symbolMap[m_curFuncName] = SmtSymbolRef{m_curFuncName, f.range()};
    std::vector<const AstNodeBase*> preExprs;
    std::vector<const AstNodeBase*> postExprs;
    std::vector<const AstNodeBase*> assertExprs;
    for (const auto& trust : f.m_trust) {
        auto* contract = dynamic_cast<TrustContract*>(trust.get());
        if (!contract || !contract->m_expr) {
            continue;
        }
        switch (contract->kind) {
        case PropertyKind::Pre:
            preExprs.push_back(contract->m_expr.get());
            break;
        case PropertyKind::Post:
            postExprs.push_back(contract->m_expr.get());
            break;
        case PropertyKind::Assert:
        case PropertyKind::kUnknown:
            assertExprs.push_back(contract->m_expr.get());
            break;
        default:
            break;
        }
    }
    // Аксиома контракта НЕЗАВИСИМА от тела: `∀params. pre → (post ∧ asserts)`.
    {
        m_buildAxiom = true;
        std::vector<SmtTerm> axiomPres;
        std::vector<SmtTerm> axiomPosts;
        bool axiomOk = true;
        const auto encodeAxiom = [&](const AstNodeBase* expr, bool post, std::vector<SmtTerm>& out) {
            if (!axiomOk) {
                return;
            }
            m_inPost = post;
            auto term = toTerm(expr, std::nullopt, nullptr);
            m_inPost = false;
            if (!term) {
                axiomOk = false;
                return;
            }
            out.push_back(std::move(*term));
        };
        for (const AstNodeBase* expr : preExprs) {
            encodeAxiom(expr, false, axiomPres);
        }
        for (const AstNodeBase* expr : postExprs) {
            encodeAxiom(expr, true, axiomPosts);
        }
        for (const AstNodeBase* expr : assertExprs) {
            encodeAxiom(expr, false, axiomPosts);
        }
        m_buildAxiom = false;
        if (!axiomOk) {
            ++m_outsideEnvelopeVcs;
            return;
        }
        const bool hasPre = !axiomPres.empty();
        if (hasPre || !axiomPosts.empty()) {
            const SmtTerm preA = sb::mkAnd(std::move(axiomPres), f.range());
            const SmtTerm postA = sb::mkAnd(std::move(axiomPosts), f.range());
            SmtTerm impl = postA;
            if (hasPre) {
                impl = sb::makeApp("=>", sb::boolSort(), {std::make_shared<SmtTerm>(preA), std::make_shared<SmtTerm>(postA)}, f.range());
            }
            std::vector<std::string> qvars;
            qvars.reserve(m_curParams.size());
            for (const auto& pname : m_curParams) {
                const auto it = m_paramSmtNames.find(pname);
                qvars.push_back(it != m_paramSmtNames.end() ? it->second : pname);
            }
            if (qvars.empty()) {
                addAxiom(std::move(impl), f.range(), m_curFuncName);
            } else {
                addAxiom(sb::makeForall(std::move(qvars), paramSorts, std::move(impl), f.range()), f.range(), m_curFuncName);
            }
        }
    }
    // Собственная VC: только при наличии тела (объявление проверять нечего) и только если
    // кодирование не вышло за область проверки.
    if (body == nullptr || !f.m_body) {
        return;
    }
    std::vector<SmtTerm> preTerms;
    for (const AstNodeBase* expr : preExprs) {
        auto term = toTerm(expr, std::nullopt, nullptr);
        if (!term) {
            ++m_outsideEnvelopeVcs;
            return;
        }
        preTerms.push_back(std::move(*term));
    }
    if (!encodeBody(f, *body, std::move(preTerms), postExprs)) {
        ++m_outsideEnvelopeVcs;
    }
}

void TrustToSmt::processTypeAssert(const Binary& typeDecl) {
    // Тип-утверждение: `MyInt ::= Int32 @{ MyInt > 0 @}`. VC: ∀v:sort. A(v), где v - значение
    // типа (плейсхолдер = имя типа), сорт - из базового типа (RHS).
    EXPECT(typeDecl.m_right && "type assertion must have a base type");
    const std::string typeName(typeDecl.m_left ? requiredText(*typeDecl.m_left) : std::string_view{});
    const TypeId base = resolvedTypeOf(typeDecl.m_right.get());
    const auto baseSort = base != INVALID_TYPE_ID ? m_sorts.sortOf(base) : std::nullopt;
    if (!baseSort) {
        reportOutsideEnvelope(typeDecl, "type assertion base type is not modelable");
        ++m_outsideEnvelopeVcs;
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
            ++m_outsideEnvelopeVcs;
            return;
        }
        // VC: ¬(∀v. A(v)) - нарушаемо → SAT (контрпример), выполнимо → UNSAT. Изолированный.
        const SmtTerm forall = sb::makeForall({typeName}, {*baseSort}, std::move(*a), typeDecl.range());
        const SmtTerm vc = sb::mkNot(std::make_shared<SmtTerm>(forall), typeDecl.range());
        addGoal(std::move(vc), typeDecl.range(), std::string{}, VcObligationKind::kTypeAssert); // аксиомы применяются все
    }
}

void TrustToSmt::walk(const AstNodeBase* node, const std::map<const FuncDecl*, const flow::MirFunction*>& mirByFunc) {
    if (node == nullptr) {
        return;
    }
    if (node->kind() == ParserToken::Kind::FuncDecl) {
        const auto& f = static_cast<const FuncDecl&>(*node);
        if (!f.m_trust.empty() || sb::bodyHasTrustContract(&f)) {
            const auto it = mirByFunc.find(&f);
            processFuncContract(f, it != mirByFunc.end() ? it->second : nullptr);
        }
    } else if (node->kind() == ParserToken::Kind::TypeDecl) {
        const auto& b = static_cast<const Binary&>(*node);
        if (!b.m_trust.empty()) {
            processTypeAssert(b);
        }
    }
    for (const auto& child : node->children()) {
        walk(child.get(), mirByFunc);
    }
}

std::optional<SmtScript> TrustToSmt::generate(const HirModule& module, const flow::MirModule& mir) {
    m_plan = VcPlan{};
    m_symbolMap.clear();
    m_declared.clear();
    m_outsideEnvelopeVcs = 0;
    // Тело каждой функции берётся из MIR (единственная производная форма); HIR даёт декларации
    // модуля и материализованные семантикой факты (имена/типы/сигнатуры уже разрешены).
    std::map<const FuncDecl*, const flow::MirFunction*> mirByFunc;
    for (const flow::MirFunction& fn : mir.functions) {
        if (fn.func != nullptr) {
            mirByFunc[fn.func] = &fn;
        }
    }
    for (const auto& n : module.nodes()) {
        walk(n.get(), mirByFunc);
    }
    if (m_ctx.diag().errorCount() > 0) {
        return std::nullopt; // диагностика уже выдана
    }
    // Контрактов нет — пустой скрипт без логики/check-sat (артефакт не создаётся).
    if (m_plan.empty()) {
        SmtScript empty;
        empty.outsideEnvelopeCount = m_outsideEnvelopeVcs;
        return empty;
    }
    m_plan.symbolMap.assign(m_symbolMap.begin(), m_symbolMap.end());
    m_plan.outsideEnvelopeCount = m_outsideEnvelopeVcs;
    return assemble(std::move(m_plan));
}

} // namespace solver
} // namespace trust
