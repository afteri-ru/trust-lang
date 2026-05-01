// include/stdlib/analyzer.hpp
// Анализ методов и функций: извлечение типов аргументов,
// типов возвращаемых значений и классификация объявлений

#ifndef STDLIB_METHOD_ANALYZER_HPP
#define STDLIB_METHOD_ANALYZER_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace clang {
class NamedDecl;
class FunctionDecl;
class CXXMethodDecl;
} // namespace clang

namespace trust {

// Категория объявления
enum class DeclCategory : uint8_t { Function, Method, ClassTemplate, FunctionTemplate, TypeAlias, Iterator, Unknown };

// Структурированная информация о методе/функции
struct MethodInfo {
    std::string qualified_name;           // std::vector::push_back
    std::string return_type;              // void
    std::vector<std::string> param_types; // [std::vector::value_type&&]
    std::string normalized_signature;     // void(value_type&&) - для сравнения
    DeclCategory category = DeclCategory::Unknown;
    bool is_const = false;
    bool is_static = false;
    bool is_template = false;
};

// Анализатор методов и функций
// Извлекает структурированную информацию из Clang AST
class MethodAnalyzer {
  public:
    // Проанализировать объявление и извлечь информацию
    static std::optional<MethodInfo> analyze(const clang::NamedDecl *decl);

  private:
    // Определить категорию объявления
    static DeclCategory classify(const clang::NamedDecl *decl);

    // Извлечь тип возвращаемого значения
    static std::string get_return_type(const clang::FunctionDecl *fd);

    // Извлечь типы параметров
    static std::vector<std::string> get_param_types(const clang::FunctionDecl *fd);

    // Построить нормализованную сигнатуру для сравнения
    static std::string build_normalized_signature(const MethodInfo &info);

    // Проверить, является ли имя внутренним (начинается с '_')
    // Делегирует в trust::is_internal_name (name_utils.hpp)
    static bool is_internal_name(const std::string &qualified_name);
};

} // namespace trust

#endif // STDLIB_METHOD_ANALYZER_HPP