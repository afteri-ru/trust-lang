// src/playground/util.cpp
// trust-playground: реализация общих утилит (см. include/playground/util.h).

#include "playground/util.h"

#include <cstdio>
#include <ctime>

namespace trust {
namespace playground {

std::string randomBytes(size_t n) {
    std::string out(n, '\0');
    FILE* f = std::fopen("/dev/urandom", "rb");
    if (f != nullptr) {
        if (std::fread(&out[0], 1, n, f) != n) {
            out.assign(n, '\0');
        }
        std::fclose(f);
    }
    return out;
}

std::string utcNowString() {
    char buf[40];
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

} // namespace playground
} // namespace trust
