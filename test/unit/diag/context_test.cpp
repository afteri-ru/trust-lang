#include "diag/context.hpp"
#include "utils/error.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

using namespace trust;

// ══════════════════════════════════════════════════════════════
//                Context - создание Location (makeLoc)
//                SourceMap::makeLoc не валидирует offset/fileIdx
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, MakeLoc_Valid) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    auto loc = ctx.source().makeLoc(src, 1);
    EXPECT_FALSE(loc.isInvalid());
    EXPECT_EQ(loc, 1);
    EXPECT_FALSE(loc.isOutput());

    auto loc_end = ctx.source().makeLoc(src, 6); // 5 байт + 1 = 6
    EXPECT_FALSE(loc_end.isInvalid());
    EXPECT_EQ(loc_end, 6);
}

// ══════════════════════════════════════════════════════════════
//                Context - создание SourceRange (makeRange)
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, MakeRange_Valid) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello world");
    auto begin = ctx.source().makeLoc(src, 1);
    auto end = ctx.source().makeLoc(src, 6);
    auto range = ctx.source().makeRange(begin, end);
    EXPECT_EQ(range.begin, begin);
    EXPECT_EQ(range.end, end);
}

TEST(ContextTest, MakeRange_Point) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    auto loc = ctx.source().makeLoc(src, 1);
    auto range = ctx.source().makeRange(loc, loc);
    EXPECT_TRUE(range.is_point());
}

TEST(ContextTest, MakeRange_EndLessThanBegin) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    auto begin = ctx.source().makeLoc(src, 5);
    auto end = ctx.source().makeLoc(src, 3);
    EXPECT_THROW(ctx.source().makeRange(begin, end), std::runtime_error);
}

TEST(ContextTest, MakeRange_DifferentFiles) {
    Context ctx;
    MapperFile src1 = ctx.source().add_source("a.cpp", "hello");
    MapperFile src2 = ctx.source().add_source("b.cpp", "world");
    auto begin = ctx.source().makeLoc(src1, 1);
    auto end = ctx.source().makeLoc(src2, 1);
    EXPECT_THROW(ctx.source().makeRange(begin, end), std::runtime_error);
}

TEST(ContextTest, MakeRange_InvalidBegin) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    auto end = ctx.source().makeLoc(src, 1);
    EXPECT_THROW(ctx.source().makeRange(MapperLocation(), end), std::runtime_error);
}

TEST(ContextTest, MakeRange_InvalidEnd) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    auto begin = ctx.source().makeLoc(src, 1);
    EXPECT_THROW(ctx.source().makeRange(begin, MapperLocation()), std::runtime_error);
}

TEST(ContextTest, MakeRange_DifferentFilesSameIdxDifferentFlag) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    MapperFile out = ctx.source().add_output("test.cpp.cpp");
    ctx.source().output_append(out, "hello");
    auto begin = ctx.source().makeLoc(src, 1);
    auto end = ctx.source().makeLoc(out, 1);
    EXPECT_THROW(ctx.source().makeRange(begin, end), std::runtime_error);
}

// ══════════════════════════════════════════════════════════════
//              Context - входные и выходные файлы
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, AddSource_And_Retrieve) {
    Context ctx;
    MapperFile src = ctx.source().add_source("myfile.cpp", "content");
    EXPECT_FALSE(src.isInvalid());
    EXPECT_FALSE(src.isOutput());
    EXPECT_EQ(ctx.source().filename(src), "myfile.cpp");
    EXPECT_EQ(ctx.source().source(src), "content");
}

TEST(ContextTest, AddOutput_And_Retrieve) {
    Context ctx;
    MapperFile out = ctx.source().add_output("output.cpp");
    EXPECT_TRUE(out.isOutput());
    EXPECT_EQ(ctx.source().filename(out), "output.cpp");
    EXPECT_TRUE(ctx.source().source(out).empty());
}

