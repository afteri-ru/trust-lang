#pragma once
// include/playground/rate_limiter.h
// trust-playground: rate-limit по клиентскому IP. Самостоятельная зона ответственности:
// ведёт карту уникальных IP и счётчик её сбросов при переполнении. Данные полностью
// изолированы от остального состояния балансировщика, поэтому используется собственный
// мьютекс - класс независимо тестируется.

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace trust {
namespace playground {

class RateLimiter {
  public:
    RateLimiter() = default;

    /// Возвращает true, если запрос превысил лимит (perIp за последние 60 секунд) и
    /// должен быть отклонён (HTTP 429). При переполнении карты (maxIps) карта очищается,
    /// а счётчик сбросов увеличивается (видно в /stats).
    bool exceeded(const std::string& ip, size_t maxIps, int perIp);

    /// Число уникальных IP в карте (для /stats).
    size_t trackedCount() const;

    /// Число сбросов карты при переполнении (для /stats).
    uint64_t resets() const;

  private:
    mutable std::mutex mu_;
    std::map<std::string, std::vector<std::chrono::steady_clock::time_point>> ipHits_;
    std::atomic<uint64_t> resets_{0};
};

} // namespace playground
} // namespace trust
