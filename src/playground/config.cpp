// src/playground/config.cpp
// trust-playground: загрузка единого конфига (см. include/playground/config.h).

#include "playground/config.h"

#include <cctype>
#include <cstring>
#include <exception>
#include <fstream>
#include <string>

namespace trust {
namespace playground {

bool isValidToken(const std::string& token) {
    if (token.size() != 64) {
        return false;
    }
    for (const char c : token) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

std::string generateToken() {
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    unsigned char bytes[32] = {0};
    if (!urandom) {
        return std::string();
    }
    urandom.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    if (urandom.gcount() != static_cast<std::streamsize>(sizeof(bytes))) {
        return std::string();
    }
    static constexpr const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (const unsigned char b : bytes) {
        out += kHex[(b >> 4) & 0x0f];
        out += kHex[b & 0x0f];
    }
    return out;
}

std::string workerLabelForToken(const PlaygroundConfig& cfg, const std::string& token) {
    for (const WorkerRegistryEntry& w : cfg.workers) {
        if (w.token == token) {
            return w.label;
        }
    }
    return std::string();
}

bool isLoopbackHost(const std::string& host) {
    if (host == "localhost" || host == "127.0.0.1" || host == "::1") {
        return true;
    }
    return host.rfind("127.", 0) == 0; // весь loopback-диапазон 127/8
}

bool validateWorkerPlaygroundUrl(const std::string& url, std::string& error) {
    const size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        error = "worker playground_url must be an absolute URL with scheme (http:// or https://)";
        return false;
    }
    const std::string scheme = url.substr(0, scheme_end);
    std::string hostport = url.substr(scheme_end + 3);
    const size_t slash = hostport.find('/');
    if (slash != std::string::npos) {
        hostport = hostport.substr(0, slash);
    }
    std::string host = hostport;
    const size_t colon = hostport.rfind(':');
    if (colon != std::string::npos) {
        host = hostport.substr(0, colon);
    }
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2); // IPv6 в скобках, напр. [::1]:8080
    }
    if (scheme != "http" && scheme != "https") {
        error = "worker playground_url: unsupported scheme '" + scheme + "://' (expected http:// or https://)";
        return false;
    }
    if (scheme == "http" && !isLoopbackHost(host)) {
        error = "worker playground_url must use https:// for non-loopback host '" + host +
                "' (plain http transmits the worker auth token and job payloads unencrypted; "
                "http is allowed only for localhost/127.x/::1)";
        return false;
    }
    return true;
}

namespace {

bool parseInt(const std::string& key, const std::string& value, int& out, std::string& error) {
    try {
        size_t pos = 0;
        int parsed = std::stoi(value, &pos, 10);
        if (pos != value.size()) {
            error = "invalid integer for '" + key + "': '" + value + "'";
            return false;
        }
        out = parsed;
        return true;
    } catch (const std::exception&) {
        error = "invalid integer for '" + key + "': '" + value + "'";
        return false;
    }
}

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
        return std::string();
    }
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

bool loadConfig(const std::string& path, PlaygroundConfig& out, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "cannot open config: " + path;
        return false;
    }

    std::string line;
    int line_no = 0;
    while (std::getline(file, line)) {
        ++line_no;
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            error = "line " + std::to_string(line_no) + ": expected 'key=value'";
            return false;
        }
        const std::string key = trim(trimmed.substr(0, eq));
        const std::string value = trim(trimmed.substr(eq + 1));
        if (key.empty() || value.empty()) {
            error = "line " + std::to_string(line_no) + ": empty key or value";
            return false;
        }

        if (key.rfind("playground.", 0) == 0) {
            const std::string k = key.substr(std::strlen("playground."));
            if (k == "listen") {
                out.listen = value;
            } else if (k == "port") {
                if (!parseInt(key, value, out.port, error)) {
                    return false;
                }
            } else if (k == "max_queue") {
                if (!parseInt(key, value, out.maxQueue, error)) {
                    return false;
                }
            } else if (k == "job_timeout") {
                if (!parseInt(key, value, out.jobTimeoutSec, error)) {
                    return false;
                }
            } else if (k == "body_limit_kb") {
                if (!parseInt(key, value, out.bodyLimitKb, error)) {
                    return false;
                }
            } else if (k == "rate_limit_per_ip") {
                if (!parseInt(key, value, out.rateLimitPerIp, error)) {
                    return false;
                }
            } else if (k == "retry") {
                if (!parseInt(key, value, out.retry, error)) {
                    return false;
                }
            } else if (k == "poll_timeout") {
                if (!parseInt(key, value, out.pollTimeoutSec, error)) {
                    return false;
                }
            } else if (k == "stats_token") {
                out.statsToken = value;
            } else if (k == "alert_email") {
                out.alertEmail = value;
            } else if (k == "alert_interval_sec") {
                if (!parseInt(key, value, out.alertIntervalSec, error)) {
                    return false;
                }
            } else if (k == "alert_from") {
                out.alertFrom = value;
            } else if (k == "alert_cmd") {
                out.alertCmd = value;
            } else if (k == "max_archive_kb") {
                if (!parseInt(key, value, out.maxArchiveKb, error)) {
                    return false;
                }
            } else if (k == "max_result_kb") {
                if (!parseInt(key, value, out.maxResultKb, error)) {
                    return false;
                }
            } else if (k == "max_worker_metrics_kb") {
                if (!parseInt(key, value, out.maxWorkerMetricsKb, error)) {
                    return false;
                }
            } else if (k == "max_rate_limit_ips") {
                if (!parseInt(key, value, out.maxRateLimitIps, error)) {
                    return false;
                }
            }
            continue;
        }

        if (key.rfind("worker.", 0) == 0) {
            const std::string k = key.substr(std::strlen("worker."));
            if (k == "playground_url") {
                out.playgroundUrl = value;
            } else if (k == "token") {
                out.token = value;
            } else if (k == "lsp_bin") {
                out.lspBin = value;
            } else if (k == "max_parallel") {
                if (!parseInt(key, value, out.maxParallel, error)) {
                    return false;
                }
            } else if (k == "max_memory_mb") {
                if (!parseInt(key, value, out.maxMemoryMb, error)) {
                    return false;
                }
            } else if (k == "max_output_kb") {
                if (!parseInt(key, value, out.maxOutputKb, error)) {
                    return false;
                }
            } else if (k == "job_timeout") {
                if (!parseInt(key, value, out.workerJobTimeoutSec, error)) {
                    return false;
                }
            } else if (k == "poll_interval_ms") {
                if (!parseInt(key, value, out.pollIntervalMs, error)) {
                    return false;
                }
            } else if (k == "stats_interval_ms") {
                if (!parseInt(key, value, out.statsIntervalMs, error)) {
                    return false;
                }
            } else if (k == "project_dir") {
                out.projectDir = value;
            } else if (k == "log_level") {
                out.logLevel = value;
            } else if (k == "lsp_opts") {
                // Список опций, разделённых пробелами/запятыми — всегда передаются в trust-lsp.
                out.lspOpts.clear();
                std::string cur;
                for (const char c : value) {
                    if (c == ' ' || c == '\t' || c == ',') {
                        if (!cur.empty()) {
                            out.lspOpts.push_back(cur);
                            cur.clear();
                        }
                    } else {
                        cur += c;
                    }
                }
                if (!cur.empty()) {
                    out.lspOpts.push_back(cur);
                }
            }
            continue;
        }

        // Registry entry: label=token (без префикса).
        WorkerRegistryEntry entry;
        entry.label = key;
        entry.token = value;
        if (!isValidToken(entry.token)) {
            error = "line " + std::to_string(line_no) + ": invalid worker token for '" + key + "' (expected 64 hex chars)";
            return false;
        }
        out.workers.push_back(entry);
    }

    return true;
}

