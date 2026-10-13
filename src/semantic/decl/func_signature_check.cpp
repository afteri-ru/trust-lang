// src/semantic/decl/func_signature_check.cpp
// Проверки сигнатуры объявления функции (см. include/semantic/decl/func_signature_check.hpp).
#include "types/type_read.hpp"
#include "semantic/core/ast_view.hpp"
#include "semantic/decl/func_signature_check.hpp"
#include "semantic/core/pass.hpp"
#include "semantic/typer/operator_check.hpp"
#include "semantic/typer/type_set.hpp"
#include "symbols/decl_binders.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "session/context.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/type_data.hpp"
#include "types/types.hpp"
#include "types/typekind.hpp"
#include "utils/error.hpp"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace trust::semantic {

bool validateReservedFuncName(Context& ctx, const FuncDecl& func_node) {
    const std::string name{func_node.text()};
    if (!op::isSystemName(name) || name == "__main__" || op::isOperatorFunctionName(name)) {
        return true;
    }
    ctx.diag().report(Severity::Error, func_node.range(), "system name '{}' is reserved; only registered operator system functions may be declared", name);
    return false;
}

bool validateGenericMethodDecl(Context& ctx, const FuncDecl& func_node, bool in_type_body) {
    if (!func_node.m_templateParams.has_value() || func_node.m_isNativeTemplateImport || !in_type_body) {
        return true;
    }
    ctx.diag().report(Severity::Error, func_node.range(), "generic methods are not implemented yet");
    return false;
}

bool validateTypeSetSignature(AnalysisContext& actx, const FuncDecl& func_node) {
    Context& ctx = actx.ctx();
    if (func_node.m_type && func_node.m_type->kind() == ParserToken::Kind::TypeSet) {
        if (validateTypeSet(static_cast<const Sequence&>(*func_node.m_type), actx)) {
            ctx.diag().report(Severity::Error, func_node.m_type->range(), "type sets in a function return type are not implemented yet");
        }
        return false;
    }
    bool has_type_set = false;
    forEachArgParam(func_node.m_params, [&](const ArgNode& arg, std::size_t) {
        if (has_type_set || !arg.m_type || arg.m_type->kind() != ParserToken::Kind::TypeSet) {
            return;
        }
        has_type_set = true;
        if (validateTypeSet(static_cast<const Sequence&>(*arg.m_type), actx)) {
            ctx.diag().report(Severity::Error, arg.m_type->range(), "type sets in a function parameter type are not implemented yet");
        }
    });
    return !has_type_set;
}

bool validateNativeForwardDecl(Context& ctx, const FuncDecl& func_node) {
    const std::string name{func_node.text()};
    if (func_node.m_body.has_value() || name.empty() || name[0] != '%' || func_node.m_type) {
        return true;
    }
    ctx.diag().report(Severity::Error, func_node.range(), "native function '{}' must have a return type in a forward declaration", name);
    return false;
}

void checkStackCheckAttr(Context& ctx, const FuncDecl& func_node) {
    const AttrPool& attrs = ctx.attrs();
    const auto sc = attrs.lookup(attr::StackCheck);
    if (!sc.has_value() || !func_node.has_attr(*sc)) {
        return;
    }
    const auto* args = func_node.attr_args(*sc);
    if (args == nullptr || args->empty()) {
        return;
    }
    if (args->size() != 1) {
        ctx.diag().report(Severity::Error, func_node.range(), "@[stack_check@] expects zero or one integer argument (stack size in bytes)");
        return;
    }
    const std::string& value = args->at(0);
    bool digits_only = !value.empty();
    for (const char c : value) {
        if (c < '0' || c > '9') {
            digits_only = false;
            break;
        }
    }
    if (!digits_only) {
        ctx.diag().report(Severity::Error, func_node.range(), "@[stack_check({})@]: argument must be a non-negative integer (stack size in bytes)", value);
    }
}

