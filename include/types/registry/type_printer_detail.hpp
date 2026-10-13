#pragma once

// include/types/registry/type_printer_detail.hpp
// Внутренние точки печати имени: разделены по ответственности (C++-представление / Trust-имя /
// диагностическая метка). Потребители внутри `types/registry`; публичный вход — `printType`.
#include "types/registry/type_printer.hpp"

#include <expected>
#include <string>

namespace trust {

std::string typeLabel(const TypePrinter& reg, TypeId id);
std::string nominalCppName(const TypePrinter& reg, const TypeDescriptor& desc, TypePrintScope scope);
std::expected<std::string, std::string> printCppName(const TypePrinter& reg, TypeId id, bool isConst, TypePrintScope scope);
std::expected<std::string, std::string> printTrustName(const TypePrinter& reg, TypeId id, TypePrintScope scope);
std::expected<std::string, std::string> printCppRefWrapper(const TypePrinter& reg, TypeId id, std::string base, TypePrintScope scope);

} // namespace trust
