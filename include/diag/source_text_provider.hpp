#pragma once

// Источник исходного текста для печати диагностик. Инвертирует зависимость diag->session:
// DiagnosticEngine получает провайдера текста (его реализует `session`: Context делегирует
// SourceMapWriter), поэтому не знает про Context/SourceMap. diag остаётся листом.

#include "diag/diagnostic_provenance.hpp"
#include "location/location.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

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

    /// Цепочка раскрытий, содержащих диапазон (внутренний кадр → внешний). Владелец факта —
    /// отображения; реализация делегирует аксессору владельца (R2/R34). Пустой вектор — раскрытия нет.
    [[nodiscard]] virtual std::vector<ExpansionFrame> expansionChain(MapperRange range) const = 0;
};

} // namespace trust
