#pragma once

#include <string>

namespace trust::utils {

/// Формирует строку с backtrace (стеком вызовов).
/// На Linux использует backtrace()/backtrace_symbols() с деманглингом
/// C++ имён через __cxxabiv1::__cxa_demangle.
/// В будущем - Windows-ветка через CaptureStackBackTrace().
std::string backtrace_string(int max_frames = 0);

/// Устанавливает глобальные обработчики аварийного завершения:
///   - std::terminate_handler - печатает backtrace (через backtrace_string)
///     и завершает процесс
///   - SIGABRT               - печатает backtrace и завершает процесс
void install_fault_handler();

} // namespace trust::utils
