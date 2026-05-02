#include "diag/mapping.hpp"

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

using namespace trust;

// ── helpers ──

static FileIdx fi(int n) { return FileIdx{static_cast<uint32_t>(n + 1)}; }                  // input file
static FileIdx fo(int n) { return FileIdx{static_cast<uint32_t>((n + 1) | (1 << FileIdx::FILEIDX_BITS))}; } // output file

static SourceLoc loc(FileIdx idx, int offset) {
    return SourceLoc(idx, offset);
}

static SourceLoc outLoc(FileIdx idx, int offset) {
    return SourceLoc(idx, offset);
}

static SourceRange rng(SourceLoc b, SourceLoc e) {
    return SourceRange{b, e};
}

// ══════════════════════════════════════════════════════════════
//                    SourceMapping: RangeMapping
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, AddRangeMapping_ForwardAndBackward) {
    SourceMapping map;
    FileIdx trustSrc = fi(0);   // input
    FileIdx cppSrc   = fo(10);  // output

    auto trustR = rng(loc(trustSrc, 10), loc(trustSrc, 20));
    auto cppR   = rng(outLoc(cppSrc,   100), outLoc(cppSrc,   200));

    EXPECT_TRUE(map.addRangeMapping(trustR, cppR));

    // Прямой поиск: точка на BEGIN диапазона → точное совпадение
    auto fwd = map.getMapTrustToCpp(loc(trustSrc, 10));
    ASSERT_TRUE(fwd.has_value());
    EXPECT_EQ(fwd->begin.packed, cppR.begin.packed);
    EXPECT_EQ(fwd->end.packed,   cppR.end.packed);

    // Точка ВНУТРИ диапазона → результат сдвинут
    fwd = map.getMapTrustToCpp(loc(trustSrc, 15));
    ASSERT_TRUE(fwd.has_value());
    EXPECT_EQ(fwd->begin.offset(), 105); // 100 + (15-10)
    EXPECT_EQ(fwd->end.offset(), 205);   // 200 + (15-10)

    // Обратный поиск (cpp → trust) по output-локации
    auto bwd = map.getMapCppToTrust(outLoc(cppSrc, 100));
    ASSERT_TRUE(bwd.has_value());
    EXPECT_EQ(bwd->begin.packed, trustR.begin.packed);
    EXPECT_EQ(bwd->end.packed,   trustR.end.packed);

    // Точка внутри cpp-диапазона → сдвинутый результат
    bwd = map.getMapCppToTrust(outLoc(cppSrc, 150));
    ASSERT_TRUE(bwd.has_value());
    EXPECT_EQ(bwd->begin.offset(), 60); // 10 + (150-100) = 60
    EXPECT_EQ(bwd->end.offset(), 70);   // 20 + (150-100) = 70
}

TEST(MappingTest, AddRangeMapping_PointOutside) {
    SourceMapping map;
    FileIdx trustSrc = fi(0);
    FileIdx cppSrc   = fo(10);

    EXPECT_TRUE(map.addRangeMapping(
        rng(loc(trustSrc, 10), loc(trustSrc, 20)),
        rng(outLoc(cppSrc,   100), outLoc(cppSrc,   200))));

    // Точка перед диапазоном
    EXPECT_FALSE(map.getMapTrustToCpp(loc(trustSrc, 5)).has_value());
    EXPECT_FALSE(map.getMapCppToTrust(outLoc(cppSrc,   50)).has_value());

    // Точка после диапазона
    EXPECT_FALSE(map.getMapTrustToCpp(loc(trustSrc, 25)).has_value());
    EXPECT_FALSE(map.getMapCppToTrust(outLoc(cppSrc,   250)).has_value());
}

TEST(MappingTest, AddRangeMapping_EmptyRange) {
    SourceMapping map;
    FileIdx src = fi(0);

    EXPECT_TRUE(map.addRangeMapping(
        rng(loc(src, 10), loc(src, 10)),
        rng(outLoc(src,   20), outLoc(src,   20))));

    auto fwd = map.getMapTrustToCpp(loc(src, 10));
    ASSERT_TRUE(fwd.has_value());
    EXPECT_EQ(fwd->begin.offset(), 20);
    EXPECT_EQ(fwd->end.offset(), 20);
}

