#pragma once

#include "diag/location.hpp"
#include "parser/token.hpp"
#include <string>
#include <cstdint>

namespace trust {

/// TokenInfo — данные токена из парсера (владеет текстом, хранит диапазон).
struct TokenInfo {
    ParserToken::Kind kind{ParserToken::Kind::END};
    SourceRange range{};
    std::string text;

    TokenInfo() = default;

    TokenInfo(ParserToken::Kind k, SourceRange r, std::string t)
        : kind(k), range(std::move(r)), text(std::move(t)) {}

    [[nodiscard]] bool empty() const noexcept { return kind == ParserToken::Kind::END; }
};

} // namespace trust