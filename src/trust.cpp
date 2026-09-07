// src/trust.cpp - точка входа в компилятор/транспилятор Trust
#include "pipeline/pipeline.hpp"
#include "pipeline/cli.hpp"
#include "pipeline/analysis_options.hpp"
#include "diag/context.hpp"
#include "diag/diag.hpp"
#include "utils/io.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ЕДИНСТВЕННЫЕ источники списков предопределённых макросов/прагм (см. include/syntax/MEMORY.md):
// из них ниже генерируются таблицы имён/описаний для справки `-Whelp-predef-macros`.
#include "syntax/predef_macro_x.hpp"
#include "syntax/pragma_macro_x.hpp"
#include "ast/check_area.hpp"

namespace trust {
namespace {

// Первая непустая строка документа макроса. Док-комментарий может быть многострочным — в
// справку выводится только первая строка (см. задачу «первая строка документирующего
// комментария»). С начала строки снимается маркер комментария (## / ##< / # / // / ///),
// чтобы в описании не было служебных символов. Пустая строка, если док отсутствует/пуст.
std::string firstDocLine(std::string_view doc) {
    std::string_view line = doc.substr(0, doc.find('\n'));
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    std::size_t b = 0, e = line.size();
    while (b < e && isSpace(static_cast<unsigned char>(line[b]))) {
        ++b;
    }
    while (e > b && isSpace(static_cast<unsigned char>(line[e - 1]))) {
        --e;
    }
    if (b < e && line[b] == '#') { // ##, ##<, # ...
        while (b < e && line[b] == '#') {
            ++b;
        }
    } else if (e - b >= 2 && line.substr(b, 2) == "//") { // //, ///
        b += 2;
        if (b < e && line[b] == '/') {
            ++b;
        }
    }
    while (b < e && isSpace(static_cast<unsigned char>(line[b]))) {
        ++b;
    }
    return std::string(line.substr(b, e - b));
}

// Предопределённые макросы вида @__...__ (value + context + pragma). Таблицы генерируются
// здесь из x-macro заголовков (единый источник списков, см. include/syntax/MEMORY.md), чтобы
// trust.cpp не зависел от parser.yy.h (генер. bison) через include/syntax/predef_macro.hpp.
// Сортировка по имени.
bool predefMacroShape(std::string_view text) {
    return text.size() > 5 && text.starts_with("@__") && text.ends_with("__");
}
std::vector<std::pair<std::string, std::string>> predefMacroHelpEntries() {
    std::vector<std::pair<std::string, std::string>> out;
    // value-макросы (@__TRUST_VERSION_*__, @__FILE__, ...).
    {
        constexpr struct {
            const char* name;
            const char* desc;
        } k[] = {
#define X(Name, Str, Desc) {Str, Desc},
            TRUST_VALUE_MACROS(X)
#undef X
        };
        for (const auto& e : k) {
            if (predefMacroShape(e.name)) {
                out.emplace_back(e.name, e.desc);
            }
        }
    }
    // context-макросы (@__CLASS__, @__NAMESPACE__, @__FUNCTION__, @__MODULE_NAME__, ...).
    {
        constexpr struct {
            const char* name;
            const char* desc;
        } k[] = {
#define X(Name, Str, Desc) {Str, Desc},
            TRUST_CONTEXT_MACROS(X)
#undef X
        };
        for (const auto& e : k) {
            if (predefMacroShape(e.name)) {
                out.emplace_back(e.name, e.desc);
            }
        }
    }
    // прагмы (@__OPTION__*, @__PRAGMA_*, @__HYGIENIC__, ...).
    {
        constexpr struct {
            const char* name;
            const char* desc;
        } k[] = {
#define X(Name, Str, Desc) {Str, Desc},
            TRUST_PRAGMA_MACROS(X)
#undef X
        };
        for (const auto& e : k) {
            if (predefMacroShape(e.name)) {
                out.emplace_back(e.name, e.desc);
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return out;
}

// Печать списка макросов загруженного DSL (trust/dsl.src или --dsl): имена и доки берём из
// реестра Context::macroDefs() (заполняется recordMacro при разборе DSL) — «из списка
// зарегистрированных макросов». В справку выводится первая строка документирующего
// комментария (## / ##<), если она есть.
int printDslMacroHelp(trust::Context& ctx, const trust::PipelineOpts& opts) {
    trust::Pipeline pipeline(ctx, opts);
    // effectiveKeywords() — публичный метод, который первым делом вызывает приватный
    // loadDslMacros() (уважает --dsl <file> / --no-dsl). Используем его как способ
    // «загрузить DSL в Context», чтобы не дублировать #embed/логику загрузки.
    pipeline.effectiveKeywords();
    std::ostream& os = trust::outs();
    if (!ctx.macro()) {
        os << "No DSL loaded (--no-dsl).\n";
        return 0;
    }
    // Имя -> первая строка дока (имя уникально; предпочитаем непустой док при дублях сигнатур).
    std::map<std::string, std::string, std::less<>> macros;
    for (const auto& md : ctx.macroDefs()) {
        std::string_view name = md.name;
        auto& doc = macros[std::string(name)];
        if (doc.empty()) {
            const std::string line = firstDocLine(md.documentation);
            if (!line.empty()) {
                doc = line;
            }
        }
    }
    os << "DSL macros (names from the loaded DSL with the first line of their doc comments):\n";
    for (const auto& [name, line] : macros) {
        os << "  " << name;
        if (!line.empty()) {
            os << "  " << line;
        }
        os << "\n";
    }
    return 0;
}

// Печать списка предопределённых макросов вида @__...__ с описанием (из x-macro таблиц).
int printPredefMacroHelp() {
    std::ostream& os = trust::outs();
    os << "Predefined macros (@__...__) with their descriptions:\n";
    for (const auto& [name, desc] : predefMacroHelpEntries()) {
        os << "  " << name << "  " << desc << "\n";
    }
    return 0;
}

// Список синтаксических областей для `@__CHECK_AREA__` (источник - x-macro TRUST_CHECK_AREAS).
int printCheckAreasHelp() {
    std::ostream& os = trust::outs();
    os << "Syntactic areas checkable via @__CHECK_AREA__ (name [, behavior] [, attr...]):\n";
    for (int i = 0; i < static_cast<int>(AreaKind::Count); ++i) {
        const auto k = static_cast<AreaKind>(i);
        const std::string_view desc = areaKindDesc(k);
        os << "  " << areaKindName(k);
        if (!desc.empty()) {
            os << "  -  " << desc;
        }
        os << "\n";
    }
    os << "\nbehavior: default | ignore | warning | error  (default -> -Wcheck-area=<severity>)\n";
    return 0;
}

} // namespace
} // namespace trust

int main(int argc, char* argv[], char* envp[]) {
    (void)envp;

    // Linux-хешбанг передаёт ВЕСЬ текст после интерпретатора ОДНИМ аргументом
    // (например "--run -Wembed=ignore" - один argv-токен, т.к. ядро не разбивает пробелы).
    // Разбиваем option-аргументы (начинающиеся с '-'), содержащие пробелы, на отдельные токены,
    // чтобы --run и -W опции распознавались корректно. Не-option аргументы (пути) не трогаем.
    std::vector<std::string> argStrs;
    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        if (i > 0 && a.size() > 1 && a[0] == '-' && a.find(' ') != std::string::npos) {
            std::istringstream ss(a);
            std::string tok;
            while (ss >> tok) {
                argStrs.push_back(std::move(tok));
            }
        } else {
            argStrs.push_back(std::move(a));
        }
    }
    std::vector<char*> newArgv;
    newArgv.reserve(argStrs.size());
    for (auto& s : argStrs) {
        newArgv.push_back(s.data());
    }

    // Парсинг аргументов командной строки
    auto result = trust::Pipeline::parseArgs(static_cast<int>(newArgv.size()), newArgv.data());

    // Help/version/errors - выходим сразу
    if (trust::Pipeline::isSpecialExit(result)) {
        return result.exit_code;
    }

    // Создаём контекст и передаём его в Pipeline
    trust::Context ctx;
    // Единая точка применения опций анализа: -W<option> (severity и feature-флаги) +
    // поведенческие флаги (--solver-mode, --keywords, -fsolver-loop-unroll). Реализация -
    // applyAnalysisOptions (include/pipeline/analysis_options.hpp), общая для trust и trust-lsp.
    // Команды справки печатаются и завершают выполнение:
    //   -Whelp                -> Options::printHelp (диагностики);
    //   -Whelp-dsl            -> список макросов загруженного DSL;
    //   -Whelp-predef-macros  -> список предопределённых макросов @__...__.
    {
        try {
            trust::applyAnalysisOptions(ctx.opts(), result);
        } catch (const std::invalid_argument& e) {
            // Неизвестная -W-опция.
            // Диагностика уже выведена в diag(); здесь только конвертируем в код выхода.
            (void)e;
            return 1;
        }
        if (ctx.opts().helpRequested()) {
            switch (ctx.opts().helpTopic()) {
            case trust::Options::HelpTopic::Diagnostics:
                ctx.opts().printHelp(trust::outs());
                return 0;
            case trust::Options::HelpTopic::DslMacros:
                return trust::printDslMacroHelp(ctx, result.opts);
            case trust::Options::HelpTopic::PredefMacros:
                return trust::printPredefMacroHelp();
            case trust::Options::HelpTopic::CheckAreas:
                return trust::printCheckAreasHelp();
            case trust::Options::HelpTopic::None:
                break;
            }
        }
    }
    trust::Pipeline pipeline(ctx, result.opts);
    try {
        return pipeline.execute();
    } catch (const trust::FatalError&) {
        // Диагностика уже выведена в diag(); Fatal прерывает выполнение.
        return 1;
    }
}
