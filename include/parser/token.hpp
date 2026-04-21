// token.hpp - public header for the token library.
// Provides a unified enum system for Flex lexer, Bison parser, and AST nodes.
// Definitions are generated from tokens.def via X-macro pattern.
//
//   #include "token.hpp"

#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <string_view>
#include <vector>
#include "diag/location.hpp"

namespace trust {

/** Bitmask flags for token classification. Supports | and & operators. */
enum class TokenFlag : int {
    FLEX_LEXEME = 1,
    BISON_TOKEN = 2,
    Expr        = 4,
    Stmt        = 8,
    Decl        = 16,
    Root        = 32,
    Count_      = 64,  // sentinel: all flags < Count_
};

constexpr TokenFlag operator|(TokenFlag a, TokenFlag b) noexcept {
    return static_cast<TokenFlag>(static_cast<int>(a) | static_cast<int>(b));
}

constexpr TokenFlag operator&(TokenFlag a, TokenFlag b) noexcept {
    return static_cast<TokenFlag>(static_cast<int>(a) & static_cast<int>(b));
}

/** AST node category classification (not a bitmask, standalone enum). */
enum class TokenCategory : int {
    None = 0,
    Expr = 1,
    Stmt = 2,
    Decl = 3,
    Root = 4,
};

// Element count via X-macro pattern
#define TK(name, val, flags) +1
static constexpr std::size_t _ALL_COUNT = 0
#include "parser/token.gen.all"
;
#undef TK

/** Unified enum for all compiler tokens (Flex lexer + Bison parser + AST nodes).
 *  ParserToken::Kind is the single source of truth for all token types.
 *  Categories are determined by TokenFlag bitmask (Expr, Stmt, Decl, Root). */
struct ParserToken {
    enum class Kind : int {
        #define TK(name, val, flags) name = val,
        #include "parser/token.gen.all"
        #undef TK
    };

    // Token name
    [[nodiscard]] static constexpr std::string_view name(Kind k) noexcept {
        switch (k) {
        #define TCASE(name) case Kind::name: return #name;
        #include "parser/token.gen.name_all"
        #undef TCASE
        }
        return "<unknown>";
    }

    // Token flags (returns TokenFlag bitmask)
    [[nodiscard]] static constexpr TokenFlag flags(Kind k) noexcept {
        switch (k) {
        #define FCASE(name, fl) case Kind::name: return fl;
        #include "parser/token.gen.flags_all"
        #undef FCASE
        }
        return TokenFlag{};
    }

    // Predicates
    [[nodiscard]] static constexpr bool is_flex_lexeme(Kind k) noexcept {
        return (flags(k) & TokenFlag::FLEX_LEXEME) != TokenFlag{};
    }
    [[nodiscard]] static constexpr bool is_bison_token(Kind k) noexcept {
        return (flags(k) & TokenFlag::BISON_TOKEN) != TokenFlag{};
    }
    [[nodiscard]] static constexpr bool is_expr(Kind k) noexcept {
        return (flags(k) & TokenFlag::Expr) != TokenFlag{};
    }
    [[nodiscard]] static constexpr bool is_stmt(Kind k) noexcept {
        return (flags(k) & TokenFlag::Stmt) != TokenFlag{};
    }
    [[nodiscard]] static constexpr bool is_decl(Kind k) noexcept {
        return (flags(k) & TokenFlag::Decl) != TokenFlag{};
    }
    [[nodiscard]] static constexpr bool is_root(Kind k) noexcept {
        return (flags(k) & TokenFlag::Root) != TokenFlag{};
    }

    /** Static array of all tokens, for range-based for via ParserToken::all(). */
    static constexpr std::array<Kind, _ALL_COUNT> _all = {{
        #define TK(name, val, flags) Kind::name,
        #include "parser/token.gen.all"
        #undef TK
    }};

    [[nodiscard]] static constexpr const auto& all() noexcept { return _all; }
    [[nodiscard]] static constexpr std::size_t all_size() noexcept { return _ALL_COUNT; }

    /** Lookup token by name (constexpr). Returns pointer to Kind or nullptr. */
    [[nodiscard]] static constexpr const Kind* from_name(std::string_view s) noexcept {
        #define LOOKUP(name) if (s == #name) { static constexpr auto _k = Kind::name; return &_k; }
        #include "parser/token.gen.lookup_all"
        #undef LOOKUP
        return nullptr;
    }

    // Lookup token by numeric value
    [[nodiscard]] static constexpr const Kind* from_value(int v) noexcept {
        for (std::size_t i = 0; i < _all.size(); ++i) {
            if (static_cast<int>(_all[i]) == v) return &_all[i];
        }
        return nullptr;
    }

    // Name by numeric value
    [[nodiscard]] static constexpr std::string_view name_by_value(int v) noexcept {
        if (auto* pk = from_value(v)) return name(*pk);
        return "<unknown>";
    }
};

// Compile-time checks: ParserToken::Kind values match tokens.def
#define TK(name, val, flags) static_assert(static_cast<int>(ParserToken::Kind::name) == val, #name);
#include "parser/token.gen.all"
#undef TK

// Hash sync check: flex.gen.h and token.gen.hash must match
#include "parser/token.gen.hash"
#if defined(FLEX_DEFINES_TOKENS_HASH)
static_assert(
    __builtin_strcmp(FLEX_DEFINES_TOKENS_HASH, TOKENS_DEF_HASH) == 0,
    "[trust::TokenGen] flex.gen.h and token.gen.hash are out of sync!");
#endif

/** Lexeme: token kind + position. Text stored as std::string_view into base buffer.
 *  Used by both Flex lexer and Bison parser as the unified token representation. */
struct Lexeme : std::string_view {
    ParserToken::Kind kind{ParserToken::Kind::END};
    SourceLoc pos{};
    Lexeme() = default;
    Lexeme(ParserToken::Kind k, std::string_view v, SourceLoc p)
        : std::string_view(v), kind(k), pos(p) {}
};

using LexemeSequence = std::vector<Lexeme>;

} // namespace trust

/** Runtime consistency check (debug only, no-op in release).
 *  Verifies hash sync and no unknown names. */
#ifndef NDEBUG
#  include <cstdio>
#  include <cstdlib>

namespace trust {
inline void validateTokenGenConsistency() noexcept {
#  if defined(FLEX_DEFINES_TOKENS_HASH)
    if (__builtin_strcmp(FLEX_DEFINES_TOKENS_HASH, TOKENS_DEF_HASH) != 0) {
        std::fprintf(stderr,
            "[trust::TokenGen] FATAL: flex.gen.h and token.gen.hash are out of sync!\n");
        std::abort();
    }
#  endif
    for (auto k : ParserToken::all()) {
        if (ParserToken::name(k) == "<unknown>") {
            std::fprintf(stderr, "[trust::TokenGen] token %d has no name!\n", static_cast<int>(k));
            std::abort();
        }
    }
}
} // namespace trust
#else
namespace trust {
inline void validateTokenGenConsistency() noexcept {}
} // namespace trust
#endif