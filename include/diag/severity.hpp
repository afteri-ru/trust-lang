#pragma once

namespace trust {

enum class Severity : int {
    Remark,
    Note,
    Warning,
    Error,
    Fatal,
};

} // namespace trust