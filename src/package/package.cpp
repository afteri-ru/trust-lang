// src/package/package.cpp
// Пакет trust (`.trust`): проверка целостности, создание/распаковка (режимы CLI
// --package/--extract). tar-примитивы - package/tar.cpp, ресурсы - package/resource.cpp.
#include "package/package.hpp"

#include "package/archive/archive_io.hpp"
#include "package/manifest.hpp"
#include "trust/version.h"
#include "utils/hash.hpp"
#include "utils/utils.hpp"
#include "utils/strings.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace trust {

namespace fs = std::filesystem;

namespace {

/// Файл-артефакт сборки, не попадающий в пакет.
bool isExcludedFile(const fs::path& p) {
    if (p.filename() == "app") {
        return true;
    }
    const std::string ext = p.extension().string();
    return ext == ".cppt" || ext == ".src_map" || ext == ".o" || ext == ".so";
}

/// Временный staging-каталог, удаляемый по выходу (RAII).
struct StagingDir {
    fs::path path;
    ~StagingDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

} // namespace

bool verifyPackage(const fs::path& dir, std::string& out_error) {
    const fs::path manifest = dir / std::string(PackageManifest::kFileName);
    std::error_code ec;
    if (!fs::is_regular_file(manifest, ec)) {
        out_error = "package has no 'trust.toml': cannot verify integrity";
        return false;
    }
    std::ifstream ifs(manifest, std::ios::binary);
    if (!ifs) {
        out_error = "cannot read '" + manifest.string() + "'";
        return false;
    }
    const std::string text{std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
    toml::table table;
    try {
        table = toml::parse(text);
    } catch (const std::exception& e) {
        out_error = "trust.toml: invalid TOML: " + std::string(e.what());
        return false;
    }
    const toml::table* files = table["files"].as_table();
    if (files == nullptr) {
        out_error = "trust.toml has no [files] integrity table";
        return false;
    }

    std::unordered_set<std::string> listed;
    for (auto&& [key, value] : *files) {
        const std::string rel(key.str());
        const auto expected = value.value<std::string>();
        if (!expected) {
            out_error = "trust.toml: [files] entry '" + rel + "' is not a string";
            return false;
        }
        listed.insert(rel);
        const fs::path file = dir / rel;
        if (!fs::is_regular_file(file, ec)) {
            out_error = "package file missing: '" + rel + "'";
            return false;
        }
        const std::string actual = utils::md5HexFile(file).value_or(std::string());
        if (actual != *expected) {
            out_error = "package file checksum mismatch: '" + rel + "'";
            return false;
        }
    }

    // Лишние файлы (кроме самого trust.toml) - признак подмены содержимого.
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const std::string rel = fs::relative(it->path(), dir, ec).generic_string();
        if (rel == std::string(PackageManifest::kFileName)) {
            continue;
        }
        if (listed.find(rel) == listed.end()) {
            out_error = "package contains unlisted file: '" + rel + "'";
            return false;
        }
    }
    return true;
}

bool extractAndVerifyPackage(const fs::path& archive, const fs::path& dest, std::string& out_error) {
    if (!package::detail::extractArchive(archive, dest, out_error)) {
        return false;
    }
    return verifyPackage(dest, out_error);
}

namespace {

bool isValidPattern(std::string_view pattern) {
    return !pattern.empty() && pattern.front() != '/' && pattern.find('/') == std::string_view::npos && pattern.find('\\') == std::string_view::npos &&
           pattern.find("..") == std::string_view::npos;
}

/// Финализация пакета из staging-каталога (определение — ниже packLayers).
int finalizePackage(const fs::path& staging, PackageManifest manifest, const std::string& name, const fs::path& output_file);

/// Раскладка пакета по слоям манифеста (`[[layout.layers]]`).
/// `source-dir` обходится рекурсивно; относительный путь файла сохраняется под `archive-dir`;
/// в пакет входят файлы, чьё ИМЯ совпало хотя бы с одной маской `patterns` (`*`, `?`).
/// Все нарушения — ошибка (без silent fallback).
int packLayers(const fs::path& package_dir, const std::vector<PackageLayer>& layers, const fs::path& staging, PackageManifest manifest, const std::string& name,
               const fs::path& output_file) {
    std::error_code ec;
    std::set<std::string> placed; // пути в архиве — контроль коллизий между слоями
    for (const PackageLayer& layer : layers) {
        if (layer.sourceDir.empty() || layer.archiveDir.empty() || layer.patterns.empty()) {
            trust::errs() << "error: --package: layout layer requires non-empty 'source-dir', 'archive-dir' and 'patterns'\n";
            return 1;
        }
        for (const std::string& pattern : layer.patterns) {
            if (!isValidPattern(pattern)) {
                trust::errs() << "error: --package: invalid layout pattern '" << pattern << "' (must be a file-name glob without '/', '\\' or '..')\n";
                return 1;
            }
        }
        // archive-dir: относительный путь внутри архива, без '..' и абсолютных путей.
        const fs::path archive_dir(layer.archiveDir);
        if (archive_dir.is_absolute() || layer.archiveDir.find("..") != std::string::npos) {
            trust::errs() << "error: --package: layout archive-dir '" << layer.archiveDir << "' must be a relative path without '..'\n";
            return 1;
        }
        const fs::path source_dir = package_dir / layer.sourceDir;
        if (!fs::is_directory(source_dir, ec)) {
            trust::errs() << "error: --package: layout source-dir '" << layer.sourceDir << "' is not a directory\n";
            return 1;
        }

        std::size_t taken = 0;
        fs::recursive_directory_iterator it(source_dir, fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec) {
                break;
            }
            if (!it->is_regular_file(ec)) {
                continue;
            }
            const fs::path& p = it->path();
            const std::string filename = p.filename().string();
            bool matched = false;
            for (const std::string& pattern : layer.patterns) {
                if (utils::globMatch(pattern, filename)) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                continue;
            }
            const fs::path rel = fs::relative(p, source_dir, ec);
            if (ec) {
                trust::errs() << "error: --package: cannot resolve '" << p.string() << "'\n";
                return 1;
            }
            const fs::path dst_rel = archive_dir / rel;
            if (!placed.insert(dst_rel.generic_string()).second) {
                trust::errs() << "error: --package: layout collision: '" << dst_rel.generic_string() << "' produced by more than one layer\n";
                return 1;
            }
            const fs::path dst = staging / dst_rel;
            fs::create_directories(dst.parent_path(), ec);
            fs::copy_file(p, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                trust::errs() << "error: --package: cannot copy '" << p.string() << "'\n";
                return 1;
            }
            ++taken;
        }
        if (ec) {
            trust::errs() << "error: --package: cannot scan '" << source_dir.string() << "'\n";
            return 1;
        }
        // Пустой слой — ошибка: пакет не должен молча оказаться неполным.
        if (taken == 0) {
            trust::errs() << "error: --package: layout layer '" << layer.sourceDir << "' -> '" << layer.archiveDir << "' matched no files\n";
            return 1;
        }
    }

