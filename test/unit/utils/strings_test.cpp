#include <gtest/gtest.h>
#include "utils/strings.hpp"

namespace trust {
namespace {

// ══════════════════════════════════════════════════════════════
//                     trim tests
// ══════════════════════════════════════════════════════════════

TEST(TrimTest, Empty) {
    EXPECT_EQ(utils::trim(""), "");
}

TEST(TrimTest, NoWhitespace) {
    EXPECT_EQ(utils::trim("hello"), "hello");
}

TEST(TrimTest, LeadingSpaces) {
    EXPECT_EQ(utils::trim("  hello"), "hello");
}

TEST(TrimTest, TrailingSpaces) {
    EXPECT_EQ(utils::trim("hello  "), "hello");
}

TEST(TrimTest, BothSides) {
    EXPECT_EQ(utils::trim("  hello  "), "hello");
}

TEST(TrimTest, TabsAndSpaces) {
    EXPECT_EQ(utils::trim("\t test\t "), "test");
}

TEST(TrimTest, OnlyWhitespace) {
    EXPECT_EQ(utils::trim("   \t  "), "");
}

// ══════════════════════════════════════════════════════════════
//                     is_number tests
// ══════════════════════════════════════════════════════════════

TEST(IsNumberTest, Empty) {
    EXPECT_FALSE(utils::is_number(""));
}

TEST(IsNumberTest, PositiveInteger) {
    EXPECT_TRUE(utils::is_number("42"));
}

TEST(IsNumberTest, NegativeInteger) {
    EXPECT_TRUE(utils::is_number("-42"));
}

TEST(IsNumberTest, Zero) {
    EXPECT_TRUE(utils::is_number("0"));
}

TEST(IsNumberTest, NotANumber) {
    EXPECT_FALSE(utils::is_number("abc"));
}

TEST(IsNumberTest, Mixed) {
    EXPECT_FALSE(utils::is_number("12a34"));
}

TEST(IsNumberTest, JustMinus) {
    EXPECT_FALSE(utils::is_number("-"));
}

// ══════════════════════════════════════════════════════════════
//                     tokenize tests
// ══════════════════════════════════════════════════════════════

TEST(TokenizeTest, Empty) {
    auto tokens = utils::tokenize("");
    EXPECT_TRUE(tokens.empty());
}

TEST(TokenizeTest, SingleToken) {
    auto tokens = utils::tokenize("hello");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0], "hello");
}

TEST(TokenizeTest, MultipleTokens) {
    auto tokens = utils::tokenize("a b c");
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], "a");
    EXPECT_EQ(tokens[1], "b");
    EXPECT_EQ(tokens[2], "c");
}

TEST(TokenizeTest, LeadingSpaces) {
    auto tokens = utils::tokenize("  a b");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], "a");
    EXPECT_EQ(tokens[1], "b");
}

TEST(TokenizeTest, TabsSeparated) {
    auto tokens = utils::tokenize("a\tb\tc");
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], "a");
    EXPECT_EQ(tokens[1], "b");
    EXPECT_EQ(tokens[2], "c");
}

TEST(TokenizeTest, OnlyWhitespace) {
    auto tokens = utils::tokenize("   ");
    EXPECT_TRUE(tokens.empty());
}

TEST(TokenizeTest, MixedWhitespace) {
    auto tokens = utils::tokenize("  one  two  ");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], "one");
    EXPECT_EQ(tokens[1], "two");
}

// ══════════════════════════════════════════════════════════════
//                     extract_name tests
// ══════════════════════════════════════════════════════════════

TEST(ExtractNameTest, Empty) {
    EXPECT_EQ(utils::extract_name("", 0), "");
}

TEST(ExtractNameTest, OffsetOutOfRange) {
    EXPECT_EQ(utils::extract_name("abc", 10), "");
}

TEST(ExtractNameTest, SimpleAscii) {
    EXPECT_EQ(utils::extract_name("hello world", 0), "hello");
}

TEST(ExtractNameTest, AtTheEnd) {
    EXPECT_EQ(utils::extract_name("hello", 0), "hello");
}

TEST(ExtractNameTest, OffsetInsideIdentifier) {
    // смещение 2 указывает на 'l' в "hello"
    EXPECT_EQ(utils::extract_name(" hello", 3), "hello");
}

TEST(ExtractNameTest, WithUnderscore) {
    EXPECT_EQ(utils::extract_name("my_var", 2), "my_var");
}

TEST(ExtractNameTest, WithColon) {
    EXPECT_EQ(utils::extract_name("std::vector", 5), "std::vector");
}

