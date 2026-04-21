#pragma once

#include <cstring>
#include "diag/context.hpp"
#include "diag/location.hpp"

namespace trust {

struct Lexer {
    Context* ctx;
    SourceIdx src_idx;
    int offset = 0;

    static int fill_buffer(const Lexer* e, char* buf, int max_size) {
        ASSERT(e);
        auto source = e->ctx->source(e->src_idx);
        if (e->offset >= static_cast<int>(source.size())) return 0;
        int n = std::min(max_size, static_cast<int>(source.size()) - e->offset);
        std::memcpy(buf, source.data() + e->offset, n);
        return n;
    }

    static void advance(Lexer* e, int n) {
        ASSERT(e);
        e->offset += n;
    }

    static int token(Lexer* e, int kind, int len) {
        ASSERT(e);
        e->offset += len;
        return kind;
    }

    static SourceLoc current_loc(const Lexer* e) {
        return SourceLoc::make(e->src_idx, e->offset);
    }

    [[noreturn]] static void error(const Lexer* e, const char* msg) {
        throw ParserError(msg, current_loc(e));
    }
};

} // namespace trust