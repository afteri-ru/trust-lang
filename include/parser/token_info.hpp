#pragma once

#include "diag/location.hpp"
#include "utils/error.hpp"
#include "parser/token.hpp"
#include <cstddef>
#include <string>
#include <memory>

namespace trust {

// Forward declaration and pointer type
struct TokenInfo;

/// Bison attempts to copy `unique_ptr` when automatically wrapping values ​​during shift/reduce operations.
/// Used `shared_ptr` for make TokenPtr copyable for Bison.
using TokenPtr = std::shared_ptr<TokenInfo>;
using TokenSequence = std::vector<TokenPtr>;

/// TokenInfo — данные токена из парсера (владеет текстом, хранит диапазон).
struct TokenInfo {
    ParserToken::Kind kind{ParserToken::Kind::END};
    std::string text;
    MapperRange range{};

    TokenPtr m_name_or_class; ///< The name or class of the token, if the term has a name or class

    TokenPtr m_ref; ///< Type of reference before the variable (valid references or its creation operator)
    TokenPtr m_namespace;
    TokenSequence m_types;
    TokenSequence m_dims;
    TokenSequence m_attrs;
    bool m_is_call{false};  ///< Call as function flag (brackets used )
    bool m_is_const{false}; ///< Immutability (non changeability) feature
    // bool m_is_take;

    TokenPtr m_docs; ///< The current namespace in the source file when this term is used

    /// child nodes AST
    TokenPtr m_left;
    TokenPtr m_right;
    TokenSequence m_sequence;

    TokenInfo() = default;

    TokenInfo(const Lexeme& lex)
    : kind(lex.kind)
    , text(lex) {
        ASSERT(lex.pos.isValid() && lex.size() < lex.pos);
        range = {lex.pos.dec(lex.size()), lex.pos};
    }
    TokenInfo(ParserToken::Kind k, std::string t, MapperRange r)
    : kind(k)
    , text(std::move(t))
    , range(std::move(r)) {}

    [[nodiscard]] bool empty() const noexcept { return kind == ParserToken::Kind::END; }

    /// Factory: create a TokenPtr from a Lexeme
    [[nodiscard]] static TokenPtr make(const Lexeme& lex) { return std::make_shared<TokenInfo>(lex); }

    /// Factory: create a TokenPtr from Kind, range, and text
    [[nodiscard]] static TokenPtr make(ParserToken::Kind k, std::string t, MapperRange r = {}) {
        return std::make_shared<TokenInfo>(k, std::move(t), std::move(r));
    }

    [[nodiscard]] bool is_block() { return kind == ParserToken::Kind::block || kind == ParserToken::Kind::sequence || kind == ParserToken::Kind::BlockStmt; }

    [[nodiscard]] static std::string dump(const TokenInfo* ptr, size_t indent = 0);
};

} // namespace trust