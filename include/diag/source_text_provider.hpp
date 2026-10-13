#pragma once

// include/diag/source_text_provider.hpp
// Источник исходного текста для печати диагностик. Инвертирует зависимость diag->session:
// DiagnosticEngine больше не знает про Context/SourceMap, а получает провайдера, который
// реализует `session` (Context делегирует SourceMapWriter). diag остаётся листом.

#include "location/location.hpp"

#include <cstddef>
#include <string_view>

namespace trust {

class SourceTextProvider {
  public:
    /// 1-based позиция в файле.
    struct LineColumn {
        std::size_t line{1};
        std::size_t column{1};
    };

    SourceTextProvider() = default;
    virtual ~SourceTextProvider() = default;
    SourceTextProvider(const SourceTextProvider&) = delete;
    SourceTextProvider& operator=(const SourceTextProvider&) = delete;

    /// Имя файла локации; пусто, если файл неизвестен.
    [[nodiscard]] virtual std::string_view fileName(MapperLocation loc) const = 0;
    /// Полный текст файла локации (view валиден, пока жив источник).
    [[nodiscard]] virtual std::string_view fileContents(MapperLocation loc) const = 0;
    /// Строка/колонка позиции.
    [[nodiscard]] virtual LineColumn lineColumn(MapperLocation loc) const = 0;
};

} // namespace trust
