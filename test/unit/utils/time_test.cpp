#include "utils/utils.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>
#include <optional>
#include <string>

namespace {

/// Локальная зона влияет на strftime; тест фиксирует UTC и восстанавливает окружение.
class ScopedUtc {
  public:
    ScopedUtc() {
        if (const char* previous = std::getenv("TZ")) {
            m_previous = previous;
        }
        setenv("TZ", "UTC", 1);
        tzset();
    }

    ~ScopedUtc() {
        if (m_previous.has_value()) {
            setenv("TZ", m_previous->c_str(), 1);
        } else {
            unsetenv("TZ");
        }
        tzset();
    }

    ScopedUtc(const ScopedUtc&) = delete;
    ScopedUtc& operator=(const ScopedUtc&) = delete;

  private:
    std::optional<std::string> m_previous;
};

} // namespace

namespace trust::utils {

TEST(TimeTest, FormatTimestampReturnsCompleteResult) {
    ScopedUtc utc;
    EXPECT_EQ(formatTimestamp(0, "%Y-%m-%d"), "1970-01-01");
    EXPECT_EQ(formatTimestamp(0, "%T"), "00:00:00");
    EXPECT_EQ(formatTimestamp(0, "%FT%TZ"), "1970-01-01T00:00:00Z");
    EXPECT_EQ(formatTimestamp(0, "%b %e %Y"), "Jan  1 1970");
    EXPECT_EQ(formatTimestamp(0, "%a %b %e %T %Y"), "Thu Jan  1 00:00:00 1970");
}

TEST(TimeTest, CurrentDateIsIsoDate) {
    ScopedUtc utc;
    const std::string date = currentDate();
    ASSERT_EQ(date.size(), 10);
    EXPECT_EQ(date[4], '-');
    EXPECT_EQ(date[7], '-');
}

} // namespace trust::utils