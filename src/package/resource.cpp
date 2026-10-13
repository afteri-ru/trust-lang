// src/package/resource.cpp
// Доступ к пакетам ресурсов toolchain (<share/trust>/<name>.trust) и чтение записей архива
// без распаковки на диск.
#include "package/archive/archive_io.hpp"
#include "package/manifest.hpp"
#include "package/package.hpp"

#include "utils/hash.hpp"
#include "utils/utils.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

namespace fs = std::filesystem;

std::optional<fs::path> resourceDirectory() {
    // Корень ресурсов: env TRUST_RESOURCE_DIR либо <...>/share/trust относительно exe
    // (с подъёмом вверх: dev `_build/trust` -> `_build/share/trust`, install
    // `<prefix>/bin/trust` -> `<prefix>/share/trust`).
    std::error_code ec;
    if (const char* env = std::getenv("TRUST_RESOURCE_DIR"); env != nullptr && *env != '\0') {
        fs::path p(env);
        if (!fs::is_directory(p, ec)) {
            return std::nullopt;
        }
        return p;
    }
    const fs::path exe = utils::executablePath();
    if (exe.empty()) {
        return std::nullopt;
    }
    fs::path dir = exe.parent_path();
    for (int up = 0; up < 4; ++up) {
        std::error_code probe;
        fs::path cand = dir / "share" / "trust";
        if (fs::is_directory(cand, probe)) {
            return cand;
        }
        fs::path parent = dir.parent_path();
        if (parent == dir || parent.empty()) {
            break;
        }
        dir = std::move(parent);
    }
    return std::nullopt;
}

std::optional<fs::path> locateResourcePackage(std::string_view name) {
    if (!isValidPackageName(name)) {
        return std::nullopt;
    }

    std::error_code ec;
    const std::optional<fs::path> root = resourceDirectory();
    if (!root) {
        return std::nullopt;
    }

    fs::path archive = *root / (std::string(name) + ".trust");
    ec.clear();
    if (!fs::is_regular_file(archive, ec) || ec) {
        return std::nullopt;
    }
    return archive;
}

// -- Кэш распакованного stdlib.trust --
// Одно верифицированное извлечение архива обслуживает и сборку build-каталогов, и
// prelude-чтения. Ключ — MD5 архива; LRU 30 суток; пользовательские пакеты не кэшируются.
// Запись создаётся только успешной extractAndVerifyPackage с атомарным rename.
namespace {

constexpr auto kStdlibCachePruneAfter = std::chrono::days(30);

// Корень кэша stdlib; пустой путь — кэш недоступен (нет XDG/HOME).
fs::path stdlibCacheRoot() {
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && xdg[0] == '/') {
        return fs::path(xdg) / "trust-lang" / "stdlib";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] == '/') {
        return fs::path(home) / ".cache" / "trust-lang" / "stdlib";
    }
    return {};
}

// Запись в силе, если это каталог с манифестом: атомарный rename при создании исключает
// «полуготовые» записи, отсутствие trust.toml — обрывок/мусор.
bool isStdlibCacheEntryValid(const fs::path& entry) {
    std::error_code ec;
    return fs::is_directory(entry, ec) && fs::is_regular_file(entry / "trust.toml", ec);
}

// Touch записи + удаление чужих записей старше 30 суток. Гигиена кэша не влияет на
// корректность сборки, поэтому ошибки — best-effort (молчаливое продолжение уместно).
void touchAndPruneStdlibCache(const fs::path& entry) {
    std::error_code ec;
    fs::last_write_time(entry, fs::file_time_type::clock::now(), ec);
    std::error_code it_ec;
    for (fs::directory_iterator it(entry.parent_path(), it_ec); !it_ec && it != fs::directory_iterator(); it.increment(it_ec)) {
        if (it->path() == entry) {
            continue;
        }
        std::error_code lm_ec;
        const auto last_used = fs::last_write_time(it->path(), lm_ec);
        if (!lm_ec && fs::file_time_type::clock::now() - last_used > kStdlibCachePruneAfter) {
            fs::remove_all(it->path(), lm_ec);
        }
    }
}

