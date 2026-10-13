#include "utils/strings.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace trust::utils {

TEST(StringsTest, Trim) {
    EXPECT_EQ(trim(""), "");
    EXPECT_EQ(trim("hello"), "hello");
    EXPECT_EQ(trim("  hello"), "hello");
    EXPECT_EQ(trim("hello  "), "hello");
    EXPECT_EQ(trim("  hello  "), "hello");
    EXPECT_EQ(trim("\t test\t "), "test");
    EXPECT_EQ(trim("   \t  "), "");
}

TEST(StringsTest, IsNumber) {
    EXPECT_FALSE(isNumber(""));
    EXPECT_TRUE(isNumber("42"));
    EXPECT_TRUE(isNumber("-42"));
    EXPECT_TRUE(isNumber("0"));
    EXPECT_FALSE(isNumber("abc"));
    EXPECT_FALSE(isNumber("12a34"));
    EXPECT_FALSE(isNumber("-"));
}

TEST(StringsTest, Tokenize) {
    EXPECT_EQ(tokenize(""), std::vector<std::string_view>{});
    EXPECT_EQ(tokenize("hello"), std::vector<std::string_view>({"hello"}));
    EXPECT_EQ(tokenize("a b c"), std::vector<std::string_view>({"a", "b", "c"}));
    EXPECT_EQ(tokenize("  a b"), std::vector<std::string_view>({"a", "b"}));
    EXPECT_EQ(tokenize("a\tb\tc"), std::vector<std::string_view>({"a", "b", "c"}));
    EXPECT_EQ(tokenize("   "), std::vector<std::string_view>{});
    EXPECT_EQ(tokenize("  one  two  "), std::vector<std::string_view>({"one", "two"}));
}

TEST(StringsTest, ExtractName) {
    EXPECT_EQ(extractName("", 0), "");
    EXPECT_EQ(extractName("abc", 10), "");
    EXPECT_EQ(extractName("hello world", 0), "hello");
    EXPECT_EQ(extractName("hello", 0), "hello");
    EXPECT_EQ(extractName(" hello", 3), "hello");
    EXPECT_EQ(extractName("my_var", 2), "my_var");
    EXPECT_EQ(extractName("std::vector", 5), "std::vector");
    EXPECT_EQ(extractName("123 abc", 0), "");
    EXPECT_EQ(extractName("var42", 3), "var42");
    EXPECT_EQ(extractName("_", 0), "_");
}

TEST(StringsTest, ExtractNameUtf8) {
    // "привет" в UTF8 - 12 байт
    const char* s = "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
    EXPECT_EQ(extractName(s, 0), std::string_view(s, 12));
    EXPECT_EQ(extractName(s, 6), std::string_view(s, 12));
    EXPECT_EQ(extractName(s, 0), std::string_view(s, std::strlen(s)));
    EXPECT_EQ(extractName(s, 1), std::string_view(s, 12));
}

// ══════════════════════════════════════════════════════════════
//  Конвертация имени в C++ идентификатор и обратно
// ══════════════════════════════════════════════════════════════

TEST(NameToCppTest, Ascii) {
    EXPECT_EQ(nameToCpp("hello"), "c_hello");
    EXPECT_EQ(nameToCpp("foo_bar"), "c_foo_bar");
    EXPECT_EQ(nameToCpp("_private"), "c__private");
}

TEST(NameToCppTest, Empty) {
    // Пустое имя → пустая строка (владеющая), не ломает конвертацию.
    EXPECT_EQ(nameToCpp(""), "");
}

TEST(StripNativePrefixTest, SlicesLeadingPercent) {
    EXPECT_EQ(stripNativePrefix("%add"), "add");
    EXPECT_EQ(stripNativePrefix("%std::max"), "std::max");
    EXPECT_EQ(stripNativePrefix("x"), "x");
    EXPECT_EQ(stripNativePrefix(""), "");
    // Срезается только ВЕДУЩИЙ '%'.
    EXPECT_EQ(stripNativePrefix("%a%"), "a%");
}

