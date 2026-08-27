// src/trust.cpp - точка входа в компилятор/транспилятор Trust
#include "pipeline/pipeline.hpp"
#include "pipeline/cli.hpp"
#include "pipeline/analysis_options.hpp"
#include "diag/diag.hpp"
#include "utils/io.hpp"

#include <sstream>
#include <string>
#include <vector>

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
    // `-Whelp` печатает список диагностик и завершает.
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
            ctx.opts().printHelp(trust::outs());
            return 0;
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
