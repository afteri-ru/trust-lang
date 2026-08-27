#pragma once

// include/pipeline/cli.hpp
// Единая инфраструктура определения и применения опций командной строки для
// драйверных бинарников (trust, trust-lsp, trust-dap).
//
// Принцип: опции драйвера и диагностики - ДВЕ РАЗНЫЕ таблицы (pipeline/PipelineOpts
// vs diag/Options). Объединяет их только инфраструктура: единый арity-aware парсер
// (собственная реализация, как драйверы gcc/clang), единая сгруппированная справка
// и единая точка применения -W-диагностик. Парсер управляется таблицей `DriverOption`
// (каждая опция объявляет точную арность `CliOpt::Flag/Value/ValueList/OptionalValue`),
// позиционные аргументы собираются отдельно - повторяемые опции не «доедают» их.
// Каждый драйверный бинарник объявляет СВОЮ таблицу DriverOption
// (enum + vector<DriverOption> + switch-связывание) и передаёт её в общий парсер/справку.
//
// ДВУХСПРАВОЧНАЯ МОДЕЛЬ (см. также diag/OPTIONS.md):
//   - `--help`  - верхнеуровневая справка с программно-специфичными опциями драйвера
//                 (группировка `CliCategory`, генерируется `driverHelp()`).
//   - `-Whelp` - единая справка по диагностикам, выводимая отдельной командой, чтобы
//                 не засорять общий вывод CLI (группировка `DiagGroup` в diag/Options,
//                 генерируется `Options::printHelp()`).
// Обе строятся из центральных таблиц опций, но показывают разные срезы.
//
// Нейминг привязан к назначению (без пересекающихся имён): `CliOpt` - вид значения опции
// CLI (арность). Символы этого файла лежат прямо в `namespace trust` (без отдельного
// `namespace cli`). Пер-компонентные id диагностик - см. diag/diag_set.hpp и OPTIONS.md.

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trust {
class Options;
struct ParseResult;

// -- Категории опций (фиксированная классификация, конвенция clang/gcc/rustc) --
enum class CliCategory {
    General,         ///< -h/--help, --version, -v/--verbose, -q/--quiet
    InputOutput,     ///< входной файл, -o, --temp-dir, -I, -L
    CompileModel,    ///< CompileMode, EmitFlags, --run, --semantic-on-errors
    Linking,         ///< -l<lib>, -L<dir>, --link-runtime, --linker
    Toolchain,       ///< --compiler, --ar, --std, -O, -g
    ProjectSpecific, ///< --dsl, --no-dsl, --no-stdlib, --module-info
    Formatting       ///< --format, --format-check, --keywords, --format-config, ...
};

/// Человекочитаемое имя категории (для заголовков grouped-help).
inline std::string_view cliCategoryName(CliCategory c) {
    switch (c) {
    case CliCategory::General:
        return "General";
    case CliCategory::InputOutput:
        return "Input & Output";
    case CliCategory::CompileModel:
        return "Compilation model";
    case CliCategory::Linking:
        return "Linking";
    case CliCategory::Toolchain:
        return "Toolchain";
    case CliCategory::ProjectSpecific:
        return "Project-specific";
    case CliCategory::Formatting:
        return "Formatting";
    }
    return {};
}

/// Вид значения опции драйвера.
enum class CliOpt {
    Flag,          ///< булев флаг (--flag)
    Value,         ///< одно значение (--opt <val>)
    ValueList,     ///< повторяемая опция списка (-l a -l b)
    OptionalValue, ///< необязательное значение, ТОЛЬКО через = (--opt / --opt=val);
                   ///< следующий токен не потребляется (значение только через =)
};

// -- Общая таблица опций для арity-aware парсера драйвера.
//    Используется trust, trust-lsp, trust-dap (реестр диагностик сюда НЕ входит -
//    он отдельно, для анализа кода). Каждый потребитель строит свою таблицу DriverOption
//    и передаёт колбэк применения значения к своим данным. --
struct DriverOption {
    int id;                 ///< идентификатор опции у потребителя (enum → int)
    std::string name;       ///< длинное имя без "--"
    std::string short_name; ///< короткая форма без "-" ("" если нет)
    CliOpt kind;            ///< Flag / Value / ValueList
    std::string type;       ///< плейсхолдер значения для справки ("" для Flag)
    std::string help;       ///< описание
    CliCategory category;   ///< категория (группировка справки)
};

/// Колбэк применения опции: id, value (для Flag value пуст; для Value/ValueList - значение).
/// Должен вернуть true при успехе, false при ошибке (сообщение печатает потребитель).
using ApplyOptionFn = std::function<bool(int id, const std::string& value)>;

/// Общий арity-aware парсер драйвера: разбирает args по table, вызывает apply.
/// Возвращает сообщение об ошибке (пусто = успех). Позиционные -> input_file (первый).
/// -W<diagnostics> -> diag_args (применяются позже через applyDiagnostics).
inline std::string parseDriverArgs(std::span<std::string> args, std::span<const DriverOption> table, const ApplyOptionFn& apply,
                                   std::vector<std::string>& diag_args, std::string& input_file, bool& diag_help_requested,
                                   std::vector<std::string>* analysis_passthrough = nullptr) {
    auto findLong = [&](const std::string& name) -> const DriverOption* {
        for (const auto& o : table) {
            if (o.name == name) {
                return &o;
            }
        }
        return nullptr;
    };
    auto findShort = [&](char c) -> const DriverOption* {
        for (const auto& o : table) {
            if (o.short_name.size() == 1 && o.short_name[0] == c) {
                return &o;
            }
            if (o.name.size() == 1 && o.name[0] == c) {
                return &o;
            }
        }
        return nullptr;
    };
    bool positional_only = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& tok = args[i];
        if (positional_only) {
            if (input_file.empty()) {
                input_file = tok;
            } else {
                return "unexpected extra argument '" + tok + "'";
            }
            continue;
        }
        if (tok == "--") {
            positional_only = true;
            continue;
        }
        if (tok.size() >= 2 && tok[0] == '-' && tok[1] == '-') {
            const std::string body = tok.substr(2);
            const std::string::size_type eq = body.find('=');
            const std::string name = (eq == std::string::npos) ? body : body.substr(0, eq);
            const bool has_val = eq != std::string::npos;
            const DriverOption* o = findLong(name);
            if (!o) {
                // Неизвестная длинная опция: если включён analysis_passthrough (trust-lsp) -
                // собираем как ОБЩУЮ опцию анализа (разбирается позже центрально applyAnalysisArgs);
                // иначе (trust) - ошибка. Форма `--name=value` (стандарт для общих опций).
                if (analysis_passthrough) {
                    analysis_passthrough->push_back(tok);
                    continue;
                }
                return "unknown option '--" + name + "'";
            }
            if (o->kind == CliOpt::Flag) {
                if (has_val) {
                    return "option '--" + name + "' does not take a value";
                }
                if (!apply(o->id, {})) {
                    return "invalid value for option '--" + name + "'";
                }
            } else if (o->kind == CliOpt::OptionalValue) {
                // Необязательное значение - только через =; следующий токен не потребляется.
                const std::string v = has_val ? body.substr(eq + 1) : std::string{};
                if (!apply(o->id, v)) {
                    return "invalid value for option '--" + name + "'";
                }
            } else {
                std::string v = has_val ? body.substr(eq + 1) : std::string{};
                if (!has_val) {
                    if (i + 1 >= args.size()) {
                        return "option '--" + name + "' requires a value";
                    }
                    v = args[++i];
                }
                if (!apply(o->id, v)) {
                    return "invalid value for option '--" + name + "'";
                }
            }
        } else if (tok.size() >= 2 && tok[0] == '-') {
            if (tok[1] == 'f' && tok.size() >= 3) {
                // Поведенческий feature-флаг по конвенции компиляторов (gcc/clang):
                // `-f<name>` включает, `-fno-<name>` выключает. НЕ диагностика (не -W).
                // Пример: -fsolver-loop-unroll / -fno-solver-loop-unroll.
                std::string fname = tok.substr(2);
                bool fenable = true;
                if (fname.rfind("no-", 0) == 0) {
                    fenable = false;
                    fname = fname.substr(3);
                }
                const DriverOption* fo = findLong(fname);
                if (!fo) {
                    // Неизвестный -f флаг: при analysis_passthrough (trust-lsp) - общая опция
                    // анализа (напр. -fsolver-loop-unroll), разбирается центрально applyAnalysisArgs.
                    if (analysis_passthrough) {
                        analysis_passthrough->push_back(tok);
                        continue;
                    }
                    return "unknown option '-f" + fname + "'";
                }
                if (fo->kind != CliOpt::Flag) {
                    return "option '-f" + fname + "' is not a boolean flag";
                }
                if (!apply(fo->id, fenable ? "on" : "off")) {
                    return "invalid value for option '-f" + fname + "'";
                }
                continue;
            }
            if (tok[1] == 'W') {
                // Справка по диагностикам - только форма `-Whelp`.
                diag_args.push_back(tok);
                if (tok == "-Whelp") {
                    diag_help_requested = true;
                }
                continue;
            }
            const char c = tok[1];
            const DriverOption* o = findShort(c);
            if (!o) {
                return "unknown option '-" + std::string(1, c) + "'";
            }
            const std::string rest = tok.substr(2);
            if (o->kind == CliOpt::Flag) {
                if (!rest.empty()) {
                    return "option '-" + std::string(1, c) + "' does not take a value";
                }
                if (!apply(o->id, {})) {
                    return "invalid value for option '-" + std::string(1, c) + "'";
                }
            } else {
                std::string v = rest;
                if (v.empty()) {
                    if (i + 1 >= args.size()) {
                        return "option '-" + std::string(1, c) + "' requires a value";
                    }
                    v = args[++i];
                }
                if (!apply(o->id, v)) {
                    return "invalid value for option '-" + std::string(1, c) + "'";
                }
            }
        } else {
            if (input_file.empty()) {
                input_file = tok;
            } else {
                return "unexpected extra argument '" + tok + "'";
            }
        }
    }
    return {};
}