// Существующая валидная запись кэша (touch + prune при обращении).
std::optional<fs::path> stdlibCacheEntry() {
    const fs::path root = stdlibCacheRoot();
    if (root.empty()) {
        return std::nullopt;
    }
    const std::optional<fs::path> archive = locateResourcePackage("stdlib");
    if (!archive) {
        return std::nullopt;
    }
    const std::optional<std::string> key = utils::md5HexFile(*archive);
    if (!key) {
        return std::nullopt;
    }
    const fs::path entry = root / *key;
    if (!isStdlibCacheEntryValid(entry)) {
        return std::nullopt;
    }
    touchAndPruneStdlibCache(entry);
    return entry;
}

} // namespace

// Гарантирует наличие валидной записи (строит при промахе). nullopt без out_error —
// кэш недоступен (нет XDG/HOME/нет записи после гонки); nullopt с out_error — ошибка
// распаковки архива. Вызывающие продолжают прямой распаковкой, если кэш недоступен.
std::optional<fs::path> ensureStdlibCacheEntry(std::string& out_error) {
    const fs::path root = stdlibCacheRoot();
    if (root.empty()) {
        return std::nullopt;
    }
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        return std::nullopt;
    }
    const std::optional<fs::path> archive = locateResourcePackage("stdlib");
    if (!archive) {
        return std::nullopt;
    }
    const std::optional<std::string> key = utils::md5HexFile(*archive);
    if (!key) {
        return std::nullopt;
    }
    const fs::path entry = root / *key;
    if (!isStdlibCacheEntryValid(entry)) {
        const fs::path tmp = root / (".tmp-" + std::to_string(fs::file_time_type::clock::now().time_since_epoch().count()));
        fs::remove_all(tmp, ec);
        if (!extractAndVerifyPackage(*archive, tmp, out_error)) {
            fs::remove_all(tmp, ec);
            return std::nullopt;
        }
        fs::rename(tmp, entry, ec);
        if (ec) {
            // Проиграли гонку с другим процессом (запись уже создана) — используем её.
            fs::remove_all(tmp, ec);
        }
    }
    if (!isStdlibCacheEntryValid(entry)) {
        out_error = "cannot create stdlib cache entry '" + entry.string() + "'";
        return std::nullopt;
    }
    touchAndPruneStdlibCache(entry);
    return entry;
}

bool readPackageEntry(const fs::path& archive, std::string_view relPath, std::string& out_content, std::string& out_error) {
    out_content.clear();
    if (relPath.empty()) {
        out_error = "empty package entry path";
        return false;
    }
    std::vector<std::string> entries;
    if (!package::detail::listEntries(archive, entries, out_error)) {
        return false;
    }
    std::string_view want = relPath;
    while (want.starts_with("./")) {
        want.remove_prefix(2);
    }
    std::string actual;
    for (const std::string& entry : entries) {
        if (!package::detail::isSafeEntry(entry)) {
            out_error = "unsafe archive entry '" + entry + "'";
            return false;
        }
        if (package::detail::stripDotSlash(entry) == want) {
            actual = entry;
            break;
        }
    }
    if (actual.empty()) {
        out_error = "no '" + std::string(want) + "' in package '" + archive.string() + "'";
        return false;
    }
    return package::detail::readEntry(archive, actual, out_content, out_error);
}

std::optional<std::string> readResourcePackageEntry(std::string_view packageName, std::string_view relPath) {
    // stdlib-модули prelude (dsl/src, iterator/src, async, builtin/string) читаются на
    // КАЖДОЙ компиляции: пофайловое чтение из архива (tar+gzip подпроцесс на файл)
    // было основным источником ~1.4 с на цикл --run/-o. Из распакованного кэша — это
    // обычное чтение файла. Прочие пакеты и отсутствующие в кэше записи — через архив.
    if (packageName == "stdlib" && !relPath.empty() && relPath.front() != '/') {
        const std::optional<fs::path> entry = stdlibCacheEntry();
        if (entry) {
            fs::path cached = *entry / fs::path(relPath);
            if (package::detail::isSafeEntry(relPath)) {
                if (auto content = utils::FileIO::read<std::string>(cached); content) {
                    return content;
                }
            }
        }
    }
    const std::optional<fs::path> archive = locateResourcePackage(packageName);
    if (!archive) {
        return std::nullopt;
    }
    std::string content;
    std::string error;
    if (!readPackageEntry(*archive, relPath, content, error)) {
        return std::nullopt;
    }
    return content;
}

} // namespace trust
