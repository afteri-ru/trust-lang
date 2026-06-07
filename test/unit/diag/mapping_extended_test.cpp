#include "diag/context.hpp"
#include "diag/mapper.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace trust;

// ── Helper: запись msgpack в файл ──
static void writeMsgpackToFile(const std::string& path, const std::vector<unsigned char>& data) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        ADD_FAILURE() << "Cannot write " << path;
        return;
    }
    ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    ofs.close();
    if (!ofs) {
        ADD_FAILURE() << "Write error " << path;
    }
}

// ── Helper: чтение msgpack из файла ──
static std::vector<unsigned char> readMsgpackFromFile(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) {
        ADD_FAILURE() << "Cannot read " << path;
        return {};
    }
    std::streamsize size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(static_cast<size_t>(size));
    ifs.read(reinterpret_cast<char*>(data.data()), size);
    if (!ifs) {
        ADD_FAILURE() << "Read error " << path;
        return {};
    }
    return data;
}

// ══════════════════════════════════════════════════════════════
//   findRangesByLine — вложенные с разными begin
// ══════════════════════════════════════════════════════════════

// Два вложенных диапазона:
//   small: trust [10,20] → cpp [100,110]
//   large: trust [5,30]  → cpp [200,225]
// Позиция column=15 попадает в оба → от меньшего к большему
TEST(MappingExtTest, FindRangesByLine_Nested) {
    Context ctx;
    MapperFile src = ctx.source().add_source("src", std::string(50, 'a'), false);
    ASSERT_FALSE(src.isInvalid());
    MapperFile cpp = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cpp, std::string(300, 'a'));
    ASSERT_FALSE(cpp.isInvalid());

    ASSERT_TRUE(ctx.source().addRangeMapping(ctx.source().makeRange(ctx.source().makeLoc(src, 10), ctx.source().makeLoc(src, 20)),
                                             ctx.source().makeRange(ctx.source().makeLoc(cpp, 100), ctx.source().makeLoc(cpp, 110))));
    ASSERT_TRUE(ctx.source().addRangeMapping(ctx.source().makeRange(ctx.source().makeLoc(src, 5), ctx.source().makeLoc(src, 30)),
                                             ctx.source().makeRange(ctx.source().makeLoc(cpp, 200), ctx.source().makeLoc(cpp, 225))));

    auto reader = ctx.source().toReader();

    // Позиция column=15 ∈ [10,20] и [5,30]
    // small: 100+(15-10)=105, 110+(15-10)=115 → size=10
    // large: 200+(15-5)=210, 225+(15-5)=235  → size=25
    auto result = reader->findRangesByLine(ReaderFile::from(src), 1, 15);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].end, 115); // small (size=10, меньше)
    EXPECT_EQ(result[1].end, 235); // large (size=25, больше)
}

// Три вложенных диапазона:
//   small: [10,15]  → cpp [100,105]
//   med:   [8,25]   → cpp [200,217]
//   large: [5,40]   → cpp [300,335]
TEST(MappingExtTest, FindRangesByLine_TripleNested) {
    Context ctx;
    MapperFile src = ctx.source().add_source("src", std::string(50, 'a'), false);
    ASSERT_FALSE(src.isInvalid());
    MapperFile cpp = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cpp, std::string(400, 'a'));
    ASSERT_FALSE(cpp.isInvalid());

    ASSERT_TRUE(ctx.source().addRangeMapping(ctx.source().makeRange(ctx.source().makeLoc(src, 10), ctx.source().makeLoc(src, 15)),
                                             ctx.source().makeRange(ctx.source().makeLoc(cpp, 100), ctx.source().makeLoc(cpp, 105))));
    ASSERT_TRUE(ctx.source().addRangeMapping(ctx.source().makeRange(ctx.source().makeLoc(src, 8), ctx.source().makeLoc(src, 25)),
                                             ctx.source().makeRange(ctx.source().makeLoc(cpp, 200), ctx.source().makeLoc(cpp, 217))));
    ASSERT_TRUE(ctx.source().addRangeMapping(ctx.source().makeRange(ctx.source().makeLoc(src, 5), ctx.source().makeLoc(src, 40)),
                                             ctx.source().makeRange(ctx.source().makeLoc(cpp, 300), ctx.source().makeLoc(cpp, 335))));

    auto reader = ctx.source().toReader();

    auto result = reader->findRangesByLine(ReaderFile::from(src), 1, 12);
    ASSERT_EQ(result.size(), 3u);
    // small: 100+(12-10)=102, 105+(12-10)=107 → size=5
    EXPECT_EQ(result[0].end, 107);
    // med:   200+(12-8)=204,  217+(12-8)=221   → size=17
    EXPECT_EQ(result[1].end, 221);
    // large: 300+(12-5)=307,  335+(12-5)=342   → size=35
    EXPECT_EQ(result[2].end, 342);
}

