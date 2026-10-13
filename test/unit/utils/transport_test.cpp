#include "utils/utils.hpp"


#include <gtest/gtest.h>

#include <string>

#include <unistd.h>

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
    // Знак '-' не является цифрой - трактуется как невалидная длина.
    EXPECT_EQ(parseContentLength("Content-Length: -5"), 0);
}

TEST(ContentLengthPacketTest, ReadsHeaderAndBody) {
    int fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(fds), 0);
    const std::string payload = "hello";
    ASSERT_TRUE(utils::writeAllToFd(fds[1], "Content-Length: 5\r\n\r\n" + payload));
    ::close(fds[1]);
    EXPECT_EQ(readContentLengthPacket(fds[0]), payload);
    ::close(fds[0]);
}

TEST(ContentLengthPacketTest, RejectsMissingLength) {
    int fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(fds), 0);
    ASSERT_TRUE(utils::writeAllToFd(fds[1], "\r\nbody"));
    ::close(fds[1]);
    EXPECT_TRUE(readContentLengthPacket(fds[0]).empty());
    ::close(fds[0]);
}

} // namespace transport
} // namespace trust