void checkReturnTypeQualifiers(Context& ctx, const FuncDecl& func_node) {
    if (!func_node.m_type) {
        return;
    }
    const AstNodeAttr* type_attr = func_node.m_type->as_attr();
    if (type_attr == nullptr) {
        return;
    }
    const AttrPool& attrs = ctx.attrs();
    const auto reject = [&](std::string_view attr_name) {
        const auto id = attrs.lookup(attr_name);
        if (!id.has_value() || !type_attr->has_attr(*id)) {
            return;
        }
        ctx.diag().report(Severity::Error, type_attr->range(),
                          "attribute '{}' is not supported on a function return type (qualifiers apply to variable/parameter declarations only)", attr_name);
    };
    reject(attr::Deleter);
    reject(attr::Lifetime);
    reject(attr::Pin);
}

void checkReturnRefMarkers(Context& ctx, const FuncDecl& func_node) {
    if (!func_node.m_type) {
        return;
    }
    const auto kind = refKindOfTypeNode(func_node.m_type.get());
    if (!kind.has_value() || !isSmartRefKind(*kind)) {
        return;
    }
    ctx.diag().report(Severity::Error, func_node.m_type->range(),
                      "short reference markers are not allowed in a function return type; use the explicit attribute @[reftype(\"...\")]");
}

bool validateOperatorForm(Context& ctx, const FuncDecl& func_node, bool in_type_body, bool inside_function) {
    if (!func_node.m_isOperator || in_type_body) {
        return true;
    }
    if (inside_function) {
        ctx.diag().report(Severity::Error, func_node.range(), "a free operator must be declared at module scope");
        return false;
    }
    return validateOperatorDecl(ctx, func_node, /*isMember=*/false);
}

bool validateTemplateParamsUsed(Context& ctx, const FuncDecl& func_node, TypeId func_type) {
    if (!func_node.m_templateParams.has_value()) {
        return true;
    }
    TypeSystem& types = ctx.types();
    std::vector<TypeId> used_params;
    collectTemplateParams(types, func_type, used_params);
    bool ok = true;
    forEachArgParam(func_node.m_templateParams, [&](const ArgNode& binder, std::size_t) {
        if (!ok) {
            return;
        }
        const TypeId param_id = types.interner().getOrCreateTemplateParamType(binder.text());
        if (std::find(used_params.begin(), used_params.end(), param_id) != used_params.end()) {
            return;
        }
        ctx.diag().report(Severity::Error, binder.range(), "template parameter '{}' is not used in the signature", binder.text());
        ok = false;
    });
    return ok;
}

bool validateFreeOperatorOperand(Context& ctx, const FuncDecl& func_node, TypeId func_type) {
    if (!func_node.m_isOperator) {
        return true;
    }
    if (!operatorHasUserOperand(ctx.types(), func_type)) {
        ctx.diag().report(Severity::Error, func_node.range(), "free operator '{}' must have at least one parameter of a user-defined type",
                          std::string_view{func_node.text()});
        return false;
    }
    // Модуль входит в идентичность, поэтому один и тот же operator для одного типа мог бы быть
    // объявлен в разных модулях; у operator-функции нет синтаксиса квалификации. Правило (как ADL
    // в C++/D): свободный оператор живёт в модуле типа одного из операндов — тогда у клиента
    // ровно один кандидат (иначе — неоднозначность без способа её разрешить).
    if (func_node.module.isValid()) {
        const auto* sig = ctx.readTypes().store().getTypeDataAs<FunctionTypeData>(func_type);
        bool same_module = false;
        if (sig != nullptr) {
            for (const TypeId param : sig->paramTypes) {
                if (ctx.readTypes().store().moduleOf(getUnqualifiedType(param)) == func_node.module) {
                    same_module = true;
                    break;
                }
            }
        }
        if (!same_module) {
            ctx.diag().report(Severity::Error, func_node.range(),
                              "free operator '{}' must be declared in the module of one of its parameter types (module isolation)",
                              std::string_view{func_node.text()});
            return false;
        }
    }
    return true;
}

PackParamInfo collectPackParams(const FuncDecl& func_node) {
    // Единый экстрактор binder-контекста объявления: pack-вид уже вычислен в bindersOf.
    PackParamInfo info;
    for (const TemplateBinder& binder : bindersOf(func_node)) {
        if (!binder.isPack) {
            continue;
        }
        ++info.count;
        info.names.insert(binder.name);
    }
    return info;
}

