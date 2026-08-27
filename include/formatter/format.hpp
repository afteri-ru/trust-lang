#pragma once
#ifndef TRUST_FORMATTER_FORMAT_HPP_
#define TRUST_FORMATTER_FORMAT_HPP_

#include <string>
#include <string_view>
#include <vector>

namespace trust {
class Parser;
}

namespace trust::formatter {

/// Настройки форматирования исходного текста Trust.
struct FormatOptions {
    int tab_size = 4;                 ///< ширина отступа (в пробелах)
    bool use_spaces = true;           ///< true — отступ пробелами; false — табами
    int max_line_width = 120;         ///< максимальная ширина строки (для inline-списков)
    bool insert_final_newline = true; ///< добавлять перевод строки в конце файла
    bool preserve_blank_lines = true; ///< сохранять пустые строки (до 2 подряд)
    /// Имена макросов (без ведущего '@' и с ним), обрабатываемые как контрольные ключевые слова
    /// (пробел перед '(', распознавание блока перед '{'). Из .trust-format `Keywords:` и/или CLI.
    std::vector<std::string> keywords;
};

/// Результат загрузки конфигурации `.trust-format` (классические настройки + keywords).
struct FormatConfig {
    bool ok = false;   ///< успех парсинга (неизвестный ключ/ошибка формата → false)
    std::string error; ///< текст ошибки (непусто при !ok)
    FormatOptions opts;
};

/// Ищет файл `.trust-format` в каталоге исходника и выше (по направлению к корню),
/// как clang-format ищет `.clang-format`. Возвращает путь к найденному файлу или пустую строку.
[[nodiscard]] std::string findConfig(std::string_view startDir);

/// Читает конфиг из файла. При неудаче чтения/парсинга ok=false и error.
[[nodiscard]] FormatConfig loadConfig(const std::string& path);

/// Парсит конфиг из текста (формат `.clang-format`: `Key: Value`, комментарии '#', опц. '---').
[[nodiscard]] FormatConfig parseConfig(std::string_view text);

/// Сериализует настройки в формат `.trust-format` с дефолтными значениями и комментариями
/// (аналог `clang-format -dump-config`). Используется для `trust --format-dump-config`.
[[nodiscard]] std::string dumpConfig(const FormatOptions& opts = {});

/// Разбивает список имён (запятая-разделение, допускается ведущий '@') в вектор.
[[nodiscard]] std::vector<std::string> splitKeywordList(std::string_view list);

/// Результат форматирования.
struct FormatResult {
    bool ok = false;   ///< успех (в т.ч. лексический успех; семантика не проверяется)
    std::string text;  ///< отформатированный текст (валиден при ok)
    std::string error; ///< текст ошибки (непусто при !ok)
};

/// Форматирует исходный текст Trust.
/// sourceName — имя источника (для лексера/диагностики, например "@input" или путь файла).
/// parser — парсер, в контексте которого уже загружен DSL. Форматтер подписывается на
///          Macro::on_macro_kind, прогоняет parser.ParseWithSource (макросы DSL/source/module
///          регистрируются, классификация собирается in-stream по позиции), затем делает свой
///          сырой проход для раскладки (токены + комментарии).
/// Возвращает отформатированный текст. При лексической ошибке возвращает ok=false.
FormatResult format(std::string_view source, const std::string& sourceName, const FormatOptions& opts, Parser& parser);

/// Форматирует файл по пути. При неудаче чтения/лексики возвращает ok=false и error.
FormatResult formatFile(const std::string& path, const FormatOptions& opts = {});

} // namespace trust::formatter

#endif // TRUST_FORMATTER_FORMAT_HPP_
