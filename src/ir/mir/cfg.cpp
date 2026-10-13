// src/ir/cfg.cpp
// Построение CFG поверх HIR (понижённой формы). См. include/ir/cfg.hpp.

#include "ir/cfg.hpp"

#include "ir/place_root.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/binary_op.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/clone.hpp"
#include "ast/kind_dispatch.hpp"
#include "ast/trust_prop.hpp"
#include "attrs/attr_builtin.hpp"
#include "attrs/attr_pool.hpp"
#include "utils/error.hpp"

#include <array>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trust::flow {
namespace {

using JumpTargets = JumpTargetsT<std::size_t>; ///< статические предикаты формата goto-меток

/// Контекст построения (цели переходов и границы скоупов).
struct BuildCtx {
    std::optional<std::size_t> loopBreak;    ///< break ближайшего цикла
    std::optional<std::size_t> loopContinue; ///< continue ближайшего цикла
    std::optional<std::size_t> throwTarget;  ///< catch-dispatch ближайшего try
    std::size_t loopScopeDepth = 0;          ///< глубина стека скоупов на входе в цикл
    std::size_t throwScopeDepth = 0;         ///< глубина стека скоупов на входе в try
};

class Builder {
  public:
    Builder(const FuncDecl& func, const AttrPool& attrs)
    : m_func(func)
    , m_attrs(attrs) {}

    ControlFlowGraph build() {
        m_cfg.blocks.clear();
        m_cfg.entry = newBlock();
        m_cfg.exit = newBlock();

        BuildCtx ctx;

        if (!m_func.m_body.has_value() || m_func.m_body->empty()) {
            addEdge(m_cfg.entry, m_cfg.exit, EdgeKind::Normal);
            markUnreachable();
            return std::move(m_cfg);
        }

        collectLoopInvariants();

        const std::size_t bodyEntry = newBlock();
        addEdge(m_cfg.entry, bodyEntry, EdgeKind::Normal);
        m_current = bodyEntry;
        buildStatements(*m_func.m_body, ctx);
        finishPath(m_cfg.exit);

        resolveGotos();
        markUnreachable();
        return std::move(m_cfg);
    }

  private:
    const FuncDecl& m_func;
    const AttrPool& m_attrs;
    ControlFlowGraph m_cfg;
    std::optional<std::size_t> m_current;
    std::vector<const AstNodeBase*> m_scopeStack; ///< активные лексические скоупы (LIFO)

    /// Метка HIR (LabelStmt): целевой блок и снимок стека скоупов в точке метки.
    struct LabelSite {
        std::size_t block = 0;
        std::vector<const AstNodeBase*> scopes;
    };

    /// Отложенный переход (GotoStmt): цель разрешается после обхода тела, потому что
    /// lowering ставит метки forward (в конце тела/блока).
    struct PendingGoto {
        std::size_t block = 0;
        std::string label;
        std::vector<const AstNodeBase*> scopes;
        const AstNodeBase* site = nullptr;
    };

    std::map<std::string, LabelSite> m_labels;
    std::vector<PendingGoto> m_pendingGotos;
    /// Привязка «инвариант → цикл» из факта lowering (`WhileStmt/DoWhileStmt::m_invariants`):
    /// MIR-элемент `Invariant` несёт её в `loopNode` (верификатор не ищет связь повторно).
    std::unordered_map<const AstNodeBase*, const AstNodeBase*> m_invariantOwners;

    /// Собрать привязки инвариантов к циклам по телу функции (факт lowering, не эвристика).
    void collectLoopInvariants() {
        const auto visit = [this](const auto& self, const AstNodeBase& node) -> void {
            if (node.is<WhileStmt>()) {
                for (const AstNodeBase* invariant : static_cast<const WhileStmt&>(node).m_invariants) {
                    m_invariantOwners.emplace(invariant, &node);
                }
            } else if (node.is<DoWhileStmt>()) {
                for (const AstNodeBase* invariant : static_cast<const DoWhileStmt&>(node).m_invariants) {
                    m_invariantOwners.emplace(invariant, &node);
                }
            }
            for (const auto& child : node.children()) {
                if (child) {
                    self(self, *child);
                }
            }
        };
        if (m_func.m_body) {
            for (const auto& stmt : *m_func.m_body) {
                if (stmt) {
                    visit(visit, *stmt);
                }
            }
        }
    }

    std::size_t newBlock() {
        const std::size_t index = m_cfg.blocks.size();
        m_cfg.blocks.push_back(FlowBlock{.id = index});
        return index;
    }

    std::size_t ensureCurrent() {
        if (!m_current) {
            m_current = newBlock();
        }
        return *m_current;
    }

    void emitInstruction(MirInstruction instruction) { m_cfg.blocks[ensureCurrent()].elements.push_back(instruction); }

    /// Текущий лексический скоуп (вершина стека скоупов; nullptr - уровень функции).
    [[nodiscard]] const AstNodeBase* currentScope() const { return m_scopeStack.empty() ? nullptr : m_scopeStack.back(); }

    void emitRead(const AstNodeBase& site, const AstNodeBase* decl) {
        if (decl) {
            emitInstruction(MirInstruction{.kind = MirOp::Read, .place = decl, .site = &site});
        }
    }
    void emitDeclare(const AstNodeBase* place, bool initialized) {
        if (!place) {
            return;
        }
        emitInstruction(MirInstruction{.kind = MirOp::Declare, .place = place, .site = place, .assigned = initialized, .scope = currentScope()});
    }
    void emitWrite(const AstNodeBase* place, bool assigned, bool finalWrite, const AstNodeBase* site, const AstNodeBase* projection = nullptr) {
        if (place) {
            emitInstruction(
                MirInstruction{.kind = MirOp::Write, .place = place, .site = site, .assigned = assigned, .finalWrite = finalWrite, .projection = projection});
        }
    }
    void emitMove(const AstNodeBase* place, const AstNodeBase* site, bool assigned, const AstNodeBase* projection = nullptr) {
        if (place) {
            emitInstruction(MirInstruction{.kind = MirOp::Move, .place = place, .site = site, .assigned = assigned, .projection = projection});
        }
    }
    void emitCall(const AstNodeBase& site, const AstNodeBase* callee) { emitInstruction(MirInstruction{.kind = MirOp::Call, .place = callee, .site = &site}); }

