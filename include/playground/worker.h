#pragma once
// include/playground/worker.h
// trust-playground: исполнительный VPS (режим по умолчанию). Подключается к
// балансировщику (reverse long-poll), забирает задачи, выполняет транспиляцию
// через субпроцесс trust-lsp --json с лимитами ресурсов и возвращает результат.

#include "playground/config.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <string>

namespace trust {
namespace playground {

class PlaygroundWorker {
  public:
    explicit PlaygroundWorker(const PlaygroundConfig& cfg);

    // Блокирующий цикл: запускает max_parallel слот-потоков + поток статистики
    // и ждёт их завершения.
    int run();

    void requestStop();

  private:
    void slotLoop(int slotIndex);
    void statsLoop();
    // Собирает метрики системы + воркера (отправляются балансировщику в /poll).
    nlohmann::json collectMetrics();

    PlaygroundConfig cfg_;
    std::atomic<bool> stop_{false};
    std::atomic<int> jobsDone_{0};         // успешно завершённые задачи
    std::atomic<int> jobsFailed_{0};       // задачи с ошибкой транспиляции
    std::atomic<int> busySlots_{0};        // слоты, занятые обработкой
    std::atomic<bool> connected_{false};   // последний /poll до балансировщика был успешным
    std::atomic<long long> memUsedMax_{0}; // пик используемой памяти (байты) с момента старта
    std::atomic<double> loadMax_{0.0};     // пик нагрузки CPU (load1) с момента старта
    std::chrono::steady_clock::time_point startTime_{};
};

} // namespace playground
} // namespace trust
