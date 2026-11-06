#pragma once

// Кадр цепочки раскрытия: определение (входной файл) и имя как в тексте (напр. `@tst`).
// Значение-проекция пары `use → definition`; ничего не хранит (R1).

#include "location/location.hpp"

#include <string>

namespace trust {

/// Кадр цепочки раскрытия: определение (входной файл) и имя раскрытия как в тексте (напр. `@tst`).
struct ExpansionFrame {
    MapperRange origin;
    std::string name;
};

} // namespace trust