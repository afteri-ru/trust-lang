// Зона печати: публичный дисплей по политике (Trust-имя и диагностическая метка).
#include "types/registry/type_printer.hpp"

#include "types/names/type_names.hpp"
#include "types/registry/type_printer_detail.hpp"
#include "types/registry/type_store.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace trust {

std::expected<std::string, std::string> printType(const TypePrinter& reg, TypeId id, TypePrintPolicy policy) {
    return printType(reg, QualType::fromTypeId(id), policy);
}

std::string TypePrinter::typeNameOf(TypeId id) const {
    // Номинальное имя типа (Trust-форма). Отсутствие дескриптора — явная ошибка, НЕ заглушка.
    auto printed = printType(*this, id, TypePrintPolicy{TypePrintTarget::kTrust});
    if (!printed) {
        FAULT("typeNameOf: {}", printed.error());
    }
    return std::move(*printed);
}

std::expected<std::string, std::string> printType(const TypePrinter& reg, QualType state, TypePrintPolicy policy) {
    const TypeId id = state.getTypeId();
    // INVALID_TYPE_ID - легитимное значение системы типов («тип не выведен», Any-подобный
    // аргумент), поэтому печатается явной меткой отсутствия, а НЕ как «неизвестный тип».
    // Настоящая неизвестность (есть kind, нет дескриптора) остаётся ошибкой ниже.
    if (id == INVALID_TYPE_ID) {
        return std::string("<no type>");
    }
    auto name = printTrustName(reg, id, policy.scope);
    if (!name) {
        return name;
    }
    if (policy.target == TypePrintTarget::kDiagnostic && policy.showDesugared) {
        const TypeId canonical = reg.store().desugaredTypeId(id);
        if (canonical != getUnqualifiedType(id)) {
            auto aka = printTrustName(reg, canonical, policy.scope);
            if (!aka) {
                return aka; // причина отказа не скрывается
            }
            *name += " (aka " + *aka + ")";
        }
    }
    return name;
}

} // namespace trust
