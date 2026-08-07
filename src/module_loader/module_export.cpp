// module_export.cpp — реализация сбора экспорт-интерфейса модуля.
#include "module_loader/module_export.hpp"

#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "syntax/term.h"

#include <algorithm>
#include <cctype>

namespace trust {

bool matchGlob(std::string_view pattern, std::string_view name) {
    // Итеративный DP по маске и имени (как в gtest, но только '*' и '?').
    const std::size_t np = pattern.size();
    const std::size_t nn = name.size();
    std::vector<std::vector<bool>> dp(np + 1, std::vector<bool>(nn + 1, false));
    dp[0][0] = true;
    for (std::size_t i = 1; i <= np; ++i) {
        dp[i][0] = dp[i - 1][0] && pattern[i - 1] == '*';
    }
    for (std::size_t i = 1; i <= np; ++i) {
        for (std::size_t j = 1; j <= nn; ++j) {
            if (pattern[i - 1] == '*') {
                dp[i][j] = dp[i - 1][j] || dp[i][j - 1];
            } else if (pattern[i - 1] == '?' || pattern[i - 1] == name[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1];
            }
        }
    }
    return dp[np][nn];
}

bool matchesAnyMask(std::string_view commaMasks, std::string_view name) {
    std::size_t start = 0;
    while (start <= commaMasks.size()) {
        std::size_t end = commaMasks.find(',', start);
        if (end == std::string_view::npos) {
            end = commaMasks.size();
        }
        std::string_view mask = commaMasks.substr(start, end - start);
        // Обрезаем пробелы/табы вокруг маски.
        while (!mask.empty() && std::isspace(static_cast<unsigned char>(mask.front()))) {
            mask.remove_prefix(1);
        }
        while (!mask.empty() && std::isspace(static_cast<unsigned char>(mask.back()))) {
            mask.remove_suffix(1);
        }
        // Пустая маска (между запятыми) считается «без фильтра».
        if (mask.empty() || matchGlob(mask, name)) {
            return true;
        }
        if (end == commaMasks.size()) {
            break;
        }
        start = end + 1;
    }
    return false;
}

namespace {

/// Является ли AST-узел экспортируемым объявлением (топ-левел декларация).
bool isExportDeclKind(ParserToken::Kind kind) {
    switch (kind) {
    case ParserToken::Kind::VarDecl:
    case ParserToken::Kind::FuncDecl:
    case ParserToken::Kind::TypeDecl:
    case ParserToken::Kind::StructDecl:
    case ParserToken::Kind::EnumDecl:
        return true;
    default:
        return false;
    }
}

/// Имя экспортируемой декларации. Для TypeDecl (`::=`) имя — слева (Ident); для
/// VarDecl/FuncDecl/StructDecl/EnumDecl — текст узла с убранным `%`-префиксом нативной
/// функции (маски фильтра соответствуют логическому имени, без маркера нативности).
std::string_view declNameOf(const AstNodeBase& node) {
    std::string_view name;
    if (node.kind() == ParserToken::Kind::TypeDecl) {
        const auto& b = static_cast<const Binary&>(node);
        if (b.m_left && b.m_left->kind() == ParserToken::Kind::Ident) {
            name = b.m_left->text();
        } else {
            return {};
        }
    } else {
        name = node.text();
    }
    if (!name.empty() && name.front() == '%') {
        name.remove_prefix(1);
    }
    return name;
}

/// Рекурсивный сбор экспортируемых термов из плоского тела (контейнеры SEQUENCE уже
/// развёрнуты; пользовательские { ... } сохраняются как ScopeBlock). Скоупы с анонимной
/// (`_`) областью и безымянные кодовые блоки пропускаются целиком; именованные/глобальные
/// области обходятся рекурсивно (квалификация имён для кодовой генерации выполняется
/// транспайлером через его собственный стек областей имён).
void collectRec(const std::vector<AstNodePtr>& body, std::string_view masks, std::vector<TermPtr>& out) {
    for (const auto& node : body) {
        if (!node) {
            continue;
        }
        if (isExportDeclKind(node->kind())) {
            // Экспортируем декларацию, если имя подходит под фильтр (пустой фильтр = всё).
            if (const TermPtr& t = node->term(); t) {
                if (matchesAnyMask(masks, declNameOf(*node))) {
                    out.push_back(t);
                }
            }
            continue;
        }
        // Области имён (ScopeBlock). Анонимная `_` и безымянный кодовый блок — пропускаем.
        if (const auto* scope = node->as_sequence(); scope && node->kind() == ParserToken::Kind::ScopeBlock) {
            const ScopeBlock& sb = static_cast<const ScopeBlock&>(*node);
            if (sb.is_hidden() || sb.is_anonymous()) {
                continue;
            }
            collectRec(sb.m_body, masks, out);
        }
    }
}

} // namespace

std::vector<TermPtr> collectExportedDecls(const std::vector<AstNodePtr>& body, std::string_view masks) {
    std::vector<TermPtr> out;
    collectRec(body, masks, out);
    return out;
}

} // namespace trust