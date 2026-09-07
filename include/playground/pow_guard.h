#pragma once
// include/playground/pow_guard.h
// trust-playground: PoW-челленджи для /run и /download. Самостоятельная зона: данные
// челленджей изолированы от остального состояния балансировщика, собственный мьютекс.
// Адаптивная сложность вычисляется балансировщиком (зависит от глубины очереди) и
// передаётся в методы параметром - PowGuard не зависит от состояния очереди.

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace trust {
namespace playground {

class PowGuard {
  public:
    PowGuard() = default;

    /// Выдаёт новый nonce для указанной сложности и чистит истёкшие челленджи.
    /// Возвращает hex-nonce (32 символа из 16 случайных байт).
    std::string issue(int difficulty, int ttlSec);

    /// Проверяет заголовок "nonce:solution" на требуемую сложность requiredDifficulty.
    /// true - решение валидно (счётчик использований челленджа увеличивается).
    bool verify(const std::string& header, int requiredDifficulty, int ttlSec, int maxUsesPerNonce);

  private:
    struct Challenge {
        int difficulty = 0;
        std::chrono::steady_clock::time_point created{};
        int uses = 0;
    };
    mutable std::mutex mu_;
    std::unordered_map<std::string, Challenge> challenges_;
};

} // namespace playground
} // namespace trust
