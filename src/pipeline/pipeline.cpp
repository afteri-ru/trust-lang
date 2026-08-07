#include "pipeline/pipeline.hpp"
#include "pipeline/makefile_build.hpp"
#include "ast/term_to_ast.hpp"
#include "module_loader/module_export.hpp"
#include "syntax/lexer.h"
#include "syntax/macro.h"
#include "syntax/parser.h"
#include "syntax/term.h"
#include "ast/ast_nodes.hpp"
#include "semantic/pass_runner.hpp"
#include "transpiler/transpiler.hpp"
#include "trust/version.h"
#include "runtime/module_api.h"

#include "utils/io.hpp"
#include "utils/elf.hpp"
#include "utils/file_io.hpp"
#include "utils/utils.hpp"

#include <cstdlib>
#include <chrono>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <variant>

#include "llvm/Support/MD5.h"

namespace trust {

// Документирующие комментарии к объявлениям привязываются ГРАММАТИКОЙ к терму-идентификатора
// (term->m_docs, см. include/syntax/parser.y.in: attachLeadingDoc/attachTrailingDoc) и переносятся
// в узел объявления TermToAstConverter::convert → AstNodeBase::documentation. SymbolCollectorHook
// читает их в finalize. Отдельный AST-обход (moduleDocMap/attachDocumentation) не требуется.

// ── Helper: current timestamp string ──
static std::string currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&time_t_now, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S %z");
    return oss.str();
}

// ── Helper: MD5 hash for a file (uses llvm::MD5Hash, same as FileEntry::getHash) ──
static std::string fileHash(const std::filesystem::path& path) {
    auto content = trust::utils::FileIO::read<std::string>(path.string());
    if (!content) {
        return "error";
    }
    auto hash = llvm::MD5Hash(*content);
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

// ── Free function: compile .cppt → .o and link via Makefile ──

static std::string makeTargetForMode(CompileMode mode) {
    switch (mode) {
    case CompileMode::ObjectFile:
        return "object";
    case CompileMode::StaticLib:
        return "static-lib";
    case CompileMode::SharedLib:
        return "shared-lib";
    case CompileMode::TrustModule:
        return "shared-lib";
    case CompileMode::Executable:
        return "executable";
    }
    return "executable";
}

// ── Helper: dynamic lookup of the trust-runtime library (no hardcoded paths) ──
// The runtime library is not bound to an absolute path at configure time. It is
// located at runtime relative to the trust executable, via LD_LIBRARY_PATH, or
// in the current working directory. The file searched for depends on the link
// mode: a static library (trust-runtime.a) or a shared one (trust-runtime.so).
static std::filesystem::path runtimeLibraryFileName(RuntimeLink link) {
    namespace fs = std::filesystem;
    return link == RuntimeLink::Static ? fs::path("trust-runtime.a") : fs::path("trust-runtime.so");
}

static std::filesystem::path locateRuntimeLibrary(RuntimeLink link) {
    namespace fs = std::filesystem;
    const fs::path kLibName = runtimeLibraryFileName(link);

    // Search <base>/{kLibName, lib/kLibName, ../lib/kLibName}.
    auto searchIn = [&](const fs::path& base) -> fs::path {
        std::error_code ec;
        for (const fs::path& rel : {kLibName, fs::path("lib") / kLibName, fs::path("..") / "lib" / kLibName}) {
            fs::path cand = base / rel;
            if (fs::is_regular_file(cand, ec)) {
                return cand;
            }
        }
        return {};
    };

    // 1) Directory of the real executable (symlinks resolved via /proc/self/exe).
    {
        std::error_code ec;
        fs::path exe = fs::canonical("/proc/self/exe", ec);
        if (!ec && !exe.empty()) {
            if (fs::path p = searchIn(exe.parent_path()); !p.empty()) {
                return p;
            }
        }
    }
    // 2) Directories from LD_LIBRARY_PATH.
    if (const char* llp = std::getenv("LD_LIBRARY_PATH")) {
        std::string paths(llp);
        size_t pos = 0;
        while (pos <= paths.size()) {
            size_t sep = paths.find(':', pos);
            std::string dir = paths.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos);
            if (!dir.empty()) {
                if (fs::path p = searchIn(dir); !p.empty()) {
                    return p;
                }
            }
            if (sep == std::string::npos) {
                break;
            }
            pos = sep + 1;
        }
    }
    // 3) Current working directory.
    {
        std::error_code ec;
        fs::path cwd = fs::current_path(ec);
        if (!ec) {
            if (fs::path p = searchIn(cwd); !p.empty()) {
                return p;
            }
        }
    }
    return {};
}

// Forward declaration (defined below in this file).
static bool extractRuntimeHeader(const std::string& headerPath, const std::filesystem::path& buildDir, RuntimeLink link);