TEST(ContextTest, AddMultipleSources) {
    Context ctx;
    MapperFile a = ctx.source().add_source("a.cpp", "aaa");
    MapperFile b = ctx.source().add_source("b.cpp", "bbb");
    MapperFile c = ctx.source().add_source("c.cpp", "ccc");

    EXPECT_EQ(ctx.source().filename(a), "a.cpp");
    EXPECT_EQ(ctx.source().filename(b), "b.cpp");
    EXPECT_EQ(ctx.source().filename(c), "c.cpp");

    EXPECT_EQ(ctx.source().source(a), "aaa");
    EXPECT_EQ(ctx.source().source(b), "bbb");
    EXPECT_EQ(ctx.source().source(c), "ccc");

    EXPECT_EQ(ctx.source().file_count(), 3);
}

TEST(ContextTest, AddMultipleOutputs) {
    Context ctx;
    MapperFile o1 = ctx.source().add_output("o1.cpp");
    MapperFile o2 = ctx.source().add_output("o2.cpp");

    EXPECT_EQ(ctx.source().filename(o1), "o1.cpp");
    EXPECT_EQ(ctx.source().filename(o2), "o2.cpp");
}

TEST(ContextTest, OutputAppendAndPrepend) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");

    EXPECT_TRUE(ctx.source().output_append(out, "body"));
    EXPECT_TRUE(ctx.source().output_prepend(out, "prefix"));
    // output_prepend добавляет '\n' в конец
    EXPECT_EQ(ctx.source().output_result(out), "prefix\nbody");
}

TEST(ContextTest, OutputAppendToInputFile_Fails) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "content");
    EXPECT_THROW(ctx.source().output_append(src, "extra"), std::runtime_error);
}

TEST(ContextTest, OutputBodyForInputFile_Fails) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "content");
    EXPECT_THROW((void)ctx.source().output_body(src), std::runtime_error);
}

// ══════════════════════════════════════════════════════════════
//              Context - normalize параметр
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, AddSource_WithNormalize) {
    Context ctx;
    MapperFile src = ctx.source().add_source("subdir/test.cpp", "content", true);
    EXPECT_FALSE(src.isInvalid());
    EXPECT_FALSE(src.isOutput());
    EXPECT_EQ(ctx.source().filename(src), "subdir/test.cpp");
    EXPECT_EQ(ctx.source().source(src), "content");
}

TEST(ContextTest, AddSource_WithoutNormalize_ValidName) {
    Context ctx;
    MapperFile src = ctx.source().add_source("my_buffer", "content", false);
    EXPECT_FALSE(src.isInvalid());
    EXPECT_FALSE(src.isOutput());
    EXPECT_EQ(ctx.source().filename(src), "my_buffer");
    EXPECT_EQ(ctx.source().source(src), "content");
}

TEST(ContextTest, AddSource_WithoutNormalize_InvalidNameSlashes) {
    Context ctx;
    EXPECT_THROW((void)ctx.source().add_source("path/to/file", "content", false), std::runtime_error);
}

TEST(ContextTest, AddSource_WithoutNormalize_InvalidNameDots) {
    Context ctx;
    EXPECT_THROW((void)ctx.source().add_source("file.txt", "content", false), std::runtime_error);
}

TEST(ContextTest, AddSource_WithoutNormalize_InvalidNameEmpty) {
    Context ctx;
    EXPECT_THROW((void)ctx.source().add_source("", "content", false), std::runtime_error);
}

TEST(ContextTest, AddOutput_WithNormalize) {
    Context ctx;
    MapperFile out = ctx.source().add_output("subdir/out.cpp", true);
    EXPECT_TRUE(out.isOutput());
    EXPECT_EQ(ctx.source().filename(out), "subdir/out.cpp");
}

TEST(ContextTest, AddOutput_WithoutNormalize_ValidName) {
    Context ctx;
    MapperFile out = ctx.source().add_output("my_output", false);
    EXPECT_TRUE(out.isOutput());
    EXPECT_EQ(ctx.source().filename(out), "my_output");
}

TEST(ContextTest, AddOutput_WithoutNormalize_InvalidName) {
    Context ctx;
    EXPECT_THROW((void)ctx.source().add_output("my/output", false), std::runtime_error);
}

TEST(ContextTest, AddOutput_WithoutNormalize_InvalidNameDots) {
    Context ctx;
    EXPECT_THROW((void)ctx.source().add_output("out.txt", false), std::runtime_error);
}

TEST(ContextTest, AddOutput_WithoutNormalize_EmptyName) {
    Context ctx;
    EXPECT_THROW((void)ctx.source().add_output("", false), std::runtime_error);
}

