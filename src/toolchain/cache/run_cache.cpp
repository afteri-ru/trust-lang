// src/toolchain/cache/run_cache.cpp
// Запись кеша `--run` (`.debug_trust_hash`): композиция строки и валидация по md5.
#include "toolchain/cache.hpp"

#include "toolchain/hash.hpp"
#include "trust/version.h"
#include "utils/utils.hpp"
#include "utils/error.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>

namespace trust {
namespace toolchain {

namespace {

/// Путь относительно CWD (запись кеша переносима между каталогами).
std::string cwdRelative(const std::filesystem::path& p) {
    return std::filesystem::relative(p, std::filesystem::current_path()).generic_string();
}

} // namespace

std::string codegenArgsRecord(const PipelineOpts& opts) {
    std::vector<std::string> args = opts.codegen_args;
    std::sort(args.begin(), args.end());
    std::string out;
    for (const auto& a : args) {
        if (!out.empty()) {
            out += ' ';
        }
        for (const char c : a) {
            out += (c == '\n' || c == '\t') ? ' ' : c;
        }
    }
    return out;
}

std::string composeRunCacheRecord(std::string_view version, std::string_view optionsRecord, const std::vector<CacheDependency>& dependencies,
                                  const std::vector<CacheDependency>& files) {
    std::string record;
    record += kRunCacheVersionPrefix;
    record += version;
    record += '\n';
    // Список фактически заданных codegen-опций: смена любой из них делает кеш невалидным.
    record += "@opts\t";
    record += optionsRecord;
    record += '\n';

    // Замыкание зависимостей, НЕ покрытых хешами модулей: пакеты ресурсов и явный DSL-файл.
    // Хеши `.src` перекрывают исходники модулей, но НЕ C++-заголовки пакетов.
    record += "@deps\t" + std::to_string(dependencies.size()) + '\n';
    for (const CacheDependency& dep : dependencies) {
        record += "@dep\t" + dep.path + '\t' + dep.md5 + '\n';
    }

    for (const CacheDependency& file : files) {
        record += file.path + '\t' + file.md5 + '\n';
    }
    return record;
}

bool runCacheValid(const std::filesystem::path& executable, const std::filesystem::path& expectedMain, std::string_view optionsRecord) {
    auto sec = trust::utils::readElfSection(executable.string(), std::string(kRunCacheSection));
    if (!sec) {
        return false;
    }
    std::string record(reinterpret_cast<const char*>(sec->data()), sec->size());
    while (!record.empty() && record.back() == '\0') {
        record.pop_back();
    }
    std::istringstream iss(record);
    std::string line;
    bool any = false;
    bool versionSeen = false;
    bool optsSeen = false;
    bool depsSeen = false;
    std::size_t depsExpected = 0;
    std::size_t depsChecked = 0;
    bool isMain = true;
    const std::string expected_main = cwdRelative(expectedMain);
    while (std::getline(iss, line)) {
        if (line.empty()) {
            continue;
        }
        const auto tab = line.find('\t');
        if (tab == std::string::npos) {
            return false;
        }
        if (line.rfind("@opts\t", 0) == 0) {
            if (line.substr(6) != optionsRecord) {
                return false;
            }
            optsSeen = true;
            continue;
        }
        if (line.rfind("@deps\t", 0) == 0) {
            const std::string count = line.substr(6);
            std::size_t expected = 0;
            const auto parsed = std::from_chars(count.data(), count.data() + count.size(), expected);
            if (count.empty() || parsed.ec != std::errc{} || parsed.ptr != count.data() + count.size()) {
                return false;
            }
            depsExpected = expected;
            depsSeen = true;
            continue;
        }
        if (line.rfind("@dep\t", 0) == 0) {
            const auto path_end = line.find('\t', 5);
            if (path_end == std::string::npos) {
                return false;
            }
            if (fileHash(std::filesystem::path(line.substr(5, path_end - 5))) != line.substr(path_end + 1)) {
                return false;
            }
            ++depsChecked;
            continue;
        }
        if (!versionSeen) {
            versionSeen = true;
            if (line != std::string(kRunCacheVersionPrefix) + TRUST_VERSION) {
                return false;
            }
            continue;
        }
        if (isMain) {
            isMain = false;
            if (line.substr(0, tab) != expected_main) {
                return false;
            }
        }
        if (fileHash(std::filesystem::path(line.substr(0, tab))) != line.substr(tab + 1)) {
            return false;
        }
        any = true;
    }
    return any && versionSeen && optsSeen && depsSeen && depsChecked == depsExpected;
}

} // namespace toolchain
} // namespace trust
