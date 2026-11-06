// Зона печати: номинальное Trust-имя (kTrust/kDiagnostic).
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

/// Номинальное имя в терминах Trust (`shared<Int32>`, `unique<Int32, FreeDeleter>`, `Range<Elem>`,
/// `Array<Elem>`): ВИД ссылки показывается, а не только базовый тип. Структурные типы без
/// собственного имени (сигнатуры) дают пустую строку - это их номинальное имя.
std::expected<std::string, std::string> printTrustName(const TypePrinter& reg, TypeId id, TypePrintScope scope) {
    const RefType rt = typeRefOf(id);
    if (rt != RefType::kValue) {
        if (rt == RefType::kMptr) {
            const auto* mp = reg.store().getTypeDataAs<MemberPointerTypeData>(id);
            if (mp == nullptr) {
                return std::unexpected(std::format("printType: member pointer id {} has no MemberPointerTypeData", id.raw()));
            }
            auto mem = printTrustName(reg, mp->memberType, scope);
            if (!mem) {
                return mem;
            }
            auto cls = printTrustName(reg, mp->classType, scope);
            if (!cls) {
                return cls;
            }
            return *mem + " " + *cls + "::*";
        }
        auto pointee = printTrustName(reg, reg.store().getPointeeType(id), scope);
        if (!pointee) {
            return pointee;
        }
        std::string deleter;
        if (rt == RefType::kUnique) {
            if (const auto* rd = reg.store().getTypeDataAs<RefTypeData>(id); rd && rd->deleterType != INVALID_TYPE_ID) {
                auto del = printTrustName(reg, rd->deleterType, scope);
                if (!del) {
                    return del;
                }
                deleter = std::move(*del);
            }
        }
        // Sync-backend: политика - второй аргумент отображаемого имени (`shared<T, Policy>`).
        if (rt == RefType::kShared || rt == RefType::kWeak) {
            if (const auto* rd = reg.store().getTypeDataAs<RefTypeData>(id); rd && rd->accessPolicyType != INVALID_TYPE_ID) {
                auto policy = printTrustName(reg, rd->accessPolicyType, scope);
                if (!policy) {
                    return policy;
                }
                return std::string(refTypeName(rt)) + "<" + *pointee + ", " + *policy + ">";
            }
        }
        return refTypeDisplayName(rt, *pointee, deleter);
    }
    if (!reg.store().hasDescriptor(id)) {
        return std::unexpected(std::format("printType: no descriptor for type id {}", id.raw()));
    }
    // Параметризованные контейнеры: имя строится из элементного типа (структурный тип
    // собственного имени не имеет).
    if (reg.store().isRangeType(id)) {
        auto elem = printTrustName(reg, reg.store().rangeElementType(id), scope);
        if (!elem) {
            return elem;
        }
        return "Range<" + *elem + ">";
    }
    if (reg.store().isArrayType(id)) {
        auto elem = printTrustName(reg, reg.store().arrayElementType(id), scope);
        if (!elem) {
            return elem;
        }
        return "Array<" + *elem + ">";
    }
    const std::string_view name = reg.store().nameOf(id);
    if (name.empty()) {
        // Структурные типы (кортеж, сигнатура функции) НОМИНАЛЬНОГО имени не имеют: в
        // диагностике такое имя было бы пустой строкой. Печатаем явную метку отсутствия
        // имени (отличается от «нет типа» и от «неизвестный тип id»).
        return std::format("<unnamed type #{}>", id.raw());
    }
    const std::string_view namespacePath = reg.store().namespacePath(id);
    if (scope == TypePrintScope::kQualified && !reg.store().isFilePrivate(id) && !namespacePath.empty()) {
        return utils::qualifyTrustName(namespacePath, name);
    }
    return std::string(name);
}

} // namespace trust