TEST(ContextTest, FilenameViaLocation) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    auto loc = ctx.source().makeLoc(src, 1);
    EXPECT_EQ(ctx.source().filename(loc), "test.cpp");
}

TEST(ContextTest, FilenameViaInvalidLocation_Empty) {
    Context ctx;
    EXPECT_THROW((void)ctx.source().filename(MapperLocation()), std::runtime_error);
}

TEST(ContextTest, SourceViaLocation) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    auto loc = ctx.source().makeLoc(src, 1);
    EXPECT_EQ(ctx.source().source(loc), "hello");
}

TEST(ContextTest, SourceViaOutputLocation_Empty) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    (void)ctx.source().output_append(out, " ");
    auto loc = ctx.source().makeLoc(out, 1);
    // source() для output файла возвращает source (не пустой)
    EXPECT_EQ(ctx.source().source(loc), " ");
}

// ══════════════════════════════════════════════════════════════
//              FileIdx - проверки (новая упаковка)
// ══════════════════════════════════════════════════════════════

TEST(FileIdxTest, IsOutput_InputFile_Bit31Clear) {
    // Входной файл: бит 31 = 0
    MapperFile input = MapperFile::make_input(0);
    EXPECT_FALSE(input.isOutput());
    // Проверяем через fromRaw: raw с битом 31 = 0
    MapperFile fromRawClear = MapperFile::fromRaw(1);
    EXPECT_FALSE(fromRawClear.isOutput());
}

TEST(FileIdxTest, IsOutput_OutputFile_Bit31Set) {
    // Выходной файл: бит 31 = 1
    MapperFile output = MapperFile::make_output(0);
    EXPECT_TRUE(output.isOutput());
    // Проверяем через fromRaw: raw с битом 31 = 1
    MapperFile fromRawSet = MapperFile::fromRaw(1u | LocationPack::OUTPUT_FILE_BIT);
    EXPECT_TRUE(fromRawSet.isOutput());
}

TEST(FileIdxTest, MakeInput_MaxFiles) {
    // MAX_FILES_INPUT = 511, проверка idx+1 < 511 → idx=509 последний валидный
    EXPECT_FALSE(MapperFile::make_input(LocationPack::MAX_FILES_INPUT - 2).isInvalid());
}

TEST(FileIdxTest, MakeOutput_MaxFiles) {
    // MAX_FILES_OUTPUT = 31, проверка idx+1 < 31 → idx=29 последний валидный
    EXPECT_FALSE(MapperFile::make_output(LocationPack::MAX_FILES_OUTPUT - 2).isInvalid());
}

TEST(FileIdxTest, AsIndex_Input_ReturnsCorrectIndex) {
    MapperFile f = MapperFile::make_input(5);
    EXPECT_EQ(f.as_index(), 5u);
}

TEST(FileIdxTest, AsIndex_Output_ReturnsCorrectIndex) {
    MapperFile f = MapperFile::make_output(3);
    EXPECT_EQ(f.as_index(), 3u);
}

TEST(FileIdxTest, RawFormat_InputNoBit31) {
    MapperFile f = MapperFile::make_input(42);
    EXPECT_EQ(f.as_index(), 42u);
    EXPECT_FALSE(f.isOutput());
}

TEST(FileIdxTest, RawFormat_OutputHasBit31) {
    MapperFile f = MapperFile::make_output(7);
    EXPECT_EQ(f.as_index(), 7u);
    EXPECT_TRUE(f.isOutput());
}

// ══════════════════════════════════════════════════════════════
//              TaggedLocation - проверки упаковки (новая раскладка)
// ══════════════════════════════════════════════════════════════

TEST(LocationPackTest, InputLocation_NoBit31) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    auto loc = ctx.source().makeLoc(src, 3);
    // У входной Location бит 31 должен быть 0
    EXPECT_EQ(loc.asPacked() & LocationPack::OUTPUT_FILE_BIT, 0u);
    EXPECT_FALSE(loc.isOutput());
}

TEST(LocationPackTest, OutputLocation_HasBit31) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    (void)ctx.source().output_append(out, "hello");
    auto loc = ctx.source().makeLoc(out, 3);
    // У выходной Location бит 31 должен быть 1
    EXPECT_NE(loc.asPacked() & LocationPack::OUTPUT_FILE_BIT, 0u);
    EXPECT_TRUE(loc.isOutput());
}

