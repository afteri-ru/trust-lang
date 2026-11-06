#include "diag/diagnostic_location.hpp"
#include "session/context.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace trust;

namespace {

TEST(DiagnosticLocationTest, DecodesFileLineColumn) {
    Context ctx;
    std::string src = "aaaa\nbbbb\ncccc\n";
    MapperFile f = ctx.source().add_source("t.trust", src);

    const MapperLocation b = ctx.source().loc_from_line(f, 2);
    const MapperRange range{b, b.inc(2)}; // строка 2, колонки 1..3 (1-based)

    const DiagnosticLocation dl = decodeRange(range, &ctx);
    EXPECT_TRUE(dl.valid());
    EXPECT_EQ(dl.file, "t.trust");
    EXPECT_EQ(dl.begin.line, 2u);
    EXPECT_EQ(dl.begin.column, 1u);
    EXPECT_EQ(dl.end.line, 2u);
    EXPECT_EQ(dl.end.column, 3u);
}

TEST(DiagnosticLocationTest, PointClampsEndToBegin) {
    Context ctx;
    std::string src = "aaaa\n";
    MapperFile f = ctx.source().add_source("t.trust", src);
    const MapperLocation b = ctx.source().loc_from_line(f, 1);
    const DiagnosticLocation dl = decodeRange(MapperRange::point(b), &ctx);
    EXPECT_TRUE(dl.valid());
    EXPECT_EQ(dl.end.line, dl.begin.line);
    EXPECT_EQ(dl.end.column, dl.begin.column);
}

TEST(DiagnosticLocationTest, CrossFileClampsEnd) {
    Context ctx;
    MapperFile f1 = ctx.source().add_source("a.trust", "aaaa\n");
    MapperFile f2 = ctx.source().add_source("b.trust", "bbbb\n");
    const MapperRange cross{ctx.source().loc_from_line(f1, 1), ctx.source().loc_from_line(f2, 1)};
    const DiagnosticLocation dl = decodeRange(cross, &ctx);
    EXPECT_TRUE(dl.valid());
    EXPECT_EQ(dl.end.line, dl.begin.line);
    EXPECT_EQ(dl.end.column, dl.begin.column);
}

TEST(DiagnosticLocationTest, InvalidRangeIsNotValid) {
    Context ctx;
    const DiagnosticLocation dl = decodeRange(MapperRange{}, &ctx);
    EXPECT_FALSE(dl.valid());
    EXPECT_TRUE(dl.file.empty());
}

TEST(DiagnosticLocationTest, MissingProviderIsNotValid) {
    Context ctx;
    MapperFile f = ctx.source().add_source("t.trust", "aaaa\n");
    const MapperRange r{ctx.source().loc_from_line(f, 1), ctx.source().loc_from_line(f, 1)};
    const DiagnosticLocation dl = decodeRange(r, nullptr);
    EXPECT_FALSE(dl.valid());
}

TEST(DiagnosticLocationTest, DiagnosticConvenienceUsesEntryRange) {
    Context ctx;
    MapperFile f = ctx.source().add_source("t.trust", "aaaa\n");
    DiagnosticEntry entry;
    entry.range = {ctx.source().loc_from_line(f, 1), ctx.source().loc_from_line(f, 1)};
    entry.message = "m";
    const DiagnosticLocation dl = diagnosticLocation(entry, &ctx);
    EXPECT_TRUE(dl.valid());
}

TEST(DiagnosticLocationTest, ZeroOffsetBeginIsNotResolvable) {
    Context ctx;
    MapperFile f = ctx.source().add_source("t.trust", "aaaa\n");
    const MapperRange r{MapperLocation::makeLoc(f, 0), MapperLocation::makeLoc(f, 0)};
    const DiagnosticLocation dl = decodeRange(r, &ctx);
    EXPECT_FALSE(dl.valid());
}

} // namespace
