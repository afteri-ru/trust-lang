// src/playground/pow_guard.cpp
// trust-playground: реализация PowGuard (см. include/playground/pow_guard.h).

#include "playground/pow_guard.h"

#include "playground/http.h" // sha256Hex
#include "playground/util.h" // randomBytes

namespace trust {
namespace playground {

std::string PowGuard::issue(int difficulty, int ttlSec) {
    static constexpr const char* kHex = "0123456789abcdef";
    const std::string bytes = randomBytes(16);
    std::string nonce;
    nonce.reserve(32);
    for (const unsigned char c : bytes) {
        nonce += kHex[c >> 4];
        nonce += kHex[c & 0x0f];
    }
    Challenge ch;
    ch.difficulty = difficulty;
    ch.created = std::chrono::steady_clock::now();
    ch.uses = 0;
    std::lock_guard<std::mutex> lock(mu_);
    challenges_[nonce] = ch;
    const int ttl = ttlSec > 0 ? ttlSec : 60;
    const auto now = std::chrono::steady_clock::now();
    for (auto it = challenges_.begin(); it != challenges_.end();) {
        if (now - it->second.created >= std::chrono::seconds(ttl)) {
            it = challenges_.erase(it);
        } else {
            ++it;
        }
    }
    return nonce;
}

bool PowGuard::verify(const std::string& header, int requiredDifficulty, int ttlSec, int maxUsesPerNonce) {
    if (requiredDifficulty <= 0) {
        return true;
    }
    if (header.empty()) {
        return false;
    }
    const size_t colon = header.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    const std::string nonce = header.substr(0, colon);
    const std::string solution = header.substr(colon + 1);
    const int ttl = ttlSec > 0 ? ttlSec : 60;
    std::lock_guard<std::mutex> lock(mu_);
    auto it = challenges_.find(nonce);
    if (it == challenges_.end()) {
        return false;
    }
    if (std::chrono::steady_clock::now() - it->second.created >= std::chrono::seconds(ttl)) {
        challenges_.erase(it);
        return false;
    }
    if (it->second.difficulty < requiredDifficulty) {
        challenges_.erase(it);
        return false;
    }
    const int maxUses = maxUsesPerNonce > 0 ? maxUsesPerNonce : 1;
    if (it->second.uses >= maxUses) {
        challenges_.erase(it);
        return false;
    }
    // Решение валидно, если sha256(nonce + solution) начинается с requiredDifficulty
    // нулевых бит. Проверяем по старшим нибблам hex-строки.
    const std::string hash = sha256Hex(nonce + solution);
    static const int kLz[16] = {4, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    int zeros = 0;
    for (const char c : hash) {
        const int v = (c >= '0' && c <= '9') ? (c - '0') : (c - 'a' + 10);
        const int n = kLz[v & 0x0f];
        if (zeros + n >= requiredDifficulty) {
            it->second.uses++;
            return true;
        }
        if (n == 0) {
            return false;
        }
        zeros += n;
    }
    return zeros >= requiredDifficulty;
}

} // namespace playground
} // namespace trust
