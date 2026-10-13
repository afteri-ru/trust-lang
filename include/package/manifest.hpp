// include/package/manifest.hpp
// Модель и I/O манифеста пакета trust (`trust.toml`): разбор, валидация
// обязательных полей (name/version) и запись. Формат - TOML (библиотека toml++).
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <toml++/toml.hpp>

namespace trust {

/// Слой раскладки пакета: какие файлы дерева-источника и куда попадают в архиве.
/// `sourceDir` — каталог относительно каталога манифеста (обходится РЕКУРСИВНО);
/// `archiveDir` — каталог внутри архива (относительный, без `..`); относительный путь
/// файла внутри `sourceDir` сохраняется под `archiveDir`. `patterns` — glob-маски
/// ИМЁН файлов (`*`, `?`); файлы, не совпавшие ни с одной маской, в пакет не входят.
struct PackageLayer {
    std::string sourceDir = ".";  ///< каталог-источник (по умолчанию каталог манифеста)
    std::string archiveDir = "."; ///< каталог в архиве (по умолчанию корень архива)
    std::vector<std::string> patterns;
};

/// Kind of a native artifact declared by a provider package (`[[artifact]]`).
/// The artifact is a LINK CONTRACT, not a file list: it says how a logical library
/// name is resolved into include dirs / linker flags.
enum class ArtifactKind {
    Source,    ///< sources inside the package, compiled by the consuming application
    Package,   ///< library files inside the package (paths relative to the package root)
    Toolchain, ///< library provided by the toolchain (resolved like trust-runtime)
    System,    ///< system library resolved by the linker search path (-l<name>)
};

/// One `[[artifact]]` entry: provides logical library names and how to link them.
struct PackageArtifact {
    ArtifactKind kind = ArtifactKind::Toolchain;
    std::vector<std::string> sources;      ///< package-relative source files (kind=Source)
    std::vector<std::string> libs;         ///< package-relative files or -l names
    std::vector<std::string> linkOptions;  ///< compile/link flags (-pthread, -Wl,...)
    std::vector<std::string> provides;     ///< logical library names covered
    std::vector<std::string> capabilities; ///< required toolchain capabilities
    std::vector<std::string> requiresLibs; ///< logical libraries needed whenever this artifact is linked
};

/// One `[[link.headers]]` rule: using `header` requires the listed logical libraries.
struct PackageHeaderLink {
    std::string header;
    std::vector<std::string> libs;
};

/// Манифест пакета trust - полное содержимое `trust.toml`.
/// Единственный источник данных - таблица `data` (включая обязательные поля name/version);
/// отдельных копий полей структура не хранит.
struct PackageManifest {
    /// Имя файла манифеста в каталоге/архиве пакета.
    static constexpr std::string_view kFileName = "trust.toml";

    toml::table data; ///< полное содержимое манифеста

    /// Значение обязательного поля `name` (пусто, если поле отсутствует или не строка).
    std::string_view name() const;
    /// Значение обязательного поля `version` (пусто, если поле отсутствует или не строка).
    std::string_view version() const;
    /// Корень для заголовков/путей пакетных файлов (`module-root`, по умолчанию ".").
    std::string_view moduleRoot() const;
    /// Раскладка пакета из таблицы `[[layout.layers]]` (пусто — пакет = всё дерево каталога).
    std::vector<PackageLayer> layers() const;
    /// Нативные артефакты пакета из массива `[[artifact]]`.
    std::vector<PackageArtifact> artifacts() const;
    /// Правила «используемый заголовок → логические библиотеки» из `[[link.headers]]`.
    std::vector<PackageHeaderLink> headerLinks() const;
};

/// Имя пакета: строчные латинские буквы, цифры и '_' (не первая/последняя позиция).
/// То же правило, что и для имён модулей (прямое отображение на объекты файловой системы).
bool isValidPackageName(std::string_view name);

/// Версия пакета: MAJOR.MINOR.PATCH (три числовых компонента через '.').
bool isValidVersion(std::string_view version);

/// Политика обязательности поля `version` при разборе манифеста.
/// `Required` — манифест готового пакета (версия в нём всегда есть: упаковщик гарантирует).
/// `Optional` — манифест каталога-источника: `version` (semver ПАКЕТА) может отсутствовать.
/// Упаковщик сохраняет авторскую версию, а при её отсутствии подставляет версию компилятора
/// (`TRUST_VERSION_SHORT`) как provenance; версия toolchain — `[build].toolchain`.
enum class ManifestVersion {
    Required,
    Optional,
};

/// Разбор TOML-текста манифеста и валидация полей.
/// `name` обязателен всегда; `version` — по политике version_policy.
/// При ошибке возвращает false и заполняет out_error (без silent fallback).
bool parseManifest(std::string_view text, PackageManifest& out, std::string& out_error, ManifestVersion version_policy = ManifestVersion::Required);

/// Чтение и разбор файла манифеста. При ошибке - false + out_error.
bool parseManifestFile(const std::filesystem::path& path, PackageManifest& out, std::string& out_error,
                       ManifestVersion version_policy = ManifestVersion::Required);

/// Запись манифеста в файл (TOML). При ошибке - false + out_error.
bool writeManifestFile(const std::filesystem::path& path, const PackageManifest& manifest, std::string& out_error);

} // namespace trust
