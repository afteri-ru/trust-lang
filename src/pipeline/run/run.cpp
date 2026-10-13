// src/pipeline/run/run.cpp
// Запуск собранного исполняемого файла (--run) и кеш по md5 исходников.
// Модуль RunManager (декомпозиция pipeline.cpp).
#include "pipeline/run/run.hpp"
#include "toolchain/cache.hpp"
#include "toolchain/hash.hpp"
#include "pipeline/paths/paths.hpp"
#include "module_loader/module_loader.hpp"
#include "package/package.hpp"
#include "session/context.hpp"
#include "utils/utils.hpp"
#include "utils/error.hpp"
#include "trust/version.h"
#include <cstdlib>
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif
namespace trust {

// -- Shell-экранирование аргумента для std::system (одинарные кавычки). --
static std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (const char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

// -- Запуск собранного исполняемого файла; возвращает его код возврата. --
static int runExecutable(const std::filesystem::path& exe, const std::vector<std::string>& run_args) {
    std::string cmd = exe.string();
    for (const auto& a : run_args) {
        cmd += ' ';
        cmd += shellQuote(a);
    }
    int rc = std::system(cmd.c_str());
#ifndef _WIN32
    rc = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : rc;
#endif
    return rc;
}

// -- Путь исполняемого файла для --run: -o, иначе <build_dir>/<stem>. --
static std::filesystem::path runExecutablePath(const PipelineOpts& opts, const std::filesystem::path& cpptPath) {
    if (!opts.output_file.empty()) {
        return std::filesystem::path(opts.output_file);
    }
    return cpptPath.parent_path() / cpptPath.stem();
}

std::vector<std::filesystem::path> programDependencyArchives(const ModuleLoader& loader, const PipelineOpts& opts) {
    namespace fs = std::filesystem;
    std::vector<fs::path> deps;
    auto add_dep = [&deps](const fs::path& p) {
        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(p, ec);
        if (ec) {
            canonical = p;
        }
        if (std::find(deps.begin(), deps.end(), canonical) == deps.end()) {
            deps.push_back(std::move(canonical));
        }
    };
    if (!opts.no_dsl || opts.use_stdlib) {
        if (auto stdlib_package = locateResourcePackage("stdlib")) {
            add_dep(*stdlib_package);
        }
    }
    if (!opts.dsl_file.empty()) {
        add_dep(opts.dsl_file);
    }
    for (const auto& archive : loader.usedPackageArchives()) {
        add_dep(archive);
    }
    return deps;
}

std::string buildProgramRecord(const std::filesystem::path& mainFile, Context& ctx, const ModuleLoader& loader, ModuleIndex mainIdx, const PipelineOpts& opts) {
    auto rel = [](const std::filesystem::path& p) { return std::filesystem::relative(p, std::filesystem::current_path()).generic_string(); };
    // Замыкание зависимостей, НЕ покрытых хешами модулей ниже: пакеты ресурсов (stdlib и
    // provider-пакеты) и явный DSL-файл. Хеши `.src` перекрывают исходники модулей, но НЕ
    // C++-заголовки пакетов; без @dep изменение заголовка оставляло бы кеш валидным.
    std::vector<toolchain::CacheDependency> deps;
    for (const auto& dep : programDependencyArchives(loader, opts)) {
        const std::string hash = toolchain::fileHash(dep);
        // Зависимость УЖЕ разрешена фронтендом (архив распакован / dsl прочитан): нехешируемая
        // зависимость - внутренняя ошибка, а не повод молча выбросить её из ключа кеша.
        EXPECT(hash != "error" && "cache dependency must be hashable");
        deps.push_back({rel(dep), hash});
    }
    // Файлы программы: первый - главный, далее импортированные модули.
    std::vector<toolchain::CacheDependency> files;
    files.push_back({rel(mainFile), toolchain::fileHash(mainFile)});
    for (std::size_t i = 0; i < loader.moduleCount(); ++i) {
        const ModuleIndex idx{i};
        if (idx == mainIdx || !loader.isLoaded(idx)) {
            continue;
        }
        const std::filesystem::path modPath(loader.modulePath(idx));
        files.push_back({rel(modPath), toolchain::fileHash(modPath)});
    }
    return toolchain::composeRunCacheRecord(TRUST_VERSION, toolchain::codegenArgsRecord(opts), deps, files);
}

// -- Запуск исполняемого файла --run: путь -o, иначе <build_dir>/<stem>. --
int runBuiltExecutable(const PipelineOpts& opts, const std::filesystem::path& cpptPath) {
    return runExecutable(runExecutablePath(opts, cpptPath), opts.run_args);
}

// -- --run: если кеш по md5 исходников валиден и exe существует - запустить без перекомпиляции. --
// Возвращает код возврата программы, если кеш применим (запуск выполнен); иначе nullopt →
// требуется перекомпиляция. Проверяет и соответствие главного файла записи кеша (A1).
std::optional<int> tryRunCached(const PipelineOpts& opts) {
    if (!opts.run || opts.compile_mode != CompileMode::Executable) {
        return std::nullopt;
    }
    const std::filesystem::path cpptForExe = opts.output_file.empty() ? computeCpptPath(opts) : std::filesystem::path{};
    const std::filesystem::path exe = runExecutablePath(opts, cpptForExe);
    if (!std::filesystem::exists(exe) || !toolchain::runCacheValid(exe, std::filesystem::path(opts.input_file), toolchain::codegenArgsRecord(opts))) {
        return std::nullopt;
    }
    if (opts.verbose) {
        trust::errs() << "info: source unchanged, running cached " << exe.string() << "\n";
    }
    return runBuiltExecutable(opts, cpptForExe);
}
} // namespace trust
