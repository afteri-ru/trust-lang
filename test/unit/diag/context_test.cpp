#include "diag/context.hpp"
#include "utils/error.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

using namespace trust;

// ══════════════════════════════════════════════════════════════
//                    Context — валидация SourceLoc
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, ValidateLoc_Invalid) {
    Context ctx;
    EXPECT_THROW(ctx.validateLoc(SourceLoc::invalid()), std::runtime_error);
}

TEST(ContextTest, ValidateLoc_ValidInputLoc) {
    Context ctx;
    FileIdx src = ctx.add_source("test.cpp", "int x = 1;\n");
    auto loc = ctx.loc_from_line(src, 1);
    EXPECT_NO_THROW(ctx.validateLoc(loc));
}

TEST(ContextTest, ValidateLoc_OffsetOutOfRange) {
    Context ctx;
    (void)ctx.add_source("test.cpp", "abc"); // 3 байта, валидный offset [1..4]
    // offset 5 > 4 — за границами
    // Создаём SourceLoc вручную, чтобы обойти проверки makeLoc
    uint32_t raw = (1u << SourceLoc::FILEIDX_SHIFT) | 5u;
    SourceLoc badLoc{raw};
    EXPECT_THROW(ctx.validateLoc(badLoc), std::runtime_error);
}

TEST(ContextTest, ValidateLoc_ZeroOffset) {
    Context ctx;
    (void)ctx.add_source("test.cpp", "abc");
    // offset = 0 — невалидный (допустимый диапазон [1..size+1])
    uint32_t raw = (1u << SourceLoc::FILEIDX_SHIFT); // offset = 0
    SourceLoc badLoc{raw};
    EXPECT_THROW(ctx.validateLoc(badLoc), std::runtime_error);
}

TEST(ContextTest, ValidateLoc_InvalidFileIdx) {
    Context ctx;
    // FileIdx{0} — невалидный
    // SourceLoc с FileIdx=0 и offset=1
    SourceLoc loc{(0 << SourceLoc::FILEIDX_SHIFT) | 1};
    EXPECT_THROW(ctx.validateLoc(loc), std::runtime_error);
}

TEST(ContextTest, ValidateLoc_FileIdxOutOfRange) {
    Context ctx;
    // FileIdx с raw=100 (нет такого файла)
    SourceLoc loc{(100 << SourceLoc::FILEIDX_SHIFT) | 1};
    EXPECT_THROW(ctx.validateLoc(loc), std::runtime_error);
}

// ══════════════════════════════════════════════════════════════
//                Context — создание SourceLoc (makeLoc)
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, MakeLoc_Valid) {
    Context ctx;
    FileIdx src = ctx.add_source("test.cpp", "hello");
    auto loc = ctx.makeLoc(src, 1);
    EXPECT_TRUE(loc.isValid());
    EXPECT_EQ(loc.offset(), 1);
    EXPECT_FALSE(loc.isOutput());

    auto loc_end = ctx.makeLoc(src, 6); // 5 байт + 1 = 6
    EXPECT_TRUE(loc_end.isValid());
    EXPECT_EQ(loc_end.offset(), 6);
}

TEST(ContextTest, MakeLoc_InvalidFileIdx) {
    Context ctx;
    EXPECT_THROW(ctx.makeLoc(FileIdx{0}, 1), std::runtime_error);
}

TEST(ContextTest, MakeLoc_FileIdxOutOfRange) {
    Context ctx;
    EXPECT_THROW(ctx.makeLoc(FileIdx{999}, 1), std::runtime_error);
}

TEST(ContextTest, MakeLoc_OffsetTooSmall) {
    Context ctx;
    FileIdx src = ctx.add_source("test.cpp", "abc");
    EXPECT_THROW(ctx.makeLoc(src, 0), std::runtime_error); // offset 0 недопустим
}

