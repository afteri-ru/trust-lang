// Lowering как проход HIR: AST остаётся деревом разбора, все трансформации «сахара»
// (SemicolonStmt, goto/метки, временные анализатора) выполняются здесь.

#include "ir/lowering.hpp"

#include "attrs/attr_builtin.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/ident_name.hpp"
#include "ast/trust_prop.hpp"
#include "diag/registry.hpp"
#include "diag/report.hpp"
#include "ir/hir/diag.hpp"
#include "session/context.hpp"
#include "types/type_system.hpp"
#include "types/names.hpp"
#include "utils/strings.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

namespace {

// Пер-компонентная регистрация диагностик hir (см. diag/registry.hpp): severity-опция
// `-Whir-invariant-unattached`.
struct HirDiagnosticsRegistrar {
    HirDiagnosticsRegistrar() {
        registerDiagnostics([](Options& opts) { HIR_DIAG_LIST(HIR_DG_ADD_OPT) });
    }
};
const HirDiagnosticsRegistrar kHirDiagnosticsRegistrar;

using JumpTargets = JumpTargetsT<std::string>;

std::string cleanLabelName(std::string_view name) {
    std::string clean;
    clean.reserve(name.size());
    for (char c : name) {
        if (c != ':') {
            clean += c;
        }
    }
    return clean;
}

std::string funcNameOf(const FuncDecl* fd) {
    std::string s{fd->text()};
    if (!s.empty() && s[0] == '%') {
        s.erase(0, 1);
    }
    return cleanLabelName(s);
}

void appendLabel(AstNodePtr& bodyNode, const std::string& label) {
    if (!bodyNode) {
        return;
    }
    if (is_block_kind(bodyNode->kind())) {
        auto* seq = bodyNode->as_sequence();
        seq->m_body.push_back(std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, label));
    } else {
        auto seq = std::make_shared<Sequence>(ParserToken::Kind::sequence);
        seq->m_body.push_back(std::move(bodyNode));
        seq->m_body.push_back(std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, label));
        bodyNode = std::move(seq);
    }
}

bool isOperandSequenceKind(ParserToken::Kind k) noexcept {
    // Dict/Tuple/Range/Filling/Ellipsis - Sequence структурно, но m_body - ОПЕРАНДЫ, а не
    // список операторов: SemicolonStmt им не ставится.
    return k == ParserToken::Kind::DictLiteral || k == ParserToken::Kind::Tuple || k == ParserToken::Kind::RangeExpr || k == ParserToken::Kind::Filling ||
           k == ParserToken::Kind::Ellipsis;
}

void lowerSequenceBody(Sequence& seq, LowerCtx& ctx) {
    if (isOperandSequenceKind(seq.kind())) {
        for (auto& child : seq.m_body) {
            lowerNode(child, ctx);
        }
        return;
    }
    lowerBody(seq.m_body, ctx);
}

// Инвариант задаётся оператором-контрактом `@{ invariant --> ... @};` и по синтаксису стоит
// ПЕРЕД циклом. Оператор остаётся в последовательности (кодоген эмитит его как рантайм-проверку),
// а ассоциация «инвариант → цикл» фиксируется в поле узла цикла: позиционная эвристика в
// потребителях (solver) не может быть надёжной (метки lowering'а, несколько циклов подряд).

[[nodiscard]] bool isLoopInvariantContract(const AstNodePtr& node) {
    return node && node->is<TrustContract>() && node->as<TrustContract>()->kind == PropertyKind::Invariant;
}

/// Цель привязки: цикл, перед которым стоят инварианты. Метки lowering'а (continue-метка
/// именованного блока) прозрачны — они не являются операторами.
[[nodiscard]] AstNodePtr* loopAfterInvariants(std::vector<AstNodePtr>& body, std::size_t index) {
    for (std::size_t i = index; i < body.size(); ++i) {
        if (!body[i]) {
            continue;
        }
        if (body[i]->kind() == ParserToken::Kind::LabelStmt) {
            continue;
        }
        return (body[i]->kind() == ParserToken::Kind::WhileStmt || body[i]->kind() == ParserToken::Kind::DoWhileStmt) ? &body[i] : nullptr;
    }
    return nullptr;
}

