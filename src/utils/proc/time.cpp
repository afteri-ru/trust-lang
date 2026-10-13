// src/utils/proc/time.cpp
#include "utils/utils.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace trust::utils {

namespace {
std::string formatNow(const char* fmt) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&time_t_now, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, fmt);
    return oss.str();
}
} // namespace

std::string currentTimestamp() {
    return formatNow("%Y-%m-%d %H:%M:%S %z");
}

std::string currentDate() {
    return formatNow("%Y-%m-%d");
}

} // namespace trust::utils