    /// Эмитирует ScopeExit для всех скоупов стека глубже boundary (LIFO).
    void emitScopeExits(std::size_t boundary, const AstNodeBase* reason) {
        if (!m_current) {
            return;
        }
        for (std::size_t i = m_scopeStack.size(); i > boundary; --i) {
            const AstNodeBase* scope = m_scopeStack[i - 1];
            emitInstruction(MirInstruction{.kind = MirOp::ScopeExit, .place = scope, .site = reason, .scope = scope});
        }
    }

    /// readonly-цель записи (attr::ReadOnly на lvalue).
    [[nodiscard]] bool isFinalWrite(const AstNodeBase* node) const {
        const AstNodeAttr* attrNode = node ? node->as_attr() : nullptr;
        return attrNode && attrNode->has_attr(m_attrs, attr::ReadOnly);
    }

    // -- Линеаризация AST в эффекты (семантический порядок) --
    void appendNode(const AstNodeBase& node);
    void appendExpr(const AstNodeBase& node);
    void appendBinary(const Binary& binary);
    void appendShortCircuit(const Binary& binary);
    void appendWriteLValue(const AstNodeBase& node, bool assigned, bool finalWrite, const AstNodeBase* diagNode, bool move = false);

    void setTerminator(std::size_t block, TerminatorKind kind, const AstNodeBase* node) {
        m_cfg.blocks[block].terminator = kind;
        m_cfg.blocks[block].terminatorNode = MirInstruction{.kind = MirOp::Terminator, .site = node};
    }

    std::size_t terminate(TerminatorKind kind, const AstNodeBase* node) {
        const std::size_t block = ensureCurrent();
        setTerminator(block, kind, node);
        m_current.reset();
        return block;
    }

    void addEdge(std::size_t from, std::size_t to, EdgeKind kind) { m_cfg.blocks[from].successors.push_back(FlowEdge{.kind = kind, .target = to}); }

    /// Завершает текущий путь ребром в target (если путь ещё открыт).
    void finishPath(std::size_t target) {
        if (m_current) {
            addEdge(*m_current, target, EdgeKind::Normal);
            m_current.reset();
        }
    }

    void markUnreachable() {
        std::vector<bool> reachable(m_cfg.blocks.size(), false);
        std::vector<std::size_t> work;
        work.push_back(m_cfg.entry);
        reachable[m_cfg.entry] = true;
        while (!work.empty()) {
            const std::size_t b = work.back();
            work.pop_back();
            for (const FlowEdge& e : m_cfg.blocks[b].successors) {
                if (!reachable[e.target]) {
                    reachable[e.target] = true;
                    work.push_back(e.target);
                }
            }
        }
        for (std::size_t i = 0; i < m_cfg.blocks.size(); ++i) {
            m_cfg.blocks[i].unreachable = !reachable[i];
        }
    }

    void buildStatements(const std::vector<AstNodePtr>& body, BuildCtx& ctx);
    void buildStmt(const AstNodePtr& node, BuildCtx& ctx);
    void buildBody(const AstNodePtr& body, BuildCtx& ctx);
    void buildScope(const ScopeBlock& block, BuildCtx& ctx);
    void buildIf(const IfStmt& stmt, BuildCtx& ctx);
    void buildWhile(const WhileStmt& stmt, BuildCtx& ctx);
    void buildDoWhile(const DoWhileStmt& stmt, BuildCtx& ctx);
    void buildMatch(const MatchStmt& stmt, BuildCtx& ctx);
    void buildWith(const WithStmt& stmt, BuildCtx& ctx);
    void buildTry(const TryCatchStmt& stmt, BuildCtx& ctx);
    void buildJump(const JumpStmt& stmt, BuildCtx& ctx);

    // Partial pass: одна таблица kind → обработчик (default = элемент блока).
    using StmtHandler = void (Builder::*)(const AstNodePtr&, BuildCtx&);
    using StmtHandlerTable = std::array<StmtHandler, kParserKindSlotCount>;
    static const StmtHandlerTable& cfgActions();

    void handleElement(const AstNodePtr& node, BuildCtx& ctx);
    void handleSequence(const AstNodePtr& node, BuildCtx& ctx);
    void handleScope(const AstNodePtr& node, BuildCtx& ctx);
    void handleIf(const AstNodePtr& node, BuildCtx& ctx);
    void handleWhile(const AstNodePtr& node, BuildCtx& ctx);
    void handleDoWhile(const AstNodePtr& node, BuildCtx& ctx);
    void handleMatch(const AstNodePtr& node, BuildCtx& ctx);
    void handleWith(const AstNodePtr& node, BuildCtx& ctx);
    void handleTry(const AstNodePtr& node, BuildCtx& ctx);
    void handleJumpNode(const AstNodePtr& node, BuildCtx& ctx);
    void handleSemicolon(const AstNodePtr& node, BuildCtx& ctx);
    void handleLabel(const AstNodePtr& node, BuildCtx& ctx);
    void handleGoto(const AstNodePtr& node, BuildCtx& ctx);
    /// trust-контракт в позиции оператора: элемент Assert/Invariant + чтения выражения
    /// (иначе DA/borrow не видят мест, используемых в условии контракта).
    void handleContract(const AstNodePtr& node, BuildCtx& ctx);

