// src/ast/lowering.cpp
// Реализация понижения: каждый класс узла переопределяет AstNodeBase::lower(self, ctx)
// согласно своему Kind и понижает своих детей. Свободные функции lowerBody/lowerBodyNode/
// lowerNode - векторно-ориентированные помощники (оборачивают statement-выражения в SemicolonStmt,
// вставляют continue-метки перед первым циклом именованного блока, рекурсивно понижают детей).

#include "ast/lowering.hpp"

#include "ast/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "diag/context.hpp"
#include "syntax/term.h"
#include "types/registry.hpp"
#include "types/type_names.hpp"
#include "utils/strings.hpp"

#include <string>
#include <vector>

namespace trust {

// -- Вспомогательные --

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

bool isExprStatement(ParserToken::Kind k) noexcept {
    switch (k) {
    case ParserToken::Kind::NameDecl:
    case ParserToken::Kind::AssignOp:
    case ParserToken::Kind::AppendStmt:
    case ParserToken::Kind::MathOp:
    case ParserToken::Kind::BitwiseOp:
    case ParserToken::Kind::CompareOp:
    case ParserToken::Kind::LogicalOp:
    case ParserToken::Kind::MemberAccess:
    case ParserToken::Kind::ArrayAccess:
    case ParserToken::Kind::CallExpr:
    case ParserToken::Kind::IntLiteral:
    case ParserToken::Kind::FloatLiteral:
    case ParserToken::Kind::StrChar:
    case ParserToken::Kind::StrWide:
    case ParserToken::Kind::RationalLiteral:
        return true;
    default:
        return false;
    }
}

void appendLabel(AstNodePtr& bodyNode, const std::string& label) {
    if (!bodyNode) {
        return;
    }
    if (is_block_kind(bodyNode->kind())) {
        auto* seq = bodyNode->as_sequence();
        seq->m_body.push_back(std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, label));
    } else {
        auto seq = std::make_shared<Sequence>(ParserToken::Kind::sequence, std::string{});
        seq->m_body.push_back(std::move(bodyNode));
        seq->m_body.push_back(std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, label));
        bodyNode = std::move(seq);
    }
}