// ══════════════════════════════════════════════════════════════
//   Нагрузочный тест: 300+ маппингов → pack → файл → from файла → сравнение
//   Уникальность begin гарантируется с обеих сторон
// ══════════════════════════════════════════════════════════════

TEST(MappingExtTest, StressTest_500Mappings) {
    constexpr int NUM_RANGES = 300;
    constexpr int NUM_NAMES = 100;
    Context ctx;
    MapperFile src = ctx.source().add_source("src", std::string(5000, 'a'), false);
    ASSERT_FALSE(src.isInvalid());
    MapperFile cpp = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cpp, std::string(10000, 'a'));
    ASSERT_FALSE(cpp.isInvalid());

    std::mt19937 rng(42);
    std::set<uint32_t> usedBegins;

    // ── 1. addRangeMapping: 300 диапазонов с уникальными key с обеих сторон ──
    for (int i = 0; i < NUM_RANGES; ++i) {
        uint32_t tBegin, cBegin;
        do {
            tBegin = rng() % 4000 + 1;
        } while (usedBegins.count(tBegin));
        usedBegins.insert(tBegin);

        do {
            cBegin = rng() % 8000 + 1;
        } while (usedBegins.count(cBegin));
        usedBegins.insert(cBegin);

        uint32_t tLen = rng() % 100 + 1;
        uint32_t tEnd = std::min(tBegin + tLen, 5000u);
        uint32_t cLen = tEnd - tBegin;
        uint32_t cEnd = std::min(cBegin + cLen, 10000u);

        ASSERT_TRUE(ctx.source().addRangeMapping(ctx.source().makeRange(ctx.source().makeLoc(src, tBegin), ctx.source().makeLoc(src, tEnd)),
                                                 ctx.source().makeRange(ctx.source().makeLoc(cpp, cBegin), ctx.source().makeLoc(cpp, cEnd))));
    }

    // ── 2. addNameMapping: 100 именованных маппингов ──
    for (int i = 0; i < NUM_NAMES; ++i) {
        uint32_t tBegin = rng() % 4000 + 1;
        uint32_t tLen = rng() % 50 + 1;
        uint32_t tEnd = std::min(tBegin + tLen, 5000u);
        uint32_t cBegin = rng() % 8000 + 1;
        uint32_t cLen = tEnd - tBegin;
        uint32_t cEnd = std::min(cBegin + cLen, 10000u);

        std::string trustName = "func_" + std::to_string(i);
        std::string cppName = "_func_" + std::to_string(i);

        ASSERT_TRUE(ctx.source().addNameMapping(ctx.source().makeRange(ctx.source().makeLoc(src, tBegin), ctx.source().makeLoc(src, tEnd)),
                                                ctx.source().makeRange(ctx.source().makeLoc(cpp, cBegin), ctx.source().makeLoc(cpp, cEnd)), trustName,
                                                cppName));
    }

    // ── 3. addNameMapping внутри диапазона (offsets > 4000) ──
    for (int i = 0; i < 50; ++i) {
        uint32_t tBegin = 4620 + i * 2;
        uint32_t tEnd = tBegin + 1;
        uint32_t cBegin = 9020 + i * 4;
        uint32_t cEnd = cBegin + 5;

        ASSERT_TRUE(ctx.source().addNameMapping(ctx.source().makeRange(ctx.source().makeLoc(src, tBegin), ctx.source().makeLoc(src, tEnd)),
                                                ctx.source().makeRange(ctx.source().makeLoc(cpp, cBegin), ctx.source().makeLoc(cpp, cEnd)),
                                                "inner_" + std::to_string(i), "_inner_" + std::to_string(i)));
    }
    // ── Сериализация → файл → десериализация из файла ──
    const auto* readerBefore = ctx.source().toReader();
    ASSERT_NE(readerBefore, nullptr);

    auto packed = readerBefore->packToMsgpack();
    ASSERT_FALSE(packed.empty());

    // Сохраняем на диск
    std::string filePath = TEST_DATA_DIR "/stress_500.msgpack";
    writeMsgpackToFile(filePath, packed);

    // Загружаем из файла
    auto loadedData = readMsgpackFromFile(filePath);
    ASSERT_EQ(loadedData.size(), packed.size());

    auto readerAfter = SourceMapReader::fromMsgpack(loadedData.data(), loadedData.size());
    ASSERT_NE(readerAfter, nullptr);

    // Сравнение findRangesByLine
    std::vector<uint32_t> testColumns = {1, 5, 10, 50, 100, 500, 1000, 2000, 3000, 4000};
    for (uint32_t col : testColumns) {
        auto before = readerBefore->findRangesByLine(ReaderFile::from(src), 1, col);
        auto after = readerAfter->findRangesByLine(ReaderFile::from(src), 1, col);

        ASSERT_EQ(before.size(), after.size()) << "Mismatch at column=" << col;
        for (size_t j = 0; j < before.size(); ++j) {
            EXPECT_EQ(before[j].begin, after[j].begin) << "begin at col=" << col << " idx=" << j;
            EXPECT_EQ(before[j].end, after[j].end) << "end at col=" << col << " idx=" << j;
        }
    }

    // Сравнение getCppName на случайных позициях
    for (int i = 0; i < 20; ++i) {
        uint32_t trustNameIdx = rng() % NUM_NAMES;
        uint32_t col = rng() % 4000 + 100;
        std::string trustName = "func_" + std::to_string(trustNameIdx);

        auto before = readerBefore->getCppName(readerBefore->makeLoc(ReaderFile::from(src), col), trustName);
        auto after = readerAfter->getCppName(readerAfter->makeLoc(ReaderFile::from(src), col), trustName);

        EXPECT_EQ(before.has_value(), after.has_value());
        if (before.has_value()) {
            EXPECT_EQ(before->rangeMap.to.begin, after->rangeMap.to.begin);
            EXPECT_EQ(before->rangeMap.to.end, after->rangeMap.to.end);
            EXPECT_EQ(before->toName, after->toName);
        }
    }

    // Сравнение getTrustFileMappings
    auto fileBefore = readerBefore->getTrustFileMappings(ReaderFile::from(src));
    auto fileAfter = readerAfter->getTrustFileMappings(ReaderFile::from(src));
    EXPECT_EQ(fileBefore.size(), fileAfter.size());
    for (size_t i = 0; i < std::min(fileBefore.size(), fileAfter.size()); ++i) {
        EXPECT_EQ(fileBefore[i].from.begin, fileAfter[i].from.begin);
        EXPECT_EQ(fileBefore[i].from.end, fileAfter[i].from.end);
        EXPECT_EQ(fileBefore[i].to.begin, fileAfter[i].to.begin);
        EXPECT_EQ(fileBefore[i].to.end, fileAfter[i].to.end);
    }

    // Проверка обратного поиска (output → input)
    for (int i = 0; i < 50; ++i) {
        uint32_t col = rng() % 8000 + 100;
        auto before = readerBefore->findRangesByLine(ReaderFile::from(cpp), 1, col);
        auto after = readerAfter->findRangesByLine(ReaderFile::from(cpp), 1, col);

        ASSERT_EQ(before.size(), after.size()) << "Output mismatch at column=" << col;
        for (size_t j = 0; j < before.size(); ++j) {
            EXPECT_EQ(before[j].begin, after[j].begin);
            EXPECT_EQ(before[j].end, after[j].end);
        }
    }
}

