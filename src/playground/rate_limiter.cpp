// src/playground/rate_limiter.cpp
// trust-playground: реализация RateLimiter (см. include/playground/rate_limiter.h).

#include "playground/rate_limiter.h"

#include <algorithm>

namespace trust {
namespace playground {

bool RateLimiter::exceeded(const std::string& ip, size_t maxIps, int perIp) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mu_);
    // Ограничиваем рост карты уникальных IP (защита от переполнения при флуде).
    if (ipHits_.size() >= maxIps && ipHits_.find(ip) == ipHits_.end()) {
        ipHits_.clear();
        resets_.fetch_add(1);
    }
    std::vector<std::chrono::steady_clock::time_point>& hits = ipHits_[ip];
    const auto cutoff = now - std::chrono::seconds(60);
    hits.erase(std::remove_if(hits.begin(), hits.end(), [&](const std::chrono::steady_clock::time_point& t) { return t < cutoff; }), hits.end());
    if (static_cast<int>(hits.size()) >= perIp) {
        return true;
    }
    hits.push_back(now);
    return false;
}

size_t RateLimiter::trackedCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return ipHits_.size();
}

uint64_t RateLimiter::resets() const {
    return resets_.load();
}

} // namespace playground
} // namespace trust
