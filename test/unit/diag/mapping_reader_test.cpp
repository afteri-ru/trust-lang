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
//    Context: line_column / loc_from_line - унаследовано от Mapper
// ══════════════════════════════════════════════════════════════
//
// Контент: "line1\nline2\nline3"
// 0-based:  012345 678901 234567...
// Строка "line1\n" = 6 байт (5 букв + \n)
// "line2" начинается на offset=7 (1-based)
// Символ '2' в "line2" находится на offset=8 (1-based), 0-based index=7

TEST(MappingTest, LineColumn) {
    Context ctx;
    auto idx = ctx.source().add_source("f", "line1\nline2\nline3", false);
    auto loc = ctx.source().makeLoc(idx, 7); // 'l' in "line2", строка 2, колонка 1
    auto lc = ctx.source().line_column(loc);
    EXPECT_EQ(lc.line, 2u);
    EXPECT_EQ(lc.column, 1u);
}

TEST(MappingTest, LocFromLine) {
    Context ctx;
    auto idx = ctx.source().add_source("f", "line1\nline2\nline3", false);
    auto loc = ctx.source().loc_from_line(idx, 2);
    EXPECT_EQ(loc, 7u); // offset 1-based: "line1\n" is 6 chars, so line2 starts at 7
}

// ══════════════════════════════════════════════════════════════
//              findRangesByLine для output файла
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, FindRangesByLine_OutputFile) {
    Context ctx;
    MapperFile trustSrc = ctx.source().add_source("trust", std::string(200, '\n'), false);
    MapperFile cppSrc = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cppSrc, std::string(200, ' '));

    ctx.source().addRangeMapping(ctx.source().makeRange(ctx.source().makeLoc(trustSrc, 2), ctx.source().makeLoc(trustSrc, 5)),
                                 ctx.source().makeRange(ctx.source().makeLoc(cppSrc, 100), ctx.source().makeLoc(cppSrc, 150)));

    auto reader = ctx.source().toReader();

    // Поиск на output-файле через Reader
    ReaderFile rCpp = reader->findFileIdx(ctx.source().filename(cppSrc));
    auto ranges = reader->findRangesByLine(rCpp, 3);
    EXPECT_TRUE(ranges.empty()); // line 3 не существует
}

// ══════════════════════════════════════════════════════════════
//   Тест с prepend'ом и множественными маппингами (комбинация)
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, PrependWithMultipleMappings) {
    Context ctx;
    MapperFile trustSrc = ctx.source().add_source("trust", std::string(200, 'a'), false);
    MapperFile cppSrc = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cppSrc, std::string(200, ' '));
    ctx.source().output_prepend(cppSrc, "PREFIX\n");

    ctx.source().addRangeMapping(ctx.source().makeRange(ctx.source().makeLoc(trustSrc, 10), ctx.source().makeLoc(trustSrc, 20)),
                                 ctx.source().makeRange(ctx.source().makeLoc(cppSrc, 10), ctx.source().makeLoc(cppSrc, 20)));
    ctx.source().addRangeMapping(ctx.source().makeRange(ctx.source().makeLoc(trustSrc, 50), ctx.source().makeLoc(trustSrc, 60)),
                                 ctx.source().makeRange(ctx.source().makeLoc(cppSrc, 50), ctx.source().makeLoc(cppSrc, 60)));

    auto reader = ctx.source().toReader();

    // Prepend сдвигает cpp offset'ы на 7 ("PREFIX\n" = 7 символов)
    // Маппинг 1 в reader: trust [10,20] → cpp [17,27] (10+7=17, 20+7=27)
    // Маппинг 2 в reader: trust [50,60] → cpp [57,67] (50+7=57, 60+7=67)
    // query=57 (cpp), ищем в backward: cpp [57,67], query=57 (delta=0)
    // → trust [50,60] (проекция 0)
    {
        auto loc = static_cast<ReaderLocation>(ctx.source().makeLoc(cppSrc, 57));
        auto r = reader->getMapCppToTrust(loc);
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(r->begin, 50u);
    }
    // query=60 (cpp), ищем в backward: cpp [57,67], query=60 (delta=3)
    // → trust [53,63] (50+3=53, 60+3=63)
    {
        auto loc = static_cast<ReaderLocation>(ctx.source().makeLoc(cppSrc, 60));
        auto r = reader->getMapCppToTrust(loc);
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(r->begin, 53u);
        EXPECT_EQ(r->end, 63u);
    }
}

// ══════════════════════════════════════════════════════════════
//   Reader напрямую: get_input / get_output на ReaderFileIdx
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, ReaderDirectAccess) {
    Context ctx;
    MapperFile trustSrc = ctx.source().add_source("trust", "content1", false);
    MapperFile cppSrc = ctx.source().add_output("cpp", false);
    ctx.source().output_append(cppSrc, "content2");

    auto reader = ctx.source().toReader();
    // Reader использует ReaderFileIdx через безопасную конвертацию
    ReaderFile rTrust = reader->findFileIdx(ctx.source().filename(trustSrc));
    ReaderFile rCpp = reader->findFileIdx(ctx.source().filename(cppSrc));

    EXPECT_EQ(reader->filename(rTrust), "trust");
    EXPECT_EQ(reader->filename(rCpp), "cpp");
    EXPECT_EQ(reader->source(rTrust), "content1");
    EXPECT_EQ(reader->source(rCpp), "content2");

    // get_file напрямую на Reader
    EXPECT_EQ(reader->get_file(rTrust).getSource(), "content1");
    EXPECT_EQ(reader->get_file(rCpp).getSource(), "content2");
}