    return finalizePackage(staging, std::move(manifest), name, output_file);
}

/// Финализация пакета из staging-каталога: [files] (MD5), манифест (версия компилятора,
/// [build]), упаковка в архив. Единый путь для всех способов раскладки файлов.
/// @param staging  каталог с уже разложенными файлами (без trust.toml).
/// @param manifest манифест источника (уже разобран).
/// @param name     имя пакета (для имени архива по умолчанию).
/// @param output_file путь архива; пусто -> `<name>.trust` в cwd.
int finalizePackage(const fs::path& staging, PackageManifest manifest, const std::string& name, const fs::path& output_file) {
    std::error_code ec;

    // -- Хеши файлов (контроль целостности): [files] = относительный путь -> MD5.
    // Сам trust.toml не хешируется (он содержит эту таблицу). --
    toml::table files_table;
    {
        std::error_code hec;
        fs::recursive_directory_iterator fit(staging, fs::directory_options::skip_permission_denied, hec);
        const fs::recursive_directory_iterator fend;
        for (; fit != fend; fit.increment(hec)) {
            if (hec) {
                break;
            }
            if (!fit->is_regular_file(hec)) {
                continue;
            }
            const fs::path rel = fs::relative(fit->path(), staging, hec);
            files_table.insert_or_assign(rel.generic_string(), utils::md5HexFile(fit->path()).value_or(std::string()));
        }
    }

    // -- Мердж манифеста: версия пакета (авторская; при отсутствии — версия компилятора как
    // provenance), авторские поля + таблицы [build] (provenance) и [files] (integrity). --
    std::string mf_error;
    if (manifest.version().empty()) {
        manifest.data.insert_or_assign("version", std::string(TRUST_VERSION_SHORT));
    }
    manifest.data.insert_or_assign("build", toml::table{
                                                {"formatVersion", 1},
                                                {"toolchain", std::string(TRUST_VERSION)},
                                                // ДАТА, не время: репаки пакета меняли бы
                                                // created → md5 архива → dep-хэш в .cppt → сброс
                                                // ccache-кэша повторных прогонов.
                                                {"created", utils::currentDate()},
                                            });
    manifest.data.insert_or_assign("files", std::move(files_table));
    if (!writeManifestFile(staging / std::string(PackageManifest::kFileName), manifest, mf_error)) {
        trust::errs() << "error: --package: " << mf_error << "\n";
        return 1;
    }

    // -- Упаковка: выходной архив (по умолчанию <name>.trust в cwd). --
    std::error_code cwd_ec;
    fs::path out = output_file.empty() ? (fs::current_path(cwd_ec) / (name + ".trust")) : output_file;
    std::error_code rm_ec;
    fs::remove(out, rm_ec);
    std::string tar_error;
    if (!createTarGz(staging, out, tar_error)) {
        trust::errs() << "error: --package: " << tar_error << "\n";
        return 1;
    }
    if (!fs::is_regular_file(out, ec) || ec) {
        trust::errs() << "error: --package: archive not produced\n";
        return 1;
    }
    trust::outs() << out.string() << "\n";
    return 0;
}

} // namespace

