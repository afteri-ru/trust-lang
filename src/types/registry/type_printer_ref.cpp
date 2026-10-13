// src/types/registry/type_printer_ref.cpp
// Зона печати: вид ссылки в C++-имени (обёртки shared/weak/unique и суффиксы).
#include "types/registry/type_printer.hpp"

#include "types/group.hpp"
#include "types/names/storage_key.hpp"
#include "types/names/type_names.hpp"
#include "types/registry/builtin_registry.hpp"
#include "types/registry/type_printer_detail.hpp"
#include "types/registry/type_store.hpp"
#include "types/type/ref_type.hpp"
#include "types/typekind.hpp"
#include "utils/strings.hpp"

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace trust {

std::expected<std::string, std::string> printCppRefWrapper(const TypePrinter& reg, TypeId id, std::string base, TypePrintScope scope) {
    const RefType rt = getRefType(getTypeKind(id));
    // Единый источник C++-имени вида ссылки (types/ref_type.hpp). Deleter внешнего ресурса
    // (`@[deleter(D)]`) - ЧАСТЬ типа `trust::Unique<T, D>`: рендерим D из RefTypeData::deleterType.
    std::string deleterCpp;
    if (rt == RefType::kUnique) {
        if (const auto* rd = reg.store().getTypeDataAs<RefTypeData>(id); rd && rd->deleterType != INVALID_TYPE_ID) {
            auto del = printCppName(reg, rd->deleterType, false, scope);
            if (!del) {
                return std::unexpected(std::move(del.error()));
            }
            deleterCpp = std::move(*del);
        }
    }
    // Sync-backend: shared/weak с политикой -> trust::AccessShared<T, Policy> (weak оборачивает его).
    // Композиция по РЕАЛЬНЫМ типам реестра: обёртка `type::AccessShared` и политика (RefTypeData::
    // accessPolicyType) - никакие строки-имена сюда не передаются. Политика всегда структурный узел,
    // поэтому RefTypeData тут присутствует.
    if (rt == RefType::kShared || rt == RefType::kWeak) {
        if (const auto* rd = reg.store().getTypeDataAs<RefTypeData>(id); rd && rd->accessPolicyType != INVALID_TYPE_ID) {
            const auto syncId = reg.store().builtinIdByName(type::AccessShared);
            if (syncId == INVALID_TYPE_ID) {
                return std::unexpected(std::format("printType: sync wrapper type '{}' not found in the registry", type::AccessShared));
            }
            auto wrapper = printCppName(reg, syncId, false, scope);
            auto policy = printCppName(reg, rd->accessPolicyType, false, scope);
            if (!wrapper) {
                return std::unexpected(std::move(wrapper.error()));
            }
            if (!policy) {
                return std::unexpected(std::move(policy.error()));
            }
            // Класс реализации механизма доступа (3-й аргумент reftype) - 3-й параметр AccessShared.
            std::string implArg;
            if (rd->implType != INVALID_TYPE_ID) {
                auto impl = printCppName(reg, rd->implType, false, scope);
                if (!impl) {
                    return std::unexpected(std::move(impl.error()));
                }
                implArg = std::move(*impl);
            }
            const std::string strong = *wrapper + "<" + base + ", " + *policy + (implArg.empty() ? "" : (", " + implArg)) + ">";
            if (rt == RefType::kWeak) {
                return std::string(refTypeCppTemplateName(RefType::kWeak)) + "<" + strong + ">";
            }
            return strong;
        }
    }
    if (rt == RefType::kUnique) {
        // Внешний ресурс (`@[deleter(D)]`) требует указательной обёртки: trust::Unique<T, D>.
        if (!deleterCpp.empty()) {
            return refTypeCppName(RefType::kUnique, base, deleterCpp);
        }
        // Монопольное владение без deleter'а: обёртка StaticUnique (inline, zero-cost; имя из реестра).
        const auto wrapId = reg.store().builtinIdByName(type::StaticUnique);
        if (wrapId == INVALID_TYPE_ID) {
            return std::unexpected(std::format("printType: wrapper type '{}' not found in the registry", type::StaticUnique));
        }
        auto wrap = printCppName(reg, wrapId, false, scope);
        if (!wrap) {
            return std::unexpected(std::move(wrap.error()));
        }
        return *wrap + "<" + base + ">";
    }
    return refTypeCppName(rt, base, deleterCpp);
}

} // namespace trust
