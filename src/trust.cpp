// src/trust.cpp — точка входа в компилятор/транспилятор Trust
#include "pipeline/pipeline.hpp"
#include "diag/diag.hpp"

#include <sstream>
#include <string>
#include <vector>

int main(int argc, char* argv[], char* envp[]) {
    (void)envp;

    // Linux-хешбанг передаёт ВЕСЬ текст после интерпретатора ОДНИМ аргументом
    // (например "--run -Wembed=ignore" — один argv-токен, т.к. ядро не разбивает пробелы).
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

    // Help/version/errors — выходим сразу
    if (trust::Pipeline::isSpecialExit(result)) {
        return result.exit_code;
    }

    // Создаём контекст и передаём его в Pipeline
    trust::Context ctx;
    // Применяем -W<option> (severity-опции и feature-флаги, напр. -Wno-comments) из
    // неизвестных CLI-аргументов. parse_argv останавливается на первом не -W аргументе.
    {
        std::vector<char*> wargv;
        for (auto& s : result.remaining_args) {
            if (s.starts_with("-W")) {
                wargv.push_back(s.data());
            }
        }
        if (!wargv.empty()) {
            ctx.opts().parse_argv(wargv);
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
