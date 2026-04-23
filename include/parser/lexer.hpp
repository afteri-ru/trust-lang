#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include "diag/context.hpp"
#include "diag/error.hpp"
#include "diag/location.hpp"
#include "parser/token.hpp"

namespace trust {

struct Lexer {
    Context *ctx;
    SourceIdx src_idx;
    int offset = 0;
    int content_start = 0; // начало контента для stateful токенов

    std::vector<Lexeme> result;

    static int fill_buffer(const Lexer *e, char *buf, int max_size) {
        ASSERT(e);
        auto source = e->ctx->source(e->src_idx);
        if (e->offset >= static_cast<int>(source.size()))
            return 0;
        int n = std::min(max_size, static_cast<int>(source.size()) - e->offset);
        std::memcpy(buf, source.data() + e->offset, n);
        return n;
    }

    static void advance(Lexer *e, int n, bool start_state = false) {
        ASSERT(e);
        e->offset += n;
        if (start_state) {
            e->content_start = e->offset;
        }
    }

    static int token(Lexer *e, int kind, unsigned len, unsigned skip_start = 0, unsigned skip_end = 0) {
        ASSERT(e);
        int token_start;
        if (skip_start || skip_end) { // stateful lexeme
            ASSERT(e->content_start);
            ASSERT(e->offset >= e->content_start);
            len += skip_start + (e->offset - e->content_start); // full length: opener + content + closer
            ASSERT(skip_start + skip_end <= len);
            token_start = e->content_start - skip_start; // token start including opener
            e->offset = token_start + len;               // correct offset update
        } else {
            token_start = e->offset;
            e->offset += len;
        }
        auto source = e->ctx->source(e->src_idx);
        std::string_view text(source.data() + token_start + skip_start, len - skip_start - skip_end);
        SourceLoc pos = SourceLoc::make(e->src_idx, token_start);
        e->result.emplace_back(static_cast<ParserToken::Kind>(kind), text, pos);
        return kind;
    }

    static SourceLoc current_loc(const Lexer *e) { return SourceLoc::make(e->src_idx, e->offset); }

    [[noreturn]] static void error(const Lexer *e, const char *msg) { throw SyntaxError(msg, current_loc(e)); }

    /** Tokenize input stored in Context at the given source index.
     *  Returns a LexemeSequence containing all tokens (excluding END token).
     *  Throws SyntaxError on lexer errors. */
    static LexemeSequence tokenize(Context &ctx, SourceIdx src_idx, int offset = 0);
};

} // namespace trust

/* Forward declarations for Flex API — avoids circular dependency with flex.gen.h */
typedef void *yy_fwd_yyscan_t;
typedef struct yy_buffer_state *yy_fwd_YY_BUFFER_STATE;
extern "C" {
extern int yylex_init(yy_fwd_yyscan_t *);
extern void yyset_extra(trust::Lexer *, yy_fwd_yyscan_t);
extern yy_fwd_YY_BUFFER_STATE yy_scan_bytes(const char *, int, yy_fwd_yyscan_t);
extern int yylex(yy_fwd_yyscan_t);
extern char *yyget_text(yy_fwd_yyscan_t);
extern int yyget_leng(yy_fwd_yyscan_t);
extern void yy_delete_buffer(yy_fwd_YY_BUFFER_STATE, yy_fwd_yyscan_t);
extern int yylex_destroy(yy_fwd_yyscan_t);
}

namespace trust {

inline LexemeSequence Lexer::tokenize(Context &ctx, SourceIdx src_idx, int offset) {
    Lexer lexer_self{&ctx, src_idx, offset};

    yy_fwd_yyscan_t scanner{};
    if (yylex_init(&scanner) != 0)
        return {};

    yyset_extra(&lexer_self, scanner);
    auto source = ctx.source(src_idx);
    yy_fwd_YY_BUFFER_STATE buf = yy_scan_bytes(source.data() + offset, static_cast<int>(source.size()) - offset, scanner);

    try {
        while (yylex(scanner) != 0) {
        }
    } catch (...) {
        yy_delete_buffer(buf, scanner);
        yylex_destroy(scanner);
        throw;
    }

    yy_delete_buffer(buf, scanner);
    yylex_destroy(scanner);
    return std::move(lexer_self.result);
}

} // namespace trust