TEST(MappingTest, AddRangeMapping_DifferentSources) {
    SourceMapping map;
    FileIdx t0 = fi(0);
    FileIdx t1 = fi(1);
    FileIdx c0 = fo(10);
    FileIdx c1 = fo(11);

    EXPECT_TRUE(map.addRangeMapping(
        rng(loc(t0, 1), loc(t0, 5)),
        rng(outLoc(c0, 10), outLoc(c0, 50))));
    EXPECT_TRUE(map.addRangeMapping(
        rng(loc(t1, 100), loc(t1, 200)),
        rng(outLoc(c1, 1000), outLoc(c1, 2000))));

    // Поиск изолирован по файлам
    EXPECT_TRUE(map.getMapTrustToCpp(loc(t0, 3)).has_value());
    EXPECT_TRUE(map.getMapCppToTrust(outLoc(c0, 30)).has_value());
    EXPECT_FALSE(map.getMapTrustToCpp(loc(t1, 3)).has_value()); // нет в t0
    EXPECT_FALSE(map.getMapCppToTrust(outLoc(c1, 30)).has_value());
}

TEST(MappingTest, GetMapCppToTrust_RejectsNonOutput) {
    SourceMapping map;
    FileIdx trustSrc = fi(0);
    FileIdx cppSrc   = fo(10);

    EXPECT_TRUE(map.addRangeMapping(
        rng(loc(trustSrc, 1), loc(trustSrc, 5)),
        rng(outLoc(cppSrc,   100), outLoc(cppSrc,   200))));

    // Input-локация (не output) для cpp → не найдёт
    // Создаём SourceLoc с тем же raw индекса (без OUTPUT_FLAG)
    FileIdx fakeInput{(cppSrc.raw & ((1 << FileIdx::FILEIDX_BITS) - 1))}; // только индекс, без флага
    EXPECT_FALSE(map.getMapCppToTrust(loc(fakeInput, 150)).has_value());
    // Output-локация → найдёт
    EXPECT_TRUE(map.getMapCppToTrust(outLoc(cppSrc, 150)).has_value());
}

// ══════════════════════════════════════════════════════════════
//                    SourceMapping: NameMapping
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, AddNameMapping_ForwardAndBackward) {
    SourceMapping map;
    FileIdx t = fi(0);
    FileIdx c = fo(10);

    EXPECT_TRUE(map.addNameMapping(
        rng(loc(t, 10), loc(t, 15)),
        rng(outLoc(c, 100), outLoc(c, 105)),
        "x", "_x"));

    // Поиск по точке на BEGIN диапазона + имени
    auto fwd = map.getCppName(loc(t, 10), "x");
    ASSERT_TRUE(fwd.has_value());
    EXPECT_EQ(fwd->range.to.begin.offset(), 100);
    EXPECT_EQ(fwd->range.to.end.offset(),   105);
    EXPECT_EQ(fwd->trustName, "x");
    EXPECT_EQ(fwd->cppName, "_x");

    // Точка ВНУТРИ → сдвиг
    fwd = map.getCppName(loc(t, 12), "x");
    ASSERT_TRUE(fwd.has_value());
    EXPECT_EQ(fwd->range.to.begin.offset(), 102); // 100 + (12-10)
    EXPECT_EQ(fwd->range.to.end.offset(),   107); // 105 + (12-10)

    // Обратный поиск по output-локации
    auto bwd = map.getTrustName(outLoc(c, 100), "_x");
    ASSERT_TRUE(bwd.has_value());
    EXPECT_EQ(bwd->range.from.begin.offset(), 10);
    EXPECT_EQ(bwd->range.from.end.offset(),   15);

    // Точка внутри со сдвигом
    bwd = map.getTrustName(outLoc(c, 102), "_x");
    ASSERT_TRUE(bwd.has_value());
    EXPECT_EQ(bwd->range.from.begin.offset(), 12); // 10 + (102-100)
    EXPECT_EQ(bwd->range.from.end.offset(),   17); // 15 + (102-100)
}