// ── writeBuildFiles: генерация build-файлов (Makefile, build.conf, _main.cppt, LICENSE
//    и trust/ рантайм-заголовки) в build_dir рядом с .cppt. Без компиляции/линковки.
//    relocatable=true — build.conf без абсолютных путей и без привязки к рантайм-
//    библиотеке (для распространяемого архива, собираемого пользователем с установленным
//    TrustLang toolchain). Используется и `trust build` (compileAndLink), и trust-lsp
//    --emit-build-dir.
bool writeBuildFiles(const PipelineOpts& opts, const std::filesystem::path& cppt_path, const std::vector<std::filesystem::path>& module_cppt_paths,
                     const std::vector<std::string>& runtime_headers, const std::vector<std::string>& link_libs, const std::string& entry_func_name) {
    namespace fs = std::filesystem;
    fs::path build_dir = cppt_path.parent_path();
    fs::path basename = cppt_path.stem();
    fs::path main_cppt_path; // path to entry point file, if any

    // ── Generate entry point file for executable mode ──
    if (opts.compile_mode == CompileMode::Executable) {
        main_cppt_path = build_dir / (basename.string() + "_main.cppt");
        std::string main_func_name = entry_func_name;
        std::ofstream main_ofs(main_cppt_path);
        if (!main_ofs) {
            trust::errs() << "error: failed to create entry file: " << main_cppt_path << "\n";
            return false;
        }
        main_ofs << "// This file was generated automatically by TrustLang " TRUST_VERSION_FULL " on " << currentTimestamp() << "\n"
                 << "// Generated entry point by trust pipeline\n"
                 << "// Module: " << cppt_path.filename().string() << "\n\n"
                 << "extern int " << main_func_name << "();\n\n"
                 << "int main() {\n"
                 << "    return " << main_func_name << "();\n"
                 << "}\n";
        if (opts.verbose) {
            trust::errs() << "info: generated entry file: " << main_cppt_path << "\n";
        }
    }

    {
        std::ofstream mf(build_dir / "Makefile");
        if (!mf) {
            trust::errs() << "error: failed to create Makefile in " << build_dir << "\n";
            return false;
        }
        mf << "# Generated by trust pipeline — do not edit manually\n";
        mf << "# To rebuild: make -C " << build_dir << "\n\n";
        mf << trust::build::kMakefileBuild;
    }

    // ── build.conf: собираем содержимое один раз и пишем одним блоком. ──
    std::string conf;
    conf += "# build.conf — generated by trust pipeline\n";
    conf += "# Platform-specific build configuration.\n";
    conf += "# Override any variable below by editing this file or passing\n";
    conf += "# via environment variables (e.g., CXX=clang++ make ...).\n";
    conf += "# NOTE: No leading whitespace — Makefile's include requires it.\n\n";
    conf += "# Build metadata\n";
    conf += "# Date:       " + currentTimestamp() + "\n";
    conf += "# Version:    " + std::string(TRUST_VERSION) + "\n";
    conf += "# Source:     " + cppt_path.filename().string() + "\n";
    conf += "# Source hash:" + fileHash(cppt_path) + "\n\n";
    conf += "# Source files\n";
    conf += "SRC       := " + cppt_path.filename().string() + "\n";
    if (!main_cppt_path.empty()) {
        conf += "SRC_MAIN  := " + main_cppt_path.filename().string() + "\n";
    }
    if (!module_cppt_paths.empty()) {
        conf += "SRC_MODULES :=";
        for (const auto& mp : module_cppt_paths) {
            conf += " " + mp.filename().string();
        }
        conf += "\n";
    }
    conf += "\n";
    conf += "# Compiler and linker\n";
    // Компилятор НЕ запекаем (единый переносимый build.conf, без путей): берётся из
    // Makefile (`CXX ?= c++`) либо переопределяется пользователем/окружением. Локальная
    // сборка `trust build` передаёт компилятор через командную строку make (см. compileAndLink).
    conf += "AR        := ar\n";
    conf += "RM        := rm -f\n\n";
    conf += "# Compiler and linker flags\n";
    // Единый переносимый build.conf: без абсолютных путей. trust/ рантайм-заголовки
    // извлекаются в каталог сборки, поэтому `-I.` достаточно для `#include \"trust/...\"`
    // при make -C <build_dir> — в любом режиме (локальная сборка и скачиваемый архив)
    // build-файлы каталога одинаковы.
    conf += "CXXFLAGS += -I.\n";
    conf += "ARFLAGS   := rcs\n\n";
    if (!opts.compiler_options.empty()) {
        conf += "# User-supplied options\n";
        conf += "CXXFLAGS += " + opts.compiler_options + "\n";
        conf += "LDFLAGS  += " + opts.compiler_options + "\n";
    }

    // ── Runtime-backed types (Rational и т.п.) ──
    // Транслятор уже вписал `#include \"<path>\"` в сгенерированный код; здесь
    // извлекаем эти заголовки из рантайм-библиотеки (.so либо .a) в <build_dir>/trust/
    // и добавляем в build.conf include-путь + линковку рантайм-библиотеки. Только те
    // заголовки, что реально использованы (не вся библиотека).
    if (!runtime_headers.empty()) {
        for (const auto& hdr : runtime_headers) {
            if (!extractRuntimeHeader(hdr, build_dir, opts.runtime_link)) {
                return false;
            }
        }
        conf += "\n# Runtime-backed types — link trust-runtime library\n";
        // Единая переносимая форма: библиотеку предоставляет TrustLang toolchain
        // пользователя (локальная сборка резолвит её через LIBRARY_PATH, см. compileAndLink);
        // не запекаем абсолютный путь и не привязываем к .so/.a.
        conf += "LIBS     += -ltrust-runtime -lgmp\n";
    }

    // ── Нативные библиотеки (@[link(\"имя\")]) ──
    // Флаги линковки (-l<имя>) добавляются в LIBS только для целей с шагом линковки
    // (executable / shared-lib / trust-module). Существование библиотеки/символа НЕ
    // проверяется — ответственность линковщика.
    const bool linksAtLinkStep =
        opts.compile_mode == CompileMode::Executable || opts.compile_mode == CompileMode::SharedLib || opts.compile_mode == CompileMode::TrustModule;
    if (!link_libs.empty() && linksAtLinkStep) {
        conf += "\n# Native libraries (@[link(...)])\n";
        conf += "LIBS     += ";
        for (std::size_t i = 0; i < link_libs.size(); ++i) {
            if (i > 0) {
                conf += " ";
            }
            conf += link_libs[i];
        }
        conf += "\n";
    }

    {
        std::ofstream cf(build_dir / "build.conf");
        if (!cf) {
            trust::errs() << "error: failed to create build.conf in " << build_dir << "\n";
            return false;
        }
        cf << conf;
    }

    return true;
}

static bool compileAndLink(const PipelineOpts& opts, const std::filesystem::path& cppt_path, const std::vector<std::filesystem::path>& module_cppt_paths,

                           const std::vector<std::string>& runtime_headers, const std::vector<std::string>& link_libs, const std::string& entry_func_name) {
    namespace fs = std::filesystem;
    fs::path build_dir = cppt_path.parent_path();
    fs::path basename = cppt_path.stem();
    // build-файлы (Makefile, build.conf, _main.cppt, LICENSE, trust/) — единая функция,
    // используется и trust-lsp --emit-build-dir (build.conf одинаковый).
    if (!writeBuildFiles(opts, cppt_path, module_cppt_paths, runtime_headers, link_libs, entry_func_name)) {
        return false;
    }
    // Единый переносимый build.conf линкует `-ltrust-runtime`. В локальной сборке runtime
    // называется trust-runtime.a/.so (без lib-префикса). Во временном каталоге создаём
    // lib-симлинк нужного типа и добавляем каталог в LIBRARY_PATH/LD_LIBRARY_PATH для
    // субпроцесса make. Временный каталог не попадает в build-каталог/архив (RAII-чистка).
    struct RuntimeLibTmpGuard {
        std::filesystem::path dir;
        ~RuntimeLibTmpGuard() {
            if (!dir.empty()) {
                std::error_code e_;
                std::filesystem::remove_all(dir, e_);
            }
        }
    } rt_guard;
    if (!runtime_headers.empty()) {
        const fs::path rt = locateRuntimeLibrary(opts.runtime_link);
        if (!rt.empty()) {
            char tmpl[] = "/tmp/trust-runtime-XXXXXX";
            char* tmpdir = ::mkdtemp(tmpl);
            if (tmpdir != nullptr) {
                const fs::path d(tmpdir);
                rt_guard.dir = d;
                std::error_code lec;
                fs::create_symlink(rt, d / ("lib" + rt.filename().string()), lec);
                const std::string libdir = d.string();
                if (const char* lp = std::getenv("LIBRARY_PATH")) {
                    std::string v = libdir + ":" + lp;
                    ::setenv("LIBRARY_PATH", v.c_str(), 1);
                } else {
                    ::setenv("LIBRARY_PATH", libdir.c_str(), 1);
                }
                // LD_LIBRARY_PATH — фактический каталог runtime (для shared-бинарника при --run).
                const std::string rt_dir = rt.parent_path().string();
                if (const char* llp = std::getenv("LD_LIBRARY_PATH")) {
                    std::string v = rt_dir + ":" + llp;
                    ::setenv("LD_LIBRARY_PATH", v.c_str(), 1);
                } else {
                    ::setenv("LD_LIBRARY_PATH", rt_dir.c_str(), 1);
                }
            }
        }
    }

    {
        std::string target = makeTargetForMode(opts.compile_mode);
        std::string cmd = "make -C " + build_dir.string() + " -f Makefile " + target;
        // Компилятор передаём на командной строке make (build.conf единый, без путей).
        if (!opts.compiler.empty()) {
            cmd += " CXX=" + opts.compiler + " LD=" + opts.compiler;
        }
        if (opts.verbose) {
            trust::errs() << "info: running: " << cmd << "\n";
        }
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            trust::errs() << "error: make " << target << " failed (exit code " << ret << ")\n";
            return false;
        }
        if (opts.verbose) {
            trust::errs() << "info: make " << target << " succeeded\n";
        }
    }

    if (!opts.output_file.empty()) {
        fs::path output_path(opts.output_file);
        fs::path default_output;
        switch (opts.compile_mode) {
        case CompileMode::ObjectFile:
            default_output = build_dir / (basename.string() + ".o");
            break;
        case CompileMode::StaticLib:
            default_output = build_dir / (basename.string() + ".a");
            break;
        case CompileMode::SharedLib:
            default_output = build_dir / (basename.string() + ".so");
            break;
        case CompileMode::TrustModule:
            default_output = build_dir / (basename.string() + ".so");
            break;
        case CompileMode::Executable:
            default_output = build_dir / basename;
            break;
        }
        if (output_path != default_output) {
            std::error_code ec;
            fs::copy_file(default_output, output_path, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                trust::errs() << "error: failed to copy artifact to " << output_path << ": " << ec.message() << "\n";
                return false;
            }
            if (opts.verbose) {
                trust::errs() << "info: artifact copied to " << output_path << "\n";
            }
        }
    }
    return true;
}

