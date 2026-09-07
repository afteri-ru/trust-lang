// src/playground/stats_session_store.cpp
// trust-playground: реализация StatsSessionStore (см. include/playground/stats_session_store.h).

#include "playground/stats_session_store.h"

#include "playground/util.h"

namespace trust {
namespace playground {

bool StatsSessionStore::ok(const std::string& sessionId, int ttlSec, int maxSec) {
    if (sessionId.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mu_);
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        return false;
    }
    const auto now = std::chrono::steady_clock::now();
    const int ttl = ttlSec > 0 ? ttlSec : 600;
    if (now - it->second.lastAccess >= std::chrono::seconds(ttl) || (maxSec > 0 && now - it->second.created >= std::chrono::seconds(maxSec))) {
        sessions_.erase(it);
        return false;
    }
    it->second.lastAccess = now; // sliding refresh
    return true;
}

std::string StatsSessionStore::create() {
    static constexpr const char* kHex = "0123456789abcdef";
    const std::string bytes = randomBytes(16);
    std::string id;
    id.reserve(32);
    for (const unsigned char c : bytes) {
        id += kHex[c >> 4];
        id += kHex[c & 0x0f];
    }
    Session s;
    s.created = std::chrono::steady_clock::now();
    s.lastAccess = s.created;
    std::lock_guard<std::mutex> lock(mu_);
    sessions_[id] = s;
    return id;
}

void StatsSessionStore::destroy(const std::string& sessionId) {
    if (sessionId.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mu_);
    sessions_.erase(sessionId);
}

} // namespace playground
} // namespace trust