TEST(MappingTest, AddNameMapping_WrongName) {
    SourceMapping map;
    FileIdx t = fi(0);
    FileIdx c = fo(10);

    EXPECT_TRUE(map.addNameMapping(
        rng(loc(t, 10), loc(t, 15)),
        rng(outLoc(c, 100), outLoc(c, 105)),
        "x", "_x"));

    EXPECT_FALSE(map.getCppName(loc(t, 12), "y").has_value());
    EXPECT_FALSE(map.getTrustName(outLoc(c, 102), "y").has_value());
}

TEST(MappingTest, AddNameMapping_EmptyName) {
    SourceMapping map;
    FileIdx t = fi(0);
    FileIdx c = fo(10);

    EXPECT_TRUE(map.addNameMapping(
        rng(loc(t, 10), loc(t, 15)),
        rng(outLoc(c, 100), outLoc(c, 105)),
        "", ""));

    auto fwd = map.getCppName(loc(t, 10), "");
    ASSERT_TRUE(fwd.has_value());
    EXPECT_EQ(fwd->cppName, "");
    EXPECT_EQ(fwd->trustName, "");

    auto bwd = map.getTrustName(outLoc(c, 100), "");
    ASSERT_TRUE(bwd.has_value());
    EXPECT_EQ(bwd->cppName, "");
    EXPECT_EQ(bwd->trustName, "");
}

TEST(MappingTest, AddNameMapping_MultipleNames) {
    SourceMapping map;
    FileIdx t = fi(0);
    FileIdx c = fo(10);

    EXPECT_TRUE(map.addNameMapping(
        rng(loc(t,  1), loc(t,  5)),
        rng(outLoc(c, 10), outLoc(c, 50)),
        "a", "_a"));
    EXPECT_TRUE(map.addNameMapping(
        rng(loc(t, 10), loc(t, 20)),
        rng(outLoc(c, 100), outLoc(c, 200)),
        "b", "_b"));

    auto fwd_a = map.getCppName(loc(t, 1), "a");
    ASSERT_TRUE(fwd_a.has_value());
    EXPECT_EQ(fwd_a->cppName, "_a");

    auto fwd_b = map.getCppName(loc(t, 10), "b");
    ASSERT_TRUE(fwd_b.has_value());
    EXPECT_EQ(fwd_b->cppName, "_b");

    auto bwd_a = map.getTrustName(outLoc(c, 10), "_a");
    ASSERT_TRUE(bwd_a.has_value());
    EXPECT_EQ(bwd_a->trustName, "a");

    auto bwd_b = map.getTrustName(outLoc(c, 100), "_b");
    ASSERT_TRUE(bwd_b.has_value());
    EXPECT_EQ(bwd_b->trustName, "b");
}

TEST(MappingTest, AddNameMapping_PointOutsideNameRange) {
    SourceMapping map;
    FileIdx t = fi(0);
    FileIdx c = fo(10);

    EXPECT_TRUE(map.addNameMapping(
        rng(loc(t, 10), loc(t, 20)),
        rng(outLoc(c, 100), outLoc(c, 200)),
        "x", "_x"));

    EXPECT_FALSE(map.getCppName(loc(t, 5), "x").has_value());
    EXPECT_FALSE(map.getCppName(loc(t, 25), "x").has_value());
    EXPECT_FALSE(map.getTrustName(outLoc(c, 50), "_x").has_value());
    EXPECT_FALSE(map.getTrustName(outLoc(c, 250), "_x").has_value());
}