// ── Free function: computeBuildDir ──

std::filesystem::path computeBuildDir(const PipelineOpts& opts) {
    namespace fs = std::filesystem;
    if (!opts.temp_dir.empty()) {
        return fs::path(opts.temp_dir);
    }
    if (opts.run) {
        // --run без --temp-dir: выходные файлы — в локальный временный каталог .trust ТЕКУЩЕГО
        // каталога, в ПОДКАТАЛОГЕ с именем компилируемого файла (<cwd>/.trust/<stem>). При запуске
        // хешбанга cwd == каталог исходника, поэтому файлы — в локальном <src_dir>/.trust/.
        // Общий каталог <cwd>/.trust/<stem> для программ с одинаковым stem из разных каталогов
        // сохраняет сверку кеша --run по относительному пути (A1, см. run_cache_same_stem).
        fs::path base = utils::resolveTempDir(".", /*create=*/true);
        fs::path dir = base / fs::path(opts.input_file).stem();
        std::error_code ec;
        fs::create_directories(dir, ec);
        return dir;
    }
    fs::path src_path(opts.input_file);
    fs::path parent = src_path.parent_path();
    return parent.empty() ? fs::path(".") : parent;
}

// ── Free function: computeCpptPath ──

std::filesystem::path computeCpptPath(const PipelineOpts& opts) {
    namespace fs = std::filesystem;
    fs::path src_path(opts.input_file);
    fs::path stem = src_path.stem();
    return computeBuildDir(opts) / (stem.string() + ".cppt");
}

// ── Helper: read a small text file into a string (for #embed) ──

static std::string readFileContents(const std::filesystem::path& path) {
    auto content = trust::utils::FileIO::read<std::string>(path.string());
    return content ? *content : std::string{};
}

// ── Запуск собранного исполняемого файла; возвращает его код возврата. ──

static int runExecutable(const std::filesystem::path& exe) {
    int rc = std::system(exe.string().c_str());
#ifndef _WIN32
    rc = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : rc;
#endif
    return rc;
}

// ── Путь исполняемого файла для --run: -o, иначе <build_dir>/<stem>. ──

static std::filesystem::path runExecutablePath(const PipelineOpts& opts, const std::filesystem::path& cpptPath) {
    if (!opts.output_file.empty()) {
        return std::filesystem::path(opts.output_file);
    }
    return cpptPath.parent_path() / cpptPath.stem();
}

// ── Запись кеша --run ──
// Первая строка записи ВСЕГДА версия компилятора `trust-lang\t<TRUST_VERSION_FULL>`; далее идёт
// главный файл, затем импортированные модули — по строке "файл\tmd5\n" на каждый.
// Запись используется для инвалидации без парсинга: имена и хеши всех файлов программы известны
// из встроенной записи, проверка лишь пере-хеширует перечисленные файлы.
// Строка версии гарантирует перекомпиляцию при смене версии компилятора (не только при
// изменении исходника) и делает невалидными записи старого формата/чужого бинарника.
// Пути в записи ОТНОСИТЕЛЬНЫЕ — от текущего каталога (CWD), откуда запускается `--run`.
// Для обычного запуска `trust --run prog.src` из каталога исходников в записи будет просто
// `prog.src`/`mymod.src` (без каталога-перехода); запись переносима между каталогами.

static constexpr const char* kTrustVersionPrefix = "trust-lang\t";

static std::string buildProgramRecord(const std::filesystem::path& mainFile, Context& ctx, std::size_t mainIdx) {
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

// ── Проверка кеша --run: читает запись из ELF-секции `.debug_trust_hash` и сверяет. ──
// Строка №0 — версия компилятора: если не совпадает с текущей `TRUST_VERSION_FULL` — кеш
// невалиден (пересборка). Это покрывает и смену версии компилятора, и старые/чужие бинарники
// (их первая строка не совпадёт). Строки ≥1 — "файл\tmd5"; если все перечисленные файлы не
// изменились и запись не пуста — кеш валиден (запускать без перекомпиляции).
// dlopen для PIE не работает, поэтому читаем секцию напрямую из файла через utils::elf::readElfSection.

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
            // Строка №0 — версия компилятора, которой собран бинарник. Не совпала с текущей
            // (другая версия/формат) — кеш не применяем, требуется пересборка.
            versionSeen = true;
            if (line != std::string(kTrustVersionPrefix) + TRUST_VERSION_FULL) {
                return false;
            }
            continue;
        }
        // Первая строка файла — главный файл. A1: запись построена для ДРУГОГО главного файла
        // (тот же stem, другой каталог) — кеш не применяем, иначе --run запустит устаревший
        // бинарь чужой программы. Последующие строки файлов (модули) — только сверка хеша.
        if (isMain) {
            isMain = false;
            if (line.substr(0, tab) != rel(expectedMain)) {
                return false;
            }
        }
        // Пути в записи относительны CWD (см. buildProgramRecord) — fileHash резолвит их оттуда.
        if (fileHash(std::filesystem::path(line.substr(0, tab))) != line.substr(tab + 1)) {
            return false;
        }
        any = true;
    }
    return any;
}

// ── Запуск исполняемого файла --run: путь -o, иначе <build_dir>/<stem>. ──

static int runBuiltExecutable(const PipelineOpts& opts, const std::filesystem::path& cpptPath) {
    return runExecutable(runExecutablePath(opts, cpptPath));
}

