#pragma once

#include <cerrno>
#include <cstddef>
#include <string_view>

#include <sys/types.h>
#include <unistd.h>

namespace trust::utils {

/// Записывает весь буфер в fd, повторяя write при EINTR/частичной записи.
/// false — ошибка записи (errno сохраняется).
inline bool writeAllToFd(int fd, std::string_view data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t written = ::write(fd, data.data() + offset, data.size() - offset);
        if (written <= 0) {
            if (written < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

} // namespace trust::utils