TEST(ExtractNameTest, NotAnIdentifier) {
    EXPECT_EQ(utils::extract_name("123 abc", 0), "");
}

TEST(ExtractNameTest, UTF8Russian) {
    const char* s = "привет мир";
    // смещение 0: "привет"
    EXPECT_EQ(utils::extract_name(s, 0), std::string_view(s, 12)); // "привет" в UTF8 — 12 байт
}

TEST(ExtractNameTest, UTF8OffsetInside) {
    const char* s = "привет мир";
    // смещение 6 (середина "привет")
    EXPECT_EQ(utils::extract_name(s, 6), std::string_view(s, 12));
}

TEST(ExtractNameTest, MixedASCIIAndUTF8) {
    const char* s = "var_привет";
    EXPECT_EQ(utils::extract_name(s, 0), std::string_view(s, std::strlen(s)));
}

TEST(ExtractNameTest, OffsetOnContinuationByte) {
    const char* s = "привет";
    // смещение 1 — continuation-байт внутри "п" (0xD0 0xBF)
    auto result = utils::extract_name(s, 1);
    EXPECT_EQ(result, std::string_view(s, 12));
}

TEST(ExtractNameTest, IdentifierWithDigits) {
    // Идентификатор может содержать цифры, но не начинаться с них;
    // offset указывает на цифру, extract_name должен найти начало слева
    EXPECT_EQ(utils::extract_name("var42", 3), "var42");
}

TEST(ExtractNameTest, JustUnderscore) {
    EXPECT_EQ(utils::extract_name("_", 0), "_");
}

TEST(ExtractNameTest, JustColon) {
    EXPECT_EQ(utils::extract_name(":", 0), ":");
}

TEST(ExtractNameTest, DigitsOnly) {
    // Цифры сами по себе не являются идентификатором (не ident_start)
    EXPECT_EQ(utils::extract_name("123", 0), "");
}

TEST(ExtractNameTest, OffsetJustBefore) {
    // offset указывает на пробел перед идентификатором
    EXPECT_EQ(utils::extract_name(" hello", 0), "");
}

// ══════════════════════════════════════════════════════════════
//                     name_to_cpp tests
// ══════════════════════════════════════════════════════════════

TEST(NameToCppTest, Empty) {
    EXPECT_EQ(utils::name_to_cpp(""), "");
}

TEST(NameToCppTest, AsciiNoColon) {
    EXPECT_EQ(utils::name_to_cpp("hello"), "c_hello");
}

TEST(NameToCppTest, AsciiWithColon) {
    EXPECT_EQ(utils::name_to_cpp("std::vector"), "cpp_std$$vector");
}

TEST(NameToCppTest, RussianOnly) {
    std::string_view s = "привет";
    EXPECT_EQ(utils::name_to_cpp(s), "ru_privet");
}

TEST(NameToCppTest, RussianUpperCase) {
    std::string_view s = "ПРИВЕТ";
    EXPECT_EQ(utils::name_to_cpp(s), "ru_PRIVET");
}

TEST(NameToCppTest, RussianWithYo) {
    std::string_view s = "Ёжик";
    EXPECT_EQ(utils::name_to_cpp(s), "ru_Yozhik");
}

TEST(NameToCppTest, NonRussianUTF8) {
    // Немецкий umlaut
    std::string_view s = "üöä";
    EXPECT_EQ(utils::name_to_cpp(s), "u8_C3BCC3B6C3A4");
}

TEST(NameToCppTest, RussianWithSoftSign) {
    // Ь — должен уходить в HEX, т.к. транслитерация необратима
    std::string_view s = "обезьяна";
    EXPECT_EQ(utils::name_to_cpp(s), "u8_D0BED0B1D0B5D0B7D18CD18FD0BDD0B0");
}

TEST(NameToCppTest, RussianWithHardSign) {
    // Ъ — должен уходить в HEX
    std::string_view s = "подъезд";
    EXPECT_EQ(utils::name_to_cpp(s), "u8_D0BFD0BED0B4D18AD0B5D0B7D0B4");
}

TEST(NameToCppTest, SoftSignRoundtrip) {
    // Roundtrip через HEX должен сохранить Ь
    std::string_view s = "обезьяна";
    auto cpp = utils::name_to_cpp(s);
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(s, back);
}

TEST(NameToCppTest, HardSignRoundtrip) {
    // Roundtrip через HEX должен сохранить Ъ
    std::string_view s = "подъезд";
    auto cpp = utils::name_to_cpp(s);
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(s, back);
}

TEST(NameToCppTest, OnlySoftSign) {
    // Только Ь (строчная)
    std::string_view s = "ь";
    auto cpp = utils::name_to_cpp(s);
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(s, back);
}

