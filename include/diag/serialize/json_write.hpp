#pragma once

// Минимальный JSON-писатель для сериализаторов диагностик (diag/serialize).
// Экранирование строк — ЕДИНЫЙ источник utils::escapeJson (utils/strings/strings_json.hpp,
// доступен через фасад utils/strings.hpp);
// внешние JSON-зависимости не нужны, байтовая стабильность ключей — на совести вызывающего
// (фиксированный порядок).

#include "utils/strings.hpp" // utils::escapeJson — единая реализация экранирования

#include <cstddef>
#include <ostream>
#include <string_view>

namespace trust::diag::json {

/// `"escaped"`. Пустая строка — тоже корректная строка (не null).
inline void writeString(std::ostream& out, std::string_view s) {
    out << '"' << utils::escapeJson(s) << '"';
}

/// `"escaped"` для непустой строки, иначе `null` (опциональное поле JSON).
inline void writeStringOrNull(std::ostream& out, std::string_view s) {
    if (s.empty()) {
        out << "null";
    } else {
        writeString(out, s);
    }
}

/// `true`/`false`.
inline void writeBool(std::ostream& out, bool v) {
    out << (v ? "true" : "false");
}

/// Число (size_t) либо `null` при флаге.
inline void writeSizeOrNull(std::ostream& out, std::size_t v, bool present) {
    if (present) {
        out << v;
    } else {
        out << "null";
    }
}

} // namespace trust::diag::json
