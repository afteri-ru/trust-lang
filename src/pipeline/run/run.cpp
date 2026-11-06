// Замыкание зависимостей программы и запись кеша `--run` (шапка `.cppt`/ELF-секция).
// Запуск собранного файла и валидация кеша перенесены в драйвер (`builddriver`).
#include "pipeline/run/run.hpp"

#include "driver/codegen_args.hpp"
#include "module_loader/module_loader.hpp"
#include "package/package.hpp"
#include "pipeline/paths/paths.hpp"
#include "trust/version.h"
#include "utils/build_record.hpp"
#include "utils/error.hpp"
#include "utils/hash.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace trust {

namespace {

/// Канонический путь зависимости без дублей (порядок первого использования).
void addProgramDependency(std::vector<std::filesystem::path>& deps, const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(p, ec);
    if (ec) {
        canonical = p;
    }
    if (std::find(deps.begin(), deps.end(), canonical) == deps.end()) {
        deps.push_back(std::move(canonical));
    }
}

} // namespace

std::vector<std::filesystem::path> programDependencyArchives(const ModuleLoader& loader, const PipelineOpts& opts) {
    std::vector<std::filesystem::path> deps;
    if (!opts.no_dsl || opts.use_stdlib) {
        if (auto stdlib_package = locateResourcePackage("stdlib")) {
            addProgramDependency(deps, *stdlib_package);
        }
    }
    if (!opts.dsl_file.empty()) {
        addProgramDependency(deps, opts.dsl_file);
    }
    for (const auto& archive : loader.usedPackageArchives()) {
        addProgramDependency(deps, archive);
    }
    return deps;
}

std::string buildProgramRecord(const std::filesystem::path& mainFile, const ModuleLoader& loader, ModuleIndex mainIdx, const PipelineOpts& opts) {
    // Замыкание зависимостей, НЕ покрытых хешами модулей ниже: пакеты ресурсов (stdlib и
    // provider-пакеты) и явный DSL-файл. Хеши `.src` перекрывают исходники модулей, но НЕ
    // C++-заголовки пакетов; без @dep изменение заголовка оставляло бы кеш валидным.
    std::vector<utils::CacheDependency> deps;
    for (const auto& dep : programDependencyArchives(loader, opts)) {
        const auto hash = utils::md5HexFile(dep);
        // Зависимость УЖЕ разрешена фронтендом (архив распакован / dsl прочитан): нехешируемая
        // зависимость - внутренняя ошибка, а не повод молча выбросить её из ключа кеша.
        EXPECT(hash.has_value() && "cache dependency must be hashable");
        deps.push_back({utils::cachePathOf(dep), *hash});
    }
    // Файлы программы: первый - главный, далее импортированные модули.
    std::vector<utils::CacheDependency> files;
    const auto mainHash = utils::md5HexFile(mainFile);
    EXPECT(mainHash.has_value() && "program main file must be hashable for the run cache");
    files.push_back({utils::cachePathOf(mainFile), *mainHash});
    for (std::size_t i = 0; i < loader.moduleCount(); ++i) {
        const ModuleIndex idx{i};
        if (idx == mainIdx || !loader.isLoaded(idx)) {
            continue;
        }
        const std::filesystem::path modPath(loader.modulePath(idx));
        const auto modHash = utils::md5HexFile(modPath);
        EXPECT(modHash.has_value() && "loaded module file must be hashable for the run cache");
        files.push_back({utils::cachePathOf(modPath), *modHash});
    }
    return utils::composeRunCacheRecord(TRUST_VERSION, codegenArgsRecord(opts), deps, files);
}

} // namespace trust