// ══════════════════════════════════════════════════════════════
//   Reader: toReader возвращает один и тот же указатель (кеш)
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, ToReaderCache) {
    Context ctx;
    ctx.source().add_source("t", "abc", false);
    auto idx = ctx.source().add_output("c", false);
    ctx.source().output_append(idx, "def");

    auto* r1 = ctx.source().toReader();
    auto* r2 = ctx.source().toReader();
    EXPECT_EQ(r1, r2); // кеш возвращает тот же reader
}

// ══════════════════════════════════════════════════════════════
//   Reader: getFileHash
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, Reader_GetFileHash) {
    Context ctx;
    MapperFile trustSrc = ctx.source().add_source("t", "abc", false);

    // hash вычисляется на основе содержимого
    auto reader = ctx.source().toReader();
    uint64_t hash = reader->getFileHash(reader->findFileIdx(ctx.source().filename(trustSrc)));
    uint64_t expected = llvm::MD5Hash("abc");
    EXPECT_EQ(hash, expected);
}

// ══════════════════════════════════════════════════════════════
//   Reader: isOutput / isInput на ReaderFileIdx
// ══════════════════════════════════════════════════════════════

TEST(MappingTest, ReaderFileIdx_IsOutput) {
    Context ctx;
    MapperFile trustSrc = ctx.source().add_source("t", "abc", false);
    MapperFile cppSrc = ctx.source().add_output("c", false);
    ctx.source().output_append(cppSrc, "def");

    auto reader = ctx.source().toReader();
    ReaderFile rTrust = reader->findFileIdx(ctx.source().filename(trustSrc));
    ReaderFile rCpp = reader->findFileIdx(ctx.source().filename(cppSrc));

    EXPECT_FALSE(rTrust.isOutput());
    EXPECT_TRUE(rCpp.isOutput());
}

// ══════════════════════════════════════════════════════════════
//   Reader: get_input с нулевым FileIdx (FAULT)
// ══════════════════════════════════════════════════════════════

TEST(MappingDeathTest, Reader_GetFile_Invalid_ZeroIdx) {
    Context ctx;
    auto cppSrc = ctx.source().add_output("c", false);
    ctx.source().output_append(cppSrc, "def");
    auto reader = ctx.source().toReader();
    EXPECT_THROW(reader->get_file(ReaderFile{}), std::exception);
}

// ══════════════════════════════════════════════════════════════
//              SourceMap::getText
// ══════════════════════════════════════════════════════════════
//
// getText использует offset() напрямую как 0-базированный индекс в substr().
// validateLoc в Context требует offset ∈ [1, size+1].
// Поэтому offset 1 соответствует substr-индексу 1 (второй символ, 'b'),
// а первый символ 'a' (substr-индекс 0) недоступен для Context, т.к. offset=0 не проходит validateLoc.
//
// Контент: "abcdefghij" (10 символов, a..j)
//  0-базированный index: a=0, b=1, c=2, d=3, e=4, f=5, g=6, h=7, i=8, j=9
//  offset → substr index: offset 1 → index 1 ('b')

TEST(MappingTest, GetText_Normal_Input) {
    Context ctx;
    MapperFile idx = ctx.source().add_source("input", "abcdefghij", false);
    // range [1,4) → substr(1, 3) = "bcd" (индексы 1,2,3 = b,c,d)
    auto range = ctx.source().makeRange(ctx.source().makeLoc(idx, 1), ctx.source().makeLoc(idx, 4));
    EXPECT_EQ(ctx.source().getText(range), "abc");
}

TEST(MappingTest, GetText_Normal_Output) {
    Context ctx;
    MapperFile idx = ctx.source().add_output("output", false);
    ctx.source().output_append(idx, "abcdefghij");
    auto range = ctx.source().makeRange(ctx.source().makeLoc(idx, 1), ctx.source().makeLoc(idx, 4));
    EXPECT_EQ(ctx.source().getText(range), "abc");
}

TEST(MappingTest, GetText_FromMiddle) {
    Context ctx;
    MapperFile idx = ctx.source().add_source("input", "abcdefghij", false);
    // range [3,8) → substr(3, 5) = "defgh" (индексы 3,4,5,6,7 = d,e,f,g,h)
    auto range = ctx.source().makeRange(ctx.source().makeLoc(idx, 3), ctx.source().makeLoc(idx, 8));
    EXPECT_EQ(ctx.source().getText(range), "cdefg");
}

TEST(MappingTest, GetText_EntireSuffix) {
    Context ctx;
    MapperFile idx = ctx.source().add_source("input", "abcdefghij", false);
    // range [1,11) → substr(1, 10) = "bcdefghij" (от index 1 до конца: 9 символов b..j)
    auto range = ctx.source().makeRange(ctx.source().makeLoc(idx, 1), ctx.source().makeLoc(idx, 11));
    EXPECT_EQ(ctx.source().getText(range), "abcdefghij");
}