    using JumpHandler = void (Builder::*)(const JumpStmt&, BuildCtx&);
    using JumpHandlerTable = std::array<JumpHandler, kParserKindSlotCount>;
    static const JumpHandlerTable& jumpActions();

    void handleReturnJump(const JumpStmt& stmt, BuildCtx& ctx);
    void handleThrowJump(const JumpStmt& stmt, BuildCtx& ctx);
    void handleBreakJump(const JumpStmt& stmt, BuildCtx& ctx);
    void handleContinueJump(const JumpStmt& stmt, BuildCtx& ctx);
    void handleUnknownJump(const JumpStmt& stmt, BuildCtx& ctx);

    /// Цели break/continue ближайшего цикла (именованные переходы в HIR уже понижены в goto).
    [[nodiscard]] std::size_t breakTarget(const BuildCtx& ctx) const;
    [[nodiscard]] std::size_t continueTarget(const BuildCtx& ctx) const;

    /// Разрешает отложенные переходы: метки уже известны, ScopeExit дописывается в блок
    /// перехода (элементы блока хранятся отдельно от терминатора).
    void resolveGotos();
};

void Builder::buildStatements(const std::vector<AstNodePtr>& body, BuildCtx& ctx) {
    for (const AstNodePtr& stmt : body) {
        if (stmt) {
            buildStmt(stmt, ctx);
        }
    }
}

void Builder::buildBody(const AstNodePtr& body, BuildCtx& ctx) {
    if (body) {
        buildStmt(body, ctx);
    }
}

void Builder::buildScope(const ScopeBlock& block, BuildCtx& ctx) {
    // Именованные блоки в HIR уже понижены: break/continue — это goto на метку
    // (`<блок>_break` в конце тела / `<блок>_continue` перед первым циклом), поэтому
    // отдельный блок-цель не нужен: ScopeExit выполняется после метки на всех путях.
    m_scopeStack.push_back(&block);
    buildStatements(block.m_body, ctx);
    if (m_current) {
        emitInstruction(MirInstruction{.kind = MirOp::ScopeExit, .place = &block, .site = &block, .scope = &block});
    }
    m_scopeStack.pop_back();
}

void Builder::buildStmt(const AstNodePtr& node, BuildCtx& ctx) {
    EXPECT(node && node->kind() != ParserToken::Kind::END && "CFG: END is not a statement");
    (this->*cfgActions()[kindIndex(node->kind())])(node, ctx);
}

const Builder::StmtHandlerTable& Builder::cfgActions() {
    static const StmtHandlerTable table = [] {
        StmtHandlerTable t;
        t.fill(&Builder::handleElement);
        t[kindIndex(ParserToken::Kind::sequence)] = &Builder::handleSequence;
        t[kindIndex(ParserToken::Kind::ModuleDecl)] = &Builder::handleSequence;
        t[kindIndex(ParserToken::Kind::ScopeBlock)] = &Builder::handleScope;
        t[kindIndex(ParserToken::Kind::IfStmt)] = &Builder::handleIf;
        t[kindIndex(ParserToken::Kind::WhileStmt)] = &Builder::handleWhile;
        t[kindIndex(ParserToken::Kind::DoWhileStmt)] = &Builder::handleDoWhile;
        t[kindIndex(ParserToken::Kind::MatchingStmt)] = &Builder::handleMatch;
        t[kindIndex(ParserToken::Kind::WithStmt)] = &Builder::handleWith;
        t[kindIndex(ParserToken::Kind::TryCatchStmt)] = &Builder::handleTry;
        t[kindIndex(ParserToken::Kind::ReturnStmt)] = &Builder::handleJumpNode;
        t[kindIndex(ParserToken::Kind::ThrowStmt)] = &Builder::handleJumpNode;
        t[kindIndex(ParserToken::Kind::BreakStmt)] = &Builder::handleJumpNode;
        t[kindIndex(ParserToken::Kind::ContinueStmt)] = &Builder::handleJumpNode;
        t[kindIndex(ParserToken::Kind::GotoStmt)] = &Builder::handleGoto;
        t[kindIndex(ParserToken::Kind::LabelStmt)] = &Builder::handleLabel;
        t[kindIndex(ParserToken::Kind::SemicolonStmt)] = &Builder::handleSemicolon;
        t[kindIndex(ParserToken::Kind::TrustContract)] = &Builder::handleContract;
        return t;
    }();
    return table;
}

void Builder::handleElement(const AstNodePtr& node, BuildCtx&) {
    // Обычный оператор/объявление/выражение: линеаризуем в эффекты. Тела вложенных
    // функций/лямбд раскрываются отдельными CFG, а не этим построителем.
    appendNode(*node);
}

void Builder::appendNode(const AstNodeBase& node) {
    if (node.is<VarDecl>()) {
        const auto& var = *node.as<VarDecl>();
        const bool initialized = var.m_initializer && !isNoneMarker(var.m_initializer.get());
        // Место инициализировано ДО чтения своего инициализатора (shadowing self-reference).
        emitDeclare(&node, initialized);
        if (var.m_initializer) {
            appendExpr(*var.m_initializer);
        }
        return;
    }
    if (node.is<DestructureDecl>()) {
        const auto& destructure = *node.as<DestructureDecl>();
        if (destructure.m_source) {
            appendExpr(*destructure.m_source);
        }
        for (const AstNodePtr& target : destructure.m_targets) {
            if (!target) {
                continue;
            }
            if (destructure.m_isAssign) {
                appendWriteLValue(*target, true, false, target.get());
            } else if (!isNoneMarker(target.get())) {
                emitDeclare(target.get(), true);
            }
        }
        return;
    }
    if (node.is<Binary>()) {
        appendBinary(*node.as<Binary>());
        return;
    }
    if (node.is<JumpStmt>()) {
        const auto& jump = *node.as<JumpStmt>();
        if (jump.m_tempDecl) {
            appendNode(*jump.m_tempDecl);
        }
        if (jump.m_value) {
            appendExpr(*jump.m_value);
        }
        return;
    }
    if (node.is<FuncDecl>()) {
        // Лямбда: capture-by-value читает внешнюю переменную в точке создания лямбды.
        const auto& func = *node.as<FuncDecl>();
        if (func.isLambda() && func.m_captures) {
            for (const auto& capture : *func.m_captures) {
                if (capture && capture->is<ArgNode>()) {
                    const auto& arg = *capture->as<ArgNode>();
                    emitRead(arg, arg.resolvedDecl);
                }
            }
        }
        return;
    }
    if (node.is<RecordDecl>() || node.is<ClassDecl>()) {
        return;
    }
    if (node.kind() == ParserToken::Kind::TypeDecl) {
        return; // compile-time объявление типа: runtime-значений не читает
    }
    if (node.kind() == ParserToken::Kind::NameDecl) {
        appendBinary(*node.as<Binary>()); // объявление с инициализацией (объявление + запись)
        return;
    }
    appendExpr(node);
}

void Builder::appendExpr(const AstNodeBase& node) {
    if (node.kind() == ParserToken::Kind::Ident) {
        const auto& ident = *node.as<IdentName>();
        emitRead(ident, ident.resolvedDecl);
        return;
    }
    if (node.is<Binary>()) {
        appendBinary(*node.as<Binary>());
        return;
    }
    if (node.is<VarDecl>() || node.is<DestructureDecl>() || node.is<JumpStmt>() || node.is<FuncDecl>()) {
        appendNode(node);
        return;
    }
    if (node.is<RecordDecl>() || node.is<ClassDecl>()) {
        return;
    }
    if (node.is<TrustContract>()) {
        // Контракт не является значением: в выражении он не встречается (m_expr не входит
        // в children()), операторная позиция обрабатывается handleContract.
        return;
    }
    if (node.kind() == ParserToken::Kind::TypeDecl) {
        return; // compile-time объявление типа
    }
    if (node.kind() == ParserToken::Kind::NameDecl) {
        appendBinary(*node.as<Binary>()); // `y := x` в позиции оператора — объявление + запись
        return;
    }
    if (node.is<CallExpr>()) {
        const auto& call = *node.as<CallExpr>();
        if (call.m_callee) {
            appendExpr(*call.m_callee);
        }
        if (call.m_args) {
            for (const AstNodePtr& arg : *call.m_args) {
                if (arg) {
                    appendExpr(*arg);
                }
            }
        }
        const AstNodeBase* callee = nullptr;
        if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::Ident) {
            const auto* ident = call.m_callee->as<IdentName>();
            callee = ident ? ident->resolvedDecl : nullptr;
        }
        emitCall(node, callee);
        return;
    }
    std::vector<AstNodePtr*> children;
    const_cast<AstNodeBase&>(node).collectChildren(children);
    for (AstNodePtr* child : children) {
        if (child && *child) {
            appendExpr(**child);
        }
    }
}

