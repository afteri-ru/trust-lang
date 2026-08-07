#include "semantic/macro_expander.hpp"

#include "ast/ident_name.hpp"
#include "ast/token.hpp"
#include "utils/strings.hpp"

namespace trust {

ContextMacroExpander::ContextMacroExpander(AnalysisContext& actx)
: m_actx(actx) {
}

// ── Мутирующий обход узла ──

bool ContextMacroExpander::onNode(AstNodePtr& node) {
    if (!node) {
        return false;
    }
    switch (node->kind()) {
    case ParserToken::Kind::ContextMacro:
        expandContextMacro(node);
        return true; // узел заменён (Literal/IdentName) — ядро его не резолвит как имя
    case ParserToken::Kind::Ident:
        // Квалификатор @:: foo уже свёрнут в текст идентификатора ("@::foo") — раскрываем
        // текстовой заменой на текущую область имён; затем имя резолвит ядро.
        expandQualifierName(node);
        return false;
    case ParserToken::Kind::VarDecl:
    case ParserToken::Kind::FuncDecl:
        // Раскрытие @:: в имени объявления до регистрации (имя объявления — в text()).
        expandDeclName(node);
        return false;
    case ParserToken::Kind::TypeDecl: {
        auto& left = static_cast<Binary&>(*node).m_left;
        if (left && left->kind() == ParserToken::Kind::Ident) {
            expandQualifierName(left);
        }
        return false;
    }
    case ParserToken::Kind::ReturnStmt:
    case ParserToken::Kind::ThrowStmt:
    case ParserToken::Kind::BreakStmt:
    case ParserToken::Kind::ContinueStmt:
        // Метка (не переменная) — раскрываем @__FUNCTION__/@::/@__FUNCDNAME__.
        expandLabel(static_cast<JumpStmt&>(*node).m_label);
        return false;
    default:
        return false;
    }
}

// ── Раскрытие контекст-макросов ──

void ContextMacroExpander::expandContextMacro(AstNodePtr& self) {
    auto& cm = static_cast<ContextMacro&>(*self);
    std::string text(cm.text());

    // Снять ведущие маркеры стрингификации (@#/@#'/@#"), их может быть несколько.
    bool stringified = false;
    while (text.rfind("@#", 0) == 0) {
        if (text.size() >= 3 && text[2] == '\'') {
            text.erase(0, 3);
        } else if (text.size() >= 3 && text[2] == '"') {
            text.erase(0, 3);
        } else {
            text.erase(0, 2);
        }
        stringified = true;
    }

    // @__FUNCSIG__ — всегда строковый литерал (сигнатура).
    if (text == "@__FUNCSIG__") {
        if (m_actx.requireFunction(cm, "@__FUNCSIG__")) {
            self = std::make_shared<Literal>(ParserToken::Kind::StrChar, m_actx.currentFunc()->signature(m_actx.namespacePath()));
        }
        return;
    }

    if (stringified) {
        // Стрингификация: маркер превращает имя-аналог в литерал.
        std::string value;
        if (text == "@__NAMESPACE__" || text == "@::") {
            value = m_actx.namespaceFull();
        } else if (text == "@__FUNCTION__") {
            if (m_actx.requireFunction(cm, "@__FUNCTION__")) {
                value = m_actx.funcShortName();
            }
        } else if (text == "@__FUNCDNAME__") {
            if (m_actx.requireFunction(cm, "@__FUNCDNAME__")) {
                value = utils::name_to_cpp(m_actx.qualifiedFuncName());
            }
        } else {
            value = std::string(cm.text()); // неизвестный контекст-макрос
        }
        self = std::make_shared<Literal>(ParserToken::Kind::StrChar, value);
        return;
    }

    // Без стрингификации — имя-аналог (NAME).
    if (text == "@__NAMESPACE__" || text == "@::") {
        self = std::make_shared<IdentName>(m_actx.namespacePath());
    } else if (text == "@__FUNCTION__") {
        if (m_actx.requireFunction(cm, "@__FUNCTION__")) {
            self = std::make_shared<IdentName>(m_actx.funcShortName());
        }
    } else if (text == "@__FUNCDNAME__") {
        if (m_actx.requireFunction(cm, "@__FUNCDNAME__")) {
            self = std::make_shared<IdentName>(utils::name_to_cpp(m_actx.qualifiedFuncName()));
        }
    } else {
        // Прочие MACRO_CONTEXT ($::, @$$ и т.п.) — не раскрываем, оставляем как имя.
        self = std::make_shared<IdentName>(text);
    }
}

void ContextMacroExpander::expandQualifierName(AstNodePtr& self) {
    if (!self || self->kind() != ParserToken::Kind::Ident) {
        return;
    }
    static_cast<IdentName&>(*self).expandQualified(m_actx.namespacePath());
}

void ContextMacroExpander::expandDeclName(AstNodePtr& self) {
    if (!self) {
        return;
    }
    // VarDecl/FuncDecl — потомки IdentName (имя в text()).
    if (self->kind() == ParserToken::Kind::VarDecl || self->kind() == ParserToken::Kind::FuncDecl) {
        static_cast<IdentName&>(*self).expandQualified(m_actx.namespacePath());
    }
}

void ContextMacroExpander::expandLabel(AstNodePtr& self) {
    if (!self) {
        return;
    }
    std::string t(self->text());
    // Квалифицированная метка вида `func::` (завершающий "::").
    bool qualified = false;
    if (t.size() >= 2 && t.compare(t.size() - 2, 2, "::") == 0) {
        qualified = true;
        t = t.substr(0, t.size() - 2);
    }
    // Снять маркеры стрингификации (для единообразия; метки обычно без них).
    while (t.rfind("@#", 0) == 0) {
        if (t.size() >= 3 && (t[2] == '\'' || t[2] == '"')) {
            t.erase(0, 3);
        } else {
            t.erase(0, 2);
        }
    }

    std::string expanded;
    if (t == "@__FUNCTION__") {
        if (!m_actx.requireFunction(*self, "@__FUNCTION__")) {
            return;
        }
        expanded = m_actx.funcShortName();
    } else if (t == "@__NAMESPACE__" || t == "@::") {
        expanded = m_actx.namespacePath();
    } else if (t == "@__FUNCDNAME__") {
        if (!m_actx.requireFunction(*self, "@__FUNCDNAME__")) {
            return;
        }
        expanded = utils::name_to_cpp(m_actx.qualifiedFuncName());
    } else {
        return; // не контекст-макрос — оставляем как есть
    }

    if (qualified) {
        expanded += "::";
    }
    self = std::make_shared<IdentName>(expanded);
}

} // namespace trust
