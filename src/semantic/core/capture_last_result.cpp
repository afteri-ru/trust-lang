#include "semantic/core/capture_last_result.hpp"

#include "ast/ident_name.hpp"
#include "ast/token.hpp"
#include "session/context.hpp"
#include "utils/partial_switch.hpp"

#include <string>
#include <vector>

namespace trust {

namespace {

void captureNode(AstNodePtr& node, CaptureLastResultContext& ctx); // forward

bool captureIsLastResultLeaf(const AstNodePtr& node) noexcept {
    return node && node->kind() == ParserToken::Kind::Ident && requiredText(*node) == "$^";
}

bool captureIsScopeKind(ParserToken::Kind kind) noexcept {
    TRUST_PARTIAL_ENUM_SWITCH_BEGIN
    switch (kind) {
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
    case ParserToken::Kind::StructDecl:
        return true;
    default:
        return false;
    }
    TRUST_PARTIAL_ENUM_SWITCH_END
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
    for (auto* child : slots) {
        if (child && *child && captureSubtreeHasLastResult(*child)) {
            return true;
        }
    }
    return false;
}

void captureReplaceLastResult(AstNodePtr node, const std::string& name) {
    if (!node) {
        return;
    }
    if (node->kind() == ParserToken::Kind::Ident && node->as_text() && node->as_text()->text() == "$^") {
        node->as<IdentName>()->set_text_interned(name);
        return;
    }
    if (captureIsScopeKind(node->kind())) {
        return;
    }
    std::vector<AstNodePtr*> slots;
    node->collectChildren(slots);
    for (auto* child : slots) {
        if (child && *child) {
            captureReplaceLastResult(*child, name);
        }
    }
}

// Категория неподдержанного `$^` (сообщение выводится В МОМЕНТ выявления).
enum class CaptureDiagKind { NoSource, CompositeSource, NoValue };

// Сообщает точечную ошибку (по первому листу `$^`) и заменяет ВСЕ листы `$^` в поддереве простого
// sink-оператора (не пересекая вложенные тела-скоупы) на ErrorExpr-заглушки-владельцы.
void captureReportAndWrap(AstNodePtr node, CaptureDiagKind kind, CaptureLastResultContext& ctx) {
    if (!node) {
        return;
    }
    std::vector<AstNodePtr*> slots;
    node->collectChildren(slots);
    bool reported = false;
    for (auto* childSlot : slots) {
        if (!childSlot || !*childSlot) {
            continue;
        }
        AstNodePtr& child = *childSlot;
        if (child->kind() == ParserToken::Kind::Ident && requiredText(*child) == "$^") {
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

void captureLastResult(std::vector<AstNodePtr>& body, CaptureLastResultContext& ctx) {
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
            // Источник = декларация `x := E`: результат - значение `x`, временная НЕ нужна.
            const std::string held = std::string(requiredText(*prev));
            if (held.empty() || held == "_" || held == "$_") {
                continue;
            }
            captureReplaceLastResult(cur, held);
            continue;
        }
        // Источник = голое выражение-оператор E; - синтетическая временная.
        const std::string tmpName = "__trust_last_" + std::to_string(++ctx.lastResultCounter);
        auto temp = std::make_shared<VarDecl>(tmpName, nullptr, std::move(prev));
        temp->m_lastResultTemp = true;
        prev = std::move(temp);
        captureReplaceLastResult(cur, tmpName);
    }
    // 1b) НЕподдержанные обращения `$^` в ЭТОМ списке: точечная ошибка + ErrorExpr-заглушка.
    for (size_t i = 0; i < body.size(); ++i) {
        AstNodePtr& stmt = body[i];
        if (!stmt) {
            continue;
        }
        if (captureIsScopeKind(stmt->kind())) {
            continue;
        }
        if (!captureSubtreeHasLastResult(stmt)) {
            continue;
        }
        CaptureDiagKind kind = CaptureDiagKind::NoSource;
        if (i != 0) {
            kind = captureIsScopeKind(body[i - 1]->kind()) ? CaptureDiagKind::CompositeSource : CaptureDiagKind::NoValue;
        }
        captureReportAndWrap(stmt, kind, ctx);
    }
    // 2) Рекурсивно вниз по вложенным телам-скоупам.
    for (auto& child : body) {
        if (child) {
            captureNode(child, ctx);
        }
    }
}

namespace {

void captureNode(AstNodePtr& node, CaptureLastResultContext& ctx) {
    if (!node) {
        return;
    }
    const ParserToken::Kind kind = node->kind();
    if (is_block_kind(kind)) { // блок: m_body - список операторов
        captureLastResult(node->as_sequence()->m_body, ctx);
        return;
    }
    TRUST_PARTIAL_ENUM_SWITCH_BEGIN
    switch (kind) {
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
        if (kind == ParserToken::Kind::IfStmt) {
            for (auto& [cond, body] : static_cast<IfStmt&>(*node).m_elseifs) {
                (void)cond;
                if (body) {
                    captureNode(body, ctx);
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
    TRUST_PARTIAL_ENUM_SWITCH_END
}

} // namespace

} // namespace trust