void attachLoopInvariants(std::vector<AstNodePtr>& body, LowerCtx& ctx) {
    for (std::size_t i = 0; i < body.size();) {
        if (!isLoopInvariantContract(body[i])) {
            ++i;
            continue;
        }
        const std::size_t first = i;
        while (i < body.size() && isLoopInvariantContract(body[i])) {
            ++i;
        }
        AstNodePtr* loop = loopAfterInvariants(body, i);
        std::vector<const AstNodeBase*>* invariants = nullptr;
        if (loop != nullptr) {
            invariants = (*loop)->kind() == ParserToken::Kind::WhileStmt ? &(*loop)->as<WhileStmt>()->m_invariants : &(*loop)->as<DoWhileStmt>()->m_invariants;
        }
        if (invariants != nullptr) {
            invariants->reserve(i - first);
            for (std::size_t k = first; k < i; ++k) {
                invariants->push_back(body[k].get());
            }
            continue;
        }
        // Инвариант без цикла: обещание индукции не к чему отнести. Молча игнорировать нельзя —
        // иначе пользователь считает условие проверяемым, а оно не проверяется.
        if (ctx.ctx != nullptr) {
            for (std::size_t k = first; k < i; ++k) {
                diag::reportWithOptions(ctx.ctx->diag(), ctx.ctx->opts(), hir::DiagId::InvariantUnattached, body[k]->range(),
                                        "loop invariant is not attached to a loop: place '@{{ invariant --> ... @}}' immediately before the loop");
            }
        }
    }
}

// Специализированные понижения (определены ниже).
void lowerScopeBlock(ScopeBlock& block, LowerCtx& ctx);
void lowerRecord(RecordDecl& record, LowerCtx& ctx);
void lowerFunc(FuncDecl& func, LowerCtx& ctx);
void lowerIf(IfStmt& stmt, LowerCtx& ctx);
void lowerWhile(WhileStmt& stmt, LowerCtx& ctx);
void lowerDoWhile(DoWhileStmt& stmt, LowerCtx& ctx);
void lowerMatch(MatchStmt& stmt, LowerCtx& ctx);
void lowerTryCatch(TryCatchStmt& stmt, LowerCtx& ctx);
void lowerWith(WithStmt& stmt, LowerCtx& ctx);
void lowerDestructure(DestructureDecl& decl, LowerCtx& ctx);
void lowerJump(AstNodePtr& self, LowerCtx& ctx);
void lowerVar(VarDecl& var, LowerCtx& ctx);
void lowerCall(CallExpr& call, LowerCtx& ctx);

} // namespace

void lowerNode(AstNodePtr& node, LowerCtx& ctx) {
    if (!node) {
        return;
    }
    switch (node->kind()) {
    case ParserToken::Kind::sequence:
        lowerSequenceBody(*node->as_sequence(), ctx);
        return;
    case ParserToken::Kind::ScopeBlock:
        lowerScopeBlock(*node->as<ScopeBlock>(), ctx);
        return;
    case ParserToken::Kind::StructDecl:
        lowerRecord(*node->as<RecordDecl>(), ctx);
        return;
    case ParserToken::Kind::FuncDecl:
        lowerFunc(*node->as<FuncDecl>(), ctx);
        return;
    case ParserToken::Kind::IfStmt:
        lowerIf(*node->as<IfStmt>(), ctx);
        return;
    case ParserToken::Kind::WhileStmt:
        lowerWhile(*node->as<WhileStmt>(), ctx);
        return;
    case ParserToken::Kind::DoWhileStmt:
        lowerDoWhile(*node->as<DoWhileStmt>(), ctx);
        return;
    case ParserToken::Kind::MatchingStmt:
        lowerMatch(*node->as<MatchStmt>(), ctx);
        return;
    case ParserToken::Kind::TryCatchStmt:
        lowerTryCatch(*node->as<TryCatchStmt>(), ctx);
        return;
    case ParserToken::Kind::WithStmt:
        lowerWith(*node->as<WithStmt>(), ctx);
        return;
    case ParserToken::Kind::DestructureDecl:
        lowerDestructure(*node->as<DestructureDecl>(), ctx);
        return;
    case ParserToken::Kind::ReturnStmt:
    case ParserToken::Kind::ThrowStmt:
    case ParserToken::Kind::BreakStmt:
    case ParserToken::Kind::ContinueStmt:
        lowerJump(node, ctx);
        return;
    case ParserToken::Kind::VarDecl:
        lowerVar(*node->as<VarDecl>(), ctx);
        return;
    case ParserToken::Kind::CallExpr:
        lowerCall(*node->as<CallExpr>(), ctx);
        return;
    default:
        break;
    }
    // Binary-семейство (MathOp/Bitwise/Compare/Logical/Assign/IncDec/Member/Array/TypeDecl/...).
    if (node->is<Binary>()) {
        auto& binary = *node->as<Binary>();
        lowerNode(binary.m_left, ctx);
        lowerNode(binary.m_right, ctx);
        return;
    }
    // Прочие Sequence-формы (ModuleDecl/CatchBlock/RefMake/RefTake/RefLock/ArrayInit/...):
    // operand-list обработан выше, здесь понижается только тело.
    if (auto* seq = node->as_sequence()) {
        lowerSequenceBody(*seq, ctx);
    }
    // Остальные узлы (литералы/имена/маркеры) детей-операторов не имеют.
}