TEST(ContextTest, MakeLoc_OffsetTooLarge) {
    Context ctx;
    FileIdx src = ctx.add_source("test.cpp", "abc"); // 3 байта, max offset = 4
    EXPECT_THROW(ctx.makeLoc(src, 5), std::runtime_error); // offset 5 > 4
}

TEST(ContextTest, MakeLoc_OffsetAtMax) {
    Context ctx;
    FileIdx src = ctx.add_source("test.cpp", "abc");
    auto loc = ctx.makeLoc(src, 4); // size + 1 — валидно
    EXPECT_TRUE(loc.isValid());
    EXPECT_EQ(loc.offset(), 4);
}

TEST(ContextTest, MakeLoc_EmptySource) {
    Context ctx;
    FileIdx src = ctx.add_source("empty.cpp", "");
    auto loc = ctx.makeLoc(src, 1); // размер 0, max offset = 1 — валидно
    EXPECT_TRUE(loc.isValid());
    EXPECT_EQ(loc.offset(), 1);
}

TEST(ContextTest, MakeLoc_EmptySourceOffsetTooLarge) {
    Context ctx;
    FileIdx src = ctx.add_source("empty.cpp", "");
    EXPECT_THROW(ctx.makeLoc(src, 2), std::runtime_error); // max offset = 1
}

TEST(ContextTest, MakeLoc_OutputFile) {
    Context ctx;
    FileIdx out = ctx.add_output("out.cpp");
    EXPECT_TRUE(out.isOutput());

    ctx.output_append(out, "int x = 1;\n");
    auto loc = ctx.makeLoc(out, 1);
    EXPECT_TRUE(loc.isValid());
    EXPECT_TRUE(loc.isOutput());
    EXPECT_EQ(loc.offset(), 1);
}

TEST(ContextTest, MakeLoc_OutputFileOffsetOutOfRange) {
    Context ctx;
    FileIdx out = ctx.add_output("out.cpp");
    ctx.output_append(out, "abc");
    EXPECT_THROW(ctx.makeLoc(out, 5), std::runtime_error); // max offset = 4
}

// ══════════════════════════════════════════════════════════════
//                Context — создание SourceRange (makeRange)
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, MakeRange_Valid) {
    Context ctx;
    FileIdx src = ctx.add_source("test.cpp", "hello world");
    auto begin = ctx.makeLoc(src, 1);
    auto end = ctx.makeLoc(src, 6);
    auto range = ctx.makeRange(begin, end);
    EXPECT_EQ(range.begin.packed, begin.packed);
    EXPECT_EQ(range.end.packed, end.packed);
}

TEST(ContextTest, MakeRange_Point) {
    Context ctx;
    FileIdx src = ctx.add_source("test.cpp", "hello");
    auto loc = ctx.makeLoc(src, 1);
    auto range = ctx.makeRange(loc, loc);
    EXPECT_TRUE(range.is_point());
}

TEST(ContextTest, MakeRange_EndLessThanBegin) {
    Context ctx;
    FileIdx src = ctx.add_source("test.cpp", "hello");
    auto begin = ctx.makeLoc(src, 5);
    auto end = ctx.makeLoc(src, 3);
    EXPECT_THROW(ctx.makeRange(begin, end), std::runtime_error);
}

TEST(ContextTest, MakeRange_DifferentFiles) {
    Context ctx;
    FileIdx src1 = ctx.add_source("a.cpp", "hello");
    FileIdx src2 = ctx.add_source("b.cpp", "world");
    auto begin = ctx.makeLoc(src1, 1);
    auto end = ctx.makeLoc(src2, 1);
    EXPECT_THROW(ctx.makeRange(begin, end), std::runtime_error);
}

TEST(ContextTest, MakeRange_InvalidBegin) {
    Context ctx;
    FileIdx src = ctx.add_source("test.cpp", "hello");
    auto end = ctx.makeLoc(src, 1);
    EXPECT_THROW(ctx.makeRange(SourceLoc::invalid(), end), std::runtime_error);
}