// -- Capture-проход «$^ = результат последней операции» (простой случай) --
// Пре-семантическое переписывание: [оператор-выражение E; sink с $^] =>
// [__trust_last_N := E; sink, где $^ -> Ident('__trust_last_N')]. Временная - сиблинг в том же
// скоупе; семантика (core.run) типизирует её из E как обычный VarDecl. Вложенные тела операторов
// рекурсия обрабатывает ОТДЕЛЬНО, поэтому «простой sink» через носитель скоупа не идёт, а поиск/
// замена `$^` такие поддеревья не пересекает (иначе $^ в теле ниже перехватил бы внешний источник).
namespace {

void captureNode(AstNodePtr& node, LowerCtx& ctx); // forward: определена ниже

bool captureIsLastResultLeaf(const AstNodePtr& n) noexcept {
    return n && n->kind() == ParserToken::Kind::Ident && n->text() == "$^";
}

bool captureIsScopeKind(ParserToken::Kind k) noexcept {
    switch (k) {
    case ParserToken::Kind::ScopeBlock:
    case ParserToken::Kind::sequence:
    case ParserToken::Kind::ModuleDecl:
    case ParserToken::Kind::FuncDecl:
    case ParserToken::Kind::IfStmt:
    case ParserToken::Kind::WhileStmt:
    case ParserToken::Kind::DoWhileStmt:
    case ParserToken::Kind::MatchingStmt:
    case ParserToken::Kind::WithStmt:
    case ParserToken::Kind::TryCatchStmt:
    case ParserToken::Kind::CatchBlock:
    case ParserToken::Kind::ClassDecl:
        return true;
    default:
        return false;
    }
}

bool captureSubtreeHasLastResult(const AstNodePtr& node) {
    if (!node) {
        return false;
    }
    if (captureIsLastResultLeaf(node)) {
        return true;
    }
    if (captureIsScopeKind(node->kind())) {
        return false; // вложенные тела - отдельным вызовом
    }
    std::vector<AstNodePtr*> slots;
    node->collectChildren(slots);
    for (auto* c : slots) {
        if (c && *c && captureSubtreeHasLastResult(*c)) {
            return true;
        }
    }
    return false;
}

void captureReplaceLastResult(AstNodePtr node, const std::string& name) {
    if (!node) {
        return;
    }
    if (node->kind() == ParserToken::Kind::Ident && node->text() == "$^") {
        static_cast<IdentName&>(*node).set_text(name);
        return;
    }
    if (captureIsScopeKind(node->kind())) {
        return;
    }
    std::vector<AstNodePtr*> slots;
    node->collectChildren(slots);
    for (auto* c : slots) {
        if (c && *c) {
            captureReplaceLastResult(*c, name);
        }
    }
}

// Категория неподдержанного `$^` (локальная; сообщение выводится В МОМЕНТ выявления в capture-проходе).
enum class CaptureDiagKind { NoSource, CompositeSource, NoValue };

// Сообщает точечную ошибку (по первому листу `$^`) и заменяет ВСЕ листы `$^` в поддереве простого
// sink-оператора (не пересекая вложенные тела-скоупы) на ErrorExpr-заглушки-владельцы: семантика НЕ
// увидит лист `$^` → не эмитит повторную диагностику, а заменённый узел не удаляется (хранится в
// ErrorExpr::m_original). ctx.ctx == nullptr (unit/ручные узлы) - только замена без сообщения.
void captureReportAndWrap(AstNodePtr node, CaptureDiagKind kind, LowerCtx& ctx) {
    if (!node) {
        return;
    }
    std::vector<AstNodePtr*> slots;
    node->collectChildren(slots);
    bool reported = false;
    for (auto* c : slots) {
        if (!c || !*c) {
            continue;
        }
        AstNodePtr& child = *c;
        if (child->kind() == ParserToken::Kind::Ident && child->text() == "$^") {
            if (!reported && ctx.ctx) {
                switch (kind) {
                case CaptureDiagKind::NoSource:
                    ctx.ctx->diag().report(Severity::Error, child->range(),
                                           "pseudo-variable '$^' (result of the last operation) has no preceding statement to "
                                           "capture; it must follow a value-producing statement");
                    break;
                case CaptureDiagKind::CompositeSource:
                    ctx.ctx->diag().report(Severity::Error, child->range(),
                                           "pseudo-variable '$^' (result of the last operation): capture from/after a composite "
                                           "statement (if/loop/match/try/block) is not implemented yet");
                    break;
                case CaptureDiagKind::NoValue:
                    ctx.ctx->diag().report(Severity::Error, child->range(),
                                           "pseudo-variable '$^' (result of the last operation): the "
                                           "preceding statement produces no value to capture");
                    break;
                }
                reported = true;
            }
            child = std::make_shared<ErrorExpr>(std::move(child)); // заглушка-владелец заменённого узла
            continue;
        }
        if (!captureIsScopeKind(child->kind())) {
            captureReportAndWrap(child, kind, ctx); // рекурсия внутрь выражения
        }
    }
}

} // namespace

