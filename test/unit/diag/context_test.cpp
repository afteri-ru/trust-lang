#include "diag/context.hpp"
#include "utils/error.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

using namespace trust;

// ══════════════════════════════════════════════════════════════
//                Context — создание Location (makeLoc)
//                SourceMap::makeLoc не валидирует offset/fileIdx
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, MakeLoc_Valid) {
    Context ctx;
    MapperFile src = ctx.add_source("test.cpp", "hello");
    auto loc = ctx.makeLoc(src, 1);
    EXPECT_TRUE(loc.isValid());
    EXPECT_EQ(loc, 1);
    EXPECT_FALSE(loc.isOutput());

    auto loc_end = ctx.makeLoc(src, 6); // 5 байт + 1 = 6
    EXPECT_TRUE(loc_end.isValid());
    EXPECT_EQ(loc_end, 6);
}

// ══════════════════════════════════════════════════════════════
//                Context — создание SourceRange (makeRange)
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, MakeRange_Valid) {
    Context ctx;
    MapperFile src = ctx.add_source("test.cpp", "hello world");
    auto begin = ctx.makeLoc(src, 1);
    auto end = ctx.makeLoc(src, 6);
    auto range = ctx.makeRange(begin, end);
    EXPECT_EQ(range.begin, begin);
    EXPECT_EQ(range.end, end);
}

TEST(ContextTest, MakeRange_Point) {
    Context ctx;
    MapperFile src = ctx.add_source("test.cpp", "hello");
    auto loc = ctx.makeLoc(src, 1);
    auto range = ctx.makeRange(loc, loc);
    EXPECT_TRUE(range.is_point());
}

TEST(ContextTest, MakeRange_EndLessThanBegin) {
    Context ctx;
    MapperFile src = ctx.add_source("test.cpp", "hello");
    auto begin = ctx.makeLoc(src, 5);
    auto end = ctx.makeLoc(src, 3);
    EXPECT_THROW(ctx.makeRange(begin, end), std::runtime_error);
}

TEST(ContextTest, MakeRange_DifferentFiles) {
    Context ctx;
    MapperFile src1 = ctx.add_source("a.cpp", "hello");
    MapperFile src2 = ctx.add_source("b.cpp", "world");
    auto begin = ctx.makeLoc(src1, 1);
    auto end = ctx.makeLoc(src2, 1);
    EXPECT_THROW(ctx.makeRange(begin, end), std::runtime_error);
}

TEST(ContextTest, MakeRange_InvalidBegin) {
    Context ctx;
    MapperFile src = ctx.add_source("test.cpp", "hello");
    auto end = ctx.makeLoc(src, 1);
    EXPECT_THROW(ctx.makeRange(MapperLocation(), end), std::runtime_error);
}

TEST(ContextTest, MakeRange_InvalidEnd) {
    Context ctx;
    MapperFile src = ctx.add_source("test.cpp", "hello");
    auto begin = ctx.makeLoc(src, 1);
    EXPECT_THROW(ctx.makeRange(begin, MapperLocation()), std::runtime_error);
}

TEST(ContextTest, MakeRange_DifferentFilesSameIdxDifferentFlag) {
    Context ctx;
    MapperFile src = ctx.add_source("test.cpp", "hello");
    MapperFile out = ctx.add_output("test.cpp.cpp");
    ctx.output_append(out, "hello");
    auto begin = ctx.makeLoc(src, 1);
    auto end = ctx.makeLoc(out, 1);
    EXPECT_THROW(ctx.makeRange(begin, end), std::runtime_error);
}

// ══════════════════════════════════════════════════════════════
//              Context — входные и выходные файлы
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, AddSource_And_Retrieve) {
    Context ctx;
    MapperFile src = ctx.add_source("myfile.cpp", "content");
    EXPECT_TRUE(src.isValid());
    EXPECT_FALSE(src.isOutput());
    EXPECT_EQ(ctx.filename(src), "myfile.cpp");
    EXPECT_EQ(ctx.source(src), "content");
}

TEST(ContextTest, AddOutput_And_Retrieve) {
    Context ctx;
    MapperFile out = ctx.add_output("output.cpp");
    EXPECT_TRUE(out.isOutput());
    EXPECT_EQ(ctx.filename(out), "output.cpp");
    EXPECT_TRUE(ctx.source(out).empty());
}