// ══════════════════════════════════════════════════════════════
//                    Serialization
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, PackUnpack_Roundtrip) {
    // Используем смещения, не требующие OUTPUT_FLAG в packed,
    // чтобы msgpack корректно сериализовал.
    SourceMapping map;
    FileIdx t = fi(0);
    FileIdx c = fo(10);

    EXPECT_TRUE(map.addRangeMapping(
        rng(loc(t, 1), loc(t, 5)),
        rng(outLoc(c, 100), outLoc(c, 500))));
    EXPECT_TRUE(map.addNameMapping(
        rng(loc(t, 10), loc(t, 15)),
        rng(outLoc(c, 1000), outLoc(c, 1500)),
        "var", "_var"));

    std::vector<unsigned char> data = map.pack();
    ASSERT_FALSE(data.empty());

    SourceMapping restored;
    EXPECT_TRUE(restored.unpack(data.data(), data.size()));

    // Прямой поиск по точке на BEGIN диапазона
    auto fwd = restored.getMapTrustToCpp(loc(t, 1));
    ASSERT_TRUE(fwd.has_value());
    EXPECT_EQ(fwd->begin.offset(), 100);
    EXPECT_EQ(fwd->end.offset(),   500);

    // Обратный поиск по output-локации
    auto bwd = restored.getMapCppToTrust(outLoc(c, 100));
    ASSERT_TRUE(bwd.has_value());
    EXPECT_EQ(bwd->begin.offset(), 1);
    EXPECT_EQ(bwd->end.offset(),   5);

    // NameMapping по точке на BEGIN
    auto nfwd = restored.getCppName(loc(t, 10), "var");
    ASSERT_TRUE(nfwd.has_value());
    EXPECT_EQ(nfwd->range.to.begin.offset(), 1000);
    EXPECT_EQ(nfwd->range.to.end.offset(),   1500);
    EXPECT_EQ(nfwd->trustName, "var");
    EXPECT_EQ(nfwd->cppName, "_var");

    auto nbwd = restored.getTrustName(outLoc(c, 1000), "_var");
    ASSERT_TRUE(nbwd.has_value());
    EXPECT_EQ(nbwd->range.from.begin.offset(), 10);
    EXPECT_EQ(nbwd->range.from.end.offset(),   15);
    EXPECT_EQ(nbwd->trustName, "var");
    EXPECT_EQ(nbwd->cppName, "_var");
}

TEST(MappingTest, PackUnpack_Empty) {
    SourceMapping map;
    auto data = map.pack();
    ASSERT_FALSE(data.empty());

    SourceMapping restored;
    EXPECT_TRUE(restored.unpack(data.data(), data.size()));

    FileIdx src = fi(0);
    EXPECT_FALSE(restored.getMapTrustToCpp(loc(src, 10)).has_value());
    EXPECT_FALSE(restored.getMapCppToTrust(outLoc(src, 10)).has_value());
    EXPECT_FALSE(restored.getCppName(loc(src, 10), "x").has_value());
}

TEST(MappingTest, PackUnpack_InvalidData) {
    SourceMapping map;
    EXPECT_FALSE(map.unpack(nullptr, 0));

    unsigned char garbage[] = {0x01, 0x02, 0xFF, 0xFE};
    EXPECT_FALSE(map.unpack(garbage, sizeof(garbage)));
}

TEST(MappingTest, PackUnpack_MultipleEntries) {
    SourceMapping map;
    FileIdx t0 = fi(0);
    FileIdx t1 = fi(1);
    FileIdx c0 = fo(10);
    FileIdx c1 = fo(11);

    EXPECT_TRUE(map.addRangeMapping(
        rng(loc(t0, 1), loc(t0, 5)),
        rng(outLoc(c0, 100), outLoc(c0, 500))));
    EXPECT_TRUE(map.addRangeMapping(
        rng(loc(t1, 10), loc(t1, 20)),
        rng(outLoc(c1, 1000), outLoc(c1, 2000))));
    EXPECT_TRUE(map.addNameMapping(
        rng(loc(t0, 50), loc(t0, 55)),
        rng(outLoc(c0, 5000), outLoc(c0, 5500)),
        "x", "_x"));

    auto data = map.pack();
    SourceMapping restored;
    ASSERT_TRUE(restored.unpack(data.data(), data.size()));

    auto fwd0 = restored.getMapTrustToCpp(loc(t0, 1));
    ASSERT_TRUE(fwd0.has_value());
    EXPECT_EQ(fwd0->begin.offset(), 100);

    auto fwd1 = restored.getMapTrustToCpp(loc(t1, 10));
    ASSERT_TRUE(fwd1.has_value());
    EXPECT_EQ(fwd1->begin.offset(), 1000);

    auto n = restored.getCppName(loc(t0, 50), "x");
    ASSERT_TRUE(n.has_value());
    EXPECT_EQ(n->trustName, "x");
    EXPECT_EQ(n->cppName, "_x");
}

