#pragma once

// include/debug/gdb/stack_frame.hpp
// Кадр стека, возвращаемый GDB/MI (-stack-list-frames).

#include <string>

namespace trust::debug {

struct StackFrameInfo {
    std::string m_func;
    std::string m_file;
    int m_line = 0;
};

} // namespace trust::debug
