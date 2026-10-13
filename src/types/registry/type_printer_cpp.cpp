// src/types/registry/type_printer_cpp.cpp
// Зона печати: C++-представление значения (kCpp) — базовое имя и вид ссылки.
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

std::expected<std::string, std::string> printCppName(const TypePrinter& reg, TypeId id, bool isConst, TypePrintScope scope) {
    // Константность - ортогональный квалификатор: лидирующий `const `. RefType (вид ссылки,
    // биты 16-19) применяется к базовому имени pointee (суффикс `*`/`&`/`&&` либо обёртка
    // trust::Shared/trust::Weak/trust::Unique). Для вложенности (RefTypeData-узел)
    // базовое имя строится рекурсивно от pointee.
    const RefType rt = getRefType(getTypeKind(id));

    // Функциональный тип (FunctionTypeData) - ЗНАЧЕНИЕ-функция: C++-представление `std::function<Ret(Args...)>`.
    // Встречается у значений-лямбд и переменных функционального типа. INVALID returnType = void;
    // INVALID тип параметра (не выведен) - `std::any`.
    if (reg.store().getTypeDataAs<FunctionTypeData>(id) != nullptr) {
        const auto* ft = reg.store().getTypeDataAs<FunctionTypeData>(id);
        std::string name = "std::function<";
        if (ft->returnType == INVALID_TYPE_ID) {
            name += "void";
        } else {
            auto ret = printCppName(reg, ft->returnType, false, scope);
            if (!ret) {
                return std::unexpected(std::move(ret.error()));
            }
            name += *ret;
        }
        name += "(";
        for (size_t i = 0; i < ft->paramTypes.size(); ++i) {
            if (i) {
                name += ", ";
            }
            if (ft->paramTypes[i] == INVALID_TYPE_ID) {
                name += "std::any";
                continue;
            }
            auto p = printCppName(reg, ft->paramTypes[i], false, scope);
            if (!p) {
                return std::unexpected(std::move(p.error()));
            }
            name += *p;
        }
        name += ")>";
        if (isConst) {
            name = "const " + name;
        }
        return name;
    }

    // Базовое C++-имя pointee/значения.
    std::string base;
    if (const auto* data = reg.store().getTypeDataAs<RefTypeData>(id)) {
        // Структурный ссылочный узел: имя рекурсивно от pointee (вложенность).
        auto inner = printCppName(reg, data->pointeeType, false, scope);
        if (!inner) {
            return std::unexpected(std::move(inner.error()));
        }
        base = std::move(*inner);
    } else if (reg.store().isNativeClassType(id)) {
        // Forward-объявление нативного класса: C++-имя живёт в NativeClassTypeData (cppName
        // дескриптора пуст). Нужно, например, для рендера deleter-типа `unique<T, D>`, где
        // D - пользовательский нативный класс.
        base = std::string(reg.store().nativeClassCppName(id));
    } else if (const auto* tp = reg.store().getTypeDataAs<TemplateParamTypeData>(reg.store().desugaredTypeId(id)); tp != nullptr) {
        // Идентификатор типового параметра шаблона (`T`/`U`): C++-имя - само имя параметра
        // (внутри `template<typename T> struct ...` аргумент пишется как `T`).
        base = tp->name;
    } else {
        // Источники C++-имени в порядке приоритета: реализация встроенного типа (провайдер),
        // cppName дескриптора, алиас, инстанциация record-шаблона, манглинг trust-имени.
        const BuiltinDefinition* bdef = reg.builtins().getBuiltinDefinition(id);
        const TypeDescriptor* desc = reg.store().descriptorOf(id);
        if (bdef != nullptr && !bdef->cppName.empty()) {
            base = bdef->cppName;
        } else if (desc == nullptr) {
            return std::unexpected(std::format("printType: no descriptor for type id {}", id.raw()));
        } else if (!desc->cppName.empty()) {
            base = desc->cppName;
        } else if (desc->baseType != INVALID_TYPE_ID) {
            // Простой алиас (`Integer`, `MyInt ::= :Int32`): C++-имя - «как написано».
            // Пользовательский алиас сохраняет trust-имя (манглинг, совпадает с `using c_...`),
            // встроенный алиас маппится на канонический C++-тип.
            if (reg.store().isUserDefinedType(id)) {
                base = nominalCppName(reg, *desc, scope);
            } else {
                auto canon = printCppName(reg, reg.store().desugaredTypeId(id), false, scope);
                if (!canon) {
                    return canon;
                }
                base = std::move(*canon);
            }
        } else if (reg.store().isRecordType(id) || reg.store().getTypeDataAs<EnumTypeData>(reg.store().desugaredTypeId(id)) != nullptr ||
                   reg.store().getTypeDataAs<VariantTypeData>(reg.store().desugaredTypeId(id)) != nullptr) {
            // Инстанциация пользовательского шаблон-класса (`Box<Int32>`): `c_Box<int32_t>`
            // (аргументы - рекурсивно; имя шаблона - из дескриптора шаблона). Определение
            // (template<...> struct) эмитит кодогенератор. Абстрактный шаблон/обычный record/
            // enum/variant - манглинг trust-имени с namespace-путём (`c_ns::c_Point`).
            const auto* rd = reg.store().getTypeDataAs<RecordTypeData>(reg.store().desugaredTypeId(id));
            if (rd != nullptr && rd->templateOf != INVALID_TYPE_ID) {
                const TypeDescriptor* tplDesc = reg.store().descriptorOf(rd->templateOf);
                base = (tplDesc != nullptr ? nominalCppName(reg, *tplDesc, scope) : utils::cppIdent(reg.getTypeName(rd->templateOf))) + "<";
                for (size_t i = 0; i < rd->templateArgs.size(); ++i) {
                    if (i) {
                        base += ", ";
                    }
                    auto arg = printCppName(reg, rd->templateArgs[i], false, scope);
                    if (!arg) {
                        return arg;
                    }
                    base += *arg;
                }
                base += ">";
            } else {
                base = nominalCppName(reg, *desc, scope);
            }
        } else {
            return std::unexpected(reg.store().isBuiltinType(id)
                                       ? std::format("printType: implementation of built-in type '{}' is not defined", typeLabel(reg, id))
                                       : std::format("printType: type '{}' has no C++ name", typeLabel(reg, id)));
        }
    }

    // const применяется к базовому имени (pointee) перед обёрткой/суффиксом.
    if (isConst) {
        base = "const " + base;
    }

    if (rt == RefType::kMptr) {
        // Указатель на член: `MemberType Class::*` из структурного MemberPointerTypeData.
        const auto* mp = reg.store().getTypeDataAs<MemberPointerTypeData>(id);
        if (!mp) {
            return std::unexpected(std::format("printType: mptr requires MemberPointerTypeData, type '{}'", typeLabel(reg, id)));
        }
        auto cls = printCppName(reg, mp->classType, false, scope);
        if (!cls) {
            return cls;
        }
        auto mem = printCppName(reg, mp->memberType, false, scope);
        if (!mem) {
            return mem;
        }
        return *mem + " " + *cls + "::*";
    }
    return printCppRefWrapper(reg, id, std::move(base), scope);
}

} // namespace trust
