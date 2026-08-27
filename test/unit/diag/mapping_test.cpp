#include "diag/context.hpp"
#include "diag/mapper.hpp"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/MD5.h"

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

using namespace trust;

// ══════════════════════════════════════════════════════════════
//                    Context: RangeMapping
// ══════════════════════════════════════════════════════════════
//
// ВАЖНО: findRange в reader.cpp возвращает СПРОЕЦИРОВАННЫЙ диапазон.
// Если маппинг [10,20]→[30,40], а query_offset=12 (на 2 больше begin),
// то возвращается [32,42] (30+2, 40+2).
// Если query_offset==begin (delta=0), возвращается точный диапазон.

TEST(MappingTest, AddRangeMapping_ForwardAndBackward) {
    Context ctx;
    MapperFile trustSrc = ctx.source().add_source("trust", std::string(200, 'a'), false);
    MapperFile cppSrc = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cppSrc, std::string(200, ' '));

    auto trustR = ctx.source().makeRange(ctx.source().makeLoc(trustSrc, 10), ctx.source().makeLoc(trustSrc, 20));
    auto cppR = ctx.source().makeRange(ctx.source().makeLoc(cppSrc, 30), ctx.source().makeLoc(cppSrc, 40));
    ASSERT_TRUE(ctx.source().addRangeMapping(trustR, cppR));

    auto reader = ctx.source().toReader();

    // -- Forward: trust → cpp (query=12, delta=2 → проекция +2) --
    {
        auto loc = static_cast<ReaderLocation>(ctx.source().makeLoc(trustSrc, 12));
        auto r = reader->getMapTrustToCpp(loc);
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(r->begin, 32u);
        EXPECT_EQ(r->end, 42u);
    }

    // -- Backward: cpp → trust (query=35, delta=5 → проекция +5) --
    {
        auto loc = static_cast<ReaderLocation>(ctx.source().makeLoc(cppSrc, 35));
        auto r = reader->getMapCppToTrust(loc);
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(r->begin, 15u);
        EXPECT_EQ(r->end, 25u);
    }

    // -- Edge: точное совпадение начала (query=10, delta=0 → проекция 0) --
    {
        auto loc = static_cast<ReaderLocation>(ctx.source().makeLoc(trustSrc, 10));
        auto r = reader->getMapTrustToCpp(loc);
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(r->begin, 30u);
    }

    // -- Edge: точное совпадение конца (query=19, delta=9 → проекция +9) --
    {
        auto loc = static_cast<ReaderLocation>(ctx.source().makeLoc(trustSrc, 19));
        auto r = reader->getMapTrustToCpp(loc);
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(r->end, 49u);
    }

    // -- Out of range: до начала маппинга --
    {
        auto loc = static_cast<ReaderLocation>(ctx.source().makeLoc(trustSrc, 5));
        auto r = reader->getMapTrustToCpp(loc);
        EXPECT_FALSE(r.has_value());
    }

    // -- Out of range: после конца маппинга --
    {
        auto loc = static_cast<ReaderLocation>(ctx.source().makeLoc(trustSrc, 25));
        auto r = reader->getMapTrustToCpp(loc);
        EXPECT_FALSE(r.has_value());
    }
}

// ══════════════════════════════════════════════════════════════
//              Context: getCppName / getTrustName
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, AddNameMapping_GetCppName_GetTrustName) {
    Context ctx;
    MapperFile trustSrc = ctx.source().add_source("trust", std::string(200, 'a'), false);
    MapperFile cppSrc = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cppSrc, std::string(200, ' '));

    auto trustR = ctx.source().makeRange(ctx.source().makeLoc(trustSrc, 10), ctx.source().makeLoc(trustSrc, 20));
    auto cppR = ctx.source().makeRange(ctx.source().makeLoc(cppSrc, 30), ctx.source().makeLoc(cppSrc, 40));
    ctx.source().addNameMapping(trustR, cppR, "trustName", "cppName");

    auto reader = ctx.source().toReader();

    // -- getCppName (query=12, delta=2) - цель hover-ссылки - ВЕСЬ диапазон имени на
    // противоположной стороне, без проекции/сдвига по позиции курсора внутри имени. --
    auto cppName = reader->getCppName(static_cast<ReaderLocation>(ctx.source().makeLoc(trustSrc, 12)), "trustName");
    ASSERT_TRUE(cppName.has_value());
    EXPECT_EQ(cppName->toName, "cppName");
    EXPECT_EQ(cppName->rangeMap.to.begin, 30u);
    EXPECT_EQ(cppName->rangeMap.to.end, 40u);

    // -- getTrustName --
    auto trustName = reader->getTrustName(static_cast<ReaderLocation>(ctx.source().makeLoc(cppSrc, 35)), "cppName");
    ASSERT_TRUE(trustName.has_value());
    EXPECT_EQ(trustName->fromName, "trustName");
    // Цель - весь trust-диапазон имени [10,20], без сдвига по курсору.
    EXPECT_EQ(trustName->rangeMap.from.begin, 10u);
    EXPECT_EQ(trustName->rangeMap.from.end, 20u);

    // -- Поиск по несуществующему имени --
    EXPECT_FALSE(reader->getCppName(static_cast<ReaderLocation>(ctx.source().makeLoc(trustSrc, 12)), "nonExistent").has_value());
}