// ══════════════════════════════════════════════════════════════
//   Нагрузочный тест: 1000 addRangeMapping → файл → загрузка → сравнение
//   Уникальность begin гарантируется с обеих сторон
// ══════════════════════════════════════════════════════════════

TEST(MappingExtTest, StressTest_1000RangeMappings) {
    Context ctx;
    MapperFile src = ctx.source().add_source("src", std::string(10000, 'a'), false);
    ASSERT_FALSE(src.isInvalid());
    MapperFile cpp = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cpp, std::string(20000, 'a'));
    ASSERT_FALSE(cpp.isInvalid());

    std::mt19937 rng(123);
    constexpr int COUNT = 1000;
    std::set<uint32_t> usedBegins;

    for (int i = 0; i < COUNT; ++i) {
        uint32_t tBegin, cBegin;
        do {
            tBegin = rng() % 8000 + 1;
        } while (usedBegins.count(tBegin));
        usedBegins.insert(tBegin);

        do {
            cBegin = rng() % 18000 + 1;
        } while (usedBegins.count(cBegin));
        usedBegins.insert(cBegin);

        uint32_t tLen = rng() % 200 + 1;
        uint32_t tEnd = std::min(tBegin + tLen, 10000u);
        uint32_t cLen = tEnd - tBegin;
        uint32_t cEnd = std::min(cBegin + cLen, 20000u);

        ASSERT_TRUE(ctx.source().addRangeMapping(ctx.source().makeRange(ctx.source().makeLoc(src, tBegin), ctx.source().makeLoc(src, tEnd)),
                                                 ctx.source().makeRange(ctx.source().makeLoc(cpp, cBegin), ctx.source().makeLoc(cpp, cEnd))));
    }

    const auto* readerBefore = ctx.source().toReader();
    ASSERT_NE(readerBefore, nullptr);

    auto packed = readerBefore->packToMsgpack();
    ASSERT_FALSE(packed.empty());

    // Сохраняем на диск
    std::string filePath = TEST_DATA_DIR "/stress_1000.msgpack";
    writeMsgpackToFile(filePath, packed);

    // Загружаем из файла
    auto loadedData = readMsgpackFromFile(filePath);
    ASSERT_EQ(loadedData.size(), packed.size());

    auto readerAfter = SourceMapReader::fromMsgpack(loadedData.data(), loadedData.size());
    ASSERT_NE(readerAfter, nullptr);

    // Сравнение findRangesByLine
    for (int i = 1; i <= 50; ++i) {
        uint32_t col = static_cast<uint32_t>(i * 200);
        auto before = readerBefore->findRangesByLine(ReaderFile::from(src), 1, col);
        auto after = readerAfter->findRangesByLine(ReaderFile::from(src), 1, col);

        ASSERT_EQ(before.size(), after.size()) << "Mismatch at column=" << col;
        for (size_t j = 0; j < before.size(); ++j) {
            EXPECT_EQ(before[j].begin, after[j].begin);
            EXPECT_EQ(before[j].end, after[j].end);
        }
    }

    // Сравнение getTrustFileMappings
    auto fileBefore = readerBefore->getTrustFileMappings(ReaderFile::from(src));
    auto fileAfter = readerAfter->getTrustFileMappings(ReaderFile::from(src));
    EXPECT_EQ(fileBefore.size(), fileAfter.size());
    for (size_t i = 0; i < std::min(fileBefore.size(), fileAfter.size()); ++i) {
        EXPECT_EQ(fileBefore[i].from.begin, fileAfter[i].from.begin);
        EXPECT_EQ(fileBefore[i].from.end, fileAfter[i].from.end);
    }
}