void captureLastResult(std::vector<AstNodePtr>& body, LowerCtx& ctx) {
    // 1) Переписывание соседних пар в ЭТОМ списке операторов.
    for (size_t i = 1; i < body.size(); ++i) {
        AstNodePtr& prev = body[i - 1];
        AstNodePtr& cur = body[i];
        if (!prev || !cur) {
            continue;
        }
        const bool prevExpr = isExprStatement(prev->kind()); // голое выражение-оператор
        if (!prevExpr && prev->kind() != ParserToken::Kind::VarDecl) {
            continue; // не источник значения (управление/составной/блок)
        }
        if (captureIsScopeKind(cur->kind())) { // sink - простой оператор (вложенные тела рекурсией)
            continue;
        }
        if (!captureSubtreeHasLastResult(cur)) {
            continue;
        }
        if (prev->kind() == ParserToken::Kind::VarDecl) {
            // Источник = декларация `x := E`: её результат - значение `x`, которое уже объявлено и
            // живёт дальше, поэтому временная НЕ нужна - переписываем `$^` в sink прямо на `x`.
            const std::string held = std::string(prev->text());
            if (held.empty() || held == "_" || held == "$_") {
                continue; // безымянный/сброс (`_`) - имя не связать, оставить (guard-диагностика)
            }
            captureReplaceLastResult(cur, held);
            continue;
        }
        // Источник = голое выражение-оператор E; (значение теряется) - нужна синтетическая временная.
        const std::string tmpName = "__trust_last_" + std::to_string(++ctx.lastResultCounter);
        auto temp = std::make_shared<VarDecl>(tmpName, nullptr, std::move(prev));
        temp->m_lastResultTemp = true; // синтетическая временная `$^` (для void/no-value диагностики)
        prev = std::move(temp);
        captureReplaceLastResult(cur, tmpName);
    }
    // 1b) НЕподдержанные обращения `$^` в ЭТОМ списке (переписанные sink-и уже без листа `$^`, остатки -
    //     неподдержанные): сообщаем точечную ошибку В МОМЕНТ выявления (здесь известен предыдущий
    //     сиблинг) и заменяем лист `$^` на ErrorExpr-заглушку (семантика не увидит `$^` → без дубля).
    for (size_t i = 0; i < body.size(); ++i) {
        AstNodePtr& s = body[i];
        if (!s) {
            continue;
        }
        if (captureIsScopeKind(s->kind())) {
            continue; // составной sink/вложенное тело обрабатывается рекурсией/страховкой
        }
        if (!captureSubtreeHasLastResult(s)) {
            continue;
        }
        CaptureDiagKind kind = CaptureDiagKind::NoSource;
        if (i != 0) {
            kind = captureIsScopeKind(body[i - 1]->kind()) ? CaptureDiagKind::CompositeSource : CaptureDiagKind::NoValue;
        }
        captureReportAndWrap(s, kind, ctx);
    }
    // 2) Рекурсивно вниз по вложенным телам-скоупам.
    for (auto& child : body) {
        if (child) {
            captureNode(child, ctx);
        }
    }
}

namespace {

void captureNode(AstNodePtr& node, LowerCtx& ctx) {
    if (!node) {
        return;
    }
    const ParserToken::Kind k = node->kind();
    if (is_block_kind(k)) { // блок (ScopeBlock/sequence/ModuleNode/...): m_body - список операторов
        captureLastResult(node->as_sequence()->m_body, ctx);
        return;
    }
    switch (k) {
    case ParserToken::Kind::FuncDecl: {
        auto& f = static_cast<FuncDecl&>(*node);
        if (f.m_body) {
            captureLastResult(*f.m_body, ctx);
        }
        return;
    }
    case ParserToken::Kind::IfStmt:
    case ParserToken::Kind::WhileStmt:
    case ParserToken::Kind::DoWhileStmt: {
        auto& cf = static_cast<ControlFlowStmt&>(*node);
        if (cf.m_body) {
            captureNode(cf.m_body, ctx);
        }
        if (cf.m_else) {
            captureNode(cf.m_else, ctx);
        }
        if (k == ParserToken::Kind::IfStmt) {
            for (auto& [cond, b] : static_cast<IfStmt&>(*node).m_elseifs) {
                (void)cond;
                if (b) {
                    captureNode(b, ctx);
                }
            }
        }
        return;
    }
    case ParserToken::Kind::MatchingStmt: {
        auto& m = static_cast<MatchStmt&>(*node);
        for (auto& c : m.m_cases) {
            if (c.body) {
                captureNode(c.body, ctx);
            }
        }
        if (m.m_default) {
            captureNode(m.m_default, ctx);
        }
        return;
    }
    case ParserToken::Kind::WithStmt: {
        auto& w = static_cast<WithStmt&>(*node);
        if (w.m_body) {
            captureNode(w.m_body, ctx);
        }
        if (w.m_else) {
            captureNode(w.m_else, ctx);
        }
        return;
    }
    case ParserToken::Kind::TryCatchStmt: {
        auto& t = static_cast<TryCatchStmt&>(*node);
        captureLastResult(t.m_body, ctx); // тело try
        for (auto& cb : t.m_catches) {
            captureNode(cb, ctx);
        }
        if (t.m_else) {
            captureNode(t.m_else, ctx);
        }
        return;
    }
    default:
        return; // прочие операторы/выражения списков операторов не содержат
    }
}

} // namespace

