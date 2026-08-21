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
    // Печатает переход состояния подключения к балансировщику (только при смене, однократно).
    void reportConnection(bool up);

    PlaygroundConfig cfg_;
    std::atomic<bool> stop_{false};
    std::atomic<int> jobsDone_{0};         // успешно завершённые задачи
    std::atomic<int> jobsFailed_{0};       // задачи с ошибкой транспиляции
    std::atomic<int> busySlots_{0};        // слоты, занятые обработкой
    std::atomic<bool> connected_{false};   // последний /poll до балансировщика был успешным
    std::atomic<int> connState_{1};        // 0 = нет связи, 1 = подключаемся, 2 = подключено (для отчёта переходов)
    std::atomic<long long> memUsedMax_{0}; // пик используемой памяти (байты) с момента старта
    std::atomic<double> loadMax_{0.0};     // пик нагрузки CPU (load1) с момента старта
    std::chrono::steady_clock::time_point startTime_{};
};

// Заменяет вхождения токена воркера (64 hex) в тексте на "<redacted>", чтобы
// конфиденциальный auth-токен не попал в публичный ответ песочницы (например, если
// он ошибочно оказался в stderr trust-lsp, который воркер возвращает как error/log).
std::string redactToken(const std::string& text, const std::string& token);

// Проверяет настройки воркера на корректность при запуске: рабочий каталог
// (worker.project_dir) существует и является каталогом, исполняемый файл trust-lsp
// (worker.lsp_bin) существует и исполняемый, worker.lsp_opts - корректные опции
// (каждый НЕПУСТОЙ элемент начинается с '-'; ловит случайный токен/позиционный
// аргумент, который иначе дал бы trust-lsp "unknown option '<токен>'"; пустой список
// допустим - lsp_opts могут не быть настроены), здравые числовые лимиты. Возвращает
// пустую строку при успехе или описание первой найденной проблемы.
std::string validateWorkerSettings(const PlaygroundConfig& cfg);

} // namespace playground
} // namespace trust