bool validatePackDeclCount(Context& ctx, const FuncDecl& func_node, const PackParamInfo& pack_info) {
    if (pack_info.count > 1) {
        ctx.diag().report(Severity::Error, func_node.range(), "multiple type parameter packs are not supported (pack zipping is not implemented)");
        return false;
    }
    if (pack_info.count == 0) {
        return true;
    }
    bool has_variadic = false;
    forEachArgParam(func_node.m_params, [&](const ArgNode& arg, std::size_t) {
        if (has_variadic || !isVariadicParamMarker(&arg)) {
            return;
        }
        has_variadic = true;
        ctx.diag().report(Severity::Error, arg.range(), "C-style variadic '...' cannot be combined with a type parameter pack");
    });
    return !has_variadic;
}

bool validatePackSlotShape(Context& ctx, const FuncDecl& func_node, const PackParamInfo& pack_info) {
    const std::size_t nparams = func_node.m_params.has_value() ? func_node.m_params->size() : 0;
    std::size_t pack_slots = 0;
    bool invalid = false;
    forEachArgParam(func_node.m_params, [&](const ArgNode& arg, std::size_t index) {
        if (invalid || !arg.isTypePack) {
            return;
        }
        // Только ПОСЛЕДНИЙ слот может быть pack-ом - поэтому больше одного слота не бывает:
        // любой предыдущий отвергается здесь же.
        ++pack_slots;
        if (index + 1 != nparams) {
            invalid = true;
            ctx.diag().report(Severity::Error, arg.range(), "type parameter pack must be the last parameter (non-trailing packs are not implemented)");
            return;
        }
        if (pack_info.count == 0) {
            invalid = true;
            ctx.diag().report(Severity::Error, arg.range(), "function parameter pack '{}' requires a template parameter declared as '<T...>'", arg.text());
        }
    });
    if (invalid) {
        return false;
    }
    if (pack_info.count == 1 && pack_slots == 0) {
        const std::string pack_name = *pack_info.names.begin();
        ctx.diag().report(Severity::Error, func_node.range(), "type parameter pack '{}' must be used as a trailing function parameter pack ('xs: {}...')",
                          pack_name, pack_name);
        return false;
    }
    return true;
}

namespace {

/// Контекст проверки позиции pack-параметра.
enum class PackUsageMode {
    ReturnType,       ///< возврат: pack допустим только внутри спискового контекста
    NonPackParameter, ///< обычный параметр: pack запрещён в любой позиции
};

/// Проверка позиции pack-параметра при обходе типа. Списковость позиции определяется ВИДОМ
/// РОДИТЕЛЯ (аргументы шаблона), а не отдельным полем: перечень type-полей задан единственным
/// исчерпывающим обходчиком types/type_traversal.hpp.
class PackUsageChecker {
  public:
    PackUsageChecker(const TypeReadModel& reg, const PackParamInfo& pack_info, PackUsageMode mode, DiagnosticEngine& diag, MapperRange range)
    : m_reg(reg)
    , m_pack_info(pack_info)
    , m_mode(mode)
    , m_diag(diag)
    , m_range(range) {}

    [[nodiscard]] bool check(TypeId type) { return visit(type, /*list_context=*/false); }

  private:
    /// Аргументы шаблона - списковая позиция (pack раскрывается); остальные type-поля одиночные.
    [[nodiscard]] bool isListParent(TypeId type) const {
        if (m_reg.store().isNativeTemplateType(type)) {
            return true;
        }
        const auto* record = m_reg.store().getTypeDataAs<RecordTypeData>(type);
        return record != nullptr && record->templateOf != INVALID_TYPE_ID;
    }

    [[nodiscard]] bool visit(TypeId raw, bool list_context);

