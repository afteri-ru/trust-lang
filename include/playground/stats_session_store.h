#pragma once
// include/playground/stats_session_store.h
// trust-playground: админ-сессии для /stats (cookie tpg_stats). Самостоятельная зона:
// данные сессий изолированы от остального состояния балансировщика, собственный мьютекс.

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace trust {
namespace playground {

class StatsSessionStore {
  public:
    StatsSessionStore() = default;

    /// Проверяет сессию: существует и не истекла (sliding TTL по lastAccess + жёсткий
    /// maxSec от created). При истечении удаляет и возвращает false. При успехе обновляет
    /// lastAccess (sliding refresh).
    bool ok(const std::string& sessionId, int ttlSec, int maxSec);

    /// Создаёт новую сессию и возвращает её id (32 hex-символа из 16 случайных байт).
    std::string create();

    /// Удаляет сессию (если id не пуст).
    void destroy(const std::string& sessionId);

  private:
    struct Session {
        std::chrono::steady_clock::time_point created{};
        std::chrono::steady_clock::time_point lastAccess{};
    };
    mutable std::mutex mu_;
    std::unordered_map<std::string, Session> sessions_;
};

} // namespace playground
} // namespace trust