TEST(NameToCppTest, CppQualified) {
    EXPECT_EQ(nameToCpp("std::vector"), "cpp_std$$vector");
    EXPECT_EQ(nameToCpp("a::b::c"), "cpp_a$$b$$c");
}

TEST(NameToCppTest, RussianTranslit) {
    EXPECT_EQ(nameToCpp("\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"), "ru_privet");
    EXPECT_EQ(nameToCpp("\xD0\x9C\xD0\xB8\xD1\x80"), "ru_Mir");
    // Полностью русское "Ю" - транслитерация, а не HEX
    EXPECT_EQ(nameToCpp("\xD0\xAE"), "ru_Yu");
}

TEST(NameToCppTest, MixedIdentifierHexEncoded) {
    // Смешанный идентификатор (русская буква + латинская ASCII) - ветка u8_,
    // HEX-кодирование всех байтов: Ю = D0 AE, A = 41.
    EXPECT_EQ(nameToCpp("\xD0\xAE"
                          "A"),
              "u8_D0AE41");
}

TEST(CppToNameTest, Ascii) {
    EXPECT_EQ(cppToName("c_hello"), "hello");
    EXPECT_EQ(cppToName("c_foo_bar"), "foo_bar");
}

TEST(CppToNameTest, CppQualified) {
    EXPECT_EQ(cppToName("cpp_std$$vector"), "std::vector");
    EXPECT_EQ(cppToName("cpp_a$$b$$c"), "a::b::c");
}

