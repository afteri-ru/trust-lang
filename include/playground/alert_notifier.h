#pragma once
// include/playground/alert_notifier.h
// trust-playground: почтовые алерты (появление/исчезновение воркеров, переполнение
// очереди и т.п.) и периодическая сводка статистики. Самостоятельная зона: использует
// собственный мьютекс, чтобы отправка не блокировала обработчики запросов, которые
// держат mu_ балансировщика (см. комментарий в server.cpp про alertMutex_).

#include <chrono>
#include <map>
#include <mutex>
#include <string>

namespace trust {
namespace playground {

class AlertNotifier {
  public:
    AlertNotifier(std::string cmd, std::string from, std::string to);

    /// true - настроен получатель (alert_email), алерты активны.
    bool enabled() const { return !to_.empty(); }

    /// Отправляет алерт по событию. Per-reason dedup: повтор того же события в течение
    /// cooldownSec не шлём (чтобы «нет воркеров» не спамило).
    void notify(const std::string& reason, const std::string& statsText, int cooldownSec);

    /// Фиксирует переход числа подключённых воркеров; шлёт алерт при переходе в 0 / из 0.
    /// Значение отслеживается ВСЕГДА (независимо от enabled()), как в исходной логике.
    void onWorkerCountChange(int connected, const std::string& statsText, int cooldownSec);

    /// Периодическая сводка статистики (alertLoop).
    void sendPeriodic(const std::string& statsText);

  private:
    void dispatch(const std::string& subject, const std::string& body) const;

    std::string cmd_;
    std::string from_;
    std::string to_;
    mutable std::mutex mu_;
    std::map<std::string, std::chrono::steady_clock::time_point> lastAlertAt_;
    int lastConnected_{-1}; // -1 = неизвестно (старт): при первом поллинге алерт не шлём
};

} // namespace playground
} // namespace trust
