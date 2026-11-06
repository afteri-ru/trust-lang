// Валидация записи кеша `--run` (`.debug_trust_hash`) по md5. Формат — `utils/build_record.hpp`.
#include "toolchain/cache.hpp"

#include "trust/version.h"
#include "utils/build_record.hpp"
#include "utils/hash.hpp"
#include "utils/utils.hpp"

#include <charconv>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace trust {
namespace toolchain {

namespace {

/// Совпадает ли md5 файла с ожидаемым: нечитаемый/отсутствующий файл — кеш невалиден (пересборка).
bool fileHashMatches(const std::filesystem::path& p, std::string_view expected) {
    const auto hash = utils::md5HexFile(p);
    return hash.has_value() && *hash == expected;
}

} // namespace

bool runCacheValid(const std::filesystem::path& executable, const std::filesystem::path& expectedMain, std::string_view optionsRecord) {
    auto sec = trust::utils::readElfSection(executable.string(), std::string(utils::kRunCacheSection));
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
    const std::string expected_main = utils::cachePathOf(expectedMain);
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
            if (!fileHashMatches(std::filesystem::path(line.substr(5, path_end - 5)), line.substr(path_end + 1))) {
                return false;
            }
            ++depsChecked;
            continue;
        }
        if (!versionSeen) {
            versionSeen = true;
            if (line != std::string(utils::kRunCacheVersionPrefix) + TRUST_VERSION) {
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
        if (!fileHashMatches(std::filesystem::path(line.substr(0, tab)), line.substr(tab + 1))) {
            return false;
        }
        any = true;
    }
    return any && versionSeen && optsSeen && depsSeen && depsChecked == depsExpected;
}

} // namespace toolchain
} // namespace trust