bool saveWorkerConfig(const std::string& path, const PlaygroundConfig& cfg, std::string& error) {
    std::vector<std::string> keep;
    {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) {
            const std::string t = trim(line);
            if (t.empty() || t[0] == '#') {
                keep.push_back(line);
                continue;
            }
            const size_t eq = t.find('=');
            const std::string key = (eq == std::string::npos) ? t : trim(t.substr(0, eq));
            if (key.rfind("worker.", 0) != 0) {
                keep.push_back(line); // playground.* и записи реестра (токены) сохраняем
            }
        }
    }

    std::ofstream out(path);
    if (!out) {
        error = "cannot write config: " + path;
        return false;
    }

    auto opt = [&](const std::string& key, const std::string& val, const std::string& desc, const std::string& def) {
        out << "worker." << key << "=" << val << "    # " << desc << " (default: " << def << ")\n";
    };

    out << "# trust-playground (воркер) — создано автоматически.\n";
    out << "# Параметры сгруппированы; для каждой опции указано назначение и значение по умолчанию.\n";

    out << "\n# ── Connection (подключение к балансировщику) ──\n";
    opt("playground_url", cfg.playgroundUrl, "URL балансировщика (https://; http:// только для localhost/127.x/::1)", "https://playground.trust-lang.net");
    opt("token", cfg.token, "токен воркера (обязательно, 64 hex)", "пусто");
    opt("lsp_bin", cfg.lspBin, "путь к исполняемому trust-lsp (обязательно)", "пусто");

    out << "\n# ── Limits / performance (лимиты и производительность) ──\n";
    opt("max_parallel", std::to_string(cfg.maxParallel), "максимум параллельных задач", "4");
    opt("max_memory_mb", std::to_string(cfg.maxMemoryMb), "лимит памяти на одну транспиляцию, МБ", "512");
    opt("max_output_kb", std::to_string(cfg.maxOutputKb), "лимит размера результата, КБ", "2048");
    opt("job_timeout", std::to_string(cfg.workerJobTimeoutSec), "таймаут одной транспиляции, сек", "30");
    opt("poll_interval_ms", std::to_string(cfg.pollIntervalMs), "период поллинга к балансировщику, мс", "200");
    opt("stats_interval_ms", std::to_string(cfg.statsIntervalMs), "период вывода статистики в консоль, мс", "10000");

    out << "\n# ── Safety (безопасность) ──\n";
    {
        std::string joined;
        for (size_t i = 0; i < cfg.lspOpts.size(); ++i) {
            if (i) {
                joined += ",";
            }
            joined += cfg.lspOpts[i];
        }
        opt("lsp_opts", joined, "доп. опции, всегда передаваемые в trust-lsp", "пусто");
    }

    out << "\n# ── Project (рабочее окружение) ──\n";
    opt("project_dir", cfg.projectDir, "рабочий каталог для trust-lsp", "пусто");

    if (!keep.empty()) {
        out << "\n# ── Playground / registry (сохранённые строки) ──\n";
        for (const std::string& l : keep) {
            out << l << "\n";
        }
    }
    return true;
}

} // namespace playground
} // namespace trust