TEST(MappingTest, GetCppName_NoMapping_ReturnsNullopt) {
    Context ctx;
    MapperFile src = ctx.source().add_source("trust", std::string(200, 'a'), false);
    auto reader = ctx.source().toReader();
    auto name = reader->getCppName(static_cast<ReaderLocation>(ctx.source().makeLoc(src, 50)), "someName");
    EXPECT_FALSE(name.has_value());
}

// ══════════════════════════════════════════════════════════════
//                 Context: findRangesByLine
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, FindRangesByLine) {
    Context ctx;
    MapperFile trustSrc = ctx.source().add_source("trust", std::string(200, '\n'), false);
    MapperFile cppSrc = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cppSrc, std::string(200, ' '));

    auto trustR1 = ctx.source().makeRange(ctx.source().makeLoc(trustSrc, 2), ctx.source().makeLoc(trustSrc, 4));
    auto cppR1 = ctx.source().makeRange(ctx.source().makeLoc(cppSrc, 10), ctx.source().makeLoc(cppSrc, 20));
    ctx.source().addRangeMapping(trustR1, cppR1);

    auto trustR2 = ctx.source().makeRange(ctx.source().makeLoc(trustSrc, 30), ctx.source().makeLoc(trustSrc, 35));
    auto cppR2 = ctx.source().makeRange(ctx.source().makeLoc(cppSrc, 50), ctx.source().makeLoc(cppSrc, 60));
    ctx.source().addRangeMapping(trustR2, cppR2);

    auto reader = ctx.source().toReader();

    // findRangesByLine на Reader-уровне
    ReaderFile rTrust = reader->findFileIdx(ctx.source().filename(trustSrc));
    auto ranges = reader->findRangesByLine(rTrust, 2);
    EXPECT_EQ(ranges.size(), 1);

    // -- Несуществующая строка --
    ranges = reader->findRangesByLine(rTrust, 999);
    EXPECT_TRUE(ranges.empty());
}

// ══════════════════════════════════════════════════════════════
//                  Context: getTrustFileMappings
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, GetTrustFileMappings) {
    Context ctx;
    MapperFile trustSrc = ctx.source().add_source("trust", std::string(200, 'a'), false);
    MapperFile cppSrc = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cppSrc, std::string(200, ' '));

    auto trustR1 = ctx.source().makeRange(ctx.source().makeLoc(trustSrc, 10), ctx.source().makeLoc(trustSrc, 20));
    auto cppR1 = ctx.source().makeRange(ctx.source().makeLoc(cppSrc, 30), ctx.source().makeLoc(cppSrc, 40));
    ctx.source().addRangeMapping(trustR1, cppR1);

    auto trustR2 = ctx.source().makeRange(ctx.source().makeLoc(trustSrc, 50), ctx.source().makeLoc(trustSrc, 60));
    auto cppR2 = ctx.source().makeRange(ctx.source().makeLoc(cppSrc, 70), ctx.source().makeLoc(cppSrc, 80));
    ctx.source().addRangeMapping(trustR2, cppR2);

    auto reader = ctx.source().toReader();
    auto mappings = reader->getTrustFileMappings(reader->findFileIdx(ctx.source().filename(trustSrc)));
    EXPECT_EQ(mappings.size(), 2);
}