TEST(ContextTest, AddMultipleSources) {
    Context ctx;
    MapperFile a = ctx.add_source("a.cpp", "aaa");
    MapperFile b = ctx.add_source("b.cpp", "bbb");
    MapperFile c = ctx.add_source("c.cpp", "ccc");

    EXPECT_EQ(ctx.filename(a), "a.cpp");
    EXPECT_EQ(ctx.filename(b), "b.cpp");
    EXPECT_EQ(ctx.filename(c), "c.cpp");

    EXPECT_EQ(ctx.source(a), "aaa");
    EXPECT_EQ(ctx.source(b), "bbb");
    EXPECT_EQ(ctx.source(c), "ccc");

    EXPECT_EQ(ctx.file_count(), 3);
}

TEST(ContextTest, AddMultipleOutputs) {
    Context ctx;
    MapperFile o1 = ctx.add_output("o1.cpp");
    MapperFile o2 = ctx.add_output("o2.cpp");

    EXPECT_EQ(ctx.filename(o1), "o1.cpp");
    EXPECT_EQ(ctx.filename(o2), "o2.cpp");
}

TEST(ContextTest, OutputAppendAndPrepend) {
    Context ctx;
    MapperFile out = ctx.add_output("out.cpp");

    EXPECT_TRUE(ctx.output_append(out, "body"));
    EXPECT_TRUE(ctx.output_prepend(out, "prefix"));
    // output_prepend добавляет '\n' в конец
    EXPECT_EQ(ctx.output_result(out), "prefix\nbody");
}

TEST(ContextTest, OutputAppendToInputFile_Fails) {
    Context ctx;
    MapperFile src = ctx.add_source("test.cpp", "content");
    EXPECT_THROW(ctx.output_append(src, "extra"), std::runtime_error);
}

TEST(ContextTest, OutputBodyForInputFile_Fails) {
    Context ctx;
    MapperFile src = ctx.add_source("test.cpp", "content");
    EXPECT_THROW((void)ctx.output_body(src), std::runtime_error);
}

// ══════════════════════════════════════════════════════════════
//              Context — normalize параметр
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, AddSource_WithNormalize) {
    Context ctx;
    MapperFile src = ctx.add_source("subdir/test.cpp", "content", true);
    EXPECT_TRUE(src.isValid());
    EXPECT_FALSE(src.isOutput());
    EXPECT_EQ(ctx.filename(src), "subdir/test.cpp");
    EXPECT_EQ(ctx.source(src), "content");
}

TEST(ContextTest, AddSource_WithoutNormalize_ValidName) {
    Context ctx;
    MapperFile src = ctx.add_source("my_buffer", "content", false);
    EXPECT_TRUE(src.isValid());
    EXPECT_FALSE(src.isOutput());
    EXPECT_EQ(ctx.filename(src), "my_buffer");
    EXPECT_EQ(ctx.source(src), "content");
}

TEST(ContextTest, AddSource_WithoutNormalize_InvalidNameSlashes) {
    Context ctx;
    EXPECT_THROW((void)ctx.add_source("path/to/file", "content", false), std::runtime_error);
}

TEST(ContextTest, AddSource_WithoutNormalize_InvalidNameDots) {
    Context ctx;
    EXPECT_THROW((void)ctx.add_source("file.txt", "content", false), std::runtime_error);
}

TEST(ContextTest, AddSource_WithoutNormalize_InvalidNameEmpty) {
    Context ctx;
    EXPECT_THROW((void)ctx.add_source("", "content", false), std::runtime_error);
}

TEST(ContextTest, AddOutput_WithNormalize) {
    Context ctx;
    MapperFile out = ctx.add_output("subdir/out.cpp", true);
    EXPECT_TRUE(out.isOutput());
    EXPECT_EQ(ctx.filename(out), "subdir/out.cpp");
}

TEST(ContextTest, AddOutput_WithoutNormalize_ValidName) {
    Context ctx;
    MapperFile out = ctx.add_output("my_output", false);
    EXPECT_TRUE(out.isOutput());
    EXPECT_EQ(ctx.filename(out), "my_output");
}

TEST(ContextTest, AddOutput_WithoutNormalize_InvalidName) {
    Context ctx;
    EXPECT_THROW((void)ctx.add_output("my/output", false), std::runtime_error);
}

TEST(ContextTest, AddOutput_WithoutNormalize_InvalidNameDots) {
    Context ctx;
    EXPECT_THROW((void)ctx.add_output("out.txt", false), std::runtime_error);
}

TEST(ContextTest, AddOutput_WithoutNormalize_EmptyName) {
    Context ctx;
    EXPECT_THROW((void)ctx.add_output("", false), std::runtime_error);
}

