// src/types/registry/type_printer.cpp
// Зона печати: публичный дисплей по политике, Trust-имя и диагностическая метка.
#include "location/module_name.hpp"
#include "types/registry/type_printer.hpp"

#include "types/group.hpp"
#include "types/names/storage_key.hpp"
#include "types/names/type_names.hpp"
#include "types/registry/builtin_registry.hpp"
#include "types/registry/type_printer_detail.hpp"
#include "types/registry/type_store.hpp"
#include "types/typekind.hpp"
#include "utils/strings.hpp"

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace trust {

/// Безопасная метка типа для текста ошибки: принтер НЕ должен зависеть от другого принтера
/// (`getTypeName` может сам отказать), поэтому имя берётся из дескриптора напрямую.
std::string typeLabel(const TypePrinter& reg, TypeId id) {
    const TypeDescriptor* desc = reg.store().descriptorOf(id);
    return desc != nullptr ? desc->name : std::format("type#{}", id.raw());
}

/// C++-имя номинального пользовательского типа: модуль (ПЕРВЫЙ сегмент) + namespace-путь из
/// ключа регистрации + display-имя. Модуль и namespace — из одного ключа, поэтому объявление и
/// использование печатаются одной функцией (рассинхронизация невозможна). `kUnqualified` — только
/// короткое имя (позиция внутри своей области/класса).
std::string nominalCppName(const TypePrinter& reg, const TypeDescriptor& desc, TypePrintScope scope) {
    const std::string name = utils::cppIdent(desc.name);
    if (scope != TypePrintScope::kQualified) {
        return name;
    }
    std::string out;
    if (desc.module.isValid()) {
        const std::string moduleNamespace = reg.moduleCppNamespace(desc.module);
        if (!moduleNamespace.empty()) {
            out += moduleNamespace;
            out += "::";
        }
    }
    if (!desc.namespacePath.empty()) {
        out += utils::cppNamespacePath(desc.namespacePath);
        out += "::";
    }
    out += name;
    return out;
}

std::expected<std::string, std::string> printType(const TypePrinter& reg, TypeId id, TypePrintPolicy policy) {
    return printType(reg, QualType::fromTypeId(id), policy);
}

std::string TypePrinter::moduleCppNamespace(ModuleIndex index) const {
    if (m_moduleNames == nullptr) {
        FAULT("moduleCppNamespace: module name provider is not injected (Pipeline must call setModuleNames)");
    }
    return trust::moduleCppNamespace(m_moduleNames->moduleLogicalName(index));
}

std::string TypePrinter::getTypeName(TypeId id) const {
    // Номинальное имя типа (Trust-форма). Отсутствие дескриптора — явная ошибка, НЕ заглушка.
    auto printed = printType(*this, id, TypePrintPolicy{TypePrintTarget::kTrust});
    if (!printed) {
        FAULT("getTypeName: {}", printed.error());
    }
    return std::move(*printed);
}

std::expected<std::string, std::string> TypePrinter::getCppTypeName(TypeId id) const {
    // Кодоген-проекция: C++-имя ВСЕГДА с namespace-путём (`c_ns::c_Point`).
    return printType(*this, id, TypePrintPolicy{TypePrintTarget::kCpp, /*showDesugared=*/false, TypePrintScope::kQualified});
}

std::expected<std::string, std::string> printType(const TypePrinter& reg, QualType state, TypePrintPolicy policy) {
    const TypeId id = state.getTypeId();
    if (policy.target == TypePrintTarget::kCpp) {
        return printCppName(reg, id, state.isConst(), policy.scope); // нет C++-представления у отсутствующего типа - отказ
    }
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

std::expected<std::string, std::string> TypePrinter::getCppTypeName(QualType state) const {
    return printType(*this, state, TypePrintPolicy{TypePrintTarget::kCpp, /*showDesugared=*/false, TypePrintScope::kQualified});
}

} // namespace trust