TEST(MappingTest, GetTrustFileMappings_Negative_NotFound) {
    Context ctx;
    MapperFile trustSrc = ctx.source().add_source("trust", std::string(200, 'a'), false);
    MapperFile cppSrc = ctx.source().add_output("cpp", false);
    {
        auto trustR = ctx.source().makeRange(ctx.source().makeLoc(trustSrc, 10), ctx.source().makeLoc(trustSrc, 20));
        auto cppR = ctx.source().makeRange(ctx.source().makeLoc(cppSrc, 30), ctx.source().makeLoc(cppSrc, 40));
        ctx.source().addRangeMapping(trustR, cppR);
    }

    // Поиск для несуществующего ReaderFileIdx через Reader
    auto reader = ctx.source().toReader();
    auto mappings = reader->getTrustFileMappings(ReaderFile{});
    EXPECT_TRUE(mappings.empty());
}

// ══════════════════════════════════════════════════════════════
//                    Roundtrip: pack → unpack
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, MsgpackRoundtrip) {
    Context ctx;
    MapperFile trustSrc = ctx.source().add_source("trust", std::string(200, 'a'), false);
    MapperFile cppSrc = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cppSrc, std::string(200, ' '));

    auto trustR = ctx.source().makeRange(ctx.source().makeLoc(trustSrc, 10), ctx.source().makeLoc(trustSrc, 20));
    auto cppR = ctx.source().makeRange(ctx.source().makeLoc(cppSrc, 30), ctx.source().makeLoc(cppSrc, 40));
    ctx.source().addRangeMapping(trustR, cppR);

    // -- Pack via reader → unpack --
    auto reader = ctx.source().toReader();
    auto packed = reader->packToMsgpack();
    auto unpacked = SourceMapReader::fromMsgpack(packed.data(), packed.size());
    ASSERT_NE(unpacked, nullptr);

    // Проверяем, что маппинг сохранился в reader (query=12, delta=2 → +2)
    auto loc = static_cast<ReaderLocation>(ctx.source().makeLoc(trustSrc, 12));
    auto r = reader->getMapTrustToCpp(loc);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->begin, 32u);
    EXPECT_EQ(r->end, 42u);

    // Проверяем unpacked reader
    auto ru = unpacked->getMapTrustToCpp(loc);
    ASSERT_TRUE(ru.has_value());
    EXPECT_EQ(ru->begin, 32u);
    EXPECT_EQ(ru->end, 42u);
}

// ══════════════════════════════════════════════════════════════
//              Msgpack: пустой SourceMapReader
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, EmptySourceMapReader) {
    auto reader = SourceMapReader::fromMsgpack(nullptr, 0);
    EXPECT_EQ(reader, nullptr);
}

// ══════════════════════════════════════════════════════════════
//              Context: filename через Location
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, FilenameViaLocation) {
    Context ctx;
    auto idx = ctx.source().add_source("/path/to/file.trust", "content", true);
    auto loc = ctx.source().makeLoc(idx, 1);
    auto fn = ctx.source().filename(loc);
    EXPECT_FALSE(fn.empty());
}

// ══════════════════════════════════════════════════════════════
//    Context: getInput - напрямую через унаследованные методы
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, GetFile_Input) {
    Context ctx;
    auto idx = ctx.source().add_source("test", "content", false);
    const auto& src = ctx.source().get_file(idx);
    EXPECT_EQ(src.getFilename(), "test");
    EXPECT_EQ(src.getSource(), "content");
}

TEST(MappingTest, GetFile_Output) {
    Context ctx;
    auto idx = ctx.source().add_output("test", false);
    ctx.source().output_append(idx, "cpp_content");
    const auto& out = ctx.source().get_file(idx);
    EXPECT_EQ(out.getFilename(), "test");
    EXPECT_EQ(out.getSource(), "cpp_content");
}

TEST(MappingTest, GetFile_Invalid_ZeroIdx) {
    Context ctx;
    EXPECT_THROW(ctx.source().get_file(MapperFile{}), std::exception);
}

TEST(MappingTest, GetFile_OutOfBounds_Input) {
    Context ctx;
    ctx.source().add_source("in1", "content1", false);
    auto bad = MapperFile::make_input(5); // индекс за пределами
    EXPECT_THROW(ctx.source().get_file(bad), std::exception);
}

TEST(MappingTest, GetFile_OutOfBounds_Output) {
    Context ctx;
    ctx.source().add_output("out1", false);
    auto bad = MapperFile::make_output(10); // индекс за пределами
    EXPECT_THROW(ctx.source().get_file(bad), std::exception);
}