/// Сгруппированная по категориям справка по таблице опций.
inline std::string driverHelp(std::span<const DriverOption> table) {
    std::string out;
    out += "Options:\n";
    const CliCategory cats[] = {CliCategory::General,   CliCategory::InputOutput,     CliCategory::CompileModel, CliCategory::Linking,
                                CliCategory::Toolchain, CliCategory::ProjectSpecific, CliCategory::Formatting};
    for (CliCategory cat : cats) {
        out += "\n" + std::string(cliCategoryName(cat)) + ":\n";
        for (const auto& o : table) {
            if (o.category != cat) {
                continue;
            }
            const std::string arg = o.type.empty() ? "" : " <" + o.type + ">";
            if (o.name == "input") {
                out += "  input\n";
            } else if (o.name.size() == 1) {
                out += "  -" + o.name + arg + "\n";
            } else {
                std::string flags = "--" + o.name + arg;
                if (!o.short_name.empty()) {
                    flags = "-" + o.short_name + arg + ", " + flags;
                }
                out += "  " + flags + "  " + o.help + "\n";
            }
        }
    }
    out += "\nDiagnostics: use `-Whelp` to list all diagnostics and -Wall/-Wextra/-Werror.\n";
    return out;
}

// -- Подкоманда `server[=<port>]` (trust-lsp / trust-dap): НЕ опция драйвера, а подкоманда
//    режима TCP-сервера. Находит и удаляет её из args; пишет порт (default_port при отсутствии
//    `=`). Возвращает false, если подкоманда не задана (args при этом не изменяется). --
inline bool extractServerCommand(std::vector<std::string>& args, int& out_port, int default_port) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i].rfind("server", 0) == 0) {
            const std::string::size_type eq = args[i].find('=');
            out_port = (eq == std::string::npos) ? default_port : std::stoi(args[i].substr(eq + 1));
            args.erase(args.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

// Каждый драйверный бинарник объявляет СВОЮ таблицу DriverOption (enum + vector<DriverOption>
// + switch-связывание) и передаёт её в общий parseDriverArgs/driverHelp. Пример для trust
// (таблица trust в cli.cpp: enum DriverOptId + buildTrustTable) и для lsp/dap/playground - в их исходниках.

/// Общие опции АНАЛИЗА (обрабатываются централизованно через applyAnalysisOptions/
/// applyAnalysisArgs, см. analysis_options.hpp): `--solver-mode`, `--keywords`,
/// `-fsolver-loop-unroll`. ЕДИНЫЙ источник имён/арности/help/категорий; id каждой опции
/// передаёт бинарник (значение из своего enum). Используется таблицей trust
/// (`buildTrustTable`); trust-lsp принимает общие опции ЧЕРЕЗ analysis_passthrough
/// (см. parseDriverArgs) и разбирает их тем же applyAnalysisArgs - общие опции не
/// дублируются ни в одной таблице драйвера LSP.
inline std::vector<DriverOption> commonAnalysisOptions(int solverModeId, int keywordsId, int solverLoopUnrollId) {
    return {
        {solverModeId, "solver-mode", "", CliOpt::Value, "mode",
         "Behavior for trust conditions (pre/post-conditions and assertions): "
         "assert | export | calculate. Not a diagnostic (severity of the presence warning is -Wsolver).\n"
         "  assert    - insert runtime checks (trust__abort__) on the given conditions\n"
         "  export    - generate a file for the z3 solver (.smt2 + .smt2.map)\n"
         "  calculate - generate the file and run it via Z3, reporting sat/unsat + counterexample",
         CliCategory::ProjectSpecific},
        {keywordsId, "keywords", "", CliOpt::Value, "list",
         "Macro names allowed without the '@' sigil (comma-separated, no spaces); "
         "suppresses the -Wsigil warning and treats them like keywords in the formatter",
         CliCategory::Formatting},
        {solverLoopUnrollId, "solver-loop-unroll", "", CliOpt::Flag, "",
         "Unroll loops without an invariant (bounded; behavioral, not a diagnostic). Default: off; "
         "loops without an invariant or unroll emit a -Wsolver-loop diagnostic instead.",
         CliCategory::ProjectSpecific},
    };
}

/// Разбирает CLI-аргументы по таблице DriverOption (арity-aware, как драйвер gcc/clang):
/// каждый токен распознаётся как длинная/короткая опция с явной арностью, либо позиционный.
/// -W<diagnostics> собираются в result.diag_args (применяются позже через applyDiagnostics).
/// Позиционные аргументы накапливаются отдельно (первый - входной файл) и НЕ поглощаются
/// опциями-списками. Возвращает сообщение об ошибке (пусто = успех); при ошибке result
/// заполнен частично (что успело разобраться), печать - обязанность вызывающего.
std::string parseDriverArgs(trust::ParseResult& result, std::span<std::string> args);

/// Сгруппированная по категориям справка по опциям драйвера (генерируется из таблицы).
std::string driverHelp();

/// Имена driver-опций (--<long>, -<short>) для shell-completion (`trust --complete-options`).
std::vector<std::string> driverOptionTokens();

/// Имена driver-опций, значение которых — файл/путь (type ∈ {file,dir,path}),
/// для shell-completion (`trust --complete-files`).
std::vector<std::string> driverFileValueTokens();

/// Единая точка применения CLI-диагностик: -Whelp, -Wall/-Wextra, -W<name>,
/// -Wno-<name>, -W<name>=<severity>. Останавливается на первом не -W аргументе.
/// Возвращает span оставшихся аргументов (начиная с первого не -W).
/// При `-Whelp` устанавливает opts.helpRequested() == true.
std::span<char*> applyDiagnostics(Options& opts, std::span<char*> args);

} // namespace trust
