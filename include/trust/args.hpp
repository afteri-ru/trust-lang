// trust/args.hpp - разбор аргументов командной строки скомпилированной trust-программы.
//
// Public runtime header: самодостаточный (только стандартные заголовки + trust/dict.hpp),
// чтобы сгенерированная C++ программа (в т.ч. `_main.cppt`) могла его подключить без
// зависимости от include-дерева компилятора. На этапе сборки встраивается в
// trust-runtime.so/.a (через #embed, в ELF-секцию "trust/args.hpp"); pipeline извлекает
// его в каталог сборки `trust/` вместе с остальными рантайм-заголовками.
//
// Назначение: генерируемый `main(int argc, char* argv[])` отделяет аргументы среды по
// единому префиксу (по умолчанию `--trust:` - зарезервировано для будущей настройки
// рантайма через trust::runtime::configure) и из оставшихся строит ДВА словаря:
//   - `argv` - позиционные аргументы (элементы Dict с пустым именем; доступ argv[0],
//     argv.size(), перебор через @while + pop_front);
//   - `args` - именованные опции: `--key=value`, `--key value`, `--flag` -> "true"
//     (доступ args['key'] по строковому ключу).
// Переменные окружения (TRUST_*) рантайм читает сам через getenv() и в сигнатуру не
// передаёт.

#pragma once

#include "trust/dict.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace trust::runtime {

/// Разобранные аргументы командной строки.
struct ParsedArgs {
    trust::Dict argv;             ///< Позиционные аргументы (элементы с пустым именем, индексный доступ).
    trust::Dict args;             ///< Именованные опции (`--key=value` / `--key value` / `--flag`).
    std::vector<std::string> env; ///< Аргументы с префиксом env_prefix (напр. "--trust:") -
                                  ///< зарезервированы для будущей настройки рантайма.
};

/// Отделяет аргументы среды по env_prefix и строит словари argv/args из остальных.
/// argv[0] (имя программы) пропускается. Значения всех элементов - строки (StrChar).
inline ParsedArgs parseArgs(int argc, char* argv[], const std::string_view env_prefix) {
    ParsedArgs out;
    constexpr uint32_t kStrChar = 9u | (1u << 8);
    bool after_ddash = false; // всё после `--` - позиционные аргументы
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (!after_ddash && arg == "--") {
            after_ddash = true;
            continue;
        }
        if (!after_ddash && !env_prefix.empty() && arg.starts_with(env_prefix)) {
            out.env.push_back(arg);
            continue;
        }
        if (!after_ddash && arg.size() > 2 && arg.starts_with("--")) {
            // Именованная опция `--key=value` / `--key value` / `--flag`.
            const std::string body = arg.substr(2);
            const std::size_t eq = body.find('=');
            if (eq != std::string::npos) {
                out.args.push_back(body.substr(0, eq), trust::TypedValue(kStrChar, body.substr(eq + 1)));
            } else {
                out.args.push_back(body, trust::TypedValue(kStrChar, std::string("true")));
            }
            continue;
        }
        // Позиционный аргумент -> элемент Dict с пустым именем.
        out.argv.push_back(std::string(), trust::TypedValue(kStrChar, arg));
    }
    return out;
}

} // namespace trust::runtime