void Builder::appendWriteLValue(const AstNodeBase& node, bool assigned, bool finalWrite, const AstNodeBase* diagNode, bool move) {
    // Место записи - КОРЕНЬ lvalue (Ident/поле/элемент/разыменование). Для проекций
    // (`o.f`, `a[i]`, `*s`) place = корневое объявление, projection = сам lvalue-узел:
    // так мутация поля/элемента видна flow-анализам, а идентичность корня сохраняется.
    const AstNodeBase* root = placeRoot(&node);
    if (root == nullptr) {
        return; // вызов/литерал в позиции lvalue: места нет
    }
    const AstNodeBase* projection = (node.kind() == ParserToken::Kind::Ident) ? nullptr : &node;
    const AstNodeBase* site = diagNode ? diagNode : &node;
    if (move) {
        emitMove(root, site, assigned, projection);
    } else {
        emitWrite(root, assigned, finalWrite, site, projection);
    }
}

bool isSimpleLValue(const AstNodeBase* node) {
    return node && node->kind() == ParserToken::Kind::Ident;
}

void Builder::appendBinary(const Binary& binary) {
    if (binary.kind() == ParserToken::Kind::NameDecl) {
        // Объявление с инициализацией (`y := x`): MIR обязан видеть объявление и значение —
        // иначе flow-анализ/верификатор теряют локальную переменную (в AST m_left — имя,
        // m_right — инициализатор). Место — узел имени (на него указывает resolvedDecl ссылок).
        const bool initialized = binary.m_right && !isNoneMarker(binary.m_right.get());
        if (initialized) {
            appendExpr(*binary.m_right);
        }
        if (binary.m_left) {
            emitDeclare(binary.m_left.get(), initialized);
            if (initialized) {
                emitWrite(binary.m_left.get(), /*assigned=*/true, /*finalWrite=*/false, &binary);
            }
        }
        return;
    }
    if (binary.kind() == ParserToken::Kind::AssignOp) {
        if (isSwapOp(binary.m_op)) {
            if (binary.m_left) {
                appendExpr(*binary.m_left);
            }
            const bool rhsDiscard = !binary.m_right || isNoneMarker(binary.m_right.get());
            if (!rhsDiscard) {
                appendExpr(*binary.m_right);
            }
            if (binary.m_left) {
                // swap обменивает значения: оба места остаются инициализированными.
                appendWriteLValue(*binary.m_left, /*assigned=*/true, /*finalWrite=*/false, &binary, /*move=*/true);
            }
            if (!rhsDiscard) {
                appendWriteLValue(*binary.m_right, /*assigned=*/true, /*finalWrite=*/false, &binary, /*move=*/true);
            }
            return;
        }

        const bool compound = isCompoundAssignOp(binary.m_op);
        if (binary.m_left) {
            // Составное присваивание читает LHS; простое - только write. Для member/index
            // база/индекс читаются всегда.
            if (compound || !isSimpleLValue(binary.m_left.get())) {
                appendExpr(*binary.m_left);
            }
        }
        const bool reset = !binary.m_right || isNoneMarker(binary.m_right.get());
        if (binary.m_right && !reset) {
            appendExpr(*binary.m_right);
        }
        if (binary.m_left) {
            // `x :=: _` (reset) - перемещение/сброс: инвалидирует представления источника.
            appendWriteLValue(*binary.m_left, !reset, isFinalWrite(binary.m_left.get()), &binary, /*move=*/reset);
        }
        return;
    }

    if (binary.kind() == ParserToken::Kind::IncDecOp) {
        const AstNodeBase* operand = binary.m_left ? binary.m_left.get() : binary.m_right.get();
        if (operand) {
            appendExpr(*operand);
            appendWriteLValue(*operand, true, isFinalWrite(operand), &binary);
        }
        return;
    }

    if (binary.kind() == ParserToken::Kind::AppendStmt) {
        if (binary.m_left) {
            appendExpr(*binary.m_left);
        }
        if (binary.m_right) {
            appendExpr(*binary.m_right);
        }
        for (const AstNodePtr& index : binary.m_indices) {
            if (index) {
                appendExpr(*index);
            }
        }
        if (binary.m_left) {
            appendWriteLValue(*binary.m_left, true, isFinalWrite(binary.m_left.get()), &binary);
        }
        return;
    }

    if (binary.kind() == ParserToken::Kind::LogicalOp && (binary.m_op == BinaryOp::LogicalAnd || binary.m_op == BinaryOp::LogicalOr)) {
        appendShortCircuit(binary);
        return;
    }

    std::vector<AstNodePtr*> children;
    const_cast<Binary&>(binary).collectChildren(children);
    for (AstNodePtr* child : children) {
        if (child && *child) {
            appendExpr(**child);
        }
    }
}

