#include "diag/context.hpp"
#include "utils/error.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

using namespace trust;
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