    const TypeReadModel& m_reg;
    const PackParamInfo& m_pack_info;
    PackUsageMode m_mode;
    DiagnosticEngine& m_diag;
    MapperRange m_range;
    std::unordered_set<TypeId> m_seen;
};

bool PackUsageChecker::visit(TypeId raw, bool list_context) {
    if (raw == INVALID_TYPE_ID) {
        return true;
    }
    const TypeId type = m_reg.store().identityOf(raw, TypeIdentityScope::kCanonical);
    if (!m_seen.insert(type).second) {
        return true;
    }
    if (m_reg.store().isPackExpansionType(type)) {
        // Раскрытие `T...`: паттерн обязан быть типовым параметром, объявленным с '...'.
        const TypeId pattern = m_reg.store().desugaredTypeId(m_reg.store().packExpansionPattern(type));
        if (!m_reg.store().isTemplateParamType(pattern)) {
            m_diag.report(Severity::Error, m_range, "type parameter pack expansion requires a type parameter");
            return false;
        }
        const std::string name{m_reg.store().templateParamName(pattern)};
        if (m_pack_info.names.count(name) == 0) {
            m_diag.report(Severity::Error, m_range, "type parameter '{}' is used as a pack expansion but is not declared with '...'", name);
            return false;
        }
        return true;
    }
    if (m_reg.store().isTemplateParamType(type)) {
        const std::string name{m_reg.store().templateParamName(type)};
        if (m_pack_info.names.count(name) == 0) {
            return true;
        }
        if (m_mode == PackUsageMode::NonPackParameter) {
            m_diag.report(Severity::Error, m_range, "type parameter packs are only allowed as the trailing pack parameter ('xs: T...')");
            return false;
        }
        if (!list_context) {
            m_diag.report(Severity::Error, m_range,
                          "type parameter pack '{}' cannot be used in a single-type position; expand it in a type-argument list (e.g. StdTuple<{}...>)", name,
                          name);
            return false;
        }
        return true;
    }
    const std::optional<TypeData>& data = m_reg.store().getTypeData(type);
    if (!data.has_value()) {
        return true;
    }
    const bool children_in_list = isListParent(type);
    bool ok = true;
    forEachTypeChild(*data, [&](TypeId child) { ok = ok && visit(child, children_in_list); });
    return ok;
}

} // namespace

bool validatePackUsage(Context& ctx, const FuncDecl& func_node, TypeId func_type, const PackParamInfo& pack_info) {
    if (pack_info.count == 0) {
        return true;
    }
    const TypeReadModel types = ctx.readTypes();
    const auto* fn_data = types.store().getTypeDataAs<FunctionTypeData>(func_type);
    // Тип сигнатуры построен buildFuncType до этой проверки: нефункциональный тип невозможен.
    EXPECT(fn_data != nullptr && "pack usage check requires a function-type signature");
    const ArgNode* pack_slot = nullptr;
    forEachArgParam(func_node.m_params, [&](const ArgNode& arg, std::size_t) {
        if (arg.isTypePack) {
            pack_slot = &arg;
        }
    });
    // Форму слота проверил validatePackSlotShape (до buildFuncType): объявленный pack без слота
    // невозможен. Это внутренний инвариант, а не входные данные - fallback не предусмотрен.
    EXPECT(pack_slot != nullptr && "declared type parameter pack must have a trailing parameter slot");
    // Тип pack-слота: ровно типовой параметр, объявленный как `<T...>`.
    const TypeId pattern = fn_data->paramPackType;
    const TypeId pattern_canonical = pattern == INVALID_TYPE_ID ? INVALID_TYPE_ID : types.store().desugaredTypeId(pattern);
    if (pattern_canonical == INVALID_TYPE_ID || !types.store().isTemplateParamType(pattern_canonical) ||
        pack_info.names.count(std::string(types.store().templateParamName(pattern_canonical))) == 0) {
        ctx.diag().report(Severity::Error, pack_slot->range(), "the type of pack parameter '{}' must be a type parameter declared with '...'",
                          pack_slot->text());
        return false;
    }
    // Обычные параметры: pack-параметр недопустим (nested pack-паттерны не поддержаны).
    std::size_t slot_index = 0;
    bool ok = true;
    forEachArgParam(func_node.m_params, [&](const ArgNode& arg, std::size_t) {
        if (!ok || isVariadicParamMarker(&arg) || arg.isTypePack || slot_index >= fn_data->paramTypes.size()) {
            return;
        }
        PackUsageChecker checker(types, pack_info, PackUsageMode::NonPackParameter, ctx.diag(), arg.m_type ? arg.m_type->range() : arg.range());
        ok = checker.check(fn_data->paramTypes[slot_index++]);
    });
    if (!ok) {
        return false;
    }
    if (func_node.m_type && fn_data->returnType != INVALID_TYPE_ID) {
        PackUsageChecker checker(types, pack_info, PackUsageMode::ReturnType, ctx.diag(), func_node.m_type->range());
        if (!checker.check(fn_data->returnType)) {
            return false;
        }
    }
    return true;
}

} // namespace trust::semantic
