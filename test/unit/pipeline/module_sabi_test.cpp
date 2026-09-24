// module_sabi_test.cpp - тесты формата ELF-секции .trust.sabi (семантический ABI модуля)
#include "module_loader/module_sabi.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace trust {
namespace {

std::vector<unsigned char> makeSection(const std::vector<std::string>& decls, bool compress = false) {
    return buildSabiSection(decls, "0.7.0-test", "trust-lang\t0.7.0\nfile\tmd5\n", compress);
}

TEST(ModuleSabiTest, RoundTripUncompressed) {
    const std::vector<std::string> decls = {"x := %c_x;", "f(a:Int32):Bool := %c_f;"};
    std::string error;
    auto data = parseSabiSection(makeSection(decls), error);
    ASSERT_TRUE(data.has_value()) << error;
    EXPECT_EQ(data->compilerVersion, "0.7.0-test");
    EXPECT_EQ(data->srcRecord, "trust-lang\t0.7.0\nfile\tmd5\n");
    EXPECT_EQ(data->decls, decls);
}

TEST(ModuleSabiTest, RoundTripCompressed) {
    const std::vector<std::string> decls = {"x := %c_x;", "g(a:Int64):Bool := %c_g_Int64_;"};
    std::string error;
    auto data = parseSabiSection(makeSection(decls, /*compress=*/true), error);
    ASSERT_TRUE(data.has_value()) << error;
    EXPECT_EQ(data->decls, decls);
}

TEST(ModuleSabiTest, EmptyDecls) {
    std::string error;
    auto data = parseSabiSection(makeSection({}), error);
    ASSERT_TRUE(data.has_value()) << error;
    EXPECT_TRUE(data->decls.empty());
}

TEST(ModuleSabiTest, DeterministicBytes) {
    EXPECT_EQ(makeSection({"a := %c_a;", "b := %c_b;"}), makeSection({"a := %c_a;", "b := %c_b;"}));
}

TEST(ModuleSabiTest, TruncatedHeader) {
    std::vector<unsigned char> bytes = makeSection({"a := %c_a;"});
    bytes.resize(4);
    std::string error;
    EXPECT_FALSE(parseSabiSection(bytes, error).has_value());
    EXPECT_NE(error.find("truncated"), std::string::npos);
}

TEST(ModuleSabiTest, TruncatedBody) {
    std::vector<unsigned char> bytes = makeSection({"a := %c_a;"});
    bytes.resize(bytes.size() - 2);
    std::string error;
    EXPECT_FALSE(parseSabiSection(bytes, error).has_value());
    EXPECT_NE(error.find("truncated"), std::string::npos);
}

TEST(ModuleSabiTest, BadMagic) {
    std::vector<unsigned char> bytes = makeSection({"a := %c_a;"});
    bytes[0] = 'X';
    std::string error;
    EXPECT_FALSE(parseSabiSection(bytes, error).has_value());
    EXPECT_NE(error.find("magic"), std::string::npos);
}

TEST(ModuleSabiTest, UnsupportedFormatVersion) {
    std::vector<unsigned char> bytes = makeSection({"a := %c_a;"});
    bytes[4] = 99; // formatVersion lo byte
    std::string error;
    EXPECT_FALSE(parseSabiSection(bytes, error).has_value());
    EXPECT_NE(error.find("format version"), std::string::npos);
}

TEST(ModuleSabiTest, CountMismatch) {
    std::vector<unsigned char> bytes = makeSection({"a := %c_a;"});
    bytes[12] = 5; // count lo byte
    std::string error;
    EXPECT_FALSE(parseSabiSection(bytes, error).has_value());
    EXPECT_NE(error.find("count mismatch"), std::string::npos);
}

TEST(ModuleSabiTest, DeclNameForms) {
    EXPECT_EQ(declName("f(a:Int32):Bool := %c_f;"), "f");
    EXPECT_EQ(declName("x:Int64 := %c_y;"), "x");
    EXPECT_EQ(declName("x := %c_x;"), "x");
    EXPECT_EQ(declName("`==`(a:V):Bool := %operator__;"), "`==`");
}

TEST(ModuleSabiTest, FilterMasks) {
    const std::vector<std::string> decls = {"alpha := %c_a;", "alpha2 := %c_a2;", "beta(a:Int32):Bool := %c_b;"};
    EXPECT_EQ(filterModuleDecls(decls, "").size(), 3u);            // пустая маска = все
    EXPECT_EQ(filterModuleDecls(decls, "   ").size(), 3u);         // пробелы = все
    EXPECT_EQ(filterModuleDecls(decls, "alpha").size(), 1u);       // точно
    EXPECT_EQ(filterModuleDecls(decls, "alpha*").size(), 2u);      // *
    EXPECT_EQ(filterModuleDecls(decls, "alph?").size(), 1u);       // ?
    EXPECT_EQ(filterModuleDecls(decls, "beta, alpha").size(), 2u); // список
    EXPECT_EQ(filterModuleDecls(decls, " beta ").size(), 1u);      // пробелы вокруг маски
    EXPECT_TRUE(filterModuleDecls(decls, "nope").empty());         // нет совпадений - пусто
}

TEST(ModuleSabiTest, UnknownFlags) {
    std::vector<unsigned char> bytes = makeSection({"a := %c_a;"});
    bytes[8] = 0x80; // неизвестный бит флагов
    std::string error;
    EXPECT_FALSE(parseSabiSection(bytes, error).has_value());
    EXPECT_NE(error.find("flags"), std::string::npos);
}

TEST(ModuleSabiTest, MalformedMetadata) {
    std::vector<unsigned char> bytes = makeSection({"a := %c_a;"});
    constexpr std::size_t kHeaderSize = 4 + 5 * sizeof(uint32_t);
    bytes[kHeaderSize] = '\0'; // обнуляем первый байт compilerVersion → пустая мета
    std::string error;
    EXPECT_FALSE(parseSabiSection(bytes, error).has_value());
    EXPECT_NE(error.find("metadata"), std::string::npos);
}

TEST(ModuleSabiTest, ReadMissingFile) {
    std::string error;
    EXPECT_FALSE(readModuleSabi("/nonexistent/path/mod.trust", error).has_value());
    EXPECT_FALSE(error.empty());
}

} // namespace
} // namespace trust