TEST(ContextTest, MakeRange_InvalidEnd) {
    Context ctx;
    FileIdx src = ctx.add_source("test.cpp", "hello");
    auto begin = ctx.makeLoc(src, 1);
    EXPECT_THROW(ctx.makeRange(begin, SourceLoc::invalid()), std::runtime_error);
}

TEST(ContextTest, MakeRange_DifferentFilesSameIdxDifferentFlag) {
    Context ctx;
    FileIdx src = ctx.add_source("test.cpp", "hello");
    FileIdx out = ctx.add_output("test.cpp.cpp");
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
    FileIdx src = ctx.add_source("myfile.cpp", "content");
    EXPECT_TRUE(src.raw != 0);
    EXPECT_FALSE(src.isOutput());
    EXPECT_EQ(ctx.filename(src), "myfile.cpp");
    EXPECT_EQ(ctx.source(src), "content");
}

TEST(ContextTest, AddOutput_And_Retrieve) {
    Context ctx;
    FileIdx out = ctx.add_output("output.cpp");
    EXPECT_TRUE(out.isOutput());
    EXPECT_EQ(ctx.filename(out), "output.cpp");
    EXPECT_TRUE(ctx.source(out).empty()); // для выходных файлов source() пуст
}

TEST(ContextTest, AddMultipleSources) {
    Context ctx;
    FileIdx a = ctx.add_source("a.cpp", "aaa");
    FileIdx b = ctx.add_source("b.cpp", "bbb");
    FileIdx c = ctx.add_source("c.cpp", "ccc");

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
    FileIdx o1 = ctx.add_output("o1.cpp");
    FileIdx o2 = ctx.add_output("o2.cpp");

    EXPECT_EQ(ctx.filename(o1), "o1.cpp");
    EXPECT_EQ(ctx.filename(o2), "o2.cpp");
}

TEST(ContextTest, OutputAppendAndPrepend) {
    Context ctx;
    FileIdx out = ctx.add_output("out.cpp");

    EXPECT_TRUE(ctx.output_append(out, "body"));
    EXPECT_TRUE(ctx.output_prepend(out, "prefix"));

    EXPECT_EQ(ctx.output_result(out), "prefixbody");
}

TEST(ContextTest, OutputAppendToInputFile_Fails) {
    Context ctx;
    FileIdx src = ctx.add_source("in.cpp", "content");
    EXPECT_FALSE(ctx.output_append(src, "extra"));
}

TEST(ContextTest, OutputPrependToInputFile_Fails) {
    Context ctx;
    FileIdx src = ctx.add_source("in.cpp", "content");
    EXPECT_FALSE(ctx.output_prepend(src, "extra"));
}

TEST(ContextTest, OutputResultForInputFile_Empty) {
    Context ctx;
    FileIdx src = ctx.add_source("in.cpp", "content");
    EXPECT_EQ(ctx.output_result(src), "");
}


TEST(ContextTest, SourceForOutputFile_Empty) {
    Context ctx;
    FileIdx out = ctx.add_output("out.cpp");
    EXPECT_TRUE(ctx.source(out).empty());
}

// ══════════════════════════════════════════════════════════════
//              Context — normalize параметр
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, AddSource_WithNormalize) {
    Context ctx;
    FileIdx src = ctx.add_source("subdir/test.cpp", "content", true);
    EXPECT_TRUE(src.raw != 0);
    EXPECT_FALSE(src.isOutput());
    // Имя должно быть нормализовано
    EXPECT_EQ(ctx.filename(src), "subdir/test.cpp");
    EXPECT_EQ(ctx.source(src), "content");
}

