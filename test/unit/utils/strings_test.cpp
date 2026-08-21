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
    EXPECT_FALSE(is_number(""));
    EXPECT_TRUE(is_number("42"));
    EXPECT_TRUE(is_number("-42"));
    EXPECT_TRUE(is_number("0"));
    EXPECT_FALSE(is_number("abc"));
    EXPECT_FALSE(is_number("12a34"));
    EXPECT_FALSE(is_number("-"));
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
    EXPECT_EQ(extract_name("", 0), "");
    EXPECT_EQ(extract_name("abc", 10), "");
    EXPECT_EQ(extract_name("hello world", 0), "hello");
    EXPECT_EQ(extract_name("hello", 0), "hello");
    EXPECT_EQ(extract_name(" hello", 3), "hello");
    EXPECT_EQ(extract_name("my_var", 2), "my_var");
    EXPECT_EQ(extract_name("std::vector", 5), "std::vector");
    EXPECT_EQ(extract_name("123 abc", 0), "");
    EXPECT_EQ(extract_name("var42", 3), "var42");
    EXPECT_EQ(extract_name("_", 0), "_");
}

TEST(StringsTest, ExtractNameUtf8) {
    // "привет" в UTF8 - 12 байт
    const char* s = "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
    EXPECT_EQ(extract_name(s, 0), std::string_view(s, 12));
    EXPECT_EQ(extract_name(s, 6), std::string_view(s, 12));
    EXPECT_EQ(extract_name(s, 0), std::string_view(s, std::strlen(s)));
    EXPECT_EQ(extract_name(s, 1), std::string_view(s, 12));
}

// ══════════════════════════════════════════════════════════════
//  Конвертация имени в C++ идентификатор и обратно
// ══════════════════════════════════════════════════════════════

TEST(NameToCppTest, Ascii) {
    EXPECT_EQ(name_to_cpp("hello"), "c_hello");
    EXPECT_EQ(name_to_cpp("foo_bar"), "c_foo_bar");
    EXPECT_EQ(name_to_cpp("_private"), "c__private");
}

TEST(NameToCppTest, Empty) {
    // Пустое имя → пустая строка (владеющая), не ломает конвертацию.
    EXPECT_EQ(name_to_cpp(""), "");
}

TEST(StripNativePrefixTest, SlicesLeadingPercent) {
    EXPECT_EQ(strip_native_prefix("%add"), "add");
    EXPECT_EQ(strip_native_prefix("%std::max"), "std::max");
    EXPECT_EQ(strip_native_prefix("x"), "x");
    EXPECT_EQ(strip_native_prefix(""), "");
    // Срезается только ВЕДУЩИЙ '%'.
    EXPECT_EQ(strip_native_prefix("%a%"), "a%");
}

TEST(NameToCppTest, CppQualified) {
    EXPECT_EQ(name_to_cpp("std::vector"), "cpp_std$$vector");
    EXPECT_EQ(name_to_cpp("a::b::c"), "cpp_a$$b$$c");
}

TEST(NameToCppTest, RussianTranslit) {
    EXPECT_EQ(name_to_cpp("\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"), "ru_privet");
    EXPECT_EQ(name_to_cpp("\xD0\x9C\xD0\xB8\xD1\x80"), "ru_Mir");
    // Полностью русское "Ю" - транслитерация, а не HEX
    EXPECT_EQ(name_to_cpp("\xD0\xAE"), "ru_Yu");
}

TEST(NameToCppTest, MixedIdentifierHexEncoded) {
    // Смешанный идентификатор (русская буква + латинская ASCII) - ветка u8_,
    // HEX-кодирование всех байтов: Ю = D0 AE, A = 41.
    EXPECT_EQ(name_to_cpp("\xD0\xAE"
                          "A"),
              "u8_D0AE41");
}

TEST(CppToNameTest, Ascii) {
    EXPECT_EQ(cpp_to_name("c_hello"), "hello");
    EXPECT_EQ(cpp_to_name("c_foo_bar"), "foo_bar");
}

TEST(CppToNameTest, CppQualified) {
    EXPECT_EQ(cpp_to_name("cpp_std$$vector"), "std::vector");
    EXPECT_EQ(cpp_to_name("cpp_a$$b$$c"), "a::b::c");
}