// ══════════════════════════════════════════════════════════════
//   Пограничные случаи: пустой файл
// ══════════════════════════════════════════════════════════════

TEST(MappingExtTest, FindRangesByLine_EmptyFile) {
    Context ctx;
    MapperFile src = ctx.source().add_source("src", "", false);
    ASSERT_FALSE(src.isInvalid());
    MapperFile cpp = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cpp, std::string(100, 'a'));
    ASSERT_FALSE(cpp.isInvalid());

    // Пустой файл — не должно быть маппингов, результат пустой
    auto reader = ctx.source().toReader();
    auto result = reader->findRangesByLine(ReaderFile::from(src), 1, 1);
    EXPECT_TRUE(result.empty());
}

// ══════════════════════════════════════════════════════════════
//   Пограничные случаи: column 0 (должен трактоваться как 1)
// ══════════════════════════════════════════════════════════════

TEST(MappingExtTest, FindRangesByLine_ColumnZero) {
    Context ctx;
    MapperFile src = ctx.source().add_source("src", std::string(50, 'a'), false);
    ASSERT_FALSE(src.isInvalid());
    MapperFile cpp = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cpp, std::string(200, 'a'));
    ASSERT_FALSE(cpp.isInvalid());

    ASSERT_TRUE(ctx.source().addRangeMapping(ctx.source().makeRange(ctx.source().makeLoc(src, 10), ctx.source().makeLoc(src, 20)),
                                             ctx.source().makeRange(ctx.source().makeLoc(cpp, 100), ctx.source().makeLoc(cpp, 110))));

    auto reader = ctx.source().toReader();

    // column=0 и column=1 должны дать одинаковый результат
    auto result0 = reader->findRangesByLine(ReaderFile::from(src), 1, 0);
    auto result1 = reader->findRangesByLine(ReaderFile::from(src), 1, 1);
    ASSERT_EQ(result0.size(), result1.size());
    if (!result0.empty()) {
        EXPECT_EQ(result0[0].begin, result1[0].begin);
        EXPECT_EQ(result0[0].end, result1[0].end);
    }
}

// ══════════════════════════════════════════════════════════════
//   Пограничные случаи: column отсутствует (только line)
// ══════════════════════════════════════════════════════════════