TEST(ContextTest, FilenameViaLocation) {
    Context ctx;
    MapperFile src = ctx.add_source("test.cpp", "hello");
    auto loc = ctx.makeLoc(src, 1);
    EXPECT_EQ(ctx.filename(loc), "test.cpp");
}

TEST(ContextTest, FilenameViaInvalidLocation_Empty) {
    Context ctx;
    EXPECT_THROW((void)ctx.filename(MapperLocation()), std::runtime_error);
}

TEST(ContextTest, SourceViaLocation) {
    Context ctx;
    MapperFile src = ctx.add_source("test.cpp", "hello");
    auto loc = ctx.makeLoc(src, 1);
    EXPECT_EQ(ctx.source(loc), "hello");
}

TEST(ContextTest, SourceViaOutputLocation_Empty) {
    Context ctx;
    MapperFile out = ctx.add_output("out.cpp");
    (void)ctx.output_append(out, " ");
    auto loc = ctx.makeLoc(out, 1);
    // source() для output файла возвращает source (не пустой)
    EXPECT_EQ(ctx.source(loc), " ");
}

// ══════════════════════════════════════════════════════════════
//              FileIdx — проверки
// ══════════════════════════════════════════════════════════════

TEST(FileIdxTest, Invalid) {
    EXPECT_FALSE(MapperFile::fromRaw(0).isValid());
}

TEST(FileIdxTest, IsOutput) {
    uint32_t outputFlag = 1u << LocationPack::FILE_BITS;
    MapperFile input = MapperFile::fromRaw(1);
    EXPECT_FALSE(input.isOutput());
    MapperFile output = MapperFile::fromRaw(1u | outputFlag);
    EXPECT_TRUE(output.isOutput());
}

TEST(FileIdxTest, IsOutputHighBit) {
    uint32_t outputFlag = 1u << LocationPack::FILE_BITS;
    MapperFile input = MapperFile::fromRaw(1);
    EXPECT_FALSE(input.isOutput());
    MapperFile output = MapperFile::fromRaw(1u | outputFlag);
    EXPECT_TRUE(output.isOutput());
}

// ══════════════════════════════════════════════════════════════
//              Context — краевые случаи
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, EmptyContext) {
    Context ctx;
    EXPECT_EQ(ctx.file_count(), 0);
}

TEST(ContextTest, LocFromLine_EmptyFile) {
    Context ctx;
    MapperFile src = ctx.add_source("empty.cpp", "");
    auto loc = ctx.loc_from_line(src, 1);
    EXPECT_TRUE(loc.isValid());
    EXPECT_EQ(loc, 1);
}

TEST(ContextTest, LocFromLine_WithContent) {
    Context ctx;
    MapperFile src = ctx.add_source("test.cpp", "abc\ndef\n");
    auto loc1 = ctx.loc_from_line(src, 1);
    EXPECT_EQ(loc1, 1);
    auto loc2 = ctx.loc_from_line(src, 2);
    EXPECT_EQ(loc2, 5); // "abc\n" = 4 байта, 5-й — начало "def"
}

TEST(ContextTest, LocFromLine_LineAtEnd) {
    std::string source = "line1\nline2\n";
    Context ctx;
    MapperFile src = ctx.add_source("test.cpp", source);
    auto loc = ctx.loc_from_line(src, 2);
    EXPECT_TRUE(loc.isValid());
    EXPECT_EQ(loc, 7);
}

// ══════════════════════════════════════════════════════════════
//              Context — Location упаковка
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, Location_PackUnpack_Input) {
    Context ctx;
    MapperFile src = ctx.add_source("test.cpp", "hello");
    auto loc = ctx.makeLoc(src, 3);
    EXPECT_FALSE(loc.isOutput());
    EXPECT_EQ(loc.fileIdx(), src);
    EXPECT_EQ(loc, 3);
}

TEST(ContextTest, Location_PackUnpack_Output) {
    Context ctx;
    MapperFile out = ctx.add_output("out.cpp");
    (void)ctx.output_append(out, "hello");
    auto loc = ctx.makeLoc(out, 3);
    EXPECT_TRUE(loc.isOutput());
    EXPECT_EQ(loc.fileIdx(), out);
    EXPECT_EQ(loc, 3);
}

TEST(ContextTest, InvalidLocation) {
    auto loc = MapperLocation();
    EXPECT_FALSE(loc.isValid());
    EXPECT_FALSE(loc.isOutput());
}

TEST(ContextTest, Location_FileIdx_Invalid) {
    auto loc = MapperLocation();
    EXPECT_FALSE(loc.fileIdx().isValid());
}