TEST(CppToNameTest, RussianTranslit) {
    EXPECT_EQ(cpp_to_name("ru_privet"), "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
    EXPECT_EQ(cpp_to_name("ru_Mir"), "\xD0\x9C\xD0\xB8\xD1\x80");
    EXPECT_EQ(cpp_to_name("ru_Yu"), "\xD0\xAE");
}

TEST(NameToCppTest, Native) {
    // Нативные %-имена - уже C++-символы рантайма: '%' срезается, остальное как есть.
    EXPECT_EQ(name_to_cpp("%add"), "add");
    EXPECT_EQ(name_to_cpp("%x"), "x");
    EXPECT_EQ(name_to_cpp("%std::max"), "std::max");
}

TEST(CppToNameTest, EmptyAndNative) {
    EXPECT_EQ(cpp_to_name(""), "");
    // Имя без известного префикса - нативное C++-имя: восстанавливается маркер '%'.
    EXPECT_EQ(cpp_to_name("a"), "%a");
    EXPECT_EQ(cpp_to_name("ab"), "%ab");
    EXPECT_EQ(cpp_to_name("add"), "%add");
}

TEST(NameRoundTripTest, Native) {
    for (const char* n : {"%add", "%x", "%std::max"}) {
        EXPECT_EQ(cpp_to_name(name_to_cpp(n)), std::string(n));
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
        auto cpp = name_to_cpp(name);
        EXPECT_FALSE(cpp.empty());
        EXPECT_EQ(cpp_to_name(cpp), std::string(name));
    }
}

// -- Вставки C++ (EmbedExpr): маркеры $/@ → name_to_cpp --

TEST(EmbedCppTest, TransformsLocalAndQualified) {
    EXPECT_EQ(transform_embed_cpp("printf(\"%s\", $msg);"), "printf(\"%s\", c_msg);");
    EXPECT_EQ(transform_embed_cpp("return @x == 42;"), "return c_x == 42;");
    // @-квалификация с :: → cpp_std$$vector.
    EXPECT_EQ(transform_embed_cpp("@std::vector"), "cpp_std$$vector");
    EXPECT_EQ(transform_embed_cpp("@ns::x"), "cpp_ns$$x");
    // Чистый C++-текст без маркеров остаётся без изменений.
    EXPECT_EQ(transform_embed_cpp("int x = 42;"), "int x = 42;");
    // Маркер не перед именем (в строке/после спецсимвола) - копируется как есть.
    EXPECT_EQ(transform_embed_cpp("printf(\"$%d\", x);"), "printf(\"$%d\", x);");
    // Юникодное trust-имя конвертируется.
    EXPECT_EQ(transform_embed_cpp("$привет"), "ru_privet");
}

TEST(EmbedCppTest, ExtractsNames) {
    auto names = extract_embed_names("a + $x + @y::z");
    ASSERT_EQ(names.size(), 2u);
    // $x - локальная переменная (маркер $ сохраняется: после нормализации локальная хранится с $).
    EXPECT_EQ(names[0], "$x");
    EXPECT_EQ(names[1], "y::z");
    // Маркеры не перед именами не извлекаются.
    EXPECT_TRUE(extract_embed_names("printf(\"$%d\", x);").empty());
}

TEST(StringsTest, EscapeCppStringAllSpecial) {
    // Кавычка, backslash и все управляющие символами в C++-escape.
    EXPECT_EQ(escape_cpp_string("\""), "\\\"");
    EXPECT_EQ(escape_cpp_string("\\"), "\\\\");
    EXPECT_EQ(escape_cpp_string("\n"), "\\n");
    EXPECT_EQ(escape_cpp_string("\t"), "\\t");
    EXPECT_EQ(escape_cpp_string("\r"), "\\r");
    EXPECT_EQ(escape_cpp_string(std::string("\0", 1)), "\\0");
    EXPECT_EQ(escape_cpp_string("\a"), "\\a");
    EXPECT_EQ(escape_cpp_string("\b"), "\\b");
    EXPECT_EQ(escape_cpp_string("\f"), "\\f");
    EXPECT_EQ(escape_cpp_string("\v"), "\\v");
    EXPECT_EQ(escape_cpp_string("?"), "\\?");
    // Прочие управляющие (0x1B, 0x01) → hex-escape.
    EXPECT_EQ(escape_cpp_string(std::string("\x1B", 1)), "\\x1B");
    EXPECT_EQ(escape_cpp_string(std::string("\x01", 1)), "\\x01");
    // UTF-8 / печатные - без изменений.
    EXPECT_EQ(escape_cpp_string("abc_123"), "abc_123");
}

TEST(StringsTest, UnescapeCppString) {

    EXPECT_EQ(unescape_cpp_string("\\\""), "\"");
    EXPECT_EQ(unescape_cpp_string("\\\\"), "\\");
    EXPECT_EQ(unescape_cpp_string("\\n"), "\n");
    EXPECT_EQ(unescape_cpp_string("\\t"), "\t");
    EXPECT_EQ(unescape_cpp_string("\\x1B"), "\x1B");
    // Неизвестная escape-последовательность сохраняется как символ.
    EXPECT_EQ(unescape_cpp_string("\\q"), "q");
    // Строка без escapes - без изменений.
    EXPECT_EQ(unescape_cpp_string("plain"), "plain");
}

TEST(StringsTest, EscapeUnescapeRoundTrip) {
    const std::string src("a\"b\\c\nd\te\rf\0g\ah\bi\fj\vk?l\x1Bm", 25);
    EXPECT_EQ(unescape_cpp_string(escape_cpp_string(src)), src);
}

} // namespace trust::utils
