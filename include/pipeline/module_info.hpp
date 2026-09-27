// include/pipeline/module_info.hpp
// Команда --module-info: вывод экспортного интерфейса предкомпилированного пакета (.pcp).
// Trust-интерфейс (<stem>.src) читается из архива .pcp (без ELF/dlopen). Выделено из pipeline.cpp.
#pragma once

#include <string>

namespace trust {

class Context;

/// Читает Trust-интерфейс из `.pcp`, валидирует декларации парсером и печатает
/// version (из манифеста)/экспорты (Trust-декларации). masks — comma-маски имён (`?`/`*`/`,`);
/// пустая = без фильтра. Явная ошибка при отсутствующем/битом `.pcp`.
/// Возвращает код возврата (0 = успех).
int showModuleInfo(Context& ctx, const std::string& module_path, const std::string& masks, bool verbose);

} // namespace trust
