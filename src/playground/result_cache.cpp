// src/playground/result_cache.cpp
// trust-playground: реализация ResultCache (см. include/playground/result_cache.h).

#include "playground/result_cache.h"

namespace trust {
namespace playground {

std::string ResultCache::get(const std::string& key, const std::string& code, int ttlSec) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mu_);
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return std::string();
    }
    if (ttlSec > 0 && now - it->second.created >= std::chrono::seconds(ttlSec)) {
        cache_.erase(it);
        return std::string();
    }
    // Защита от отравления кеша: ключ (имя примера) не совпал с кодом - считаем промахом.
    if (it->second.code != code) {
        return std::string();
    }
    it->second.lastAccess = now;
    return it->second.result;
}

void ResultCache::put(const std::string& key, const std::string& code, const std::string& result, int ttlSec, size_t maxEntries, size_t maxMb) {
    if (key.empty() || result.empty()) {
        return;
    }
    if (maxEntries <= 0 && maxMb <= 0) {
        return; // кеш отключён
    }
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mu_);
    auto& e = cache_[key];
    e.code = code;
    e.result = result;
    e.size = result.size();
    e.created = now;
    e.lastAccess = now;
    evictLocked(ttlSec, maxEntries, maxMb);
}

void ResultCache::evictLocked(int ttlSec, size_t maxEntries, size_t maxMb) {
    const auto now = std::chrono::steady_clock::now();
    const size_t maxBytes = maxMb * 1024 * 1024;
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (ttlSec > 0 && now - it->second.created >= std::chrono::seconds(ttlSec)) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
    const auto totalBytes = [&]() {
        size_t s = 0;
        for (const auto& kv : cache_) {
            s += kv.second.size;
        }
        return s;
    };
    while ((maxEntries > 0 && cache_.size() > maxEntries) || (maxBytes > 0 && totalBytes() > maxBytes)) {
        auto lru = cache_.begin();
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->second.lastAccess < lru->second.lastAccess) {
                lru = it;
            }
        }
        if (lru == cache_.end()) {
            break;
        }
        cache_.erase(lru);
    }
}

} // namespace playground
} // namespace trust
