// src/pipeline/pm_client.cpp
#include "pipeline/pm_client.hpp"

#include "diag/diag.hpp"
#include "utils/io.hpp"

#include <nlohmann/json.hpp>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace trust {
namespace {

namespace fs = std::filesystem;

// Reports the failure (trust.cpp's FatalError handler assumes the message has
// already been emitted) and throws.
[[noreturn]] void fail(const std::string& message) {
    trust::errs() << "error: " << message << "\n";
    throw FatalError(message);
}

std::string envValue(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
}

bool manifestPresent(const PipelineOpts& opts, std::string& manifest) {
    if (!opts.pm_manifest.empty()) {
        manifest = opts.pm_manifest;
        return true;
    }
    if (opts.input_file.empty()) {
        return false;
    }
    fs::path dir = fs::path(opts.input_file).parent_path();
    if (dir.empty()) {
        dir = ".";
    }
    const fs::path candidate = dir / "trust.pkg.json";
    std::error_code ec;
    if (fs::is_regular_file(candidate, ec)) {
        manifest = candidate.string();
        return true;
    }
    return false;
}

// Runs the package manager and captures stdout; returns the exit code.
int runCapture(const std::vector<std::string>& argv, std::string& output) {
    int fds[2];
    if (::pipe(fds) != 0) {
        fail("trust: cannot create a pipe for trust-pm");
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        fail("trust: cannot fork to run trust-pm");
    }
    if (pid == 0) {
        ::dup2(fds[1], STDOUT_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        std::vector<char*> cargv;
        cargv.reserve(argv.size() + 1);
        for (const std::string& arg : argv) {
            cargv.push_back(const_cast<char*>(arg.c_str()));
        }
        cargv.push_back(nullptr);
        ::execvp(cargv[0], cargv.data());
        ::_exit(127);
    }
    ::close(fds[1]);
    std::array<char, 4096> buffer{};
    for (;;) {
        const ssize_t got = ::read(fds[0], buffer.data(), buffer.size());
        if (got <= 0) {
            break;
        }
        output.append(buffer.data(), static_cast<std::size_t>(got));
    }
    ::close(fds[0]);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string locateTrustPm(const PipelineOpts& opts) {
    if (!opts.pm_path.empty()) {
        return opts.pm_path;
    }
    const std::string from_env = envValue("TRUST_PM");
    if (!from_env.empty()) {
        return from_env;
    }
    return "trust-pm";
}

} // namespace

std::vector<std::string> resolvePackageModules(const PipelineOpts& opts) {
    if (opts.pm_mode == "off") {
        return {};
    }
    std::string manifest;
    if (!manifestPresent(opts, manifest)) {
        if (opts.pm_mode == "on") {
            fail("trust: --pm-mode=on requires a package manifest (trust.pkg.json)");
        }
        return {};
    }
    const std::string pm = locateTrustPm(opts);
    std::vector<std::string> argv = {pm, "resolve", "--manifest", manifest, "--json"};
    if (opts.pm_frozen) {
        argv.push_back("--frozen");
    }
    if (opts.pm_offline) {
        argv.push_back("--offline");
    }
    for (const std::string& source : opts.pm_sources) {
        argv.push_back("--source");
        argv.push_back(source);
    }
    if (!opts.pm_cache.empty()) {
        argv.push_back("--cache");
        argv.push_back(opts.pm_cache);
    }

    std::string output;
    const int code = runCapture(argv, output);
    if (code == 127) {
        fail("trust: package manager '" + pm + "' not found; install trust-pm, set --pm=<path>/TRUST_PM, or use --pm-mode=off for local-only");
    }
    nlohmann::json envelope;
    try {
        envelope = nlohmann::json::parse(output);
    } catch (const nlohmann::json::parse_error&) {
        fail("trust: cannot parse trust-pm output (protocol mismatch?)");
    }
    if (!envelope.value("ok", false)) {
        std::string details;
        if (envelope.contains("errors") && envelope.at("errors").is_array()) {
            for (const nlohmann::json& error : envelope.at("errors")) {
                details += error.value("message", std::string()) + "; ";
            }
        }
        fail("trust: dependency resolution failed: " + details);
    }
    std::vector<std::string> files;
    if (envelope.contains("data") && envelope.at("data").is_object() && envelope.at("data").contains("packages")) {
        for (const nlohmann::json& item : envelope.at("data").at("packages")) {
            if (item.contains("file")) {
                files.push_back(item.at("file").get<std::string>());
            }
        }
    }
    return files;
}

} // namespace trust
