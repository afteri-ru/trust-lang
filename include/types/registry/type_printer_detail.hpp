#pragma once

// Внутренняя точка печати Trust-имени. Потребители внутри `types/registry`; публичный вход — `printType`.
#include "types/registry/type_printer.hpp"

#include <expected>
#include <string>

namespace trust {

std::expected<std::string, std::string> printTrustName(const TypePrinter& reg, TypeId id, TypePrintScope scope);

} // namespace trust