TEST(LocationPackTest, InputLocation_FileIdxAndOffsetCorrect) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    auto loc = ctx.source().makeLoc(src, 3);
    EXPECT_EQ(loc.fileIdx(), src);
    EXPECT_EQ(loc.offset(), 3u);
}

TEST(LocationPackTest, OutputLocation_FileIdxAndOffsetCorrect) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    (void)ctx.source().output_append(out, "hello world");
    auto loc = ctx.source().makeLoc(out, 5);
    EXPECT_EQ(loc.fileIdx(), out);
    EXPECT_EQ(loc.offset(), 5u);
}

TEST(LocationPackTest, InputLocation_MaxOffset) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", std::string(LocationPack::MAX_OFFSET_INPUT, 'x'));
    auto loc = ctx.source().makeLoc(src, LocationPack::MAX_OFFSET_INPUT);
    EXPECT_EQ(loc.offset(), LocationPack::MAX_OFFSET_INPUT);
}

TEST(LocationPackTest, InputLocation_ExceedMaxOffset_Fault) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    EXPECT_THROW((void)MapperLocation::makeLoc(src, LocationPack::MAX_OFFSET_INPUT + 1), std::runtime_error);
}

TEST(LocationPackTest, OutputLocation_MaxOffset) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    (void)ctx.source().output_append(out, "hello");
    // Проверяем, что максимальный offset (26 бит, 67108863) упаковывается
    auto loc = MapperLocation::makeLoc(out, LocationPack::MAX_OFFSET_OUTPUT);
    EXPECT_TRUE(loc.isOutput());
    EXPECT_EQ(loc.offset(), LocationPack::MAX_OFFSET_OUTPUT);
    // Проверяем, что бит 31 установлен
    EXPECT_NE(loc.asPacked() & LocationPack::OUTPUT_FILE_BIT, 0u);
}

TEST(LocationPackTest, OutputLocation_ExceedMaxOffset_Fault) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    (void)ctx.source().output_append(out, "hello");
    EXPECT_THROW((void)MapperLocation::makeLoc(out, LocationPack::MAX_OFFSET_OUTPUT + 1), std::runtime_error);
}

TEST(LocationPackTest, InputOutput_CrossCheck_PackedFlags) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    MapperFile out = ctx.source().add_output("out.cpp");
    (void)ctx.source().output_append(out, "hello");
    auto locIn = ctx.source().makeLoc(src, 3);
    auto locOut = ctx.source().makeLoc(out, 3);
    // input: бит 31 = 0
    EXPECT_EQ(locIn.asPacked() >> 31, 0u);
    // output: бит 31 = 1
    EXPECT_EQ(locOut.asPacked() >> 31, 1u);
    // fileIdx и offset корректны
    EXPECT_EQ(locIn.fileIdx(), src);
    EXPECT_EQ(locOut.fileIdx(), out);
    EXPECT_EQ(locIn.offset(), 3u);
    EXPECT_EQ(locOut.offset(), 3u);
}

TEST(LocationPackTest, MakeLoc_StaticMethod_Input) {
    MapperFile src = MapperFile::make_input(0);
    auto loc = MapperLocation::makeLoc(src, 42);
    EXPECT_FALSE(loc.isOutput());
    EXPECT_EQ(loc.offset(), 42u);
}

TEST(LocationPackTest, MakeLoc_StaticMethod_Output) {
    MapperFile out = MapperFile::make_output(0);
    auto loc = MapperLocation::makeLoc(out, 100);
    EXPECT_TRUE(loc.isOutput());
    EXPECT_EQ(loc.offset(), 100u);
}

TEST(FileIdxTest, IsOutput_FromRaw) {
    uint32_t outputFlag = LocationPack::OUTPUT_FILE_BIT;
    MapperFile input = MapperFile::fromRaw(1);
    EXPECT_FALSE(input.isOutput());
    MapperFile output = MapperFile::fromRaw(1u | outputFlag);
    EXPECT_TRUE(output.isOutput());
}

// ══════════════════════════════════════════════════════════════
//              Context - краевые случаи
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, EmptyContext) {
    Context ctx;
    EXPECT_EQ(ctx.source().file_count(), 0);
}

