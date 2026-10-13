// test/guards/fixture_component_deps/src/cc/cc.cpp
// Нарушение правила A: dd включён, но ребра cc -> dd в сборке нет.
#include "dd/dd.hpp"

int cc_value() { return 0; }
