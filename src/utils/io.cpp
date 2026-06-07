#include "utils/io.hpp"

#include <iostream>

namespace trust {

static std::ostream* s_out = &std::cout;
static std::ostream* s_err = &std::cerr;

std::ostream& outs() {
    return *s_out;
}

std::ostream& errs() {
    return *s_err;
}

std::ostream* setOuts(std::ostream* os) {
    auto prev = s_out;
    s_out = os ? os : &std::cout;
    return prev;
}

std::ostream* setErrs(std::ostream* os) {
    auto prev = s_err;
    s_err = os ? os : &std::cerr;
    return prev;
}

} // namespace trust