void Builder::appendShortCircuit(const Binary& binary) {
    const std::size_t join = newBlock();
    if (binary.m_left) {
        appendExpr(*binary.m_left);
    }

    const std::size_t condBlock = ensureCurrent();
    setTerminator(condBlock, TerminatorKind::Conditional, &binary);
    m_current.reset();

    const std::size_t rightEntry = newBlock();
    if (binary.m_op == BinaryOp::LogicalAnd) {
        addEdge(condBlock, rightEntry, EdgeKind::True);
        addEdge(condBlock, join, EdgeKind::False);
    } else {
        addEdge(condBlock, rightEntry, EdgeKind::False);
        addEdge(condBlock, join, EdgeKind::True);
    }

    m_current = rightEntry;
    if (binary.m_right) {
        appendExpr(*binary.m_right);
    }
    finishPath(join);
    m_current = join;
}

void Builder::handleSequence(const AstNodePtr& node, BuildCtx& ctx) {
    if (const Sequence* seq = node->as_sequence()) {
        buildStatements(seq->m_body, ctx);
    }
}

void Builder::handleScope(const AstNodePtr& node, BuildCtx& ctx) {
    buildScope(*node->as<ScopeBlock>(), ctx);
}

void Builder::handleIf(const AstNodePtr& node, BuildCtx& ctx) {
    buildIf(*node->as<IfStmt>(), ctx);
}

void Builder::handleWhile(const AstNodePtr& node, BuildCtx& ctx) {
    buildWhile(*node->as<WhileStmt>(), ctx);
}

void Builder::handleDoWhile(const AstNodePtr& node, BuildCtx& ctx) {
    buildDoWhile(*node->as<DoWhileStmt>(), ctx);
}

void Builder::handleMatch(const AstNodePtr& node, BuildCtx& ctx) {
    buildMatch(*node->as<MatchStmt>(), ctx);
}

void Builder::handleWith(const AstNodePtr& node, BuildCtx& ctx) {
    buildWith(*node->as<WithStmt>(), ctx);
}

void Builder::handleTry(const AstNodePtr& node, BuildCtx& ctx) {
    buildTry(*node->as<TryCatchStmt>(), ctx);
}

void Builder::handleJumpNode(const AstNodePtr& node, BuildCtx& ctx) {
    buildJump(*node->as<JumpStmt>(), ctx);
}

/// Вид ребра для goto-метки: формат имён задан lowering'ом
/// (`ast/convert/jump_targets.hpp`, реэкспорт через `ast/clone.hpp`: `<блок>_break` / `<блок>_continue`).
EdgeKind edgeKindOfLabel(std::string_view label) {
    if (JumpTargets::isBreakLabel(label)) {
        return EdgeKind::Break;
    }
    if (JumpTargets::isContinueLabel(label)) {
        return EdgeKind::Continue;
    }
    FAULT("unexpected goto label '{}' (only break/continue labels are produced by lowering)", label);
}

void Builder::handleSemicolon(const AstNodePtr& node, BuildCtx&) {
    // HIR: выражение в позиции оператора (точка с запятой уже явная) - линеаризуем выражение.
    const auto& semicolon = *node->as<SemicolonStmt>();
    if (semicolon.m_expr) {
        appendExpr(*semicolon.m_expr);
    }
}

void Builder::handleContract(const AstNodePtr& node, BuildCtx&) {
    // Контракт — наблюдаемое действие (проверка условия), поэтому он элемент CFG, а не
    // прозрачный узел: верификатору нужен порядок относительно операторов, flow-анализам —
    // чтения мест внутри выражения условия.
    const auto& contract = *node->as<TrustContract>();
    const MirOp kind = contract.kind == PropertyKind::Invariant ? MirOp::Invariant : MirOp::Assert;
    MirInstruction instruction{.kind = kind, .site = node.get()};
    if (kind == MirOp::Invariant) {
        const auto owner = m_invariantOwners.find(node.get());
        instruction.loopNode = owner != m_invariantOwners.end() ? owner->second : nullptr;
    }
    emitInstruction(std::move(instruction));
    if (contract.m_expr) {
        appendExpr(*contract.m_expr);
    }
}