// ══════════════════════════════════════════════════════════════
//                Edge Cases и граничные условия
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, IdenticalRanges) {
    SourceMapping map;
    FileIdx trustSrc = fi(0);
    FileIdx cppSrc   = fo(10);

    EXPECT_TRUE(map.addRangeMapping(
        rng(loc(trustSrc, 10), loc(trustSrc, 20)),
        rng(outLoc(cppSrc, 10), outLoc(cppSrc, 20))));

    // Точка на begin
    auto fwd = map.getMapTrustToCpp(loc(trustSrc, 10));
    ASSERT_TRUE(fwd.has_value());
    EXPECT_EQ(fwd->begin.offset(), 10);
    EXPECT_EQ(fwd->end.offset(), 20);

    auto bwd = map.getMapCppToTrust(outLoc(cppSrc, 10));
    ASSERT_TRUE(bwd.has_value());
    EXPECT_EQ(bwd->begin.offset(), 10);
    EXPECT_EQ(bwd->end.offset(), 20);
}

TEST(MappingTest, GetMapTrustToCpp_NotFound) {
    SourceMapping map;
    FileIdx src = fi(0);
    EXPECT_FALSE(map.getMapTrustToCpp(loc(src, 10)).has_value());
}

TEST(MappingTest, GetMapCppToTrust_NotFound) {
    SourceMapping map;
    FileIdx src = fi(0);
    EXPECT_FALSE(map.getMapCppToTrust(outLoc(src, 10)).has_value());
}

TEST(MappingTest, GetCppName_NotFound) {
    SourceMapping map;
    FileIdx src = fi(0);
    EXPECT_FALSE(map.getCppName(loc(src, 10), "nonexistent").has_value());
}

TEST(MappingTest, GetTrustName_NotFound) {
    SourceMapping map;
    FileIdx src = fi(0);
    EXPECT_FALSE(map.getTrustName(outLoc(src, 10), "nonexistent").has_value());
}

TEST(MappingTest, GetMapTrustToCpp_RejectsOutputLoc) {
    SourceMapping map;
    FileIdx trustSrc = fi(0);
    EXPECT_TRUE(map.addRangeMapping(
        rng(loc(trustSrc, 1), loc(trustSrc, 5)),
        rng(outLoc(trustSrc, 100), outLoc(trustSrc, 500))));
    // output-локация в getMapTrustToCpp → не найдёт
    // Создаём SourceLoc с OUTPUT_FLAG явно
    uint32_t base = trustSrc.raw << SourceLoc::FILEIDX_SHIFT;
    uint32_t outputFlag = SourceLoc::OUTPUT_FLAG;
    SourceLoc outputLoc{static_cast<uint32_t>(outputFlag | base | 3)};
    EXPECT_FALSE(map.getMapTrustToCpp(outputLoc).has_value());
}

TEST(MappingTest, GetMapCppToTrust_RejectsNonOutputLoc) {
    SourceMapping map;
    FileIdx src = fi(0);
    EXPECT_TRUE(map.addRangeMapping(
        rng(loc(src, 1), loc(src, 5)),
        rng(outLoc(src, 100), outLoc(src, 500))));
    // не-output-локация в getMapCppToTrust → не найдёт
    EXPECT_FALSE(map.getMapCppToTrust(loc(src, 150)).has_value());
}

TEST(MappingTest, AddRangeMapping_NullRanges) {
    SourceMapping map;
    // Невалидные диапазоны
    EXPECT_FALSE(map.addRangeMapping(
        rng(SourceLoc::invalid(), SourceLoc::invalid()),
        rng(SourceLoc::invalid(), SourceLoc::invalid())));
}

TEST(MappingTest, AddRangeMapping_Monotonicity) {
    SourceMapping map;
    FileIdx src = fi(0);

    EXPECT_TRUE(map.addRangeMapping(
        rng(loc(src, 10), loc(src, 20)),
        rng(outLoc(src, 100), outLoc(src, 200))));

    // Диапазон с begin < предыдущего end → нарушение монотонности
    EXPECT_FALSE(map.addRangeMapping(
        rng(loc(src, 20), loc(src, 25)),   // begin == предыдущего end → перекрытие
        rng(outLoc(src, 200), outLoc(src, 250))));

    // Неперекрывающийся диапазон с большим begin → ok
    EXPECT_TRUE(map.addRangeMapping(
        rng(loc(src, 21), loc(src, 30)),   // begin > предыдущего end → ok
        rng(outLoc(src, 210), outLoc(src, 300))));
}
