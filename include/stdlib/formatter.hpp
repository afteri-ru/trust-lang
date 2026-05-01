// include/stdlib/formatter.hpp
// Форматирование результатов анализа API
// Генерация файлов с группировкой по классам и отображением изменений

#ifndef STDLIB_formatter_HPP
#define STDLIB_formatter_HPP

#include "stdlib/api_comparator.hpp"

#include <fstream>
#include <string>
#include <vector>

namespace trust {

class OutputFormatter {
  public:
    explicit OutputFormatter(const ApiComparator &comparator);

    // Записать все файлы для всех паттернов
    void write_all(const std::string &output_dir) const;

    // Записать единый файл iterators.txt со всеми итераторами, сгруппированными по паттернам
    void write_iterators_file(const std::string &output_dir) const;

  private:
    void write_one(std::ofstream &out, const std::string &pattern) const;

    // Получить короткое имя метода (push_back из std::vector::push_back)
    static std::string short_name(const std::string &full);

    // Получить имя класса из полного имени
    static std::string class_name(const std::string &full);

    const ApiComparator &comparator_;
};

} // namespace trust

#endif // STDLIB_formatter_HPP