// ── --run: если кеш по md5 исходников валиден и exe существует — запустить без перекомпиляции. ──
// Возвращает код возврата программы, если кеш применим (запуск выполнен); иначе nullopt →
// требуется перекомпиляция. Проверяет и соответствие главного файла записи кеша (A1).

static std::optional<int> tryRunCached(const PipelineOpts& opts) {
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

// ── Извлечение рантайм-заголовка из рантайм-библиотеки ──
// Заголовок хранится в ELF-секции, названной его путём (например
// "trust/rational.hpp"), внутри рантайм-библиотеки: .so (одиночный ELF) либо .a
// (ar-архив, у членов которого секция ищется отдельно). Содержимое записывается
// в <build_dir>/<path>, чтобы Makefile мог найти его по `#include "<path>"`.
static bool extractRuntimeHeader(const std::string& headerPath, const std::filesystem::path& buildDir, RuntimeLink link) {
    namespace fs = std::filesystem;
    const fs::path lib_path = locateRuntimeLibrary(link);
    if (lib_path.empty()) {
        trust::errs() << "error: cannot locate trust-runtime library (" << runtimeLibraryFileName(link).string()
                      << "; set LD_LIBRARY_PATH or run from the build directory)\n";
        return false;
    }
    auto section = trust::utils::readSectionFromLibrary(lib_path.string(), headerPath);
    if (!section) {
        trust::errs() << "error: runtime header '" << headerPath << "' not found in " << lib_path.string() << "\n";
        return false;
    }
    // Отбросить хвостовой '\0', добавленный при #embed.
    while (!section->empty() && section->back() == '\0') {
        section->pop_back();
    }

    fs::path outPath = buildDir / headerPath;
    std::error_code ec;
    fs::create_directories(outPath.parent_path(), ec);
    std::ofstream ofs(outPath, std::ios::binary);
    if (!ofs) {
        trust::errs() << "error: cannot write runtime header '" << outPath << "'\n";
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(section->data()), static_cast<std::streamsize>(section->size()));
    return static_cast<bool>(ofs);
}

// ── Встроенный trust/dsl.src: компилируется в бинарник через #embed ──
// Относительный путь от каталога исходника (src/pipeline/ → include/trust/).

static constexpr char kEmbeddedDslSrc[] = {
#embed "../../include/trust/dsl.src"
    , 0};

// ── Встроенная лицензия: текст LICENSE компилируется в бинарник через #embed ──
// Относительный путь от каталога исходника (src/pipeline/ → корень проекта). Служит
// источником для копии LICENSE в каталог сборки и для `#embed "LICENSE"` в .cppt.
static constexpr char kEmbeddedLicense[] = {
#embed "../../LICENSE"
    , 0};

// Встроенный текст лицензии без хвостового NUL, добавленного при #embed.
static std::string embeddedLicenseText() {
    return std::string(kEmbeddedLicense, sizeof(kEmbeddedLicense) - 1);
}

// ── Free function: saveCppAndEmbedSourceMap ──

// Экранирование для строкового литерала C++: переводы строк, табуляции, кавычки, слэши.
static std::string cppStringEscape(const std::string& s) {
    std::string out;
    for (const char ch : s) {
        if (ch == '\n') {
            out += "\\n";
        } else if (ch == '\r') {
            out += "\\r";
        } else if (ch == '\t') {
            out += "\\t";
        } else {
            if (ch == '"' || ch == '\\') {
                out += '\\';
            }
            out += ch;
        }
    }
    return out;
}

bool saveCppAndEmbedSourceMap(Context& ctx, MapperFile cpp_idx, const std::filesystem::path& cppt_path, bool verbose,
                              const std::vector<CppTranspiler::ExportEntry>& exports, bool embed_export_table, const std::string& program_record) {
    namespace fs = std::filesystem;
    {
        // ── Шапка автогенерируемого файла (1-я строка) ──
        // Первая строка: сообщение о том, что файл автогенерируемый, название проекта,
        // полная версия компилятора и дата/время генерации. Кладётся в leading-префикс
        // выходного буфера (первой строкой, до инклудов), а source-map учитывает его
        // через prepend-смещение (toReader → prependSizes).
        // Текст LICENSE в выходной файл НЕ встраивается — лицензия просто копируется
        // в каталог сборки (ниже), рядом с Makefile/build.conf.
        std::string prefix;
        prefix += "// This file was generated automatically by TrustLang " TRUST_VERSION_FULL " on " + currentTimestamp() + "\n\n";
        ctx.source().output_prepend_leading(cpp_idx, prefix);

        std::string cpp_content = ctx.source().output_result(cpp_idx);

        // ── Копия LICENSE в каталог сборки (рядом с Makefile/build.conf). ──
        const std::string license_text = embeddedLicenseText();
        if (!license_text.empty()) {
            const fs::path license_path = cppt_path.parent_path() / "LICENSE";
            std::ofstream lf(license_path, std::ios::binary);
            if (lf) {
                lf.write(license_text.data(), static_cast<std::streamsize>(license_text.size()));
            } else if (verbose) {
                trust::errs() << "warning: failed to write LICENSE to " << license_path << "\n";
            }
        }

        if (embed_export_table) {
            cpp_content += "\n// Exported symbols for dynamic loading\n";
            std::string module_api_content = readFileContents(PROJECT_INCLUDE_DIR "/runtime/module_api.h");
            if (!module_api_content.empty()) {
                static const std::string pragma_once = "#pragma once\n";
                auto pos = module_api_content.find(pragma_once);
                if (pos != std::string::npos) {
                    module_api_content.erase(pos, pragma_once.length());
                }
                cpp_content += "// Embedded from include/runtime/module_api.h\n";
                cpp_content += module_api_content;
                cpp_content += "\n";
            } else {
                cpp_content += "struct __trust_export_entry {\n"
                               "    const char* name;\n"
                               "    void* addr;\n"
                               "};\n"
                               "struct __trust_exports {\n"
                               "    int count;\n"
                               "    const char* version;\n"
                               "    const __trust_export_entry* entries;\n"
                               "    const char* decls;\n"
                               "    const char* srcHash;\n"
                               "};\n\n";
            }
            // Строка-перечисление экспортируемых ПРЕДВАРИТЕЛЬНЫХ ОБЪЯВЛЕНИЙ в Trust-синтаксисе
            // (разделитель '\n') — реальные семантические конструкции, пригодные для парсинга
            // при загрузке модуля как бинарного файла (напр. "x:Int32 := ...;\n").
            std::string decls;
            for (const auto& entry : exports) {
                if (entry.fwdDecl.empty()) {
                    continue;
                }
                decls += entry.fwdDecl;
                decls += '\n';
            }
            cpp_content += "static const __trust_export_entry __trust_export_entries[] = {\n";
            for (const auto& entry : exports) {
                cpp_content += std::format("    {{ \"{}\", reinterpret_cast<void*>(&::{}) }},\n", entry.trustName, entry.cppName);
            }
            cpp_content += "};\n\n";
            cpp_content += "static const char __trust_export_decls[] = \"";
            cpp_content += cppStringEscape(decls);
            cpp_content += "\";\n\n";
            // Запись кеша --run: первая строка — версия компилятора "trust-lang\t<TRUST_VERSION_FULL>",
            // далее список "файл\tmd5\n" (главный файл, затем модули) — в отдельной ELF-секции
            // .debug_trust_hash. Читается через utils::elf::readElfSection — dlopen не
            // работает для PIE-исполняемых. Та же строка доступна и как __trust_exports.srcHash.
            cpp_content +=
                "static const char kTrustSrcHash[] __attribute__((section(\".debug_trust_hash\"), used)) = \"" + cppStringEscape(program_record) + "\";\n\n";
            cpp_content += "extern \"C\" __trust_exports __trust_get_exports(void)\n"
                           "    __attribute__((visibility(\"default\")));\n"
                           "extern \"C\" __trust_exports __trust_get_exports(void) {\n"
                           "    __trust_exports result;\n"
                           "    result.count = static_cast<int>("
                           "sizeof(__trust_export_entries) / sizeof(__trust_export_entries[0]));\n"
                           "    result.version = \"" TRUST_VERSION_FULL "\";\n"
                           "    result.entries = __trust_export_entries;\n"
                           "    result.decls = __trust_export_decls;\n"
                           "    result.srcHash = kTrustSrcHash;\n"
                           "    return result;\n"
                           "}\n\n";
        }
        std::ofstream ofs(cppt_path);
        if (!ofs) {
            trust::errs() << "error: failed to write output file: " << cppt_path << "\n";
            return false;
        }
        ofs << cpp_content;
    }

    fs::path map_path = cppt_path;
    map_path.replace_extension(".src_map");
    {
        auto* reader = ctx.source().toReader();
        if (reader) {
            auto msgpack_data = reader->packToMsgpack();
            std::ofstream ofs(map_path, std::ios::binary);
            if (ofs) {
                ofs.write(reinterpret_cast<const char*>(msgpack_data.data()), static_cast<std::streamsize>(msgpack_data.size()));
                if (verbose) {
                    trust::errs() << "info: source map saved to " << map_path << "\n";
                }
            } else {
                trust::errs() << "warning: failed to write source map: " << map_path << "\n";
            }
        }
    }

    if (fs::exists(map_path)) {
        std::ofstream ofs(cppt_path, std::ios::app);
        if (ofs) {
            ofs << "\n// Embedded source map (trust → cpp mapping)\n"
                << "static const unsigned char __debug_trust_source_map[]\n"
                << "    __attribute__((section(\".debug_trust_map\"), used)) = {\n"
                << "    #embed \"" << map_path.filename().string() << "\"\n"
                << "};\n";
            if (verbose) {
                trust::errs() << "info: source map embedded into " << cppt_path.filename() << "\n";
            }
        }
    }
    return true;
}

// ── determineSteps: EmitFlags → PipelineSteps ──
// Transpile включён в битмаску для Cpp-режима.

PipelineSteps Pipeline::determineSteps(EmitFlags flags) {
    if ((flags & EmitFlags::Cpp) != EmitFlags::None) {
        return PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile;
    }
    if ((flags & EmitFlags::AST) != EmitFlags::None) {
        return PipelineSteps::ParseAST;
    }
    if ((flags & EmitFlags::Tokens) != EmitFlags::None) {
        return PipelineSteps::ParseAST;
    }
    return PipelineSteps::None;
}

// ── Pipeline constructor ──

Pipeline::Pipeline(Context& ctx, const PipelineOpts& opts)
: m_ctx(ctx)
, m_opts(opts) {
    m_ctx.diag().setMinSeverity(opts.quiet ? Severity::Error : Severity::Remark);
    // Pipeline владеет ModuleLoader и TypeRegistry и внедряет их в Context
    // (невладеющие указатели), чтобы diag не зависел от module_loader и types.
    m_loader = std::make_unique<ModuleLoader>(m_ctx);
    m_ctx.setLoader(m_loader.get());
    m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
    m_ctx.setTypes(m_types.get());
}

// ── Pipeline::loadDslMacros ──
// By default the embedded trust/dsl.src is loaded into m_ctx. --dsl <file>
// replaces it, --no-dsl disables loading entirely. The Macro is owned by
// m_ctx and inherited by every (nested) Parser via Context.

void Pipeline::loadDslMacros() {
    if (m_opts.no_dsl || m_ctx.macro()) {
        return;
    }

    std::string source;
    if (!m_opts.dsl_file.empty()) {
        auto content = trust::utils::FileIO::read<std::string>(m_opts.dsl_file);
        if (!content) {
            FAULT("Failed to open DSL file '{}'", m_opts.dsl_file);
        }
        source = std::move(*content);
    } else {
        source.assign(kEmbeddedDslSrc, sizeof(kEmbeddedDslSrc) - 1);
    }

    auto macro = std::make_shared<Macro>(m_ctx);
    m_ctx.setMacro(macro);
    Parser parser(m_ctx);
    // Встроенный DSL — «фиктивный» in-memory источник под именем "@dsl"
    // (префикс '@' = файла на диске нет, readFilesFromDisk его пропускает).
    // Содержимое "@dsl" — это trust/dsl.src; LSP сохраняет его на диск как
    // <tempDir>/trust/dsl.src, чтобы ссылки на определения макросов были
    // навигируемы (см. lsp/NAVIGATION.md).
    TermPtr term = parser.ParseText(source, "@dsl");
    if (!term || m_ctx.diag().errorCount() > 0) {
        FAULT("Failed to parse DSL source");
    }
}

// ── runPipeline (без Transpile) ──

PipelineResult Pipeline::runPipeline(PipelineSteps steps, MapperFile inputFile) {
    PipelineResult result;

    if (hasStep(steps, PipelineSteps::ParseAST)) {
        // Read source and register main file as a module.
        // parseSourceModule recursively parses the file (expand_module=true)
        // and stores the result in the registry.
        loadDslMacros();
        std::string moduleName = std::string(m_ctx.source().filename(inputFile));
        std::size_t idx = m_ctx.loader().parseSourceModule(moduleName, inputFile);

        // Root node of the program is a ModuleNode wrapping the module body.
        auto modTerm = Term::Create(TermID::MODULE, moduleName);
        auto mn = std::make_shared<ModuleNode>(idx, std::move(modTerm));
        convertModuleBody(m_ctx, m_ctx.loader().body(idx), mn->m_body);
        std::vector<AstNodePtr> astNodes;
        astNodes.push_back(std::move(mn));
        result.astNodes = std::move(astNodes);
    }

    // Если конвертация Term→AST дала ошибки (например нереализованная конструкция:
    // await/yield/when/filling) — семантический анализ и транспиляция на неполном/повреждённом
    // AST не запускаются (могут упасть на незаполненных детях). astNodes структурно построены;
    // факт ошибки виден вызывающему по diag().errorCount()>0.
    if (hasStep(steps, PipelineSteps::ParseAST) && m_ctx.diag().errorCount() > 0 && !m_opts.allow_semantic_on_errors) {
        return result;
    }

    if (hasStep(steps, PipelineSteps::Semantic)) {
        EXPECT(result.astNodes.has_value() && "runAst must produce astNodes");
        SemanticPassRunner runner(m_ctx);
        bool ok = runner.run(*result.astNodes);
        // Сбор символов для LSP — выполняется даже при ошибках (частичный AST).
        if (m_ctx.opts().is_enabled(FlagKind::Symbols)) {
            result.symbols = runner.takeSymbolIndex();
            // Макроопределения, записанные во время парсинга (не теряются после PopScope модуля).
            appendMacroSymbols(m_ctx, *result.symbols);
        }
        if (!ok) {
            return result;
        }
    }

    // Transpile без cppOut — FAULT
    if (hasStep(steps, PipelineSteps::Transpile)) {
        FAULT("runPipeline without cppOut called with Transpile step");
    }

    return result;
}

// ── runPipeline (с Transpile) ──

namespace {

/// Рекурсивно заполняет экспорт-интерфейс всех сайтов импорта (`ModuleNode::isImport()`):
/// связывает индекс модуля через loader, сохраняет «полный» экспорт в реестре и кладёт
/// отфильтрованный (по маскам `\module(mod, masks)`) список экспортов в узел. Это шаг
/// «анализатора», выполняемый в конвейере после построения AST.
void resolveImportExports(Context& ctx, const std::vector<AstNodePtr>& astNodes) {
    for (const auto& node : astNodes) {
        if (!node) {
            continue;
        }
        if (node->kind() == ParserToken::Kind::ModuleDecl) {
            const auto& mn = static_cast<const ModuleNode&>(*node);
            if (mn.isImport()) {
                if (auto idx = ctx.loader().indexOf(mn.moduleId()); idx) {
                    const_cast<ModuleNode&>(mn).setModuleIndex(*idx);
                    const auto& body = mn.m_body;
                    std::vector<TermPtr> full = collectExportedDecls(body, "");
                    ctx.loader().setInterface(*idx, full);
                    const_cast<ModuleNode&>(mn).setExports(collectExportedDecls(body, mn.importMasks()));
                } else {
                    ctx.diag().report(Severity::Error, mn.range(), "Module '{}' is not loaded", mn.moduleId());
                }
            }
        }
        // Обход детей (в т.ч. тела импортированного модуля — там могут быть вложенные импорты).
        for (const auto& child : node->children()) {
            if (child) {
                std::vector<AstNodePtr> one{child};
                resolveImportExports(ctx, one);
            }
        }
    }
}

} // namespace

PipelineResult Pipeline::runPipeline(PipelineSteps steps, MapperFile inputFile, MapperFile cppOut, std::vector<CppTranspiler::ExportEntry>* out_exports,
                                     std::vector<std::string>* out_runtime_headers, std::vector<std::string>* out_link_libs) {
    PipelineResult result;

    if (hasStep(steps, PipelineSteps::ParseAST)) {
        // Read source and register main file as a module.
        // parseSourceModule recursively parses the file (expand_module=true)
        // and stores the result in the registry.
        loadDslMacros();
        std::string moduleName = std::string(m_ctx.source().filename(inputFile));
        // Главный файл программы — от него отсчитывается имя модуля (@__MODULE_NAME__)
        // и имя entry-функции. Должен быть установлен до парсинга (parseSourceModule).
        m_ctx.source().setMainModuleFile(inputFile);
        std::size_t idx = m_ctx.loader().parseSourceModule(moduleName, inputFile);
        m_mainModuleIndex = idx;

        // Root node of the program is a ModuleNode wrapping the module body.
        auto modTerm = Term::Create(TermID::MODULE, moduleName);
        auto mn = std::make_shared<ModuleNode>(idx, std::move(modTerm));
        convertModuleBody(m_ctx, m_ctx.loader().body(idx), mn->m_body);
        std::vector<AstNodePtr> astNodes;
        astNodes.push_back(std::move(mn));
        result.astNodes = std::move(astNodes);

        // Заполнить экспорт-интерфейс сайтов импорта (анализатор).
        resolveImportExports(m_ctx, *result.astNodes);
    }

    // Если конвертация Term→AST дала ошибки (например нереализованная конструкция:
    // await/yield/when/filling) — семантический анализ и транспиляция на неполном/повреждённом
    // AST не запускаются (могут упасть на незаполненных детях). astNodes структурно построены;
    // факт ошибки виден вызывающему по diag().errorCount()>0.
    if (hasStep(steps, PipelineSteps::ParseAST) && m_ctx.diag().errorCount() > 0 && !m_opts.allow_semantic_on_errors) {
        return result;
    }

    // Семантический анализ. Runner живёт до конца функции, чтобы разрешённая семантикой
    // таблица символов (TypeId) была доступна кодогенерации (проброс в CppTranspiler).
    SemanticPassRunner runner(m_ctx);
    if (hasStep(steps, PipelineSteps::Semantic)) {
        EXPECT(result.astNodes.has_value() && "runAst must produce astNodes");
        bool ok = runner.run(*result.astNodes);
        // Сбор символов для LSP — выполняется даже при ошибках (частичный AST).
        if (m_ctx.opts().is_enabled(FlagKind::Symbols)) {
            result.symbols = runner.takeSymbolIndex();
            // Макроопределения, записанные во время парсинга (не теряются после PopScope модуля).
            appendMacroSymbols(m_ctx, *result.symbols);
        }
        if (!ok) {
            return result;
        }
    }

    if (hasStep(steps, PipelineSteps::Transpile)) {
        EXPECT(result.astNodes.has_value() && "runAst must produce astNodes");
        EXPECT(!cppOut.isInvalid() && "cppOut must be a valid output file");
        // Проброс разрешённых типов из семантики в кодогенерацию (единый TypeId с анализом).
        CppTranspiler transpiler(m_ctx, &runner.analysis().symbols());
        transpiler.generateToFile(*result.astNodes, cppOut);
        if (out_exports) {
            *out_exports = transpiler.exports();
        }
        if (out_runtime_headers) {
            const auto& hdrs = transpiler.runtimeHeaders();
            out_runtime_headers->assign(hdrs.begin(), hdrs.end());
        }
        if (out_link_libs) {
            const auto& libs = transpiler.linkLibs();
            out_link_libs->assign(libs.begin(), libs.end());
        }
    }

    return result;
}

std::unique_ptr<TypeRegistry> Pipeline::releaseTypes() {
    return std::move(m_types);
}

// ── emitOutput: вывод для emit-режимов ──

int Pipeline::emitOutput(const PipelineResult& result) {
    auto flags = m_opts.emit_flags;

    if ((flags & EmitFlags::Tokens) != EmitFlags::None) {
        EXPECT(result.astNodes.has_value() && "runAst must produce astNodes");
        for (const auto& nodePtr : *result.astNodes) {
            if (nodePtr) {
                trust::outs() << nodePtr->text() << "\t" << ParserToken::name(nodePtr->kind()) << "\n";
            }
        }
        return 0;
    }

    if ((flags & EmitFlags::AST) != EmitFlags::None) {
        EXPECT(result.astNodes.has_value() && "runAst must produce astNodes");
        for (const auto& nodePtr : *result.astNodes) {
            if (nodePtr) {
                trust::outs() << nodePtr->dump() << "\n";
            }
        }
        return 0;
    }

    FAULT("unreachable: emitOutput called for emit-flags without matching handler");
    return 1;
}

// ── runTranspileAndSave: общий helper для compile и emit-cpp ──

Pipeline::TranspileOutput Pipeline::runTranspileAndSave(MapperFile inputFile) {
    TranspileOutput out;
    out.cpptPath = computeCpptPath(m_opts);
    // Use the real .cppt basename so the source map's output filename can be
    // resolved back to the generated file (findFile / findCppToTrust).
    out.outputIdx = m_ctx.source().add_output(out.cpptPath.filename().string());
    if (out.outputIdx.isInvalid()) {
        trust::errs() << "error: failed to create output file entry\n";
        return out;
    }

    auto steps = PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile;
    auto result = runPipeline(steps, inputFile, out.outputIdx, &out.exports, &out.runtimeHeaders, &out.linkLibs);
    if (!result.isValid() || m_ctx.diag().errorCount() > 0) {
        return out;
    }

    if (!saveCppAndEmbedSourceMap(m_ctx, out.outputIdx, out.cpptPath, m_opts.verbose, out.exports, /*embed_export_table=*/true,
                                  buildProgramRecord(std::filesystem::path(m_opts.input_file), m_ctx, m_mainModuleIndex))) {
        return out;
    }

    if (m_opts.verbose) {
        trust::errs() << "info: generated " << out.cpptPath << "\n";
    }

    out.valid = true;
    return out;
}

// ── generateModuleOutputs / transpileModuleBody: отдельные .cppt исходных модулей ──

std::vector<std::filesystem::path> Pipeline::generateModuleOutputs(std::vector<std::string>* module_runtime_headers,
                                                                   std::vector<std::string>* module_link_libs) {
    namespace fs = std::filesystem;
    std::vector<fs::path> paths;
    const fs::path build_dir = computeBuildDir(m_opts);
    for (std::size_t idx = 0; idx < m_ctx.loader().moduleCount(); ++idx) {
        if (idx == m_mainModuleIndex) {
            continue; // главный файл генерируется как основной .cppt
        }
        if (!m_ctx.loader().isLoaded(idx)) {
            continue; // незагруженный (бинарный/заглушка) модуль не транслируем
        }
        const fs::path stem = fs::path(m_ctx.loader().moduleName(idx)).stem();
        const fs::path modPath = build_dir / (stem.string() + ".cppt");
        transpileModuleBody(idx, modPath, module_runtime_headers, module_link_libs);
        paths.push_back(std::move(modPath));
    }
    return paths;
}

void Pipeline::transpileModuleBody(std::size_t idx, const std::filesystem::path& cpptPath, std::vector<std::string>* runtime_headers,
                                   std::vector<std::string>* link_libs) {
    namespace fs = std::filesystem;
    MapperFile outputIdx = m_ctx.source().add_output(cpptPath.filename().string());
    if (outputIdx.isInvalid()) {
        return;
    }

    // Корневой узел модуля с полным телом (определения) — отдельная единица трансляции.
    auto modTerm = Term::Create(TermID::MODULE, m_ctx.loader().moduleName(idx));
    auto mn = std::make_shared<ModuleNode>(idx, std::move(modTerm));
    convertModuleBody(m_ctx, m_ctx.loader().body(idx), mn->m_body);
    std::vector<AstNodePtr> astNodes;
    astNodes.push_back(std::move(mn));
    resolveImportExports(m_ctx, astNodes); // вложенные импорты внутри модуля

    if (m_ctx.diag().errorCount() > 0) {
        return;
    }

    // Семантика + кодогенерация тела модуля (аналогично главному файлу).
    SemanticPassRunner runner(m_ctx);
    if (!runner.run(astNodes)) {
        return;
    }
    CppTranspiler transpiler(m_ctx, &runner.analysis().symbols());
    transpiler.generateToFile(astNodes, outputIdx);
    if (runtime_headers) {
        const auto& hdrs = transpiler.runtimeHeaders();
        runtime_headers->insert(runtime_headers->end(), hdrs.begin(), hdrs.end());
    }
    if (link_libs) {
        const auto& libs = transpiler.linkLibs();
        link_libs->insert(link_libs->end(), libs.begin(), libs.end());
    }
    if (m_ctx.diag().errorCount() > 0) {
        return;
    }
    // Модуль-исходник линкуется в программу: экспорт-таблица принадлежит главному файлу.
    saveCppAndEmbedSourceMap(m_ctx, outputIdx, cpptPath, m_opts.verbose, transpiler.exports(), /*embed_export_table=*/false);
}

// ── --module-info: show module exports and version ──

static int showModuleInfo(const std::string& module_path, bool verbose) {
    void* handle = dlopen(module_path.c_str(), RTLD_LAZY);
    if (!handle) {
        trust::errs() << "error: failed to load module '" << module_path << "': " << dlerror() << "\n";
        return 1;
    }
    dlerror();
    using GetExportsFn = __trust_exports (*)();
    auto get_exports = reinterpret_cast<GetExportsFn>(dlsym(handle, "__trust_get_exports"));
    const char* dlsym_error = dlerror();
    if (dlsym_error) {
        trust::errs() << "error: __trust_get_exports not found in '" << module_path << "': " << dlsym_error << "\n";
        dlclose(handle);
        return 1;
    }
    __trust_exports exports = get_exports();
    trust::outs() << "version: " << (exports.version ? exports.version : "(null)") << "\n";
    trust::outs() << "src-md5: " << (exports.srcHash ? exports.srcHash : "(null)") << "\n";
    trust::outs() << "exports: " << exports.count << "\n";
    for (int i = 0; i < exports.count; ++i) {
        trust::outs() << "  " << (exports.entries[i].name ? exports.entries[i].name : "(null)") << "\n";
    }
    if (verbose) {
        trust::errs() << "info: module loaded successfully from " << module_path << "\n";
    }
    dlclose(handle);
    return 0;
}

// ── Execute: полный цикл для CLI ──

int Pipeline::execute() {
    // ── 1. Special modes (module-info) ──
    if (m_opts.module_info_requested) {
        return showModuleInfo(m_opts.input_file, m_opts.verbose);
    }

    // ── 2. Load input file ──
    if (!std::filesystem::exists(m_opts.input_file)) {
        trust::errs() << "error: input file not found: " << m_opts.input_file << "\n";
        return 1;
    }

    MapperFile inputFile = m_ctx.source().load_file(m_opts.input_file);
    if (m_opts.verbose) {
        trust::errs() << "info: loaded " << m_opts.input_file << "\n";
    }

    // ── 3. LexemesOnly — быстрый путь: только legacy лексер ──
    // Модули здесь не раскрываются: это режим вывода лексем, а не загрузки AST.
    if ((m_opts.emit_flags & EmitFlags::LexemesOnly) != EmitFlags::None) {
        loadDslMacros();
        // Файл уже загружен (inputFile) — парсим из реального источника.
        trust::Parser parser(m_ctx);
        trust::TermPtr term = parser.ParseWithSource(inputFile, /*expand_module=*/false);
        if (term) {
            // Walk the tree and print leaf terms
            std::function<void(const trust::TermPtr&)> dumpTerm = [&](const trust::TermPtr& t) {
                if (!t || t->getTermID() == trust::TermID::END) {
                    return;
                }
                trust::outs() << t->getText() << "\t" << trust::toString(t->getTermID()) << "\n";
                for (const auto& child : t->m_sequence) {
                    dumpTerm(child);
                }
                if (t->m_args) {
                    for (const auto& [_, arg] : *t->m_args) {
                        dumpTerm(arg);
                    }
                }
                if (t->m_left) {
                    dumpTerm(t->m_left);
                }
                if (t->m_right) {
                    dumpTerm(t->m_right);
                }
            };
            dumpTerm(term);
        }
        return 0;
    }

    // ── 4. Compile mode ──
    if (m_opts.should_compile()) {
        // --run: кеш по md5 исходника — если файл(ы) не менялись и exe существует,
        // НЕ перекомпилировать, а сразу запустить (md5 встроен в exe: __trust_exports.srcHash).
        if (auto cached_rc = tryRunCached(m_opts); cached_rc.has_value()) {
            return *cached_rc;
        }

        auto out = runTranspileAndSave(inputFile);
        if (!out.valid) {
            return 1;
        }
        // Исходные модули компилируются отдельными единицами и линкуются с главным файлом.
        std::vector<std::string> module_runtime_headers;
        std::vector<std::string> module_link_libs;
        auto moduleCppts = generateModuleOutputs(&module_runtime_headers, &module_link_libs);
        // Рантайм-заголовки главного файла + модулей — только реально использованные.
        std::vector<std::string> runtime_headers = out.runtimeHeaders;
        runtime_headers.insert(runtime_headers.end(), module_runtime_headers.begin(), module_runtime_headers.end());
        // Флаги линковки нативных библиотек главного файла + модулей.
        std::vector<std::string> link_libs = out.linkLibs;
        link_libs.insert(link_libs.end(), module_link_libs.begin(), module_link_libs.end());
        // Имя entry-функции совпадает с DSL-макросом `main`: <имя_модуля>__main__.
        std::string entry_func_name = m_ctx.source().moduleName(inputFile) + "__main__";
        // --emit-build-dir: только генерируем build-каталог (единый переносимый build.conf),
        // БЕЗ компиляции/линковки. Используется trust-lsp для скачиваемого архива.
        if (m_opts.emit_build_dir_only) {
            if (!writeBuildFiles(m_opts, out.cpptPath, moduleCppts, runtime_headers, link_libs, entry_func_name)) {
                return 1;
            }
            return 0;
        }
        if (!compileAndLink(m_opts, out.cpptPath, moduleCppts, runtime_headers, link_libs, entry_func_name)) {
            return 1;
        }
        // --run: запустить собранный исполняемый файл (md5 исходника встроен в srcHash).
        if (m_opts.run && m_opts.compile_mode == CompileMode::Executable) {
            return runBuiltExecutable(m_opts, out.cpptPath);
        }
        return 0;
    }

    // ── 5. Emit: Cpp mode (transpile + stdout) ──
    if ((m_opts.emit_flags & EmitFlags::Cpp) != EmitFlags::None) {
        auto out = runTranspileAndSave(inputFile);
        if (!out.valid) {
            return 1;
        }
        trust::outs() << m_ctx.source().output_result(out.outputIdx);
        return 0;
    }

    // ── 6. Other emit modes (Tokens, AST) ──
    {
        auto steps = determineSteps(m_opts.emit_flags);
        auto result = runPipeline(steps, inputFile);
        if (!result.isValid() && steps != PipelineSteps::None) {
            return 1;
        }
        return emitOutput(result);
    }
}

// ── createTarGz: упаковка каталога dir в tar.gz (через системный tar, без шелла —
//    безопасно от shell-инъекций по путям). Архив должен лежать ВНЕ dir. ──
static bool createTarGz(const std::filesystem::path& dir, const std::filesystem::path& archive, std::string& out_error) {
#ifndef _WIN32
    const pid_t pid = ::fork();
    if (pid < 0) {
        out_error = "fork failed";
        return false;
    }
    if (pid == 0) {
        // Тишина в дочернем процессе: stdout/stderr → /dev/null (tar пишет ошибки туда).
        const int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }
        ::execlp("tar", "tar", "czf", archive.string().c_str(), "--exclude=*.o", "--exclude=*.so", "--exclude=app", "-C", dir.string().c_str(), ".",
                 static_cast<char*>(nullptr));
        ::_exit(127);
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        out_error = "tar failed";
        return false;
    }
    return true;
#else
    (void)dir;
    (void)archive;
    out_error = "tar not supported on this platform";
    return false;
#endif
}