void lowerBody(std::vector<AstNodePtr>& body, LowerCtx& ctx) {
    std::vector<AstNodePtr> out;
    out.reserve(body.size() * 2 + 1);
    for (auto& child : body) {
        if (!child) {
            continue;
        }
        // continue-метка именованного блока: первый цикл в теле (по DFS) потребляет её.
        if (JumpTargets::consumesPendingContinue(child->kind())) {
            if (const std::optional<std::string> blockName = ctx.jumpTargets.takePendingContinue()) {
                const std::string label = JumpTargets::continueLabel(*blockName);
                if (child->kind() == ParserToken::Kind::WhileStmt) {
                    out.push_back(std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, label));
                } else {
                    appendLabel(static_cast<DoWhileStmt*>(child.get())->m_body, label);
                }
            }
        }
        // SemicolonStmt-обёртка: точка с запятой для statement-выражений явно в AST.
        if (isExprStatement(child->kind())) {
            out.push_back(std::make_shared<SemicolonStmt>(std::move(child)));
        } else {
            out.push_back(std::move(child));
        }
    }
    body = std::move(out);
    for (auto& child : body) {
        if (child) {
            lowerNode(child, ctx);
        }
    }
    attachLoopInvariants(body, ctx);
}

void lowerBodyNode(AstNodePtr& bodyNode, LowerCtx& ctx) {
    if (!bodyNode) {
        return;
    }
    if (is_block_kind(bodyNode->kind())) {
        lowerBody(static_cast<Sequence*>(bodyNode.get())->m_body, ctx);
    } else {
        std::vector<AstNodePtr> one;
        one.push_back(std::move(bodyNode));
        lowerBody(one, ctx);
        bodyNode = std::move(one[0]);
    }
}

namespace {

void lowerScopeBlock(ScopeBlock& block, LowerCtx& ctx) {
    if (ctx.inFunction && !block.is_anonymous() && !block.is_hidden()) {
        const std::string clean = cleanLabelName(block.name());
        if (!clean.empty()) {
            // Метки именованного блока: break - после тела, continue - первый цикл в теле.
            const JumpTargets::SavePoint saved = ctx.jumpTargets.enterNamedBlock(clean, JumpTargets::breakLabel(clean));
            lowerBody(block.m_body, ctx);
            block.m_body.push_back(std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, JumpTargets::breakLabel(clean)));
            ctx.jumpTargets.leaveNamedBlock(saved);
            return;
        }
    }
    lowerBody(block.m_body, ctx);
}

void lowerRecord(RecordDecl& record, LowerCtx& ctx) {
    // Члены тела (поля/методы): тела методов - список операторов, требует lowering.
    // Базы (m_baseTypes) - тип-ссылки, не операторы.
    if (!record.hasBody()) {
        return;
    }
    for (auto& member : *record.body()) {
        lowerNode(member, ctx);
    }
}

void lowerFunc(FuncDecl& func, LowerCtx& ctx) {
    auto* fb = func.body();
    if (!fb) {
        return;
    }
    const bool savedInFn = ctx.inFunction;
    const std::string savedFunc = ctx.funcName;
    ctx.inFunction = true;
    ctx.funcName = funcNameOf(&func);
    lowerBody(*fb, ctx);
    ctx.inFunction = savedInFn;
    ctx.funcName = savedFunc;
}

void lowerIf(IfStmt& stmt, LowerCtx& ctx) {
    lowerNode(stmt.m_cond, ctx);
    lowerBodyNode(stmt.m_body, ctx);
    for (auto& [cond, body] : stmt.m_elseifs) {
        lowerNode(cond, ctx);
        lowerBodyNode(body, ctx);
    }
    lowerBodyNode(stmt.m_else, ctx);
}

