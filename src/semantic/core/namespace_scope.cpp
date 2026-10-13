// src/semantic/core/namespace_scope.cpp
// Текущая позиция в скоуп-стеке: namespace-путь (с кэшем), функция/класс, признаки скоупа.
#include "semantic/core/namespace_scope.hpp"

#include "symbols/scope_path.hpp"
#include "session/context.hpp"
#include "ast/ast_nodes.hpp"
#include "diag/diag.hpp"
#include "utils/strings.hpp"

#include <utility>

namespace trust {

NamespaceScope::NamespaceScope(Context& ctx, const SymbolTable& symbols)
: m_ctx(ctx)
, m_symbols(symbols) {
}

// -- Контекст области имён и текущей функции (из скоуп-стека SymbolTable) --

std::string NamespaceScope::namespacePath() const {
    // Конфигурация стека однозначно задаётся (depth, creator): узел-создатель скоупа
    // посещается в обходе ОДИН раз, поэтому повторный вход с теми же координатами = тот же путь.
    const std::size_t depth = m_symbols.depth();
    const AstNodeBase* creator = m_symbols.currentCreator();
    if (m_pathDepth == depth && m_pathCreator == creator) {
        return m_path;
    }
    // Сегменты каждой области имён собираются по скоупам (внутри наружу), затем
    // последовательность скоупов разворачивается: путь - от внешней к внутренней.
    std::vector<std::vector<std::string>> scopeSegs;
    m_symbols.forEachScope([&](const Scope& s) {
        // Класс (скоуп с creator=ClassDecl, см. analyzeClassDecl): имя класса - сегмент области
        // имён, чтобы `@::`/`@__NAMESPACE__` внутри класса раскрывались в `ns::Class`, а статический
        // член `@::field` → `ns::Class::field` (затем регистрируется как статическая переменная).
        if (s.creator && s.creator->kind() == ParserToken::Kind::ClassDecl) {
            scopeSegs.push_back({std::string(static_cast<const ClassDecl*>(s.creator)->text())});
            return;
        }
        if (!s.creator || s.creator->kind() != ParserToken::Kind::ScopeBlock) {
            return;
        }
        const auto& sb = static_cast<const ScopeBlock&>(*s.creator);
        const std::string_view text = sb.text();
        const bool isCodeBlock = text.empty() || text == "{";
        const bool isGlobalNs = text == "::";
        const bool isLabel = !isCodeBlock && !sb.is_hidden() && !isGlobalNs && text.find("::") == std::string_view::npos;
        if (isCodeBlock || isLabel || sb.is_hidden() || isGlobalNs) {
            return;
        }
        // Область имён: "ns::name::" / "::ns::name". Разбор сегментов — единая точка
        // `symbols/scope_path.hpp` (та же, что у экспорт-поверхности модуля).
        std::vector<std::string> segs;
        for (const std::string_view seg : namespaceSegmentsOf(text)) {
            segs.emplace_back(seg);
        }
        scopeSegs.push_back(std::move(segs));
    });

    std::string result;
    for (auto it = scopeSegs.rbegin(); it != scopeSegs.rend(); ++it) { // от внешней к внутренней
        for (const auto& seg : *it) {
            if (!result.empty()) {
                result += "::";
            }
            result += seg;
        }
    }
    m_path = std::move(result);
    m_pathDepth = depth;
    m_pathCreator = creator;
    return m_path;
}

std::string NamespaceScope::namespaceFull() const {
    // Глобальная область — ровно "::" (без удвоения разделителя).
    const std::string path = namespacePath();
    if (path.empty()) {
        return "::";
    }
    return "::" + path + "::";
}

const FuncDecl* NamespaceScope::currentFunc() const {
    const FuncDecl* result = nullptr;
    // forEachScope идёт от внутреннего скоупа к глобальному - первый FuncDecl и есть текущий.
    m_symbols.forEachScope([&](const Scope& s) {
        if (!result && s.creator && s.creator->kind() == ParserToken::Kind::FuncDecl) {
            result = static_cast<const FuncDecl*>(s.creator);
        }
    });
    return result;
}

const ClassDecl* NamespaceScope::currentClass() const {
    const ClassDecl* result = nullptr;
    // forEachScope идёт от внутреннего скоупа к глобальному - первый ClassDecl и есть текущий.
    m_symbols.forEachScope([&](const Scope& s) {
        if (!result && s.creator && s.creator->kind() == ParserToken::Kind::ClassDecl) {
            result = static_cast<const ClassDecl*>(s.creator);
        }
    });
    return result;
}

bool NamespaceScope::insideTypeBody() const {
    bool result = false;
    // forEachScope идёт от внутреннего скоупа к глобальному: достаточно одного творца-типа.
    m_symbols.forEachScope([&](const Scope& s) {
        if (!result && s.creator && (s.creator->kind() == ParserToken::Kind::StructDecl || s.creator->kind() == ParserToken::Kind::ClassDecl)) {
            result = true;
        }
    });
    return result;
}

std::string NamespaceScope::funcShortName() const {
    const FuncDecl* f = currentFunc();
    if (!f) {
        return {};
    }
    // Нативный %-префикс срезается (единый источник - utils::stripNativePrefix).
    return std::string(utils::stripNativePrefix(requiredText(*f)));
}

std::string NamespaceScope::qualifiedFuncName() const {
    std::string ns = namespacePath();
    std::string name = funcShortName();
    if (name.empty()) {
        return ns;
    }
    return utils::qualifyTrustName(ns, name);
}

bool NamespaceScope::requireFunction(const AstNodeBase& node, const char* macro) const {
    if (currentFunc()) {
        return true;
    }
    m_ctx.diag().report(Severity::Error, node.range(), "macro {} can only be used inside a function", macro);
    return false;
}

bool NamespaceScope::isInLocalScope() const {
    bool isLocal = false;
    m_symbols.forEachScope([&](const Scope& s) {
        if (s.creator && s.creator->kind() == ParserToken::Kind::FuncDecl) {
            isLocal = true;
        }
    });
    return isLocal;
}

bool NamespaceScope::isInLoop() const {
    bool inLoop = false;
    m_symbols.forEachScope([&](const Scope& s) {
        if (s.creator && (s.creator->kind() == ParserToken::Kind::WhileStmt || s.creator->kind() == ParserToken::Kind::DoWhileStmt)) {
            inLoop = true;
        }
    });
    return inLoop;
}

PropertyKind NamespaceScope::currentTrustKind() const {
    PropertyKind result = PropertyKind::kUnknown;
    m_symbols.forEachScope([&](const Scope& s) {
        if (result == PropertyKind::kUnknown && s.creator && s.creator->kind() == ParserToken::Kind::TrustContract) {
            result = static_cast<const TrustContract&>(*s.creator).kind;
        }
    });
    return result;
}

} // namespace trust