TEST(ContextTest, LocFromLine_EmptyFile) {
    Context ctx;
    MapperFile src = ctx.source().add_source("empty.cpp", "");
    auto loc = ctx.source().loc_from_line(src, 1);
    EXPECT_FALSE(loc.isInvalid());
    EXPECT_EQ(loc, 1);
}

TEST(ContextTest, LocFromLine_WithContent) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "abc\ndef\n");
    auto loc1 = ctx.source().loc_from_line(src, 1);
    EXPECT_EQ(loc1, 1);
    auto loc2 = ctx.source().loc_from_line(src, 2);
    EXPECT_EQ(loc2, 5); // "abc\n" = 4 байта, 5-й - начало "def"
}

TEST(ContextTest, LocFromLine_LineAtEnd) {
    std::string source = "line1\nline2\n";
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", source);
    auto loc = ctx.source().loc_from_line(src, 2);
    EXPECT_FALSE(loc.isInvalid());
    EXPECT_EQ(loc, 7);
}

// ══════════════════════════════════════════════════════════════
//              Context - Location упаковка
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, Location_PackUnpack_Input) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    auto loc = ctx.source().makeLoc(src, 3);
    EXPECT_FALSE(loc.isOutput());
    EXPECT_EQ(loc.fileIdx(), src);
    EXPECT_EQ(loc, 3);
}

TEST(ContextTest, Location_PackUnpack_Output) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    (void)ctx.source().output_append(out, "hello");
    auto loc = ctx.source().makeLoc(out, 3);
    EXPECT_TRUE(loc.isOutput());
    EXPECT_EQ(loc.fileIdx(), out);
    EXPECT_EQ(loc, 3);
}

TEST(ContextTest, Location_FileIdx_Invalid) {
    auto loc = MapperLocation();
    EXPECT_TRUE(loc.fileIdx().isInvalid());
}

// ══════════════════════════════════════════════════════════════
//              Context - isValid(Location)
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, IsValid_Location_Valid) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    auto loc = ctx.source().makeLoc(src, 1);
    EXPECT_TRUE(ctx.source().isValid(loc));
}

TEST(ContextTest, IsValid_Location_Invalid) {
    Context ctx;
    EXPECT_FALSE(ctx.source().isValid(MapperLocation()));
}

TEST(ContextTest, IsValid_Location_OutOfBounds) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    // offset > size файла (5)
    auto loc = MapperLocation::makeLoc(src, 100);
    EXPECT_FALSE(ctx.source().isValid(loc));
}

TEST(ContextTest, IsValid_Location_OutputFile) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    (void)ctx.source().output_append(out, "hello");
    auto loc = ctx.source().makeLoc(out, 3);
    EXPECT_TRUE(ctx.source().isValid(loc));
}

TEST(ContextTest, IsValid_Location_OutputFile_OutOfBounds) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    (void)ctx.source().output_append(out, "hello");
    auto loc = MapperLocation::makeLoc(out, 100);
    EXPECT_FALSE(ctx.source().isValid(loc));
}

// ══════════════════════════════════════════════════════════════
//              Context - isValid(Range)
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, IsValid_Range_Valid) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello world");
    auto begin = ctx.source().makeLoc(src, 1);
    auto end = ctx.source().makeLoc(src, 6);
    auto range = ctx.source().makeRange(begin, end);
    EXPECT_TRUE(ctx.source().isValid(range));
}

TEST(ContextTest, IsValid_Range_Point) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    auto loc = ctx.source().makeLoc(src, 3);
    auto range = ctx.source().makeRange(loc, loc);
    EXPECT_TRUE(ctx.source().isValid(range));
}

TEST(ContextTest, IsValid_Range_InvalidBegin) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    auto end = ctx.source().makeLoc(src, 1);
    MapperRange range;
    range.end = end;
    EXPECT_FALSE(ctx.source().isValid(range));
}

TEST(ContextTest, IsValid_Range_InvalidEnd) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    auto begin = ctx.source().makeLoc(src, 1);
    MapperRange range;
    range.begin = begin;
    EXPECT_FALSE(ctx.source().isValid(range));
}