void lowerNode(AstNodePtr& node, LowerCtx& ctx) {
    if (node) {
        node->lower(node, ctx);
    }
}

void lowerBody(std::vector<AstNodePtr>& body, LowerCtx& ctx) {
    std::vector<AstNodePtr> out;
    out.reserve(body.size() * 2 + 1);
    for (auto& child : body) {
        if (!child) {
            continue;
        }
        // continue-метка именованного блока: первый цикл в теле (по DFS) потребляет её.
        if (!ctx.pendingContinue.empty()) {
            if (child->kind() == ParserToken::Kind::WhileStmt) {
                // while: метка перед циклом - goto переоценивает условие, не повторяя init.
                out.push_back(std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, ctx.pendingContinue));
                ctx.pendingContinue.clear();
            } else if (child->kind() == ParserToken::Kind::DoWhileStmt) {
                // do-while: метка в КОНЕЦ тела (перед '}') - goto переходит к проверке условия.
                appendLabel(static_cast<DoWhileStmt*>(child.get())->m_body, ctx.pendingContinue);
                ctx.pendingContinue.clear();
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
    // Рекурсивно вниз (после вставки обёрток, чтобы pendingContinue учитывал новые узлы).
    for (auto& child : body) {
        if (child) {
            lowerNode(child, ctx);
        }
    }
}

void lowerBodyNode(AstNodePtr& bodyNode, LowerCtx& ctx) {
    if (!bodyNode) {
        return;
    }
    // Тело цикла/ветки - НЕ именованный блок: если это блок (Sequence/ScopeBlock/ModuleNode),
    // обрабатываем его m_body как операторы (без named-block меток); иначе - одиночный оператор.
    if (is_block_kind(bodyNode->kind())) {
        lowerBody(static_cast<Sequence*>(bodyNode.get())->m_body, ctx);
    } else {
        std::vector<AstNodePtr> one;
        one.push_back(std::move(bodyNode));
        lowerBody(one, ctx);
        bodyNode = std::move(one[0]);
    }
}

// -- Node lower: понижение согласно Kind каждого класса --

void AstNodeBase::lower(AstNodePtr&, LowerCtx&) {
}

void Sequence::lower(AstNodePtr&, LowerCtx& ctx) {
    // DictLiteral / Tuple / RangeExpr - это Sequence структурно, но их m_body - это операнды
    // (у кортежа/конструкции: [имя=значение, ...]; у диапазона: [start, stop, (step)]),
    // а НЕ список операторов. Поэтому НЕ оборачиваем детей в SemicolonStmt (иначе каждый
    // элемент получил бы ';'), а понижаем их рекурсивно как выражения.
    if (m_kind == ParserToken::Kind::DictLiteral || m_kind == ParserToken::Kind::Tuple || m_kind == ParserToken::Kind::RangeExpr ||
        m_kind == ParserToken::Kind::NativeRefMakeExpr || m_kind == ParserToken::Kind::NativeRefTakeExpr) {
        for (auto& child : m_body) {
            lowerNode(child, ctx);
        }
        return;
    }
    lowerBody(m_body, ctx);
}

void ScopeBlock::lower(AstNodePtr&, LowerCtx& ctx) {
    if (ctx.inFunction && !is_anonymous() && !is_hidden()) {
        const std::string clean = cleanLabelName(name());
        if (!clean.empty()) {
            // Метки именованного блока: break - после тела, continue - первый цикл в теле.
            const std::string saved = ctx.pendingContinue;
            ctx.pendingContinue = clean + "_continue";
            lowerBody(m_body, ctx);
            m_body.push_back(std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, clean + "_break"));
            ctx.pendingContinue = saved;
            return;
        }
    }
    lowerBody(m_body, ctx);
}

void ModuleNode::lower(AstNodePtr&, LowerCtx& ctx) {
    lowerBody(m_body, ctx);
}

void FuncDecl::lower(AstNodePtr&, LowerCtx& ctx) {
    // Тело функции - вектор операторов (m_body), range блока берётся из m_term->m_right.
    if (m_body) {
        const bool savedInFn = ctx.inFunction;
        const std::string savedFunc = ctx.funcName;
        ctx.inFunction = true;
        ctx.funcName = funcNameOf(this);
        lowerBody(*m_body, ctx);
        ctx.inFunction = savedInFn;
        ctx.funcName = savedFunc;
    }
}

void IfStmt::lower(AstNodePtr&, LowerCtx& ctx) {
    lowerNode(m_cond, ctx);
    lowerBodyNode(m_body, ctx);
    for (auto& [c, b] : m_elseifs) {
        lowerNode(c, ctx);
        lowerBodyNode(b, ctx);
    }
    lowerBodyNode(m_else, ctx);
}

void WhileStmt::lower(AstNodePtr&, LowerCtx& ctx) {
    // While-else: в C++ нет while...else - else эмулируется флагом «вошёл ли цикл хотя бы раз»
    // (`bool _weN := 0;`, затем `_weN = true;` в теле, `if (!_weN) { else }`). Флаг создаёт lowering
    // (инвариант «временные — уровень анализатора»), транспилятор его эмитит и читает имя.
    if (m_else) {
        const std::string flagName = "_we" + std::to_string(++ctx.whileElseCounter);
        auto flag = std::make_shared<VarDecl>(flagName, nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::string("0")));
        if (ctx.ctx) {
            flag->inferredType = ctx.ctx->types().getType(type::Bool); // bool-флаг
        }
        m_elseFlag = flag;
    }
    lowerNode(m_elseFlag, ctx);
    lowerNode(m_cond, ctx);
    lowerBodyNode(m_body, ctx);
    lowerBodyNode(m_else, ctx);
}

