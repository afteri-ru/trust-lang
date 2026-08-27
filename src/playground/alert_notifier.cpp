// src/playground/alert_notifier.cpp
// trust-playground: реализация AlertNotifier (см. include/playground/alert_notifier.h).

#include "playground/alert_notifier.h"

#include "playground/util.h" // utcNowString

#include <cstdio>
#include <ctime>
#include <thread>
#include <utility>

namespace trust {
namespace playground {

namespace {

// Отправляет письмо через alert_cmd (по умолчанию sendmail -t), читающий письмо из stdin.
// Возвращает false при пустом получателе или ошибке команды.
bool sendMail(const std::string& cmd, const std::string& from, const std::string& to, const std::string& subject, const std::string& body) {
    if (to.empty() || cmd.empty()) {
        return false;
    }
    char date[64];
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %z", &tm);
    const std::string msg = "From: " + from +
                            "\r\n"
                            "To: " +
                            to +
                            "\r\n"
                            "Subject: " +
                            subject +
                            "\r\n"
                            "Date: " +
                            date +
                            "\r\n"
                            "Content-Type: text/plain; charset=utf-8\r\n"
                            "\r\n" +
                            body;
    FILE* pipe = ::popen(cmd.c_str(), "w");
    if (pipe == nullptr) {
        return false;
    }
    const size_t n = std::fwrite(msg.data(), 1, msg.size(), pipe);
    const int rc = ::pclose(pipe);
    return n == msg.size() && rc == 0;
}

} // namespace

AlertNotifier::AlertNotifier(std::string cmd, std::string from, std::string to)
: cmd_(std::move(cmd))
, from_(std::move(from))
, to_(std::move(to)) {
}

void AlertNotifier::notify(const std::string& reason, const std::string& statsText, int cooldownSec) {
    if (!enabled()) {
        return;
    }
    // НЕМЕДЛЕННО при первом появлении события; повтор того же события в течение
    // cooldown_sec не шлём (per-reason dedup).
    const int cooldown_sec = cooldownSec > 0 ? cooldownSec : 86400;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto now = std::chrono::steady_clock::now();
        auto it = lastAlertAt_.find(reason);
        if (it != lastAlertAt_.end() && now - it->second < std::chrono::seconds(cooldown_sec)) {
            return;
        }
        lastAlertAt_[reason] = now;
    }
    const std::string body = "trust-playground: " + reason + "\nTime: " + utcNowString() + "\n\n" + statsText;
    dispatch("trust-playground: " + reason, body);
}

void AlertNotifier::onWorkerCountChange(int connected, const std::string& statsText, int cooldownSec) {
    std::string reason;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (connected != lastConnected_) {
            if (lastConnected_ > 0 && connected == 0) {
                reason = "all workers disconnected";
            } else if (lastConnected_ == 0 && connected > 0) {
                reason = "workers reconnected";
            }
            lastConnected_ = connected;
        }
    }
    if (!reason.empty()) {
        notify(reason, statsText, cooldownSec);
    }
}

void AlertNotifier::sendPeriodic(const std::string& statsText) {
    if (!enabled()) {
        return;
    }
    const std::string body = "trust-playground periodic stats\n"
                             "Time: " +
                             utcNowString() + "\n\n" + statsText;
    dispatch("trust-playground periodic stats", body);
}

void AlertNotifier::dispatch(const std::string& subject, const std::string& body) const {
    // Отправка в отдельном потоке - не блокирует обработчик запроса.
    const std::string cmd = cmd_, from = from_, to = to_;
    std::thread([cmd, from, to, subject, body] { sendMail(cmd, from, to, subject, body); }).detach();
}

} // namespace playground
} // namespace trust
