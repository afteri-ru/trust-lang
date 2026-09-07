// include/pipeline/module_info.hpp
// Команда --module-info: вывод версии и экспортов скомпилированного модуля (.so/.trust).
// Выделено из pipeline.cpp.
#pragma once

#include <string>

namespace trust {

/// Загружает модуль через dlopen, читает __trust_get_exports и печатает версию/src-md5/экспорты.
/// Возвращает код возврата (0 = успех).
int showModuleInfo(const std::string& module_path, bool verbose);

} // namespace trust
