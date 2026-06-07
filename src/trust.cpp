// src/trust.cpp — точка входа в компилятор/транспилятор Trust
#include "pipeline/pipeline.hpp"
#include "diag/diag.hpp"

int main(int argc, char* argv[], char* envp[]) {
    (void)envp;

    // Парсинг аргументов командной строки
    auto result = trust::Pipeline::parseArgs(argc, argv);

    // Help/version/errors — выходим сразу
    if (trust::Pipeline::isSpecialExit(result)) {
        return result.exit_code;
    }

    // Создаём контекст и передаём его в Pipeline
    trust::Context ctx;
    trust::Pipeline pipeline(ctx, result.opts);
    try {
        return pipeline.execute();
    } catch (const trust::FatalError&) {
        // Диагностика уже выведена в diag(); Fatal прерывает выполнение.
        return 1;
    }
}