int createTrustPackage(const fs::path& package_dir, const fs::path& output_file) {
    std::error_code ec;
    if (!fs::is_directory(package_dir, ec)) {
        trust::errs() << "error: --package: '" << package_dir.string() << "' is not a directory\n";
        return 1;
    }

    const fs::path manifest_path = package_dir / std::string(PackageManifest::kFileName);
    if (!fs::is_regular_file(manifest_path, ec)) {
        trust::errs() << "error: --package: no '" << PackageManifest::kFileName << "' in '" << package_dir.string() << "'\n";
        return 1;
    }

    // -- Манифест: чтение и валидация полей. Версия пакета проставляется упаковщиком
    // (всегда версия компилятора); в манифесте источника она может отсутствовать,
    // а указанная версия обязана совпасть с версией компилятора. --
    PackageManifest manifest;
    std::string mf_error;
    if (!parseManifestFile(manifest_path, manifest, mf_error, ManifestVersion::Optional)) {
        trust::errs() << "error: --package: " << PackageManifest::kFileName << ": " << mf_error << "\n";
        return 1;
    }
    const std::string name(manifest.name());

    // -- Staging-каталог (временный; удаляется RAII). --
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    StagingDir staging{fs::temp_directory_path(ec) / ("trust-pkg-" + name + "-" + std::to_string(unique_suffix))};
    fs::remove_all(staging.path, ec);
    ec.clear();
    fs::create_directories(staging.path, ec);
    if (ec) {
        trust::errs() << "error: --package: cannot create staging directory\n";
        return 1;
    }

    // -- Раскладка файлов в staging. --
    // Если манифест описывает слои ([[layout.layers]]), пакет собирается ТОЛЬКО по ним:
    // source обходится рекурсивно, относительный путь сохраняется под target, в пакет
    // входят файлы с расширениями из include. Без слоёв — всё дерево каталога
    // (кроме build-артефактов), с проверкой коллизий `name.src`/`name.trust`.
    const std::vector<PackageLayer> layers = manifest.layers();
    if (layers.empty()) {
        fs::recursive_directory_iterator it(package_dir, fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec) {
                break;
            }
            const fs::path& p = it->path();
            if (it->is_directory(ec)) {
                // Build-каталог `.trust` в пакет не входит и не обходится.
                if (p.filename() == ".trust") {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!it->is_regular_file(ec)) {
                continue;
            }
            // Корневой trust.toml заменяется копией с добавленными полями (см. ниже).
            if (p == manifest_path) {
                continue;
            }
            // Коллизия: `name.src` (модуль) и `name.trust` (пакет) в одном каталоге недопустимы.
            const std::string ext = p.extension().string();
            if (ext == ".src" || ext == ".trust") {
                const std::string other_ext = (ext == ".src") ? ".trust" : ".src";
                const fs::path other = p.parent_path() / (p.stem().string() + other_ext);
                std::error_code exists_ec;
                if (fs::exists(other, exists_ec)) {
                    trust::errs() << "error: --package: name collision: '" << p.filename().string() << "' and '" << other.filename().string() << "' in '"
                                  << p.parent_path().string() << "'\n";
                    return 1;
                }
            }
            if (isExcludedFile(p)) {
                continue;
            }
            const fs::path rel = fs::relative(p, package_dir, ec);
            const fs::path dst = staging.path / rel;
            fs::create_directories(dst.parent_path(), ec);
            fs::copy_file(p, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                trust::errs() << "error: --package: cannot copy '" << p.string() << "'\n";
                return 1;
            }
        }
        if (ec) {
            trust::errs() << "error: --package: cannot scan '" << package_dir.string() << "'\n";
            return 1;
        }
        return finalizePackage(staging.path, std::move(manifest), name, output_file);
    }

    return packLayers(package_dir, layers, staging.path, std::move(manifest), name, output_file);
}