void DoWhileStmt::lower(AstNodePtr&, LowerCtx& ctx) {
    lowerBodyNode(m_body, ctx);
    lowerNode(m_cond, ctx);
}

void MatchStmt::lower(AstNodePtr&, LowerCtx& ctx) {
    // Scrutivee-временная (_matchN) создана СЕМАНТИКОЙ (m_tempDecl, инвариант «временные — уровень
    // анализатора») - здесь только понижаем её и значение (m_value = ссылка на временную _matchN).
    lowerNode(m_tempDecl, ctx);
    lowerNode(m_value, ctx);
    for (auto& c : m_cases) {
        for (auto& p : c.patterns) {
            lowerNode(p, ctx);
        }
        lowerBodyNode(c.body, ctx);
    }
    lowerBodyNode(m_default, ctx);
}

void WithStmt::lower(AstNodePtr&, LowerCtx& ctx) {
    // With-else: C++ не различает исключение в инициализаторе vs в теле. Флаг `bool _wN := 0;`
    // создаёт LOWERING (инвариант «временные — уровень анализатора»); транспилятор эмитит
    //   try { биндинги; _wN = true; тело } catch(...) { if(_wN) throw; else }
    // Флаг нужен ТОЛЬКО при наличии else (без else - простой блок, исключения пробрасываются).
    if (m_else) {
        const std::string flagName = "_w" + std::to_string(++ctx.withFailCounter);
        auto flag = std::make_shared<VarDecl>(flagName, nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::string("0")));
        if (ctx.ctx) {
            flag->inferredType = ctx.ctx->types().getType(type::Bool); // bool-флаг
        }
        m_failFlag = flag;
    }
    lowerNode(m_failFlag, ctx);

    // Захват ссылки (`*ref` / `*^ref` в паре (lock, binding)): LOWERING создаёт Locker-темп
    // `__cap_N` (тип trust::Locker не языковой → объявляется `auto`, транспилятор эмитит через
    // emitSyntheticVar) и для именованного биндинга переписывает его в ссылку на значение
    // (`T& name = *__cap_N` через RefLockDeref) - авторазыменов. Транспилятор только эмитит.
    //   (RefLockExpr, binding VarDecl)     → темп (auto, держится) + биндинг (T&, держится);
    //   (RefLockExpr, binding `_`)          → темп (auto, держится), значение не связывается;
    //   (RefLockExpr, nullptr)              → только RefLockExpr (временный лок, снимается сразу).
    //   (nullptr, binding VarDecl)          → обычный value-биндинг (без захвата).
    for (auto& [lock, binding] : m_locks) {
        lowerNode(lock, ctx);
        lowerNode(binding, ctx);
        if (!lock) {
            continue; // обычный value-биндинг (lock == nullptr)
        }
        Sequence& take = static_cast<Sequence&>(*lock);
        if (take.m_body.empty()) {
            continue;
        }
        // Признак read-only для `*^` живёт в атрибуте узла attr::ReadOnly (общий принцип;
        // его ставит convertAttrsToNode по суффиксу '^'), а не в text()/term() take.
        const bool immutable = ctx.ctx && take.has_attr(ctx.ctx->attrs(), attr::ReadOnly);
        AstNodePtr operand = std::move(take.m_body[0]);
        // RefLockExpr: <ref>.lock() / <ref>.lock_const() (источник захвата). Синтетический узел
        // без Term - read-only переносим в атрибут attr::ReadOnly (а не в text '*^'/'*').
        auto lock_expr = std::make_shared<Sequence>(ParserToken::Kind::RefLockExpr, "*");
        if (immutable) {
            if (auto readonly_id = ctx.ctx->attrs().lookup(attr::ReadOnly); readonly_id) {
                lock_expr->add_attr(readonly_id.value());
            }
        }
        lock_expr->m_body.push_back(std::move(operand));
        const std::string cap_name = "__cap_" + std::to_string(++ctx.withCapCounter);
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
        // Именованный биндинг: темп (auto, держится) + `T& name = *__cap_N;` (авторазыменов).
        auto deref = std::make_shared<Sequence>(ParserToken::Kind::RefLockDeref, "*");
        deref->m_body.push_back(std::make_shared<IdentName>(cap_name));
        vd.m_initializer = std::move(deref);
        if (ctx.ctx && vd.inferredType != INVALID_TYPE_ID) {
            vd.inferredType = ctx.ctx->types().applyRefType(vd.inferredType, RefType::kRef);
        }
        lock = std::move(cap);
    }

    lowerBodyNode(m_body, ctx);
    lowerBodyNode(m_else, ctx);
}