TEST(ContextTest, IsValid_Range_DifferentFiles) {
    Context ctx;
    MapperFile src1 = ctx.source().add_source("a.cpp", "hello");
    MapperFile src2 = ctx.source().add_source("b.cpp", "world");
    auto begin = ctx.source().makeLoc(src1, 1);
    auto end = ctx.source().makeLoc(src2, 1);
    MapperRange range;
    range.begin = begin;
    range.end = end;
    EXPECT_FALSE(ctx.source().isValid(range));
}

TEST(ContextTest, IsValid_Range_EndBeforeBegin) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello world");
    auto begin = ctx.source().makeLoc(src, 5);
    auto end = ctx.source().makeLoc(src, 3);
    MapperRange range;
    range.begin = begin;
    range.end = end;
    EXPECT_FALSE(ctx.source().isValid(range));
}

TEST(ContextTest, IsValid_Range_OutOfBoundsBegin) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    auto begin = MapperLocation::makeLoc(src, 100);
    auto end = ctx.source().makeLoc(src, 1);
    MapperRange range;
    range.begin = begin;
    range.end = end;
    EXPECT_FALSE(ctx.source().isValid(range));
}

TEST(ContextTest, IsValid_Range_OutOfBoundsEnd) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.cpp", "hello");
    auto begin = ctx.source().makeLoc(src, 1);
    auto end = MapperLocation::makeLoc(src, 100);
    MapperRange range{begin, end};
    EXPECT_FALSE(ctx.source().isValid(range));
}

// ══════════════════════════════════════════════════════════════
//              Context - getText
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, SourceText_Basic) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.trust", "hello world");
    auto begin = ctx.source().makeLoc(src, 1);
    auto end = ctx.source().makeLoc(src, 6);
    MapperRange range = ctx.source().makeRange(begin, end);
    EXPECT_EQ(ctx.source().getText(range), "hello");
}

TEST(ContextTest, SourceText_MidRange) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.trust", "abcdefgh");
    auto begin = ctx.source().makeLoc(src, 3);
    auto end = ctx.source().makeLoc(src, 7);
    MapperRange range = ctx.source().makeRange(begin, end);
    EXPECT_EQ(ctx.source().getText(range), "cdef");
}

TEST(ContextTest, SourceText_PointRange_Empty) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.trust", "hello");
    auto loc = ctx.source().makeLoc(src, 3);
    MapperRange range = ctx.source().makeRange(loc, loc);
    EXPECT_EQ(ctx.source().getText(range), "");
}

TEST(ContextTest, SourceText_WholeString) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.trust", "hello");
    auto begin = ctx.source().makeLoc(src, 1);
    auto end = ctx.source().makeLoc(src, 6);
    MapperRange range = ctx.source().makeRange(begin, end);
    EXPECT_EQ(ctx.source().getText(range), "hello");
}

TEST(ContextTest, SourceText_OnOutputFile) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    (void)ctx.source().output_append(out, "some code");
    auto begin = ctx.source().makeLoc(out, 1);
    auto end = ctx.source().makeLoc(out, 5);
    MapperRange range = ctx.source().makeRange(begin, end);
    EXPECT_EQ(ctx.source().getText(range), "some");
}

TEST(ContextTest, SourceText_RangeOutOfBounds_Fault) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.trust", "abc");
    // end offset > source.size() + 1 - getText бросает FAULT
    auto begin = ctx.source().makeLoc(src, 2);
    auto end = ctx.source().makeLoc(src, 100); // намного больше размера
    MapperRange range = ctx.source().makeRange(begin, end);
    EXPECT_THROW((void)ctx.source().getText(range), std::runtime_error);
}

// -- outputBodyText --

TEST(ContextTest, OutputBodyText_Basic) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    (void)ctx.source().output_append(out, "int x = 1;");
    auto begin = ctx.source().makeLoc(out, 1);
    auto end = ctx.source().makeLoc(out, 6);
    MapperRange range = ctx.source().makeRange(begin, end);
    EXPECT_EQ(ctx.source().getText(range), "int x");
}