void Builder::handleLabel(const AstNodePtr& node, BuildCtx&) {
    const auto& label = *node->as<LabelRef>();
    EXPECT(!label.text().empty() && "HIR goto label must have a name");
    const std::size_t block = ensureCurrent();
    const bool inserted = m_labels.emplace(std::string(label.text()), LabelSite{.block = block, .scopes = m_scopeStack}).second;
    EXPECT(inserted && "duplicate goto label in the HIR body");
}

void Builder::handleGoto(const AstNodePtr& node, BuildCtx&) {
    const auto& label = *node->as<LabelRef>();
    const std::size_t block = terminate(TerminatorKind::Goto, node.get());
    m_pendingGotos.push_back(PendingGoto{.block = block, .label = std::string(label.text()), .scopes = m_scopeStack, .site = node.get()});
}

void Builder::resolveGotos() {
    // Метки forward-объявлены lowering'ом (в конце тела/блока), поэтому переходы
    // разрешаются после обхода: снимки стека скоупов уже известны для обеих точек.
    for (const PendingGoto& pending : m_pendingGotos) {
        const auto it = m_labels.find(pending.label);
        EXPECT(it != m_labels.end() && "goto target label is not defined in the HIR body");
        const std::vector<const AstNodeBase*>& targetScopes = it->second.scopes;
        std::size_t common = 0;
        while (common < pending.scopes.size() && common < targetScopes.size() && pending.scopes[common] == targetScopes[common]) {
            ++common;
        }
        // Выход из скоупов — LIFO; скоупы, общие с меткой, не закрываются (переход внутри них).
        for (std::size_t i = pending.scopes.size(); i > common; --i) {
            const AstNodeBase* scope = pending.scopes[i - 1];
            m_cfg.blocks[pending.block].elements.push_back(MirInstruction{.kind = MirOp::ScopeExit, .place = scope, .site = pending.site, .scope = scope});
        }
        addEdge(pending.block, it->second.block, edgeKindOfLabel(pending.label));
    }
}

std::size_t Builder::breakTarget(const BuildCtx& ctx) const {
    if (!ctx.loopBreak) {
        FAULT("break outside a loop");
    }
    return *ctx.loopBreak;
}

std::size_t Builder::continueTarget(const BuildCtx& ctx) const {
    if (!ctx.loopContinue) {
        FAULT("continue outside a loop");
    }
    return *ctx.loopContinue;
}

void Builder::buildIf(const IfStmt& stmt, BuildCtx& ctx) {
    const std::size_t join = newBlock();

    appendExpr(*stmt.m_cond);
    const std::size_t condBlock = terminate(TerminatorKind::Conditional, &stmt);

    const std::size_t thenEntry = newBlock();
    addEdge(condBlock, thenEntry, EdgeKind::True);

    std::optional<std::size_t> falseEntry;
    if (!stmt.m_elseifs.empty() || stmt.m_else) {
        falseEntry = newBlock();
        addEdge(condBlock, *falseEntry, EdgeKind::False);
    } else {
        addEdge(condBlock, join, EdgeKind::False);
    }

    m_current = thenEntry;
    buildBody(stmt.m_body, ctx);
    finishPath(join);

    if (!stmt.m_elseifs.empty()) {
        m_current = *falseEntry;
        for (std::size_t i = 0; i < stmt.m_elseifs.size(); ++i) {
            const AstNodePtr& cond = stmt.m_elseifs[i].first;
            const AstNodePtr& body = stmt.m_elseifs[i].second;

            appendExpr(*cond);
            const std::size_t eifCond = terminate(TerminatorKind::Conditional, cond.get());

            const std::size_t eifThen = newBlock();
            addEdge(eifCond, eifThen, EdgeKind::True);

            const bool hasNext = i + 1 < stmt.m_elseifs.size();
            if (hasNext || stmt.m_else) {
                const std::size_t next = newBlock();
                addEdge(eifCond, next, EdgeKind::False);
                m_current = eifThen;
                buildBody(body, ctx);
                finishPath(join);
                m_current = next;
            } else {
                addEdge(eifCond, join, EdgeKind::False);
                m_current = eifThen;
                buildBody(body, ctx);
                finishPath(join);
            }
        }
        if (stmt.m_else) {
            buildBody(stmt.m_else, ctx);
            finishPath(join);
        }
    } else if (stmt.m_else) {
        m_current = *falseEntry;
        buildBody(stmt.m_else, ctx);
        finishPath(join);
    }

    m_current = join;
}

void Builder::buildWhile(const WhileStmt& stmt, BuildCtx& ctx) {
    const std::size_t condBlock = newBlock();
    const std::size_t bodyBlock = newBlock();
    const std::size_t exitBlock = newBlock();

    finishPath(condBlock);

    m_current = condBlock;
    appendExpr(*stmt.m_cond);
    const std::size_t condEnd = terminate(TerminatorKind::Conditional, &stmt);
    addEdge(condEnd, bodyBlock, EdgeKind::True);

    // В HIR именованный continue уже развёрнут в goto на метку `<блок>_continue`,
    // поэтому привязка pending-цели здесь не нужна.

    if (stmt.m_else) {
        const std::size_t elseEntry = newBlock();
        addEdge(condEnd, elseEntry, EdgeKind::False);
        const auto savedBreak = ctx.loopBreak;
        const auto savedContinue = ctx.loopContinue;
        ctx.loopBreak.reset();
        ctx.loopContinue.reset();
        m_current = elseEntry;
        buildBody(stmt.m_else, ctx);
        finishPath(exitBlock);
        ctx.loopBreak = savedBreak;
        ctx.loopContinue = savedContinue;
    } else {
        addEdge(condEnd, exitBlock, EdgeKind::False);
    }

    const auto savedBreak = ctx.loopBreak;
    const auto savedContinue = ctx.loopContinue;
    const std::size_t savedLoopDepth = ctx.loopScopeDepth;
    ctx.loopBreak = exitBlock;
    ctx.loopContinue = condBlock;
    ctx.loopScopeDepth = m_scopeStack.size();

    m_current = bodyBlock;
    buildBody(stmt.m_body, ctx);
    finishPath(condBlock);

    ctx.loopBreak = savedBreak;
    ctx.loopContinue = savedContinue;
    ctx.loopScopeDepth = savedLoopDepth;
    m_current = exitBlock;
}