void DestructureDecl::lower(AstNodePtr&, LowerCtx& ctx) {
    // Временная копия источника spread-деструктуризации (кроме mutating-rest) — создаёт lowering
    // (инвариант «временные — уровень анализатора»): `auto _trust_dst_N := <m_source>;`. При
    // mutating-rest (`item, dict... := ... dict`) попа прямо в источник, temp не нужна. Источник
    // остаётся в m_source (temp делит узел как инициализатор); транспилятор эмитит temp (auto) и
    // читает её имя.
    if (m_isSpread && m_source) {
        std::string srcCpp;
        if (m_source->kind() == ParserToken::Kind::Ident) {
            srcCpp = utils::name_to_cpp(m_source->text());
        }
        std::string restCpp;
        for (size_t i = 0; i < m_targets.size(); ++i) {
            auto* t = m_targets[i].get();
            if (i < m_targetIsRest.size() && m_targetIsRest[i] && t && t->kind() == ParserToken::Kind::Ident && t->text() != "_") {
                restCpp = utils::name_to_cpp(t->text());
            }
        }
        const bool mutatingRest = !restCpp.empty() && restCpp == srcCpp;
        if (!mutatingRest) {
            const std::string tmpName = "__trust_dst_" + std::to_string(ctx.destructureCounter++);
            m_sourceTemp = std::make_shared<VarDecl>(tmpName, nullptr, m_source);
        }
    }
}

