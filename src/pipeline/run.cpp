// src/pipeline/run.cpp
// Запуск собранного исполняемого файла (--run) и кеш по md5 исходников.
// Модуль RunManager (декомпозиция pipeline.cpp).
#include "pipeline/run.hpp"
#include "pipeline/io.hpp"
#include "pipeline/build.hpp"
#include "utils/io.hpp"
#include "utils/elf.hpp"
#include "trust/version.h"
#include <cstdlib>
#include <filesystem>
#include <sstream>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif
namespace trust {

// -- Запуск собранного исполняемого файла; возвращает его код возврата. --
static int runExecutable(const std::filesystem::path& exe) {
    int rc = std::system(exe.string().c_str());
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

// -- Запись кеша --run --
// Первая строка записи ВСЕГДА версия компилятора `trust-lang\t<TRUST_VERSION_FULL>`; далее идёт
// главный файл, затем импортированные модули - по строке "файл\tmd5\n" на каждый.
// Запись используется для инвалидации без парсинга: имена и хеши всех файлов программы известны
// из встроенной записи, проверка лишь пере-хеширует перечисленные файлы.
// Строка версии гарантирует перекомпиляцию при смене версии компилятора (не только при
// изменении исходника) и делает невалидными записи старого формата/чужого бинарника.
// Пути в записи ОТНОСИТЕЛЬНЫЕ - от текущего каталога (CWD), откуда запускается `--run`.
// Для обычного запуска `trust --run prog.src` из каталога исходников в записи будет просто
// `prog.src`/`mymod.src` (без каталога-перехода); запись переносима между каталогами.
static constexpr const char* kTrustVersionPrefix = "trust-lang\t";

std::string buildProgramRecord(const std::filesystem::path& mainFile, Context& ctx, std::size_t mainIdx) {
    namespace fs = std::filesystem;
    auto rel = [](const fs::path& p) { return fs::relative(p, fs::current_path()).generic_string(); };
    std::string record;
    record += kTrustVersionPrefix;
    record += TRUST_VERSION_FULL;
    record += '\n';
    record += rel(mainFile) + '\t' + fileHash(mainFile) + '\n';
    for (std::size_t idx = 0; idx < ctx.loader().moduleCount(); ++idx) {
        if (idx == mainIdx || !ctx.loader().isLoaded(idx)) {
            continue;
        }
        const fs::path modPath(ctx.loader().moduleName(idx));
        record += rel(modPath) + '\t' + fileHash(modPath) + '\n';
    }
    return record;
}

// -- Проверка кеша --run: читает запись из ELF-секции `.debug_trust_hash` и сверяет. --
static bool runCacheValid(const std::filesystem::path& exe, const std::filesystem::path& expectedMain) {
    auto sec = trust::utils::readElfSection(exe.string(), ".debug_trust_hash");
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
    bool isMain = true;
    auto rel = [](const std::filesystem::path& p) { return std::filesystem::relative(p, std::filesystem::current_path()).generic_string(); };
    while (std::getline(iss, line)) {
        if (line.empty()) {
            continue;
        }
        const auto tab = line.find('\t');
        if (tab == std::string::npos) {
            return false;
        }
        if (!versionSeen) {
            // Строка №0 - версия компилятора, которой собран бинарник. Не совпала с текущей
            // (другая версия/формат) - кеш не применяем, требуется пересборка.
            versionSeen = true;
            if (line != std::string(kTrustVersionPrefix) + TRUST_VERSION_FULL) {
                return false;
            }
            continue;
        }
        // Первая строка файла - главный файл. A1: запись построена для ДРУГОГО главного файла
        // (тот же stem, другой каталог) - кеш не применяем, иначе --run запустит устаревший
        // бинарь чужой программы. Последующие строки файлов (модули) - только сверка хеша.
        if (isMain) {
            isMain = false;
            if (line.substr(0, tab) != rel(expectedMain)) {
                return false;
            }
        }
        // Пути в записи относительны CWD (см. buildProgramRecord) - fileHash резолвит их оттуда.
        if (fileHash(std::filesystem::path(line.substr(0, tab))) != line.substr(tab + 1)) {
            return false;
        }
        any = true;
    }
    return any;
}

// -- Запуск исполняемого файла --run: путь -o, иначе <build_dir>/<stem>. --
int runBuiltExecutable(const PipelineOpts& opts, const std::filesystem::path& cpptPath) {
    return runExecutable(runExecutablePath(opts, cpptPath));
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
    if (!std::filesystem::exists(exe) || !runCacheValid(exe, std::filesystem::path(opts.input_file))) {
        return std::nullopt;
    }
    if (opts.verbose) {
        trust::errs() << "info: source unchanged, running cached " << exe.string() << "\n";
    }
    return runBuiltExecutable(opts, cpptForExe);
}
} // namespace trust
