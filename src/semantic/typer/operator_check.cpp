// Валидация объявления перегружаемого оператора (см. semantic/typer/operator_check.hpp).
// Идентичность оператора — системное dunder-имя (`__eq__`, ...); допустимый набор и правила
// member/free/арности — ЕДИНЫЙ источник types/operator_registry.hpp.
#include "types/type_read.hpp"
#include "semantic/typer/operator_check.hpp"
#include "diag/diag.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "utils/error.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace trust::semantic {

namespace {

// Postfix `++`/`--` в C++ имеет ровно один dummy-параметр типа int. В Trust он объявляется
// ровно как `Int32` (эмитится как C++ `int`): любой другой целый тип сделал бы два postfix-
// объявления неразличимыми в C++ (повторное определение `operator++(int)`).
bool isInt32DummyParam(Context& ctx, const FuncDecl& f, size_t index) {
    if (!f.m_params || index >= f.m_params->size()) {
        return false;
    }
    const AstNodePtr& p = (*f.m_params)[index];
    if (!p || p->kind() != ParserToken::Kind::ArgNode) {
        return false;
    }
    const auto& arg = static_cast<const ArgNode&>(*p);
    if (!arg.m_type) {
        return false;
    }
    // Маркер ссылки (RefMake/RefTake) не является именем типа Int32-заглушки.
    const HasText* typeText = arg.m_type->as_text();
    if (typeText == nullptr) {
        return false;
    }
    // Текстовый домен ДОПУСТИМ только для reserved builtin-имён: пользовательский тип не может
    // перекрыть встроенное имя (`reserveTypeName`), поэтому `findType(text)` здесь однозначен.
    const auto tid = ctx.readTypes().findType(typeText->text());
    if (!tid.has_value()) {
        return false;
    }
    const TypeId int32 = ctx.readTypes().store().desugaredTypeId(ctx.readTypes().getType(type::Int32));
    return ctx.readTypes().store().desugaredTypeId(*tid) == int32;
}

} // namespace

bool validateOperatorDecl(Context& ctx, const FuncDecl& f, bool isMember) {
    const std::string_view name = f.text();
    // Оператор-ШАБЛОН (<T> __eq__(...)) не реализован: явная ошибка (не молчаливый пропуск).
    if (f.m_templateParams.has_value()) {
        ctx.diag().report(Severity::Error, f.range(), "template operators are not implemented");
        return false;
    }
    const op::OperatorInfo* info = op::findByFunctionName(name);
    if (info == nullptr) {
        ctx.diag().report(Severity::Error, f.range(), "system function '{}' is not a known operator (system names must be declared in the operator registry)",
                          name);
        return false;
    }
    const std::string_view sym = info->symbol;
    if (isMember && !info->allowMember) {
        ctx.diag().report(Severity::Error, f.range(), "operator '{}' cannot be declared as a class method", sym);
        return false;
    }
    if (!isMember && !info->allowFree) {
        ctx.diag().report(Severity::Error, f.range(), "operator '{}' must be declared as a class method", sym);
        return false;
    }
    if (!f.m_type) {
        ctx.diag().report(Severity::Error, f.range(), "operator '{}' must have an explicit return type", sym);
        return false;
    }
    const size_t params = (f.m_params ? f.m_params->size() : 0);
    if (info->arity == op::Arity::kIncDec) {
        // Prefix: 0 (member) / 1 (free). Postfix: 1 (member) / 2 (free), последний - dummy int.
        const size_t prefixParams = isMember ? 0u : 1u;
        const size_t postfixParams = isMember ? 1u : 2u;
        if (params == postfixParams) {
            if (!isInt32DummyParam(ctx, f, params - 1)) {
                ctx.diag().report(Severity::Error, f.range(), "operator '{}' postfix form requires an 'Int32' dummy parameter (emitted as C++ int)", sym);
                return false;
            }
            return true;
        }
        if (params != prefixParams) {
            ctx.diag().report(Severity::Error, f.range(), "operator '{}' in this form expects {} (prefix) or {} (postfix) parameter(s), got {}", sym,
                              prefixParams, postfixParams, params);
            return false;
        }
        return true;
    }
    if (const std::optional<size_t> expected = op::operatorParamCount(info->arity, isMember); expected.has_value() && params != *expected) {
        ctx.diag().report(Severity::Error, f.range(), "operator '{}' in this form expects {} parameter(s), got {}", sym, *expected, params);
        return false;
    }
    return true;
}

bool operatorHasUserOperand(const TypeReadModel& reg, TypeId signature) {
    const auto* fd = reg.store().getTypeDataAs<FunctionTypeData>(getUnqualifiedType(signature));
    EXPECT(fd != nullptr && "operatorHasUserOperand: signature must be a function type");
    for (const TypeId p : fd->paramTypes) {
        const TypeId c = reg.store().desugaredTypeId(p);
        if (c != INVALID_TYPE_ID && (reg.store().isRecordType(c) || reg.store().isNativeClassType(c) || reg.store().isNativeTemplateType(c))) {
            return true;
        }
    }
    return false;
}

} // namespace trust::semantic
