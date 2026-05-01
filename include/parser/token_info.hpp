#pragma once

#include "diag/location.hpp"
#include "parser/token.hpp"
#include <string>
#include <memory>
#include <optional>

namespace trust {

/// TokenInfo — данные токена из парсера (владеет текстом, хранит диапазон).
struct TokenInfo {
    ParserToken::Kind kind{ParserToken::Kind::END};
    SourceRange range{};
    std::string text;

    TokenInfo() = default;

    TokenInfo(ParserToken::Kind k, SourceRange r, std::string t) : kind(k), range(std::move(r)), text(std::move(t)) {}

    [[nodiscard]] bool empty() const noexcept { return kind == ParserToken::Kind::END; }
};

// --- Forward declarations ---
struct Expr;
struct Stmt;
struct Decl;
class AstVisitor;

// --- Base interface ---
struct AstVisitable {
    virtual ~AstVisitable() = default;
    virtual void accept(AstVisitor *v) const = 0;
};

// --- Base AST node ---
struct AstNodeBase : AstVisitable {
    SourceLoc loc;
    std::optional<TokenInfo> source;
    virtual ~AstNodeBase() = default;
    virtual ParserToken::Kind token_kind() const = 0;
    void set_source(const TokenInfo &ti);

    template <typename T>
    [[nodiscard]] bool is() const noexcept {
        return token_kind() == T::static_token_kind();
    }
    template <typename T>
    [[nodiscard]] T *as() noexcept {
        return is<T>() ? static_cast<T *>(this) : nullptr;
    }
    template <typename T>
    [[nodiscard]] const T *as() const noexcept {
        return is<T>() ? static_cast<const T *>(this) : nullptr;
    }
};

using AstNodePtr = std::unique_ptr<AstNodeBase>;
using AstNodeSequence = std::vector<AstNodePtr>;

AstNodePtr make_int_literal_node(int value, ParserToken::Kind kind, std::string text, SourceLoc loc);
AstNodePtr make_string_literal_node(std::string text, ParserToken::Kind kind, SourceLoc loc);

} // namespace trust