TEST(ContextTest, OutputBodyText_WithPrepend) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    (void)ctx.source().output_prepend(out, "// header\n");
    (void)ctx.source().output_append(out, "int main() {}");
    // body = "int main() {}" (13 байт), prependSize = 10 ("// header\n")
    // makeLoc валидирует offset по body.size() (13), поэтому offset в body, не в result
    // "int m" в body → offset = 1 в body (1-based)
    auto begin = ctx.source().makeLoc(out, 1);
    auto end = ctx.source().makeLoc(out, 6); // "int m" (1-based body [1..5])
    MapperRange range = ctx.source().makeRange(begin, end);
    // outputBodyText берёт из body (не result), поэтому offset уже в body
    EXPECT_EQ(ctx.source().getText(range), "int m");
}

TEST(ContextTest, GetText_OnInputFile) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.trust", "hello");
    auto begin = ctx.source().makeLoc(src, 1);
    auto end = ctx.source().makeLoc(src, 6);
    MapperRange range = ctx.source().makeRange(begin, end);
    EXPECT_EQ(ctx.source().getText(range), "hello");
}

TEST(ContextTest, OutputBodyText_PointRange_Empty) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    (void)ctx.source().output_append(out, "body");
    auto loc = ctx.source().makeLoc(out, 2);
    MapperRange range = ctx.source().makeRange(loc, loc);
    EXPECT_EQ(ctx.source().getText(range), "");
}

// ══════════════════════════════════════════════════════════════
//              OutputBuffer - prepend с namespace
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, OutputPrepend_Global) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    EXPECT_TRUE(ctx.source().output_prepend(out, "#include <vector>"));
    EXPECT_TRUE(ctx.source().output_prepend(out, "#include <string>"));
    EXPECT_TRUE(ctx.source().output_prepend(out, "#include <vector>")); // дубликат
    std::string result = ctx.source().output_result(out);
    EXPECT_EQ(result, "#include <string>\n#include <vector>\n");
}

TEST(ContextTest, OutputPrepend_WithNs) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    EXPECT_TRUE(ctx.source().output_prepend(out, "class vector;", "std"));
    EXPECT_TRUE(ctx.source().output_prepend(out, "class string;", "std"));
    std::string result = ctx.source().output_result(out);
    // std::set сортирует строки лексикографически: "class string;" < "class vector;"
    EXPECT_EQ(result, "namespace std {\n    class string;\n    class vector;\n}\n");
}

TEST(ContextTest, OutputPrepend_DedupInSameNs) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    EXPECT_TRUE(ctx.source().output_prepend(out, "class vector;", "std"));
    EXPECT_TRUE(ctx.source().output_prepend(out, "class vector;", "std")); // дубликат
    std::string result = ctx.source().output_result(out);
    EXPECT_EQ(result, "namespace std {\n    class vector;\n}\n");
}

TEST(ContextTest, OutputPrepend_SamePrefixDifferentNs_BothAppear) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    EXPECT_TRUE(ctx.source().output_prepend(out, "class foo;", "ns1"));
    EXPECT_TRUE(ctx.source().output_prepend(out, "class foo;", "ns2"));
    std::string result = ctx.source().output_result(out);
    EXPECT_EQ(result, "namespace ns1 {\n    class foo;\n}\nnamespace ns2 {\n    class foo;\n}\n");
}

TEST(ContextTest, OutputPrepend_MixedGlobalAndNs) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    EXPECT_TRUE(ctx.source().output_prepend(out, "#include <vector>"));
    EXPECT_TRUE(ctx.source().output_prepend(out, "class vector;", "std"));
    EXPECT_TRUE(ctx.source().output_prepend(out, "class string;", "std"));
    std::string result = ctx.source().output_result(out);
    // std::set сортирует строки лексикографически: "class string;" < "class vector;"
    EXPECT_EQ(result, "#include <vector>\nnamespace std {\n    class string;\n    class vector;\n}\n");
}

TEST(ContextTest, OutputPrepend_MultipleNsOrdering) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    EXPECT_TRUE(ctx.source().output_prepend(out, "class z;", "zzz"));
    EXPECT_TRUE(ctx.source().output_prepend(out, "class a;", "aaa"));
    EXPECT_TRUE(ctx.source().output_prepend(out, "class m;", "mmm"));
    std::string result = ctx.source().output_result(out);
    // map упорядочен по ключу (алфавитно)
    EXPECT_EQ(result, "namespace aaa {\n    class a;\n}\nnamespace mmm {\n    class m;\n}\nnamespace zzz {\n    class z;\n}\n");
}

