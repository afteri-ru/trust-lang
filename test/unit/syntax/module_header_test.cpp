// Файловый заголовок модуля `<path>;` / `<path> { … };` (scanModuleHeader/maskModuleHeader).
// Контракт: заголовок — первая значимая конструкция; путь статичен и в AST не входит; блок
// интерфейса разбирает грамматика. `(` до `;`/`{` означает выражение импорта, а не заголовок.

#include "syntax/util/warning_push.h"
#include <gtest/gtest.h>
#include "syntax/util/warning_pop.h"

#include "syntax/module_header.hpp"

#include <algorithm>
#include <string>

namespace {

using trust::maskModuleHeader;
using trust::ModuleHeaderScan;
using trust::scanModuleHeader;

TEST(ModuleHeaderScan, NoHeaderWhenFirstSignificantCharIsNotBackslash) {
    const ModuleHeaderScan h = scanModuleHeader("func() := {};");
    EXPECT_FALSE(h.found);
    EXPECT_TRUE(h.error.empty());
}

TEST(ModuleHeaderScan, HeaderEndsWithSemicolon) {
    const ModuleHeaderScan h = scanModuleHeader("\\lib;");
    ASSERT_TRUE(h.found);
    EXPECT_TRUE(h.error.empty());
    EXPECT_EQ(h.header, "\\lib");
    EXPECT_FALSE(h.hasInterface);
    EXPECT_EQ(h.end, 5u);
}

TEST(ModuleHeaderScan, HeaderWithInterfaceBlock) {
    const std::string text = "\\lib { f(); };";
    const ModuleHeaderScan h = scanModuleHeader(text);
    ASSERT_TRUE(h.found);
    EXPECT_TRUE(h.error.empty());
    EXPECT_EQ(h.header, "\\lib");
    EXPECT_TRUE(h.hasInterface);
    EXPECT_EQ(h.interfaceBegin, text.find('{'));
}

TEST(ModuleHeaderScan, SkipsBomHashAndBlockComments) {
    const ModuleHeaderScan bom = scanModuleHeader("\xEF\xBB\xBF\\lib;");
    ASSERT_TRUE(bom.found);
    EXPECT_EQ(bom.header, "\\lib");
    EXPECT_EQ(bom.begin, 3u);

    const ModuleHeaderScan hash = scanModuleHeader("# a comment\n\\lib;");
    ASSERT_TRUE(hash.found);
    EXPECT_EQ(hash.header, "\\lib");

    const ModuleHeaderScan shebang = scanModuleHeader("#!/usr/bin/env trust\n\\lib;");
    ASSERT_TRUE(shebang.found);
    EXPECT_EQ(shebang.header, "\\lib");

    const ModuleHeaderScan block = scanModuleHeader("/* c */ \\lib;");
    ASSERT_TRUE(block.found);
    EXPECT_EQ(block.header, "\\lib");
}

TEST(ModuleHeaderScan, GroupAndPackagePaths) {
    const ModuleHeaderScan group = scanModuleHeader("\\\\util\\string;");
    ASSERT_TRUE(group.found);
    EXPECT_EQ(group.header, "\\\\util\\string");

    const ModuleHeaderScan package = scanModuleHeader("\\\\\\pkg\\f;");
    ASSERT_TRUE(package.found);
    EXPECT_EQ(package.header, "\\\\\\pkg\\f");
}

TEST(ModuleHeaderScan, ParenMeansImportNotHeader) {
    const ModuleHeaderScan h = scanModuleHeader("\\lib();");
    EXPECT_FALSE(h.found);
    EXPECT_TRUE(h.error.empty());
}

TEST(ModuleHeaderScan, MissingSemicolonIsDiagnosed) {
    // Перевод строки до `;`/`{`: путь не фиксируется (found=false), но диагностика выдаётся —
    // загрузчик сообщает ошибку и не берёт logical name из заголовка.
    const ModuleHeaderScan h = scanModuleHeader("\\lib\n");
    EXPECT_FALSE(h.found);
    EXPECT_EQ(h.error, "module header must end with ';'");
}

TEST(ModuleHeaderScan, UnterminatedHeaderIsDiagnosed) {
    const ModuleHeaderScan h = scanModuleHeader("\\lib");
    ASSERT_TRUE(h.found);
    EXPECT_EQ(h.header, "\\lib");
    EXPECT_EQ(h.error, "unterminated module header");
}

TEST(ModuleHeaderMask, ReplacesPathKeepsNewlines) {
    std::string source = "\\lib { \n f(); \n};";
    const std::size_t newlines = static_cast<std::size_t>(std::count(source.begin(), source.end(), '\n'));
    maskModuleHeader(source);
    EXPECT_EQ(static_cast<std::size_t>(std::count(source.begin(), source.end(), '\n')), newlines);
    EXPECT_EQ(source, "     { \n f(); \n};");
}

} // namespace
