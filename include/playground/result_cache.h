#pragma once
// include/playground/result_cache.h
// trust-playground: LRU-кеш результатов /run (по имени примера X-Example-Name / хешу
// кода). Самостоятельная зона: данные кеша изолированы от остального состояния
// балансировщика, поэтому используется собственный мьютекс - класс независимо тестируется.

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

namespace trust {
namespace playground {

class ResultCache {
  public:
    ResultCache() = default;

    /// Возвращает кешированный результат для key, если он не истёк и код совпадает
    /// (защита от отравления кеша). Пустая строка - промах.
    std::string get(const std::string& key, const std::string& code, int ttlSec);

    /// Сохраняет результат по key. Если кеш отключён (maxEntries<=0 && maxMb<=0) - no-op.
    /// После вставки выполняет вытеснение LRU по TTL/макс. размеру/макс. числу записей.
    void put(const std::string& key, const std::string& code, const std::string& result, int ttlSec, size_t maxEntries, size_t maxMb);

  private:
    struct Entry {
        std::string code;
        std::string result;
        size_t size = 0;
        std::chrono::steady_clock::time_point created{};
        std::chrono::steady_clock::time_point lastAccess{};
    };
    void evictLocked(int ttlSec, size_t maxEntries, size_t maxMb);

    mutable std::mutex mu_;
    std::unordered_map<std::string, Entry> cache_;
};

} // namespace playground
} // namespace trust