void lowerWhile(WhileStmt& stmt, LowerCtx& ctx) {
    // While-else эмулируется флагом «вошёл ли цикл хотя бы раз».
    if (stmt.m_else) {
        const std::string flagName = "_we" + std::to_string(++ctx.whileElseCounter);
        auto flag = std::make_shared<VarDecl>(flagName, nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::string("0")));
        if (ctx.types) {
            flag->inferredType = QualType::fromTypeId(ctx.types->read().getType(type::Bool)); // bool-флаг
        }
        stmt.m_elseFlag = flag;
    }
    lowerNode(stmt.m_elseFlag, ctx);
    lowerNode(stmt.m_cond, ctx);
    lowerBodyNode(stmt.m_body, ctx);
    lowerBodyNode(stmt.m_else, ctx);
}

void lowerDoWhile(DoWhileStmt& stmt, LowerCtx& ctx) {
    lowerBodyNode(stmt.m_body, ctx);
    lowerNode(stmt.m_cond, ctx);
}

void lowerMatch(MatchStmt& stmt, LowerCtx& ctx) {
    // Scrutinee-временная создана СЕМАНТИКОЙ; здесь только понижаем её и значение.
    lowerNode(stmt.m_tempDecl, ctx);
    lowerNode(stmt.m_value, ctx);
    for (auto& c : stmt.m_cases) {
        for (auto& p : c.patterns) {
            lowerNode(p, ctx);
        }
        lowerBodyNode(c.body, ctx);
    }
    lowerBodyNode(stmt.m_default, ctx);
}

void lowerTryCatch(TryCatchStmt& stmt, LowerCtx& ctx) {
    lowerBody(stmt.m_body, ctx);
    // Ветки catch: каждая - CatchBlock (Sequence) → lowerBody её m_body; m_binding не понижается.
    for (auto& cb : stmt.m_catches) {
        lowerNode(cb, ctx);
    }
    lowerBodyNode(stmt.m_else, ctx);
}

} // namespace

namespace {

void lowerWith(WithStmt& stmt, LowerCtx& ctx) {
    // With-else: флаг `bool _wN := 0;` создаёт lowering; транспилятор эмитит
    //   try { биндинги; _wN = true; тело } catch(...) { if(_wN) throw; else }
    if (stmt.m_else) {
        const std::string flagName = "_w" + std::to_string(++ctx.withFailCounter);
        auto flag = std::make_shared<VarDecl>(flagName, nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::string("0")));
        if (ctx.types) {
            flag->inferredType = QualType::fromTypeId(ctx.types->read().getType(type::Bool)); // bool-флаг
        }
        stmt.m_failFlag = flag;
    }
    lowerNode(stmt.m_failFlag, ctx);

    // Захват ссылки (`*ref` / `*^ref`): Locker-темп `__cap_N` (auto) + биндинг `T& name = *__cap_N`.
    for (auto& [lock, binding] : stmt.m_locks) {
        lowerNode(lock, ctx);
        lowerNode(binding, ctx);
        if (!lock) {
            continue; // обычный value-биндинг (lock == nullptr)
        }
        Sequence& take = static_cast<Sequence&>(*lock);
        if (take.m_body.empty()) {
            continue;
        }
        const bool immutable = ctx.ctx && take.has_attr(ctx.ctx->attrs(), attr::ReadOnly);
        AstNodePtr operand = std::move(take.m_body[0]);
        auto lock_expr = std::make_shared<Sequence>(ParserToken::Kind::RefLockExpr);
        if (immutable) {
            if (auto readonly_id = ctx.ctx->attrs().lookup(attr::ReadOnly); readonly_id) {
                lock_expr->add_attr(readonly_id.value());
            }
        }
        lock_expr->m_body.push_back(std::move(operand));
        const std::string cap_name = std::string(kLockerTempPrefix) + std::to_string(++ctx.withCapCounter);
        auto cap = std::make_shared<VarDecl>(cap_name, nullptr, lock_expr);

        if (!binding || binding->kind() != ParserToken::Kind::VarDecl) {
            // Голый `*ref` без биндинга: временный лок (снимается сразу), тело без лока.
            lock = std::move(lock_expr);
            continue;
        }
        VarDecl& vd = static_cast<VarDecl&>(*binding);
        if (vd.text() == "_" || vd.text() == "$_") {
            // Игнорируемое имя: захват на всё тело блока (Locker держится), без значения.
            lock = std::move(cap);
            binding = nullptr;
            continue;
        }
        // Именованный биндинг: темп (auto, держится) + `T& name = *__cap_N;`.
        auto deref = std::make_shared<Sequence>(ParserToken::Kind::RefLockDeref);
        deref->m_body.push_back(std::make_shared<IdentName>(cap_name));
        vd.m_initializer = std::move(deref);
        if (ctx.types && vd.inferredType.getTypeId() != INVALID_TYPE_ID) {
            vd.inferredType = QualType::fromTypeId(ctx.types->interner().applyRefType(vd.inferredType.getTypeId(), RefType::kRef));
        }
        lock = std::move(cap);
    }

