#include "utils/transport.hpp"

#include <gtest/gtest.h>

namespace trust {
namespace transport {

TEST(ParseContentLengthTest, ValidValue) {
    EXPECT_EQ(parseContentLength("Content-Length: 42"), 42);
}

TEST(ParseContentLengthTest, LeadingWhitespace) {
    EXPECT_EQ(parseContentLength("Content-Length: \t 123"), 123);
}

TEST(ParseContentLengthTest, TrailingCr) {
    EXPECT_EQ(parseContentLength("Content-Length: 100\r"), 100);
}

TEST(ParseContentLengthTest, NoColon) {
    EXPECT_EQ(parseContentLength("Content-Length 42"), 0);
}

TEST(ParseContentLengthTest, EmptyValue) {
    EXPECT_EQ(parseContentLength("Content-Length:"), 0);
}

TEST(ParseContentLengthTest, NonNumeric) {
    EXPECT_EQ(parseContentLength("Content-Length: 12a4"), 0);
    EXPECT_EQ(parseContentLength("Content-Length: abc"), 0);
}

TEST(ParseContentLengthTest, NegativeNotSupported) {
    // Знак '-' не является цифрой — трактуется как невалидная длина.
    EXPECT_EQ(parseContentLength("Content-Length: -5"), 0);
}

} // namespace transport
} // namespace trust
