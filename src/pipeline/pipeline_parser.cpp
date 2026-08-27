#include "pipeline/pipeline.hpp"
#include "pipeline/cli.hpp"
#include "semantic/solver.hpp"
#include "trust/version.h"
#include "utils/io.hpp"

#include <filesystem>
#include <iostream>

namespace trust {

// -- Pipeline::parseArgs --

ParseResult Pipeline::parseArgs(std::span<char*> argv) {
    ParseResult result;

    if (argv.empty()) {
        return result;
    }

    // Set default compiler from CMake config
    result.opts.compiler = TRUST_DEFAULT_COMPILER;

    // Арity-aware табличный парсер опций драйвера (см. cli.hpp): сам собирает -W в
    // diag_args, позиционные отдельно (не «доедаются» опциями-списками), ошибки -> errs().
    std::vector<std::string> args;
    args.reserve(argv.size());
    for (std::size_t i = 0; i < argv.size(); ++i) {
        args.emplace_back(argv[i]);
    }
    const std::string err = parseDriverArgs(result, args);
    if (!err.empty()) {
        trust::errs() << "error: " << err << "\n";
        result.exit_code = 1;
    }
    if (result.exit_code != 0) {
        return result;
    }

    // `-Whelp`: справка по диагностикам (печатается в trust.cpp, нужен Context). Здесь
    // только определяем, что входной файл не требуется. Единый флаг справки -
    // Options::helpRequested() (set в parse_argv через applyDiagnostics); раннее значение
    // для пропуска проверки входного файла - ParseResult::diag_help_requested (из парсера).
    const bool diag_help = result.diag_help_requested;

    // -- Дополнительная обработка после парсинга --

    // --help / --version
    if (result.opts.help_requested) {
        trust::outs() << driverHelp();
        return result;
    }
    if (result.opts.version_requested) {
        // Брендовое имя проекта (TrustLang), как у Python/rustc - версия печатается
        // с заглавным именем. Это же имя - единственный уникальный идентификатор
        // (бинарь `trust` конфликтует с p11-kit trust), по которому shell-completion
        // отличает наш компилятор от чужого бинаря.
        trust::outs() << "TrustLang " << TRUST_VERSION_FULL << "\n";
        return result;
    }

    // `-Whelp`: справка по диагностикам применяется в trust.cpp (нужен Context/Options);
    // здесь только пропускаем проверку входного файла.
    if (diag_help) {
        return result;
    }

    // --module-info: нужен только входной файл, всё остальное игнорируется
    if (result.opts.module_info_requested) {
        if (result.opts.input_file.empty()) {
            trust::errs() << "error: --module-info requires a .trust module file\n";
            result.exit_code = 1;
            return result;
        }
        // Возвращаем результат без дополнительной обработки
        return result;
    }

    // --format-dump-config / --complete-options / --complete-files: входной файл не требуется.
    if (result.opts.format_dump_config || result.opts.complete_options || result.opts.complete_files) {
        return result;
    }

    // Проверка входного файла (кроме --module-info)
    if (result.opts.input_file.empty()) {
        trust::errs() << "error: no input file specified\n";
        trust::errs() << driverHelp();
        result.exit_code = 1;
        return result;
    }

    // Валидация взаимоисключающих флагов dsl
    if (result.opts.no_dsl && !result.opts.dsl_file.empty()) {
        trust::errs() << "error: --dsl and --no-dsl are mutually exclusive\n";
        result.exit_code = 1;
        return result;
    }

    // Валидация значения --solver-mode (см. include/semantic/solver.hpp): только поведенческие режимы.
    if (!result.opts.solver_mode.empty() && !semantic::parseSolverMode(result.opts.solver_mode)) {
        trust::errs() << "error: invalid value for --solver-mode: '" << result.opts.solver_mode << "' (expected: assert|export|calculate)\n";
        result.exit_code = 1;
        return result;
    }

    // Оставшиеся аргументы: парсер ошибок не оставляет (неизвестные -> exit_code!=0);
    // остаются пустыми.
    result.remaining_args.clear();

    // Warn about compile options being used with emit flags
    if (!result.opts.should_compile()) {
        if (!result.opts.temp_dir.empty()) {
            trust::errs() << "warning: --temp-dir is ignored when using emit flags\n";
        }
        if (!result.opts.compiler_options.empty()) {
            trust::errs() << "warning: --options is ignored when using emit flags\n";
        }
        if (result.opts.compile_mode != CompileMode::Executable) {
            trust::errs() << "warning: -c/-a/--shared-lib is ignored when using emit flags\n";
        }
        if (result.opts.run) {
            trust::errs() << "warning: --run is ignored when using emit flags\n";
        }
    }

    return result;
}

} // namespace trust