// ── emitBuildDirArchive: транспилирует trust_code и собирает tar.gz-архив build-каталога
//    (<emit_dir>/trust-lang-<версия>-generated.tar.gz) БЕЗ компиляции. Временные файлы
//    build-каталога удаляются (RAII). Возвращает путь к архиву; пусто при ошибке. ──
std::filesystem::path emitBuildDirArchive(const std::string& trust_code, const std::filesystem::path& emit_dir, std::string& out_error) {
    namespace fs = std::filesystem;
    out_error.clear();
    std::error_code ec;

    fs::create_directories(emit_dir, ec);
    // build-каталог (временный; удаляется после архивации).
    const fs::path work = emit_dir / "work";
    fs::create_directories(work, ec);

    // Исходник — во временный .src для пайплайна.
    const fs::path src = work / "program.src";
    {
        std::ofstream ofs(src, std::ios::binary);
        if (!ofs) {
            out_error = "cannot create source file in " + emit_dir.string();
            return {};
        }
        ofs << trust_code;
    }

    // RAII: удалить каталог сборки при выходе (в т.ч. по ошибке) — чистка временных файлов.
    struct WorkDirGuard {
        fs::path p;
        ~WorkDirGuard() {
            std::error_code ec_;
            fs::remove_all(p, ec_);
        }
    } work_guard{work};

    trust::PipelineOpts opts;
    opts.input_file = src.string();
    opts.temp_dir = work.string();
    opts.compile_mode = CompileMode::Executable;
    opts.emit_build_dir_only = true;

    trust::Context ctx;
    trust::Pipeline pipeline(ctx, opts);
    try {
        if (pipeline.execute() != 0) {
            out_error = "build-dir emission failed (trust pipeline error)";
            return {};
        }
    } catch (const std::exception& e) {
        out_error = std::string("build-dir emission failed: ") + e.what();
        return {};
    }

    // Архив: tar.gz ВНЕ work (иначе tar видит изменение каталога при чтении).
    const fs::path archive = emit_dir / ("trust-lang-" + std::string(TRUST_VERSION_FULL) + "-generated.tar.gz");
    fs::remove(archive, ec);
    if (!createTarGz(work, archive, out_error)) {
        return {};
    }
    if (!fs::is_regular_file(archive, ec) || ec) {
        out_error = "archive not produced";
        return {};
    }
    return archive;
}

} // namespace trust