int extractTrustPackage(const fs::path& archive, const fs::path& dest_dir) {
    std::error_code ec;
    if (!fs::is_regular_file(archive, ec)) {
        trust::errs() << "error: --extract: '" << archive.string() << "' is not a file\n";
        return 1;
    }

    // -- Листинг записей: проверка безопасности путей ДО распаковки. --
    std::vector<std::string> entries;
    std::string tar_error;
    if (!package::detail::listEntries(archive, entries, tar_error)) {
        trust::errs() << "error: --extract: cannot list archive: " << tar_error << "\n";
        return 1;
    }
    std::string manifest_entry;
    for (const std::string& entry : entries) {
        if (!package::detail::isSafeEntry(entry)) {
            trust::errs() << "error: --extract: unsafe archive entry '" << entry << "'\n";
            return 1;
        }
        if (package::detail::stripDotSlash(entry) == PackageManifest::kFileName) {
            manifest_entry = entry;
        }
    }
    if (manifest_entry.empty()) {
        trust::errs() << "error: --extract: no '" << PackageManifest::kFileName << "' in '" << archive.string() << "'\n";
        return 1;
    }

    // -- Чтение и валидация манифеста из архива (без извлечения на диск). --
    std::string manifest_text;
    if (!package::detail::readEntry(archive, manifest_entry, manifest_text, tar_error)) {
        trust::errs() << "error: --extract: cannot read manifest: " << tar_error << "\n";
        return 1;
    }
    PackageManifest manifest;
    std::string mf_error;
    if (!parseManifest(manifest_text, manifest, mf_error)) {
        trust::errs() << "error: --extract: " << PackageManifest::kFileName << ": " << mf_error << "\n";
        return 1;
    }

    // -- Каталог назначения: -o <dir>, иначе <name>/ в cwd. --
    fs::path dest = dest_dir;
    if (dest.empty()) {
        std::error_code cwd_ec;
        dest = fs::current_path(cwd_ec) / std::string(manifest.name());
    }
    if (fs::is_regular_file(dest, ec)) {
        trust::errs() << "error: --extract: destination '" << dest.string() << "' is a file\n";
        return 1;
    }
    ec.clear();
    fs::create_directories(dest, ec);
    if (ec) {
        trust::errs() << "error: --extract: cannot create destination '" << dest.string() << "'\n";
        return 1;
    }

    // -- Распаковка + сверка целостности по [files]. --
    if (!extractAndVerifyPackage(archive, dest, tar_error)) {
        trust::errs() << "error: --extract: " << tar_error << "\n";
        return 1;
    }
    trust::outs() << dest.string() << "\n";
    return 0;
}

} // namespace trust