void Builder::buildDoWhile(const DoWhileStmt& stmt, BuildCtx& ctx) {
    const std::size_t bodyBlock = newBlock();
    const std::size_t condBlock = newBlock();
    const std::size_t exitBlock = newBlock();

    finishPath(bodyBlock);

    const auto savedBreak = ctx.loopBreak;
    const auto savedContinue = ctx.loopContinue;
    const std::size_t savedLoopDepth = ctx.loopScopeDepth;
    ctx.loopBreak = exitBlock;
    ctx.loopContinue = condBlock;
    ctx.loopScopeDepth = m_scopeStack.size();

    m_current = bodyBlock;
    buildBody(stmt.m_body, ctx);
    finishPath(condBlock);

    m_current = condBlock;
    appendExpr(*stmt.m_cond);
    const std::size_t condEnd = terminate(TerminatorKind::Conditional, &stmt);
    addEdge(condEnd, bodyBlock, EdgeKind::True);
    addEdge(condEnd, exitBlock, EdgeKind::False);

    ctx.loopBreak = savedBreak;
    ctx.loopContinue = savedContinue;
    ctx.loopScopeDepth = savedLoopDepth;
    m_current = exitBlock;
}

void Builder::buildMatch(const MatchStmt& stmt, BuildCtx& ctx) {
    const std::size_t join = newBlock();

    if (stmt.m_tempDecl) {
        appendNode(*stmt.m_tempDecl);
    }
    if (stmt.m_value) {
        appendExpr(*stmt.m_value);
    }
    const std::size_t switchBlock = ensureCurrent();
    setTerminator(switchBlock, TerminatorKind::Switch, &stmt);
    m_current.reset();

    for (const MatchStmt::MatchCase& matchCase : stmt.m_cases) {
        const std::size_t caseEntry = newBlock();
        addEdge(switchBlock, caseEntry, EdgeKind::Normal);
        m_current = caseEntry;
        for (const AstNodePtr& pattern : matchCase.patterns) {
            if (pattern) {
                appendExpr(*pattern);
            }
        }
        buildBody(matchCase.body, ctx);
        finishPath(join);
    }

    if (stmt.m_default) {
        const std::size_t defaultEntry = newBlock();
        addEdge(switchBlock, defaultEntry, EdgeKind::Normal);
        m_current = defaultEntry;
        buildBody(stmt.m_default, ctx);
        finishPath(join);
    } else {
        // Неисчерпывающий match: путь "ни одна ветка не совпала" идёт в join.
        addEdge(switchBlock, join, EdgeKind::Normal);
    }

    m_current = join;
}

void Builder::buildWith(const WithStmt& stmt, BuildCtx& ctx) {
    const std::size_t join = newBlock();
    const std::size_t dispatch = newBlock();
    finishPath(dispatch);

    m_current = dispatch;
    for (const auto& [lock, binding] : stmt.m_locks) {
        if (lock) {
            appendExpr(*lock);
        }
        if (binding) {
            appendNode(*binding);
        }
    }
    const std::size_t dispatchEnd = ensureCurrent();
    setTerminator(dispatchEnd, TerminatorKind::Dispatch, &stmt);
    m_current.reset();

    const std::size_t bodyEntry = newBlock();
    addEdge(dispatchEnd, bodyEntry, EdgeKind::Normal);

    // Исключение в инициализаторе биндинга: ветка else (или проброс наружу).
    if (stmt.m_else) {
        const std::size_t elseEntry = newBlock();
        addEdge(dispatchEnd, elseEntry, EdgeKind::Exception);
        m_current = elseEntry;
        buildBody(stmt.m_else, ctx);
        finishPath(join);
    } else {
        addEdge(dispatchEnd, ctx.throwTarget.value_or(m_cfg.exit), EdgeKind::Exception);
    }

    m_current = bodyEntry;
    buildBody(stmt.m_body, ctx);
    finishPath(join);
    m_current = join;
}

void Builder::buildTry(const TryCatchStmt& stmt, BuildCtx& ctx) {
    const std::size_t join = newBlock();
    const std::size_t dispatch = newBlock();
    finishPath(dispatch);
    m_current = dispatch;
    setTerminator(dispatch, TerminatorKind::Dispatch, &stmt);
    m_current.reset();

    const std::size_t tryEntry = newBlock();
    addEdge(dispatch, tryEntry, EdgeKind::Normal);

    std::vector<std::size_t> catchEntries;
    catchEntries.reserve(stmt.m_catches.size());
    for (std::size_t i = 0; i < stmt.m_catches.size(); ++i) {
        const std::size_t entry = newBlock();
        catchEntries.push_back(entry);
        addEdge(dispatch, entry, EdgeKind::Exception);
    }

    // Явный throw внутри try направляется в catch-dispatch (не в exit).
    std::optional<std::size_t> catchDispatch;
    if (!catchEntries.empty()) {
        catchDispatch = newBlock();
        setTerminator(*catchDispatch, TerminatorKind::Dispatch, &stmt);
        for (std::size_t entry : catchEntries) {
            addEdge(*catchDispatch, entry, EdgeKind::Exception);
        }
    } else {
        addEdge(dispatch, ctx.throwTarget.value_or(m_cfg.exit), EdgeKind::Exception);
    }

    BuildCtx tryCtx = ctx;
    tryCtx.throwTarget = catchDispatch ? catchDispatch : ctx.throwTarget;
    // throw выходит из всех скоупов, открытых внутри try (LIFO до границы try).
    tryCtx.throwScopeDepth = m_scopeStack.size();

    const std::size_t tryNormal = newBlock();
    m_current = tryEntry;
    buildStatements(stmt.m_body, tryCtx);
    finishPath(tryNormal);

    // Тела catch анализируются в ИСХОДНОМ контексте: throw из catch не ловится этим же try.
    for (std::size_t i = 0; i < stmt.m_catches.size(); ++i) {
        const CatchBlock& cb = *stmt.m_catches[i]->as<CatchBlock>();
        m_current = catchEntries[i];
        if (cb.m_binding) {
            appendNode(*cb.m_binding);
        }
        buildStatements(cb.m_body, ctx);
        finishPath(join);
    }

    // Нормальное завершение try: ветка else (если есть) либо сразу join.
    m_current = tryNormal;
    if (stmt.m_else) {
        buildBody(stmt.m_else, ctx);
    }
    finishPath(join);

    m_current = join;
}

