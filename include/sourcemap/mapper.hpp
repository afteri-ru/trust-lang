#pragma once

// Зонтик source map: FileEntry, SourceMap<FileIdx>, OutputBuffer, SourceMapWriter,
// SourceMapReader и общий помощник SourceLocation. Узкие зоны - в отдельных заголовках.

#include "sourcemap/store/file_entry.hpp"
#include "sourcemap/store/output_buffer.hpp"
#include "sourcemap/serialize/reader.hpp"
#include "sourcemap/store/source_map.hpp"
#include "sourcemap/writer.hpp"

#include <string>

namespace trust {

/// Имя файла и номер строки исходного .src для диапазона (через SourceMap).
/// ЕДИНЫЙ источник «файл:строка» для парсера (контекст-макросы `@__FILE_NAME__`/`@__FILE_LINE__`)
/// и транспилятора (разворачивание интринсика `intrinsic_assert`, см. CppTranspiler::emitIntrinsic).
/// При невалидном диапазоне - пустое имя файла и строка 0.
struct SourceLocation {
    std::string file;
    int line = 0;
};

[[nodiscard]] inline SourceLocation sourceLocation(const SourceMapWriter& map, MapperRange range) {
    SourceLocation loc;
    if (!range.begin.isInvalid()) {
        loc.file = std::string(map.get_file(range.begin.fileIdx()).getFilename());
        loc.line = static_cast<int>(map.line_column(range.begin).line);
    }
    return loc;
}

} // namespace trust
