#include "diag/diag.hpp"
#include "session/context.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace trust;

namespace {

std::vector<std::string> messagesOf(const DiagnosticEngine& engine) {
    std::vector<std::string> out;
    for (const auto& e : engine.diagnostics()) {
        out.push_back(std::string(severityName(e.severity)) + ": " + e.message);
    }
    return out;
}

MapperRange pointAt(Context& ctx, MapperFile file, int line) {
    const MapperLocation loc = ctx.source().loc_from_line(file, line);
    return MapperRange{loc, loc};
}

} // namespace

// R30/R50: диагностика внутри инстанциации/раскрытия сопровождается цепочкой Notes от внутреннего
// кадра к внешнему; владелец стека — движок диагностики (diag).
TEST(DiagContextStack, NotesFollowErrorInInnerToOuterOrder) {
    Context ctx;
    const MapperFile file = ctx.source().add_source("t.src", "line1\n");
    const MapperRange range = pointAt(ctx, file, 1);

    {
        const DiagnosticEngine::ContextScope outer(ctx.diag(), range, "requested here");
        const DiagnosticEngine::ContextScope inner(ctx.diag(), range, "in instantiation of template 'Rec'");
        ctx.diag().report(Severity::Error, range, "recursive instantiation of template 'Rec'");
    }

    const std::vector<std::string> msgs = messagesOf(ctx.diag());
    ASSERT_EQ(msgs.size(), 3u);
    EXPECT_EQ(msgs[0], "error: recursive instantiation of template 'Rec'");
    EXPECT_EQ(msgs[1], "note: in instantiation of template 'Rec'");
    EXPECT_EQ(msgs[2], "note: requested here");
}

// RAII-кадр снимается при выходе из области видимости (в т.ч. по исключению): состояние не «протекает».
TEST(DiagContextStack, ScopeIsBalancedAndEmptyAfterDestruction) {
    Context ctx;
    const MapperFile file = ctx.source().add_source("t.src", "line1\n");
    const MapperRange range = pointAt(ctx, file, 1);

    {
        const DiagnosticEngine::ContextScope scope(ctx.diag(), range, "ctx");
        EXPECT_FALSE(ctx.diag().contextEmpty());
    }
    EXPECT_TRUE(ctx.diag().contextEmpty());
}

// R30/R31: report() отдаёт указатель на запись, к которой затем прикрепляется fixit. Добавление
// Note-кадров контекста не должно инвалидировать этот указатель (стабильные адреса записей).
TEST(DiagContextStack, ReturnedEntrySurvivesContextNotesForFixit) {
    Context ctx;
    const MapperFile file = ctx.source().add_source("t.src", "line1\n");
    const MapperRange range = pointAt(ctx, file, 1);

    {
        const DiagnosticEngine::ContextScope scope(ctx.diag(), range, "in expansion of macro 'm'");
        DiagnosticEntry* entry = ctx.diag().report(Severity::Error, range, "plain error");
        ASSERT_NE(entry, nullptr);
        ctx.diag().fixit(entry, range, "replacement");
    }

    ASSERT_EQ(ctx.diag().diagnostics().size(), 2u); // error + один Note-кадр
    EXPECT_EQ(ctx.diag().diagnostics()[0].message, "plain error");
    ASSERT_EQ(ctx.diag().diagnostics()[0].fixits.size(), 1u);
    EXPECT_EQ(ctx.diag().diagnostics()[0].fixits[0].replacement, "replacement");
}

// Без активного контекста диагностика остаётся единственной записью (нет ложных Notes).
TEST(DiagContextStack, NoNotesWithoutContext) {
    Context ctx;
    const MapperFile file = ctx.source().add_source("t.src", "line1\n");
    const MapperRange range = pointAt(ctx, file, 1);

    ctx.diag().report(Severity::Error, range, "plain error");

    ASSERT_EQ(ctx.diag().diagnostics().size(), 1u);
    EXPECT_EQ(severityName(ctx.diag().diagnostics()[0].severity), "error");
}