TEST(ContextTest, AddSource_WithoutNormalize_ValidName) {
    Context ctx;
    FileIdx src = ctx.add_source("my_buffer", "content", false);
    EXPECT_TRUE(src.raw != 0);
    EXPECT_FALSE(src.isOutput());
    EXPECT_EQ(ctx.filename(src), "my_buffer");
    EXPECT_EQ(ctx.source(src), "content");
}

TEST(ContextTest, AddSource_WithoutNormalize_InvalidNameSlashes) {
    Context ctx;
    FileIdx src = ctx.add_source("path/to/file", "content", false);
    EXPECT_TRUE(src.raw == 0); // должен быть невалидным
}

TEST(ContextTest, AddSource_WithoutNormalize_InvalidNameDots) {
    Context ctx;
    FileIdx src = ctx.add_source("file.txt", "content", false);
    EXPECT_TRUE(src.raw == 0);
}

TEST(ContextTest, AddSource_WithoutNormalize_InvalidNameEmpty) {
    Context ctx;
    FileIdx src = ctx.add_source("", "content", false);
    EXPECT_TRUE(src.raw == 0);
}

TEST(ContextTest, AddOutput_WithNormalize) {
    Context ctx;
    FileIdx out = ctx.add_output("subdir/out.cpp", true);
    EXPECT_TRUE(out.isOutput());
    EXPECT_EQ(ctx.filename(out), "subdir/out.cpp");
}

TEST(ContextTest, AddOutput_WithoutNormalize_ValidName) {
    Context ctx;
    FileIdx out = ctx.add_output("my_output", false);
    EXPECT_TRUE(out.isOutput());
    EXPECT_EQ(ctx.filename(out), "my_output");
}

TEST(ContextTest, AddOutput_WithoutNormalize_InvalidName) {
    Context ctx;
    FileIdx out = ctx.add_output("my/output", false);
    EXPECT_TRUE(out.raw == 0);
}

TEST(ContextTest, AddOutput_WithoutNormalize_InvalidNameDots) {
    Context ctx;
    FileIdx out = ctx.add_output("out.txt", false);
    EXPECT_TRUE(out.raw == 0);
}

TEST(ContextTest, AddOutput_WithoutNormalize_EmptyName) {
    Context ctx;
    FileIdx out = ctx.add_output("", false);
    EXPECT_TRUE(out.raw == 0);
}

TEST(ContextTest, FilenameViaSourceLoc) {
    Context ctx;
    FileIdx src = ctx.add_source("test.cpp", "hello");
    auto loc = ctx.makeLoc(src, 1);
    EXPECT_EQ(ctx.filename(loc), "test.cpp");
}

TEST(ContextTest, FilenameViaInvalidSourceLoc_Empty) {
    Context ctx;
    EXPECT_TRUE(ctx.filename(SourceLoc::invalid()).empty());
}

TEST(ContextTest, SourceViaSourceLoc) {
    Context ctx;
    FileIdx src = ctx.add_source("test.cpp", "hello");
    auto loc = ctx.makeLoc(src, 1);
    EXPECT_EQ(ctx.source(loc), "hello");
}

TEST(ContextTest, SourceViaOutputSourceLoc_Empty) {
    Context ctx;
    FileIdx out = ctx.add_output("out.cpp");
    auto loc = ctx.makeLoc(out, 1);
    EXPECT_TRUE(ctx.source(loc).empty());
}

// ══════════════════════════════════════════════════════════════
//              FileIdx — проверки
// ══════════════════════════════════════════════════════════════

TEST(FileIdxTest, Invalid) {
    EXPECT_FALSE(FileIdx{0}.raw != 0); // invalid
}

TEST(FileIdxTest, IsOutput) {
    uint32_t outputFlag = 1u << FileIdx::FILEIDX_BITS;
    FileIdx input{1};
    EXPECT_FALSE(input.isOutput());
    FileIdx output{1u | outputFlag};
    EXPECT_TRUE(output.isOutput());
}