TEST(MappingTest, GetText_SingleChar) {
    Context ctx;
    MapperFile idx = ctx.source().add_source("input", "abcdefghij", false);
    // range [5,6) → substr(5, 1) = "f" (индекс 5 = 'f')
    auto range = ctx.source().makeRange(ctx.source().makeLoc(idx, 5), ctx.source().makeLoc(idx, 6));
    EXPECT_EQ(ctx.source().getText(range), "e");
}

TEST(MappingTest, GetText_EmptyRange) {
    Context ctx;
    MapperFile idx = ctx.source().add_source("input", "abcdefghij", false);
    // range [5,5) → substr(5, 0) = ""
    auto range = ctx.source().makeRange(ctx.source().makeLoc(idx, 5), ctx.source().makeLoc(idx, 5));
    EXPECT_EQ(ctx.source().getText(range), "");
}

TEST(MappingTest, GetText_LastChar) {
    Context ctx;
    MapperFile idx = ctx.source().add_source("input", "abcdefghij", false);
    // Последний символ: j на 0-базированном индексе 9 → offset 9
    // range [9,10) → substr(9, 1) = "j"
    auto range = ctx.source().makeRange(ctx.source().makeLoc(idx, 10), ctx.source().makeLoc(idx, 11));
    EXPECT_EQ(ctx.source().getText(range), "j");
}

// ══════════════════════════════════════════════════════════════
//              SourceMapReader::getText
// ══════════════════════════════════════════════════════════════
//
// getText доступен на SourceMap<ReaderFile> через toReader(),
// так как getText теперь const.

TEST(MappingTest, GetText_OnReader_Input) {
    Context ctx;
    MapperFile idx = ctx.source().add_source("input", "abcdefghij", false);
    auto reader = ctx.source().toReader();
    auto rFile = reader->findFileIdx(ctx.source().filename(idx));
    auto loc1 = static_cast<ReaderLocation>(ctx.source().makeLoc(idx, 1));
    auto loc4 = static_cast<ReaderLocation>(ctx.source().makeLoc(idx, 4));
    ReaderRange range(loc1, loc4); // (1,4) → substr(0, 3) = "abc"
    EXPECT_EQ(reader->getText(range), "abc");
}

TEST(MappingTest, GetText_OnReader_Output) {
    Context ctx;
    MapperFile idx = ctx.source().add_output("output", false);
    ctx.source().output_append(idx, "abcdefghij");
    auto reader = ctx.source().toReader();
    auto rFile = reader->findFileIdx(ctx.source().filename(idx));
    auto loc1 = static_cast<ReaderLocation>(ctx.source().makeLoc(idx, 1));
    auto loc4 = static_cast<ReaderLocation>(ctx.source().makeLoc(idx, 4));
    ReaderRange range(loc1, loc4); // (1,4) → substr(0, 3) = "abc"
    EXPECT_EQ(reader->getText(range), "abc");
}

TEST(MappingDeathTest, GetText_InvalidRange_DefaultConstructed) {
    Context ctx;
    // Range с default-конструктором (begin и end == 0) - невалидный
    MapperRange invalidRange;
    EXPECT_THROW(ctx.source().getText(invalidRange), std::exception);
}

// ══════════════════════════════════════════════════════════════
// Регрессия: кэш line_column не должен коллизировать по offset между
// разными input-файлами. Раньше кэш был ключом только по offset - при
// нескольких файлах с одинаковыми offset'ами возвращались неверные line/col
// (например, name-маппинги для второй строки в trust-lsp).
// ══════════════════════════════════════════════════════════════
TEST(MappingTest, LineColumnCacheDoesNotCollideAcrossFiles) {
    Context ctx;
    // offset 5 в файле A - 'C' (строка 3, колонка 1), в файле B - '5' (строка 1, колонка 5).
    MapperFile a = ctx.source().add_source("A", "A\nB\nC\n", false);
    MapperFile b = ctx.source().add_source("B", "12345\n", false);
    auto* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ReaderFile ra = ReaderFile::from(a);
    ReaderFile rb = ReaderFile::from(b);

    // Прямой порядок.
    auto la = reader->line_column(reader->makeLoc(ra, 5));
    EXPECT_EQ(la.line, 3u);
    EXPECT_EQ(la.column, 1u);
    auto lb = reader->line_column(reader->makeLoc(rb, 5));
    EXPECT_EQ(lb.line, 1u);
    EXPECT_EQ(lb.column, 5u);

    // Обратный порядок (кэш уже заполнен) - результат не должен зависеть от порядка.
    auto lb2 = reader->line_column(reader->makeLoc(rb, 5));
    EXPECT_EQ(lb2.line, 1u);
    EXPECT_EQ(lb2.column, 5u);
    auto la2 = reader->line_column(reader->makeLoc(ra, 5));
    EXPECT_EQ(la2.line, 3u);
    EXPECT_EQ(la2.column, 1u);
}
