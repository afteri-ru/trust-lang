// include/pipeline/module_info.hpp
// Команда --module-info: вывод версии и экспортов скомпилированного модуля (.so/.trust).
// Данные читаются из ELF-секции .trust.sabi (без dlopen). Выделено из pipeline.cpp.
#pragma once

#include <string>

namespace trust {

class Context;

/// Читает секцию .trust.sabi модуля, валидирует декларации парсером и печатает
/// version/src-md5/экспорты (Trust-декларации). masks — comma-маски имён (`?`/`*`/`,`);
/// пустая = без фильтра. Явная ошибка при отсутствующей/битой/несовместимой секции.
/// Возвращает код возврата (0 = успех).
int showModuleInfo(Context& ctx, const std::string& module_path, const std::string& masks, bool verbose);

} // namespace trust
