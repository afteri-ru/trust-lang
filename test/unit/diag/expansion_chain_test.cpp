// test/unit/diag/expansion_chain_test.cpp
// R50: макро-кадры — Note-записи единой модели из пары `use → definition` (порт), имя — слово буфера.
#include "diag/diag.hpp"
#include "session/context.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace trust;

namespace {

MapperRange span(MapperFile file, Context& ctx, size_t beginOffset, size_t endOffset) {
    return MapperRange{ctx.source().makeLoc(file, beginOffset), ctx.source().makeLoc(file, endOffset)};
}

std::vector<std::string> messagesOf(const DiagnosticEngine& engine) {
    std::vector<std::string> out;
    for (const auto& e : engine.diagnostics()) {
        out.push_back(std::string(severityName(e.severity)) + ": " + e.message);
    }
    return out;
}

} // namespace

// Диагностика в точке использования макроса сопровождается Note в точке определения.
TEST(DiagExpansionChain, MacroExpansionAddsNoteAtDefinition) {
    Context ctx;
    const MapperFile file = ctx.source().add_source("t.src", "@tst;\n@@ tst @@ 42 @@@@;\n");
    const MapperRange use = span(file, ctx, 1, 5);          // "@tst"
    const MapperRange definition = span(file, ctx, 18, 20); // "42"
    ctx.source().addMacroMapping(use, definition);

    ctx.diag().report(Severity::Error, use, "macro expansion failed");

    const std::vector<std::string> msgs = messagesOf(ctx.diag());
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_EQ(msgs[0], "error: macro expansion failed");
    EXPECT_EQ(msgs[1], "note: in expansion of macro '@tst'");
}

// Вложенные пары `use → definition`: кадры идут от внутреннего к внешнему.
TEST(DiagExpansionChain, NestedExpansionsInnerToOuter) {
    Context ctx;
    const MapperFile file = ctx.source().add_source("t.src", "@o; @i; aaa bbb\n");
    const MapperRange useOuter = span(file, ctx, 1, 3);   // "@o;"
    const MapperRange defOuter = span(file, ctx, 9, 11);  // "aaa"
    const MapperRange defInner = span(file, ctx, 13, 15); // "bbb"
    ctx.source().addMacroMapping(useOuter, defOuter);
    ctx.source().addMacroMapping(span(file, ctx, 9, 15), defInner); // точка использования внутри определения

    ctx.diag().report(Severity::Error, useOuter, "inside nested expansion");

    const std::vector<std::string> msgs = messagesOf(ctx.diag());
    ASSERT_EQ(msgs.size(), 3u);
    EXPECT_EQ(msgs[1], "note: in expansion of macro '@o'");
    EXPECT_EQ(msgs[2], "note: in expansion of macro 'aaa'");
}

// Самоссылочная пара (определение внутри точки использования) не зацикливает обход.
TEST(DiagExpansionChain, CycleDoesNotHang) {
    Context ctx;
    const MapperFile file = ctx.source().add_source("t.src", "@self;\n");
    const MapperRange use = span(file, ctx, 1, 6);
    const MapperRange definition = span(file, ctx, 3, 4);
    ctx.source().addMacroMapping(use, definition);

    ctx.diag().report(Severity::Error, use, "self expansion");

    const std::vector<std::string> msgs = messagesOf(ctx.diag());
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_EQ(msgs[1], "note: in expansion of macro '@self'");
}

// Без раскрытия цепочки нет: диагностика остаётся единственной записью.
TEST(DiagExpansionChain, NoExpansionNoNote) {
    Context ctx;
    const MapperFile file = ctx.source().add_source("t.src", "plain;\n");
    const MapperRange range = span(file, ctx, 1, 5);

    ctx.diag().report(Severity::Error, range, "plain error");

    ASSERT_EQ(ctx.diag().diagnostics().size(), 1u);
    EXPECT_EQ(messagesOf(ctx.diag())[0], "error: plain error");
}