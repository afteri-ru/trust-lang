#include "utils/utils.hpp"

#include <ctime>
#include <string>

namespace trust::utils {

std::string formatTimestamp(std::time_t timestamp, std::string_view format) {
    std::tm local_time{};
    localtime_r(&timestamp, &local_time);
    const std::string fmt(format);
    std::size_t capacity = 64;
    while (capacity <= 4096) {
        std::string buffer(capacity, '\0');
        const std::size_t written = std::strftime(buffer.data(), buffer.size(), fmt.c_str(), &local_time);
        if (written != 0) {
            buffer.resize(written);
            return buffer;
        }
        capacity *= 2;
    }
    FAULT("formatTimestamp: format '{}' does not fit a 4096-byte buffer", format);
}

std::string currentDate() {
    return formatTimestamp(std::time(nullptr), "%Y-%m-%d");
}

} // namespace trust::utils