    lowerBodyNode(stmt.m_body, ctx);
    lowerBodyNode(stmt.m_else, ctx);
}

void lowerDestructure(DestructureDecl& decl, LowerCtx& ctx) {
    // Временная копия источника spread-деструктуризации (кроме mutating-rest).
    if (decl.m_isSpread && decl.m_source) {
        std::string srcCpp;
        if (decl.m_source->kind() == ParserToken::Kind::Ident) {
            srcCpp = utils::nameToCpp(requiredText(*decl.m_source));
        }
        std::string restCpp;
        for (const DestructureDecl::Target& target : decl.m_targets) {
            auto* t = target.name.get();
            if (target.isRest && t && t->kind() == ParserToken::Kind::Ident && requiredText(*t) != "_") {
                restCpp = utils::nameToCpp(requiredText(*t));
            }
        }
        const bool mutatingRest = !restCpp.empty() && restCpp == srcCpp;
        if (!mutatingRest) {
            const std::string tmpName = "__trust_dst_" + std::to_string(ctx.destructureCounter++);
            decl.m_sourceTemp = std::make_shared<VarDecl>(tmpName, nullptr, decl.m_source);
            // Тип источника вычислен семантикой ДО lowering: временная сразу типизирована.
            static_cast<VarDecl&>(*decl.m_sourceTemp).inferredType = decl.m_sourceType;
        }
    }
}

} // namespace

namespace {

void lowerJump(AstNodePtr& self, LowerCtx& ctx) {
    auto& jump = *self->as<JumpStmt>();
    // Именованные break/continue → goto; break по имени функции → void-return.
    if (jump.kind() == ParserToken::Kind::BreakStmt || jump.kind() == ParserToken::Kind::ContinueStmt) {
        if (jump.m_label) {
            const AstNodeBase* source = self.get(); // источник для origin синтетического узла
            const std::string clean = cleanLabelName(requiredText(*jump.m_label));
            if (jump.kind() == ParserToken::Kind::BreakStmt) {
                if (ctx.inFunction && JumpTargets::isFunctionBreak(clean, ctx.funcName)) {
                    // `break <имя_функции>` = void-return; метка сохраняется.
                    auto ret = std::make_shared<JumpStmt>(ParserToken::Kind::ReturnStmt);
                    ret->origin = source;
                    ret->m_label = std::move(jump.m_label);
                    self = std::move(ret);
                } else {
                    auto gotoStmt = std::make_shared<LabelRef>(ParserToken::Kind::GotoStmt, JumpTargets::breakLabel(clean));
                    gotoStmt->origin = source;
                    self = std::move(gotoStmt);
                }
            } else {
                auto gotoStmt = std::make_shared<LabelRef>(ParserToken::Kind::GotoStmt, JumpTargets::continueLabel(clean));
                gotoStmt->origin = source;
                self = std::move(gotoStmt);
            }
        }
        return;
    }
    // return/throw: понижаем значение и синтезированную временную возврата.
    lowerNode(jump.m_tempDecl, ctx);
    lowerNode(jump.m_value, ctx);
}

void lowerVar(VarDecl& var, LowerCtx& ctx) {
    lowerNode(var.m_initializer, ctx);
    lowerNode(var.m_type, ctx);
}

void lowerCall(CallExpr& call, LowerCtx& ctx) {
    lowerNode(call.m_callee, ctx);
    if (call.m_args) {
        for (auto& arg : *call.m_args) {
            lowerNode(arg, ctx);
        }
    }
}

} // namespace

} // namespace trust