TEST(MappingExtTest, FindRangesByLine_LineOnly) {
    Context ctx;
    MapperFile src = ctx.source().add_source("src", std::string(50, 'a'), false);
    ASSERT_FALSE(src.isInvalid());
    MapperFile cpp = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cpp, std::string(300, 'a'));
    ASSERT_FALSE(cpp.isInvalid());

    ASSERT_TRUE(ctx.source().addRangeMapping(ctx.source().makeRange(ctx.source().makeLoc(src, 10), ctx.source().makeLoc(src, 20)),
                                             ctx.source().makeRange(ctx.source().makeLoc(cpp, 100), ctx.source().makeLoc(cpp, 110))));
    ASSERT_TRUE(ctx.source().addRangeMapping(ctx.source().makeRange(ctx.source().makeLoc(src, 1), ctx.source().makeLoc(src, 30)),
                                             ctx.source().makeRange(ctx.source().makeLoc(cpp, 200), ctx.source().makeLoc(cpp, 229))));

    auto reader = ctx.source().toReader();

    // Без column (по умолчанию 1) — только для line=1, column=1
    // small [10,20] не содержит column=1, large [1,30] содержит
    auto result = reader->findRangesByLine(ReaderFile::from(src), 1);
    ASSERT_EQ(result.size(), 1u);
    // large: [1,30], column=1 → 200+(1-1)=200, 229+(1-1)=229
    EXPECT_EQ(result[0].begin, 200);
    EXPECT_EQ(result[0].end, 229);
}

// ══════════════════════════════════════════════════════════════
//   Пограничные случаи: мультилайн
// ══════════════════════════════════════════════════════════════

TEST(MappingExtTest, FindRangesByLine_MultiLine) {
    Context ctx;
    // Строка 1: 10 байт, строка 2: 20 байт
    MapperFile src = ctx.source().add_source("src", std::string(10, 'a') + "\n" + std::string(20, 'a'), false);
    ASSERT_FALSE(src.isInvalid());
    MapperFile cpp = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cpp, std::string(200, 'a'));
    ASSERT_FALSE(cpp.isInvalid());

    // Диапазон на второй строке: src line=2, offset от 12 до 20 (1-based)
    //      10 ('a'*10) + 1 ('\n') = позиция 12 = начало строки 2
    ASSERT_TRUE(ctx.source().addRangeMapping(ctx.source().makeRange(ctx.source().makeLoc(src, 12), ctx.source().makeLoc(src, 20)),
                                             ctx.source().makeRange(ctx.source().makeLoc(cpp, 100), ctx.source().makeLoc(cpp, 108))));
    // Диапазон на первой строке
    ASSERT_TRUE(ctx.source().addRangeMapping(ctx.source().makeRange(ctx.source().makeLoc(src, 3), ctx.source().makeLoc(src, 8)),
                                             ctx.source().makeRange(ctx.source().makeLoc(cpp, 50), ctx.source().makeLoc(cpp, 55))));

    auto reader = ctx.source().toReader();

    // Поиск на второй строке, column 4
    // loc = начало_строки_2 + (4-1) = offset 12 + 3 = 15
    // range: [12,20] → 100+(15-12)=103, 108+(15-12)=111
    auto result = reader->findRangesByLine(ReaderFile::from(src), 2, 4);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].begin, 103);
    EXPECT_EQ(result[0].end, 111);

    // Поиск на первой строке, column 5 → loc = 5
    result = reader->findRangesByLine(ReaderFile::from(src), 1, 5);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].begin, 52); // 50+(5-3)
    EXPECT_EQ(result[0].end, 57);   // 55+(5-3)
}

// ══════════════════════════════════════════════════════════════
//   Пограничные случаи: line за пределами файла
// ══════════════════════════════════════════════════════════════

TEST(MappingExtTest, FindRangesByLine_LineOutOfRange) {
    Context ctx;
    MapperFile src = ctx.source().add_source("src", std::string(50, 'a'), false);
    ASSERT_FALSE(src.isInvalid());
    MapperFile cpp = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cpp, std::string(200, 'a'));
    ASSERT_FALSE(cpp.isInvalid());

    ASSERT_TRUE(ctx.source().addRangeMapping(ctx.source().makeRange(ctx.source().makeLoc(src, 10), ctx.source().makeLoc(src, 20)),
                                             ctx.source().makeRange(ctx.source().makeLoc(cpp, 100), ctx.source().makeLoc(cpp, 110))));

    auto reader = ctx.source().toReader();
    auto result = reader->findRangesByLine(ReaderFile::from(src), 100, 1);
    EXPECT_TRUE(result.empty());
}