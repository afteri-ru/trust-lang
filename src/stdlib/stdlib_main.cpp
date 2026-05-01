// src/stdlib/stdlib_main.cpp
// Точка запуска — использует AST Analyzer и ApiComparator
// для анализа C++ кода и проверки совместимости API между стандартами.

#include "stdlib/api_comparator.hpp"
#include "stdlib/ast_matcher.hpp"
#include "stdlib/analyzer.hpp"
#include "stdlib/formatter.hpp"
#include "types/forward.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace trust;

// Все варианты C++ стандартов
static constexpr std::array cpp_standards{
    LanguageVersion::CPP11,
    LanguageVersion::CPP17,
    LanguageVersion::CPP20,
    LanguageVersion::CPP23,
};

int main(int argc, const char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: stdlib <output-dir>\n";
        return 1;
    }

    std::string output_dir = argv[1];

    // Создание выходного каталога
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
        std::cerr << "Error: cannot create output directory " << output_dir << ": " << ec.message() << "\n";
        return 1;
    }

    // Путь к файлу для анализа
    std::string source_file = SOURCE_FILE_PATH;

    // Базовые флаги компиляции
    // Включаем пути к заголовкам проекта, чтобы libclang мог найти их
    std::vector<std::string> base_args = {
        "-fsyntax-only",
        "-I" + std::string(PROJECT_INCLUDE_DIR),
    };

    ApiComparator comparator;
    bool any_error = false;

    for (LanguageVersion ver : cpp_standards) {
        const char *std_name = language_version_string(ver);

        std::vector<std::string> args = base_args;
        args.push_back(std::string("-std=") + std_name);

        std::vector<MethodInfo> results;
        bool ok = run_clang_analysis(source_file, args, results);

        if (!ok) {
            std::cerr << "Warning: clang analysis failed with -std=" << std_name << ", skipping\n";
            // Не прерываем анализ — пропускаем проблемные версии
            continue;
        }

        // Фильтруем результаты по паттерну
        for (const auto &pattern_entry : comparator.get_patterns()) {
            const std::string &pattern = pattern_entry.first;
            std::vector<MethodInfo> filtered;

            for (const auto &info : results) {
                if (ApiComparator::match_pattern(info.qualified_name) == pattern) {
                    filtered.push_back(info);
                }
            }

            if (!filtered.empty()) {
                if (!comparator.add_version(pattern, ver, filtered)) {
                    any_error = true;
                    break;
                }
            }
        }

        if (any_error)
            break;
    }

    if (any_error) {
        return 1;
    }

    // Все проверки пройдены — записываем файлы
    OutputFormatter formatter(comparator);
    formatter.write_all(output_dir);
    formatter.write_iterators_file(output_dir);

    std::cout << "Analysis complete. Results in: " << output_dir << "\n";
    return 0;
}