void Builder::buildJump(const JumpStmt& stmt, BuildCtx& ctx) {
    if (stmt.m_tempDecl) {
        appendNode(*stmt.m_tempDecl);
    }
    (this->*jumpActions()[kindIndex(stmt.kind())])(stmt, ctx);
}

const Builder::JumpHandlerTable& Builder::jumpActions() {
    static const JumpHandlerTable table = [] {
        JumpHandlerTable t;
        t.fill(&Builder::handleUnknownJump);
        t[kindIndex(ParserToken::Kind::ReturnStmt)] = &Builder::handleReturnJump;
        t[kindIndex(ParserToken::Kind::ThrowStmt)] = &Builder::handleThrowJump;
        t[kindIndex(ParserToken::Kind::BreakStmt)] = &Builder::handleBreakJump;
        t[kindIndex(ParserToken::Kind::ContinueStmt)] = &Builder::handleContinueJump;
        return t;
    }();
    return table;
}

void Builder::handleReturnJump(const JumpStmt& stmt, BuildCtx&) {
    if (stmt.m_value) {
        appendExpr(*stmt.m_value);
    }
    emitScopeExits(0, &stmt);
    const std::size_t block = terminate(TerminatorKind::Return, &stmt);
    addEdge(block, m_cfg.exit, EdgeKind::Return);
}

void Builder::handleThrowJump(const JumpStmt& stmt, BuildCtx& ctx) {
    if (stmt.m_value) {
        appendExpr(*stmt.m_value);
    }
    emitScopeExits(ctx.throwScopeDepth, &stmt);
    const std::size_t block = terminate(TerminatorKind::Throw, &stmt);
    addEdge(block, ctx.throwTarget.value_or(m_cfg.exit), EdgeKind::Throw);
}

void Builder::handleBreakJump(const JumpStmt& stmt, BuildCtx& ctx) {
    // Именованный break понижен в goto; здесь остаётся только break ближайшего цикла.
    EXPECT(!stmt.m_label && "labeled break must be lowered to goto in HIR");
    emitScopeExits(ctx.loopScopeDepth, &stmt);
    const std::size_t block = terminate(TerminatorKind::Break, &stmt);
    addEdge(block, breakTarget(ctx), EdgeKind::Break);
}

void Builder::handleContinueJump(const JumpStmt& stmt, BuildCtx& ctx) {
    EXPECT(!stmt.m_label && "labeled continue must be lowered to goto in HIR");
    emitScopeExits(ctx.loopScopeDepth, &stmt);
    const std::size_t block = terminate(TerminatorKind::Continue, &stmt);
    addEdge(block, continueTarget(ctx), EdgeKind::Continue);
}

void Builder::handleUnknownJump(const JumpStmt& stmt, BuildCtx&) {
    FAULT("unexpected jump kind '{}' in CFG builder", ParserToken::name(stmt.kind()));
}

} // namespace

ControlFlowGraph buildCfg(const FuncDecl& func, const AttrPool& attrs) {
    return Builder(func, attrs).build();
}

namespace {

void collectFunctions(const AstNodePtr& node, std::vector<const FuncDecl*>& out) {
    if (!node) {
        return;
    }
    if (node->is<FuncDecl>()) {
        const auto& func = *node->as<FuncDecl>();
        if (func.m_body) {
            out.push_back(&func);
        }
    }
    for (const AstNodePtr& child : node->children()) {
        collectFunctions(child, out);
    }
}

} // namespace

MirModule buildMir(const std::vector<AstNodePtr>& roots, const AttrPool& attrs) {
    std::vector<const FuncDecl*> functions;
    for (const AstNodePtr& root : roots) {
        collectFunctions(root, functions);
    }
    MirModule module;
    module.roots = roots;
    module.functions.reserve(functions.size());
    for (const FuncDecl* func : functions) {
        module.functions.push_back(MirFunction{func, buildCfg(*func, attrs)});
    }
    return module;
}

std::string dumpCfg(const ControlFlowGraph& cfg) {
    std::string out;
    for (const FlowBlock& b : cfg.blocks) {
        out += std::format("block {}{}:\n", b.id, b.unreachable ? " (unreachable)" : "");
        for (const MirInstruction& element : b.elements) {
            out += std::format("  {} {}\n", mirOpName(element.kind), element.site ? ParserToken::name(element.site->kind()) : "-");
        }
        if (b.terminator != TerminatorKind::Fallthrough) {
            out += std::format("  term {}\n", static_cast<int>(b.terminator));
        }
        for (const FlowEdge& e : b.successors) {
            out += std::format("  -> {} ({})\n", e.target, static_cast<int>(e.kind));
        }
    }
    return out;
}

} // namespace trust::flow
