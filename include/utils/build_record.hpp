// Формат записи кеша сборки `--run` и константы раскладки build-каталога.
//
// Единый владелец формата: запись ФОРМИРУЕТ кодоген (шапка `.cppt` + ELF-секция
// `.debug_trust_hash`), а ЧИТАЕТ `toolchain` (`runCacheValid`). Оба слоя зависят от `utils`,
// поэтому формат живёт здесь, а не в одном из них (R1: одно хранимое представление формата).
// `kPackagesSubdir` — раскладка build-каталога, общая для кеша пакетов загрузчика (`pipeline`)
// и распаковки пакетов тулчейном (`toolchain`).
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace trust::utils {

/// Секция ELF, в которой лежит запись кеша `--run` (читается `toolchain::runCacheValid`).
inline constexpr std::string_view kRunCacheSection = ".debug_trust_hash";
/// Префикс первой строки записи (версия компилятора).
inline constexpr std::string_view kRunCacheVersionPrefix = "trust-lang\t";
/// Подкаталог build-каталога, куда извлекаются пакеты `.trust`.
inline constexpr std::string_view kPackagesSubdir = "packages";

/// Элемент ключа кеша: путь ОТНОСИТЕЛЬНО CWD + md5 содержимого.
struct CacheDependency {
    std::string path;
    std::string md5;
};

/// Путь относительно CWD — правило формата записи кеша: writer и reader применяют ОДНУ функцию.
[[nodiscard]] inline std::string cachePathOf(const std::filesystem::path& path) {
    return std::filesystem::relative(path, std::filesystem::current_path()).generic_string();
}

/// Собрать запись кеша: версия, `@opts`, `@deps` (замыкание) и файлы (первый — главный).
/// Байт-стабильна: изменение формата инвалидирует кеш `--run` детерминированно.
[[nodiscard]] inline std::string composeRunCacheRecord(std::string_view version, std::string_view optionsRecord,
                                                       const std::vector<CacheDependency>& dependencies, const std::vector<CacheDependency>& files) {
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

} // namespace trust::utils