TEST(NameToCppTest, OnlyHardSign) {
    // Только Ъ (заглавная)
    std::string_view s = "Ъ";
    auto cpp = utils::name_to_cpp(s);
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(s, back);
}

TEST(NameToCppTest, RussianWithBothSigns) {
    // И Ъ и Ь в одном имени — должно быть HEX
    std::string_view s = "съешь";
    auto cpp = utils::name_to_cpp(s);
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(s, back);
}

TEST(NameToCppTest, MixedCyrillicAndLatin) {
    // Смесь русских (без Ъ/Ь) и ASCII латиницы — уходит в HEX из-за ASCII букв
    std::string_view s = "test_переменная";
    auto cpp = utils::name_to_cpp(s);
    EXPECT_EQ(cpp.substr(0, 3), "u8_");
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(s, back);
}

TEST(NameToCppTest, ColonWithRussianNoSpecial) {
    // Двоеточия с русскими буквами без Ъ/Ь и без ASCII латиницы — транслитерация с заменой :
    std::string_view s = "пакет::имя";
    EXPECT_EQ(utils::name_to_cpp(s), "ru_paket$$imya");
}

TEST(NameToCppTest, ColonWithRussianNoSpecialRoundtrip) {
    std::string_view s = "пакет::имя";
    auto cpp = utils::name_to_cpp(s);
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(s, back);
}

// ══════════════════════════════════════════════════════════════
//                     cpp_to_name tests
// ══════════════════════════════════════════════════════════════

TEST(CppToNameTest, Empty) {
    EXPECT_EQ(utils::cpp_to_name(""), "");
}

TEST(CppToNameTest, TooShort) {
    EXPECT_EQ(utils::cpp_to_name("ab"), "");
}

TEST(CppToNameTest, AsciiPrefix) {
    EXPECT_EQ(utils::cpp_to_name("c_hello"), "hello");
}

TEST(CppToNameTest, CppPrefix) {
    EXPECT_EQ(utils::cpp_to_name("cpp_std$$vector"), "std::vector");
}

TEST(CppToNameTest, RussianPrefix) {
    EXPECT_EQ(utils::cpp_to_name("ru_privet"), "привет");
}

TEST(CppToNameTest, RussianUpperCaseRoundtrip) {
    auto cpp = utils::name_to_cpp("ПРИВЕТ");
    EXPECT_EQ(cpp, "ru_PRIVET");
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(back, "ПРИВЕТ");
}

TEST(CppToNameTest, RoundtripRussian) {
    std::string_view original = "привет";
    auto cpp = utils::name_to_cpp(original);
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(original, back);
}

TEST(CppToNameTest, RoundtripAscii) {
    std::string_view original = "hello";
    auto cpp = utils::name_to_cpp(original);
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(original, back);
}

TEST(CppToNameTest, RoundtripCpp) {
    std::string_view original = "std::vector";
    auto cpp = utils::name_to_cpp(original);
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(original, back);
}

TEST(CppToNameTest, HexPrefix) {
    auto result = utils::cpp_to_name("u8_C3BCC3B6C3A4");
    EXPECT_EQ(result, "üöä");
}

TEST(CppToNameTest, RoundtripNonRussian) {
    std::string_view original = "üöä";
    auto cpp = utils::name_to_cpp(original);
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(original, back);
}

TEST(CppToNameTest, RoundtripMixedRussianLatin) {
    std::string_view original = "test_переменная";
    auto cpp = utils::name_to_cpp(original);
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(original, back);
}

TEST(CppToNameTest, RoundtripColonWithRussianNoSpecial) {
    std::string_view original = "пакет::имя";
    auto cpp = utils::name_to_cpp(original);
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(original, back);
}

TEST(CppToNameTest, RoundtripOnlyUnderscore) {
    std::string_view original = "_";
    auto cpp = utils::name_to_cpp(original);
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(original, back);
}

TEST(CppToNameTest, RoundtripContainsDigits) {
    std::string_view original = "abc123";
    auto cpp = utils::name_to_cpp(original);
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(original, back);
}

TEST(CppToNameTest, RoundtripOnlyColon) {
    std::string_view original = "::";
    auto cpp = utils::name_to_cpp(original);
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(original, back);
}

TEST(CppToNameTest, RoundtripTripleColon) {
    std::string_view original = "a::b::c";
    auto cpp = utils::name_to_cpp(original);
    auto back = utils::cpp_to_name(cpp);
    EXPECT_EQ(original, back);
}

} // namespace
} // namespace trust