TEST(ContextTest, Location_Constructor) {
    MapperLocation loc = MapperLocation::fromPacked(0);
    EXPECT_FALSE(loc.isValid());
    EXPECT_FALSE(loc.isValid());
}

// ══════════════════════════════════════════════════════════════
//              Context — getText
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, SourceText_Basic) {
    Context ctx;
    MapperFile src = ctx.add_source("test.trust", "hello world");
    auto begin = ctx.makeLoc(src, 1);
    auto end = ctx.makeLoc(src, 6);
    MapperRange range = ctx.makeRange(begin, end);
    EXPECT_EQ(ctx.getText(range), "hello");
}

TEST(ContextTest, SourceText_MidRange) {
    Context ctx;
    MapperFile src = ctx.add_source("test.trust", "abcdefgh");
    auto begin = ctx.makeLoc(src, 3);
    auto end = ctx.makeLoc(src, 7);
    MapperRange range = ctx.makeRange(begin, end);
    EXPECT_EQ(ctx.getText(range), "cdef");
}

TEST(ContextTest, SourceText_PointRange_Empty) {
    Context ctx;
    MapperFile src = ctx.add_source("test.trust", "hello");
    auto loc = ctx.makeLoc(src, 3);
    MapperRange range = ctx.makeRange(loc, loc);
    EXPECT_EQ(ctx.getText(range), "");
}

TEST(ContextTest, SourceText_WholeString) {
    Context ctx;
    MapperFile src = ctx.add_source("test.trust", "hello");
    auto begin = ctx.makeLoc(src, 1);
    auto end = ctx.makeLoc(src, 6);
    MapperRange range = ctx.makeRange(begin, end);
    EXPECT_EQ(ctx.getText(range), "hello");
}

TEST(ContextTest, SourceText_OnOutputFile) {
    Context ctx;
    MapperFile out = ctx.add_output("out.cpp");
    (void)ctx.output_append(out, "some code");
    auto begin = ctx.makeLoc(out, 1);
    auto end = ctx.makeLoc(out, 5);
    MapperRange range = ctx.makeRange(begin, end);
    EXPECT_EQ(ctx.getText(range), "some");
}

TEST(ContextTest, SourceText_RangeOutOfBounds_Fault) {
    Context ctx;
    MapperFile src = ctx.add_source("test.trust", "abc");
    // end offset > source.size() + 1 — getText бросает FAULT
    auto begin = ctx.makeLoc(src, 2);
    auto end = ctx.makeLoc(src, 100); // намного больше размера
    MapperRange range = ctx.makeRange(begin, end);
    EXPECT_THROW((void)ctx.getText(range), std::runtime_error);
}

// ── outputBodyText ──

TEST(ContextTest, OutputBodyText_Basic) {
    Context ctx;
    MapperFile out = ctx.add_output("out.cpp");
    (void)ctx.output_append(out, "int x = 1;");
    auto begin = ctx.makeLoc(out, 1);
    auto end = ctx.makeLoc(out, 6);
    MapperRange range = ctx.makeRange(begin, end);
    EXPECT_EQ(ctx.getText(range), "int x");
}

TEST(ContextTest, OutputBodyText_WithPrepend) {
    Context ctx;
    MapperFile out = ctx.add_output("out.cpp");
    (void)ctx.output_prepend(out, "// header\n");
    (void)ctx.output_append(out, "int main() {}");
    // body = "int main() {}" (13 байт), prependSize = 10 ("// header\n")
    // makeLoc валидирует offset по body.size() (13), поэтому offset в body, не в result
    // "int m" в body → offset = 1 в body (1-based)
    auto begin = ctx.makeLoc(out, 1);
    auto end = ctx.makeLoc(out, 6); // "int m" (1-based body [1..5])
    MapperRange range = ctx.makeRange(begin, end);
    // outputBodyText берёт из body (не result), поэтому offset уже в body
    EXPECT_EQ(ctx.getText(range), "int m");
}

TEST(ContextTest, GetText_OnInputFile) {
    Context ctx;
    MapperFile src = ctx.add_source("test.trust", "hello");
    auto begin = ctx.makeLoc(src, 1);
    auto end = ctx.makeLoc(src, 6);
    MapperRange range = ctx.makeRange(begin, end);
    EXPECT_EQ(ctx.getText(range), "hello");
}

TEST(ContextTest, OutputBodyText_PointRange_Empty) {
    Context ctx;
    MapperFile out = ctx.add_output("out.cpp");
    (void)ctx.output_append(out, "body");
    auto loc = ctx.makeLoc(out, 2);
    MapperRange range = ctx.makeRange(loc, loc);
    EXPECT_EQ(ctx.getText(range), "");
}