TEST(CppToNameTest, RussianTranslit) {
    EXPECT_EQ(cppToName("ru_privet"), "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
    EXPECT_EQ(cppToName("ru_Mir"), "\xD0\x9C\xD0\xB8\xD1\x80");
    EXPECT_EQ(cppToName("ru_Yu"), "\xD0\xAE");
}

TEST(NameToCppTest, Native) {
    // Нативные %-имена - уже C++-символы рантайма: '%' срезается, остальное как есть.
    EXPECT_EQ(nameToCpp("%add"), "add");
    EXPECT_EQ(nameToCpp("%x"), "x");
    EXPECT_EQ(nameToCpp("%std::max"), "std::max");
}

TEST(CppToNameTest, EmptyAndNative) {
    EXPECT_EQ(cppToName(""), "");
    // Имя без известного префикса - нативное C++-имя: восстанавливается маркер '%'.
    EXPECT_EQ(cppToName("a"), "%a");
    EXPECT_EQ(cppToName("ab"), "%ab");
    EXPECT_EQ(cppToName("add"), "%add");
}

TEST(NameRoundTripTest, Native) {
    for (const char* n : {"%add", "%x", "%std::max"}) {
        EXPECT_EQ(cppToName(nameToCpp(n)), std::string(n));
    }
}

TEST(NameRoundTripTest, Russian) {
    const char* russian[] = {
        "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82",
        "\xD0\x9C\xD0\xB8\xD1\x80",
        "\xD0\xA2\xD0\xB5\xD1\x81\xD1\x82",
        "\xD0\xAE",
    };
    for (const char* name : russian) {
        auto cpp = nameToCpp(name);
        EXPECT_FALSE(cpp.empty());
        EXPECT_EQ(cppToName(cpp), std::string(name));
    }
}

// -- Вставки C++ (EmbedExpr): маркеры $/@ → nameToCpp --

TEST(EmbedCppTest, TransformsLocalAndQualified) {
    EXPECT_EQ(transformEmbedCpp("printf(\"%s\", $msg);"), "printf(\"%s\", c_msg);");
    EXPECT_EQ(transformEmbedCpp("return @x == 42;"), "return c_x == 42;");
    // @-квалификация с :: → cpp_std$$vector.
    EXPECT_EQ(transformEmbedCpp("@std::vector"), "cpp_std$$vector");
    EXPECT_EQ(transformEmbedCpp("@ns::x"), "cpp_ns$$x");
    // Чистый C++-текст без маркеров остаётся без изменений.
    EXPECT_EQ(transformEmbedCpp("int x = 42;"), "int x = 42;");
    // Маркер не перед именем (в строке/после спецсимвола) - копируется как есть.
    EXPECT_EQ(transformEmbedCpp("printf(\"$%d\", x);"), "printf(\"$%d\", x);");
    // Юникодное trust-имя конвертируется.
    EXPECT_EQ(transformEmbedCpp("$привет"), "ru_privet");
}

TEST(EmbedCppTest, ExtractsNames) {
    auto names = extractEmbedNames("a + $x + @y::z");
    ASSERT_EQ(names.size(), 2u);
    // $x - локальная переменная (маркер $ сохраняется: после нормализации локальная хранится с $).
    EXPECT_EQ(names[0], "$x");
    EXPECT_EQ(names[1], "y::z");
    // Маркеры не перед именами не извлекаются.
    EXPECT_TRUE(extractEmbedNames("printf(\"$%d\", x);").empty());
}

TEST(StringsTest, EscapeCppStringAllSpecial) {
    // Кавычка, backslash и все управляющие символами в C++-escape.
    EXPECT_EQ(escapeCppString("\""), "\\\"");
    EXPECT_EQ(escapeCppString("\\"), "\\\\");
    EXPECT_EQ(escapeCppString("\n"), "\\n");
    EXPECT_EQ(escapeCppString("\t"), "\\t");
    EXPECT_EQ(escapeCppString("\r"), "\\r");
    EXPECT_EQ(escapeCppString(std::string("\0", 1)), "\\0");
    EXPECT_EQ(escapeCppString("\a"), "\\a");
    EXPECT_EQ(escapeCppString("\b"), "\\b");
    EXPECT_EQ(escapeCppString("\f"), "\\f");
    EXPECT_EQ(escapeCppString("\v"), "\\v");
    EXPECT_EQ(escapeCppString("?"), "\\?");
    // Прочие управляющие (0x1B, 0x01) → hex-escape.
    EXPECT_EQ(escapeCppString(std::string("\x1B", 1)), "\\x1B");
    EXPECT_EQ(escapeCppString(std::string("\x01", 1)), "\\x01");
    // UTF-8 / печатные - без изменений.
    EXPECT_EQ(escapeCppString("abc_123"), "abc_123");
}

TEST(StringsTest, UnescapeCppString) {

    EXPECT_EQ(unescapeCppString("\\\""), "\"");
    EXPECT_EQ(unescapeCppString("\\\\"), "\\");
    EXPECT_EQ(unescapeCppString("\\n"), "\n");
    EXPECT_EQ(unescapeCppString("\\t"), "\t");
    EXPECT_EQ(unescapeCppString("\\x1B"), "\x1B");
    // Неизвестная escape-последовательность сохраняется как символ.
    EXPECT_EQ(unescapeCppString("\\q"), "q");
    // Строка без escapes - без изменений.
    EXPECT_EQ(unescapeCppString("plain"), "plain");
}

TEST(StringsTest, EscapeUnescapeRoundTrip) {
    const std::string src("a\"b\\c\nd\te\rf\0g\ah\bi\fj\vk?l\x1Bm", 25);
    EXPECT_EQ(unescapeCppString(escapeCppString(src)), src);
}

// -- Сокращение пути и префиксация строк (отладочный вывод) --

TEST(StringsTest, FileNameOfAndShortenedPath) {
    EXPECT_EQ(fileNameOf("a/b/c.src"), "c.src");
    EXPECT_EQ(fileNameOf("c.src"), "c.src");
    EXPECT_EQ(shortenedPath("test/lit/pipeline/debug.src"), ".../debug.src");
    EXPECT_EQ(shortenedPath("debug.src"), "debug.src");
}

TEST(StringsTest, PrefixEachLine) {
    EXPECT_EQ(prefixEachLine("scope: depth=2\n  [1] ModuleDecl", ".../f.src:7: "),
              ".../f.src:7: scope: depth=2\n.../f.src:7:   [1] ModuleDecl\n");
}

TEST(StringsTest, PrefixEachLineTrailingNewline) {
    EXPECT_EQ(prefixEachLine("one\n", "p: "), "p: one\n");
}

TEST(StringsTest, PrefixEachLineSingleLine) {
    EXPECT_EQ(prefixEachLine("msg", "p: "), "p: msg\n");
}

} // namespace trust::utils