TEST(ContextTest, OutputPrepend_DefaultNs_Global) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    EXPECT_TRUE(ctx.source().output_prepend(out, "#include <vector>", "")); // явный пустой ns = глобальный
    EXPECT_TRUE(ctx.source().output_prepend(out, "#include <string>", ""));
    std::string result = ctx.source().output_result(out);
    EXPECT_EQ(result, "#include <string>\n#include <vector>\n");
}

TEST(ContextTest, OutputPrepend_CustomIndent) {
    OutputBuffer buf;
    buf.prepend("class vector;", "std");
    EXPECT_EQ(buf.build(2), "namespace std {\n  class vector;\n}\n");
    EXPECT_EQ(buf.build(4), "namespace std {\n    class vector;\n}\n");
    EXPECT_EQ(buf.build(8), "namespace std {\n        class vector;\n}\n");
}

TEST(ContextTest, OutputPrepend_MultipleOutputs) {
    Context ctx;
    MapperFile o1 = ctx.source().add_output("o1.cpp");
    MapperFile o2 = ctx.source().add_output("o2.cpp");
    EXPECT_TRUE(ctx.source().output_prepend(o1, "#include <a>"));
    EXPECT_TRUE(ctx.source().output_prepend(o2, "#include <b>", "ns"));
    EXPECT_EQ(ctx.source().output_result(o1), "#include <a>\n");
    EXPECT_EQ(ctx.source().output_result(o2), "namespace ns {\n    #include <b>\n}\n");
}

TEST(ContextTest, OutputPrepend_EmptyText) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    EXPECT_TRUE(ctx.source().output_prepend(out, ""));     // empty string, всё равно добавится в set
    EXPECT_TRUE(ctx.source().output_prepend(out, "", "")); // ещё один пустой - дубликат
    std::string result = ctx.source().output_result(out);
    // пустая строка будет в set, build выведет её как пустую строку + \n
    EXPECT_EQ(result, "\n");
}

TEST(ContextTest, OutputPrepend_AppendAfterPrepend_OutputResultOrder) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    EXPECT_TRUE(ctx.source().output_prepend(out, "// header"));
    EXPECT_TRUE(ctx.source().output_append(out, "int main() {}"));
    std::string result = ctx.source().output_result(out);
    EXPECT_EQ(result, "// header\nint main() {}");
}

TEST(ContextTest, OutputPrepend_NoPrepend_OutputBodyUnchanged) {
    Context ctx;
    MapperFile out = ctx.source().add_output("out.cpp");
    EXPECT_TRUE(ctx.source().output_append(out, "int x = 1;"));
    EXPECT_EQ(ctx.source().output_result(out), "int x = 1;");
}

// Документирующий комментарий перед макроопределением записывается в MacroDef.
TEST(ContextTest, RecordMacroCapturesDocComment) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.src", "/// macro doc\n@foo");
    auto loc = ctx.source().loc_from_line(src, 2);
    ctx.recordMacro("foo", MapperRange{loc, loc});

    ASSERT_EQ(ctx.macroDefs().size(), 1);
    EXPECT_NE(ctx.macroDefs()[0].documentation.find("macro doc"), std::string::npos) << ctx.macroDefs()[0].documentation;
    EXPECT_FALSE(ctx.macroDefs()[0].documentation.empty());
}

// Хвостовой inline-док (`///<`) после определения макроса тоже привязывается к макроопределению.
TEST(ContextTest, RecordMacroCapturesTrailingDocComment) {
    Context ctx;
    MapperFile src = ctx.source().add_source("test.src", "@foo := 42 ///< macro trail");
    auto begin = ctx.source().makeLoc(src, 1);
    auto end = ctx.source().makeLoc(src, 11); // после `42`, перед `///<`
    ctx.recordMacro("foo", MapperRange{begin, end});

    ASSERT_EQ(ctx.macroDefs().size(), 1);
    EXPECT_NE(ctx.macroDefs()[0].documentation.find("macro trail"), std::string::npos) << ctx.macroDefs()[0].documentation;
    EXPECT_NE(ctx.macroDefs()[0].documentation.find("///<"), std::string::npos);
}