TEST(FileIdxTest, IsOutputHighBit) {
    uint32_t outputFlag = 1u << FileIdx::FILEIDX_BITS;
    FileIdx input{1};
    EXPECT_FALSE(input.isOutput());
    FileIdx output{1u | outputFlag};
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
    FileIdx src = ctx.add_source("empty.cpp", "");
    auto loc = ctx.loc_from_line(src, 1);
    EXPECT_TRUE(loc.isValid());
    EXPECT_EQ(loc.offset(), 1); // пустой файл — только позиция в конце
}

TEST(ContextTest, LocFromLine_WithContent) {
    Context ctx;
    FileIdx src = ctx.add_source("test.cpp", "abc\ndef\n");
    auto loc1 = ctx.loc_from_line(src, 1);
    EXPECT_EQ(loc1.offset(), 1);
    auto loc2 = ctx.loc_from_line(src, 2);
    EXPECT_EQ(loc2.offset(), 5); // "abc\n" = 4 байта, 5-й — начало "def"
}

TEST(ContextTest, LocFromLine_LineAtEnd) {
    std::string source = "line1\nline2\n";
    Context ctx;
    FileIdx src = ctx.add_source("test.cpp", source);
    auto loc = ctx.loc_from_line(src, 2);
    EXPECT_TRUE(loc.isValid());
    // offset = позиция "line2" = 7 (с единицы)
    EXPECT_EQ(loc.offset(), 7);
}

// ══════════════════════════════════════════════════════════════
//              Context — SourceLoc упаковка
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, SourceLoc_PackUnpack_Input) {
    Context ctx;
    FileIdx src = ctx.add_source("test.cpp", "hello");
    auto loc = ctx.makeLoc(src, 3);
    EXPECT_FALSE(loc.isOutput());
    EXPECT_EQ(loc.fileIdx().raw, src.raw);
    EXPECT_EQ(loc.offset(), 3);
}

TEST(ContextTest, SourceLoc_PackUnpack_Output) {
    Context ctx;
    FileIdx out = ctx.add_output("out.cpp");
    ctx.output_append(out, "hello");
    auto loc = ctx.makeLoc(out, 3);
    EXPECT_TRUE(loc.isOutput());
    EXPECT_EQ(loc.fileIdx().raw, out.raw);
    EXPECT_EQ(loc.offset(), 3);
}

TEST(ContextTest, InvalidSourceLoc) {
    auto loc = SourceLoc::invalid();
    EXPECT_FALSE(loc.isValid());
    EXPECT_FALSE(loc.isOutput());
}

TEST(ContextTest, SourceLoc_FileIdx_Invalid) {
    auto loc = SourceLoc::invalid();
    EXPECT_EQ(loc.fileIdx().raw, 0);
}

TEST(ContextTest, SourceLoc_Constructor) {
    SourceLoc loc{0};
    EXPECT_FALSE(loc.isValid());
    EXPECT_EQ(loc.packed, 0);
}

// ══════════════════════════════════════════════════════════════
//              Context — validateLoc для output
// ══════════════════════════════════════════════════════════════

TEST(ContextTest, ValidateLoc_OutputFile) {
    Context ctx;
    FileIdx out = ctx.add_output("out.cpp");
    ctx.output_append(out, "output data");
    auto loc = ctx.makeLoc(out, 1);
    EXPECT_NO_THROW(ctx.validateLoc(loc));
}

TEST(ContextTest, ValidateLoc_OutputFileOffsetOutOfRange) {
    Context ctx;
    FileIdx out = ctx.add_output("out.cpp");
    ctx.output_append(out, "abc");
    uint32_t base = out.raw & ((1u << FileIdx::FILEIDX_BITS) - 1u);
    uint32_t flag = 1u << 31;
    uint32_t raw = flag | (base << SourceLoc::FILEIDX_SHIFT) | 5u;
    SourceLoc badLoc{raw};
    EXPECT_THROW(ctx.validateLoc(badLoc), std::runtime_error);
}