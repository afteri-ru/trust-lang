// src/pipeline/build.cpp
// Генерация build-файлов (Makefile, build.conf, _main.cppt) и компиляция/линковка
// сгенерированного .cppt через make. Модуль BuildManager (декомпозиция pipeline.cpp).
#include "pipeline/build.hpp"
#include "pipeline/io.hpp"
#include "pipeline/runtime_locator.hpp"
#include "utils/io.hpp"
#include "utils/utils.hpp"
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace trust {

// -- Free function: compile .cppt → .o and link via Makefile --
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

// -- Build-заголовок главной функции main (общая для многофайлового `_main.cppt` и
//    однофайлового режима `-fsingle-file`, где она встраивается в тот же .cppt модуля).
//    Возвращает C++-код (инклюды + extern + int main), идущий ПОСЛЕ тела модуля.
//    Entry с параметрами (`@main(argv^: Dict, args^: Dict)`): обёртка принимает
//    (argc, argv), отделяет аргументы среды по префиксу "--trust:" через
//    trust::runtime::parseArgs и передаёт в entry словари (argv, args). Entry без
//    параметров - как раньше (`int main() { return <entry>(); }`), без аргументов.
std::string buildEntryMainSource(const PipelineOpts& opts, bool usesStackCheck, bool usesSync, const std::string& entry_func_name,
                                 const std::string& entry_params) {
    std::string s;
    // Контроль переполнения стека: TLS `info` (границы стека потока) определяется в ЕДИНСТВЕННОЙ
    // TU программы - здесь (как в оригинальном stack-check.h, где пользователь определял
    // `const thread_local trust::stack_check trust::stack_check::info;` в своём TU).
    if (usesStackCheck) {
        s += "#include \"trust/stack_check.hpp\"\n";
        s += "const thread_local trust::stack_check trust::stack_check::info;\n\n";
    }
    if (entry_params.empty()) {
        s += "extern int " + entry_func_name + "();\n\n";
        s += "int main() {\n";
        s += "    return " + entry_func_name + "();\n";
        s += "}\n";
    } else {
        s += "#include \"trust/args.hpp\"\n";
        if (usesSync) {
            s += "#include \"trust/trusted-cpp-sync.hpp\"\n";
        }
        s += "extern int " + entry_func_name + "(" + entry_params + ");\n\n";
        s += "int main(int argc, char* argv[]) {\n";
        s += "    auto __trust_parsed = trust::runtime::parseArgs(argc, argv, \"--trust:\");\n";
        if (usesSync) {
            // Compile-time дефолт детектора (-fsync-deadlock=...) - до системной опции среды,
            // чтобы --trust:fsync-deadlock=... на старте программы перекрыл его.
            if (!opts.sync_deadlock_timeout.empty()) {
                s += "    trust::runtime::setSyncDeadlockFromString(\"" + opts.sync_deadlock_timeout + "\");\n";
            }
            s += "    trust::runtime::applySystemEnv(__trust_parsed.env);\n";
        }
        s += "    return " + entry_func_name + "(__trust_parsed.argv, __trust_parsed.args);\n";
        s += "}\n";
    }
    return s;
}

// -- writeBuildFiles: генерация build-файлов (Makefile, build.conf, _main.cppt, LICENSE
//    и trust/ рантайм-заголовки) в build_dir рядом с .cppt. Без компиляции/линковки.
//    relocatable=true - build.conf без абсолютных путей и без привязки к рантайм-
//    библиотеке (для распространяемого архива, собираемого пользователем с установленным
//    TrustLang toolchain). Используется и `trust build` (compileAndLink), и trust-lsp
//    --emit-build-dir.
bool writeBuildFiles(const PipelineOpts& opts, const std::filesystem::path& cppt_path, const std::vector<std::filesystem::path>& module_cppt_paths,
                     const std::vector<std::string>& runtime_headers, const std::vector<std::string>& link_libs, const std::string& entry_func_name,
                     const std::string& entry_params) {
    namespace fs = std::filesystem;
    fs::path build_dir = cppt_path.parent_path();
    fs::path basename = cppt_path.stem();
    fs::path main_cppt_path; // path to entry point file, if any
    // Контроль переполнения стека: транспилятор записал runtime-заголовок trust/stack_check.hpp,
    // если сгенерированная программа реально использует защиту стека.
    const bool usesStackCheck = std::find(runtime_headers.begin(), runtime_headers.end(), "trust/stack_check.hpp") != runtime_headers.end();
    // Межпотоковая синхронизация ссылок: транспилятор записал trust/trusted-cpp-sync.hpp при
    // использовании синхронизированной ссылки. В этом случае main применяет системные опции среды
    // `--trust:...` (напр. --trust:fsync-deadlock=...) через trust::runtime::applySystemEnv.
    const bool usesSync = std::find(runtime_headers.begin(), runtime_headers.end(), "trust/trusted-cpp-sync.hpp") != runtime_headers.end();

    // -- Generate entry point file for executable mode (кроме однофайлового режима) --
    // В однофайловом режиме `-fsingle-file` обёртка main уже встроена в тот же .cppt модуля
    // (см. pipeline.cpp), поэтому отдельный `_main.cppt` здесь не создаётся и SRC_MAIN в
    // build.conf не заполняется - Makefile компилирует/линкует только основной .cppt
    // (+ SRC_MODULES для импортированных модулей).
    if (opts.compile_mode == CompileMode::Executable && !opts.single_file) {
        main_cppt_path = build_dir / (basename.string() + "_main.cppt");
        std::ofstream main_ofs(main_cppt_path);
        if (!main_ofs) {
            trust::errs() << "error: failed to create entry file: " << main_cppt_path << "\n";
            return false;
        }
        main_ofs << "// This file was generated automatically by TrustLang " TRUST_VERSION " on " << currentTimestamp() << "\n"
                 << "// Generated entry point by trust pipeline\n"
                 << "// Module: " << cppt_path.filename().string() << "\n\n";
        main_ofs << buildEntryMainSource(opts, usesStackCheck, usesSync, entry_func_name, entry_params);
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
        mf << "# Generated by trust pipeline - do not edit manually\n";
        mf << "# To rebuild: run 'make' from this directory\n\n";
        mf << trust::build::kMakefileBuild;
    }

    // -- build.conf: собираем содержимое один раз и пишем одним блоком. --
    std::string conf;
    conf += "# build.conf - generated by trust pipeline\n";
    conf += "# Platform-specific build configuration.\n";
    conf += "# Override any variable below by editing this file or passing\n";
    conf += "# via environment variables (e.g., CXX=clang++ make ...).\n";
    conf += "# NOTE: No leading whitespace - Makefile's include requires it.\n\n";
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
    // при make -C <build_dir> - в любом режиме (локальная сборка и скачиваемый архив)
    // build-файлы каталога одинаковы.
    conf += "CXXFLAGS += -I.\n";
    conf += "ARFLAGS   := rcs\n\n";
    if (!opts.compiler_options.empty()) {
        conf += "# User-supplied options\n";
        conf += "CXXFLAGS += " + opts.compiler_options + "\n";
        conf += "LDFLAGS  += " + opts.compiler_options + "\n";
    }

    // -- Runtime-backed types (Rational и т.п.) --
    // Транслятор уже вписал `#include \"<path>\"` в сгенерированный код; здесь
    // извлекаем эти заголовки из рантайм-библиотеки (.so либо .a) в <build_dir>/trust/
    // и добавляем в build.conf include-путь + линковку рантайм-библиотеки. Только те
    // заголовки, что реально использованы (не вся библиотека).
    std::vector<std::string> all_headers = runtime_headers;
    // Entry с параметрами: `_main.cppt` включает trust/args.hpp (parseArgs), который сам
    // тянет trust/dict.hpp - эти заголовки нужны обёртке, поэтому добавляем их явно
    // (транспилятор их не записывает, т.к. использует их только сгенерированный main).
    if (!entry_params.empty()) {
        all_headers.push_back("trust/args.hpp");
        all_headers.push_back("trust/dict.hpp");
    }
    // trust/stack_check.hpp зависит от trust/interrupt.hpp (stack_overflow - подкласс IntMinus),
    // поэтому при использовании контроля стека извлекаем и interrupt.hpp (иначе он может не попасть
    // в trust/, если программа не использует try/catch-блоки TRY_*).
    if (usesStackCheck) {
        all_headers.push_back("trust/interrupt.hpp");
    }
    // trust/trusted-cpp.hpp (Shared/Weak) тоже зависит от trust/interrupt.hpp (IntMinus: сбой
    // захвата - встроенная ошибка языка), поэтому при использовании ссылочных типов/захвата
    // извлекаем interrupt.hpp.
    const bool usesTrustedCpp = std::find(runtime_headers.begin(), runtime_headers.end(), "trust/trusted-cpp.hpp") != runtime_headers.end();
    if (usesTrustedCpp) {
        all_headers.push_back("trust/interrupt.hpp");
    }
    if (!all_headers.empty()) {
        for (const auto& hdr : all_headers) {
            if (!extractRuntimeHeader(hdr, build_dir, opts.runtime_link)) {
                return false;
            }
        }
        conf += "\n# Runtime-backed types - link trust-runtime library\n";
        // Единая переносимая форма: библиотеку предоставляет TrustLang toolchain
        // пользователя (локальная сборка резолвит её через LIBRARY_PATH, см. compileAndLink);
        // не запекаем абсолютный путь и не привязываем к .so/.a.
        conf += "LIBS     += -ltrust-runtime -lgmp\n";
    }
    // -- Контроль переполнения стека -- проверки (check_stack_limit / check_overflow / check_limit)
    // требуют секции .stack_sizes (для m_stack_limit) и pthread (для границ стека потока).
    if (usesStackCheck) {
        conf += "# Stack overflow protection: .stack_sizes section + pthread\n";
        conf += "CXXFLAGS += -fstack-size-section\n";
        conf += "LIBS     += -lpthread\n";
    }

    // -- Нативные библиотеки (@[link(\"имя\")]) --
    // Флаги линковки (-l<имя>) добавляются в LIBS только для целей с шагом линковки
    // (executable / shared-lib / trust-module). Существование библиотеки/символа НЕ
    // проверяется - ответственность линковщика.
    // CLI-библиотеки объединяются с библиотеками из исходника (@[link] + -l<name>).
    std::vector<std::string> all_link_libs = link_libs;
    if (!opts.link_libs_cli.empty()) {
        all_link_libs.insert(all_link_libs.end(), opts.link_libs_cli.begin(), opts.link_libs_cli.end());
    }
    // Каталоги поиска библиотек из CLI (-L<dir>) - в LDFLAGS.
    if (!opts.link_dirs.empty()) {
        conf += "\n# Library search paths (-L<dir>)\n";
        conf += "LDFLAGS  += ";
        for (std::size_t i = 0; i < opts.link_dirs.size(); ++i) {
            if (i > 0) {
                conf += " ";
            }
            conf += "-L" + opts.link_dirs[i];
        }
        conf += "\n";
    }
    const bool linksAtLinkStep =
        opts.compile_mode == CompileMode::Executable || opts.compile_mode == CompileMode::SharedLib || opts.compile_mode == CompileMode::TrustModule;
    if (!all_link_libs.empty() && linksAtLinkStep) {
        conf += "\n# Native libraries (@[link(...)] and -l<name>)\n";
        conf += "LIBS     += ";
        for (std::size_t i = 0; i < all_link_libs.size(); ++i) {
            if (i > 0) {
                conf += " ";
            }
            conf += all_link_libs[i];
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

bool compileAndLink(const PipelineOpts& opts, const std::filesystem::path& cppt_path, const std::vector<std::filesystem::path>& module_cppt_paths,
                    const std::vector<std::string>& runtime_headers, const std::vector<std::string>& link_libs, const std::string& entry_func_name,
                    const std::string& entry_params) {
    namespace fs = std::filesystem;
    fs::path build_dir = cppt_path.parent_path();
    fs::path basename = cppt_path.stem();
    // build-файлы (Makefile, build.conf, _main.cppt, LICENSE, trust/) - единая функция,
    // используется и trust-lsp --emit-build-dir (build.conf одинаковый).
    if (!writeBuildFiles(opts, cppt_path, module_cppt_paths, runtime_headers, link_libs, entry_func_name, entry_params)) {
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
                // LD_LIBRARY_PATH - фактический каталог runtime (для shared-бинарника при --run).
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

// -- Free function: computeBuildDir --

std::filesystem::path computeBuildDir(const PipelineOpts& opts) {
    namespace fs = std::filesystem;
    if (!opts.temp_dir.empty()) {
        return fs::path(opts.temp_dir);
    }
    fs::path src_path(opts.input_file);
    fs::path stem = src_path.stem();
    // Каталог `.trust`: для --run - <cwd>/.trust, иначе - <каталог исходника>/.trust. Итоговый
    // build_dir = <база>/.trust/<stem> (подкаталог по имени файла, чтобы Makefile/build.conf и
    // артефакты программ с одинаковым stem из разных каталогов не перезаписывали друг друга).
    // Артефакты НЕ кладём рядом с исходником (раньше не---run ложил их в каталог исходника) -
    // только в скрытый `.trust`, чтобы не засорять исходное дерево.
    std::string base;
    if (opts.run) {
        // При запуске шебанга cwd == каталог исходника, поэтому файлы - в локальном <src_dir>/.trust/.
        // Общий каталог <cwd>/.trust/<stem> сохраняет сверку кеша --run по относительному пути
        // (A1, см. run_cache_same_stem).
        base = ".";
    } else {
        base = src_path.parent_path().empty() ? "." : src_path.parent_path().string();
    }
    fs::path dir = utils::resolveTempDir(base, /*create=*/true) / stem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

// -- Free function: computeCpptPath --

std::filesystem::path computeCpptPath(const PipelineOpts& opts) {
    namespace fs = std::filesystem;
    fs::path src_path(opts.input_file);
    fs::path stem = src_path.stem();
    return computeBuildDir(opts) / (stem.string() + ".cppt");
}
} // namespace trust
