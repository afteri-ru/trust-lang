// Test file: public runtime I/O helper (trust/io.hpp).
//   - trust::trust__print__ - форматирование в стиле std::format и вывод в trust::outs()
//     (бэкенд DSL-макроса `print`).
//   - Rational форматируется как символьная строка "num\den" через std::format / print.

#include "trust/io.hpp"
#include "stdlib/core/builtin/rational.hpp"
#include "utils/utils.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

namespace {

// Перенаправляет trust::outs() в заданный поток, возвращает предыдущий.
std::ostream* redirectOuts(std::ostream& target) {
    return trust::setOuts(&target);
}

TEST(PrintTest, FormatsAndWritesToOuts) {
    std::ostringstream oss;
    std::ostream* prev = redirectOuts(oss);
    trust::trust__print__("val={} str={}", 42, "x");
    trust::setOuts(prev);
    EXPECT_EQ(oss.str(), "val=42 str=x");
}

TEST(PrintTest, NoArgs) {
    std::ostringstream oss;
    std::ostream* prev = redirectOuts(oss);
    trust::trust__print__("hello\n");
    trust::setOuts(prev);
    EXPECT_EQ(oss.str(), "hello\n");
}

TEST(PrintTest, MultiplePrintsAccumulate) {
    std::ostringstream oss;
    std::ostream* prev = redirectOuts(oss);
    trust::trust__print__("{}", 1);
    trust::trust__print__(" {}", 2);
    trust::setOuts(prev);
    EXPECT_EQ(oss.str(), "1 2");
}

// Rational выводится как символьная строка "num\den" (через GetAsString).
TEST(PrintTest, RationalFormatsAsSymbolicString) {
    EXPECT_EQ(std::format("{}", trust::Rational("3", "4")), "3\\4");
    EXPECT_EQ(std::format("{}", trust::Rational("1", "6")), "1\\6");
    EXPECT_EQ(std::format("{}", trust::Rational(42)), "42\\1");

    std::ostringstream oss;
    std::ostream* prev = redirectOuts(oss);
    trust::trust__print__("r={}", trust::Rational("3", "4"));
    trust::setOuts(prev);
    EXPECT_EQ(oss.str(), "r=3\\4");
}

// Fill/align/width работают как для строки (padding), без обрезания.
TEST(PrintTest, RationalFormatsWithPadding) {
    EXPECT_EQ(std::format("{:>8}", trust::Rational("3", "4")), "     3\\4");
    EXPECT_EQ(std::format("{:<8}", trust::Rational("3", "4")), "3\\4     ");
    EXPECT_EQ(std::format("{:*^8}", trust::Rational("3", "4")), "**3\\4***");
    EXPECT_EQ(std::format("{:.>8}", trust::Rational("3", "4")), ".....3\\4");
}

// Точность для рационального не имеет числового смысла (символьная строка
// "num\den") - она отклоняется, а не молча обрезает. Для литеральной форматной
// строки это ошибка компиляции; для runtime-строки (std::vformat) - format_error.
TEST(PrintTest, RationalRejectsPrecision) {
    trust::Rational r("3", "4");
    EXPECT_THROW(std::vformat("{:.2}", std::make_format_args(r)), std::format_error);
    // Числовые типы (округление/шестнадцатеричный и т.п.) также отклоняются.
    EXPECT_THROW(std::vformat("{:f}", std::make_format_args(r)), std::format_error);
    EXPECT_THROW(std::vformat("{:g}", std::make_format_args(r)), std::format_error);
    EXPECT_THROW(std::vformat("{:e}", std::make_format_args(r)), std::format_error);
}

// Overflow-guard по ширине: libstdc++ хранит width в unsigned short (≤ 65535),
// поэтому чрезмерная ширина отклоняется (format_error), а не приводит к
// гигантской аллокации. Покрывается и литеральной строкой (ошибка компиляции),
// и runtime-строкой (vformat).
TEST(PrintTest, RationalRejectsOverflowWidth) {
    trust::Rational r("3", "4");
    EXPECT_THROW(std::vformat("{:100000}", std::make_format_args(r)), std::format_error);
    EXPECT_THROW(std::vformat("{:99999999999999999999}", std::make_format_args(r)), std::format_error);
}

} // namespace