void TryCatchStmt::lower(AstNodePtr&, LowerCtx& ctx) {
    // Тело try (m_body) - как у обычного Sequence (оборачивает statement-выражения в SemicolonStmt).
    lowerBody(m_body, ctx);
    // Ветки catch: каждая - CatchBlock (Sequence) → lowerBody её m_body; m_binding не понижается
    // (это не оператор, а связанная переменная). Иначе expression-операторы в телах catch не
    // получили бы ';' (visit_<CatchBlock> наследует Sequence::lower, который видит только m_body).
    for (auto& cb : m_catches) {
        lowerNode(cb, ctx);
    }
    lowerBodyNode(m_else, ctx);
}

void JumpStmt::lower(AstNodePtr& self, LowerCtx& ctx) {
    // Именованные break/continue → goto; break по имени функции → void-return.
    if (kind() == ParserToken::Kind::BreakStmt || kind() == ParserToken::Kind::ContinueStmt) {
        if (m_label) {
            const std::string clean = cleanLabelName(m_label->text());
            if (kind() == ParserToken::Kind::BreakStmt) {
                if (ctx.inFunction && !ctx.funcName.empty() && clean == ctx.funcName) {
                    // Синтетический узел без Term (невалидный range, без source-map).
                    // Метка СОХРАНЯЕТСЯ: `break <имя_функции>` = void-return, а неименованное
                    // `++ _ ++` (label==nullptr) - «положительное прерывание» (throw IntPlus).
                    auto r = std::make_shared<JumpStmt>(ParserToken::Kind::ReturnStmt);
                    r->m_label = std::move(m_label);
                    self = std::move(r);
                } else {
                    self = std::make_shared<LabelRef>(ParserToken::Kind::GotoStmt, clean + "_break");
                }
            } else {
                self = std::make_shared<LabelRef>(ParserToken::Kind::GotoStmt, clean + "_continue");
            }
        }
        return;
    }
    // return/throw: понижаем значение. Синтезированную временную возврата (__trust_res_N) тоже
    // понижаем (её инициализатор - исходное выражение значения; m_value - ссылка на неё).
    lowerNode(m_tempDecl, ctx);
    lowerNode(m_value, ctx);
}

void VarDecl::lower(AstNodePtr&, LowerCtx& ctx) {
    lowerNode(m_initializer, ctx);
    lowerNode(m_type, ctx);
}

void Binary::lower(AstNodePtr&, LowerCtx& ctx) {
    lowerNode(m_left, ctx);
    lowerNode(m_right, ctx);
}

void CallExpr::lower(AstNodePtr&, LowerCtx& ctx) {
    lowerNode(m_callee, ctx);
    if (m_args) {
        for (auto& a : *m_args) {
            lowerNode(a, ctx);
        }
    }
}

} // namespace trust
