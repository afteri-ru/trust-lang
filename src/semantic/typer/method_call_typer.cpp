// src/semantic/typer/method_call_typer.cpp
// Типизация вызова метода: поиск сигнатур, выбор перегрузки, привязка аргументов, многоточие и
// тип результата. Доступ к объекту остаётся в `AccessResolver`.
#include "semantic/typer/method_call_typer.hpp"
#include "semantic/core/name_resolution.hpp"
#include "semantic/typer/ellipsis.hpp"
#include "semantic/typer/overload_call.hpp"
#include "semantic/typer/call_binding.hpp"
#include "types/call.hpp"
#include "semantic/verify/format_check.hpp"
#include "semantic/solver.hpp"
#include "symbols/symbol_table.hpp"
#include "semantic/typer/type_inference.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "utils/strings.hpp"
#include <algorithm>
#include <format>
#include <string>

namespace trust {

// -- Вызов метода на объекте: obj.method(args) --
// По типу объекта ищет метод в реестре типов (TypeSystem::findMethod), проверяет наличие и
// количество аргументов по сигнатуре, типизирует результат возвращаемым типом. Метод - это
// функциональный тип (метод и функция - одно и то же), поэтому проверка аргументов идёт по
// FunctionTypeData::paramTypes единым путём с функциями. Проверка происходит ДО генерации C++.
void MethodCallTyper::typeMethodCall(Binary& n) {
    auto& call = static_cast<CallExpr&>(*n.m_right);
    const std::string mname = call.m_callee ? std::string(requiredText(*call.m_callee)) : std::string();
    // Аргументы вызова МЕТОДА анализируем ЗДЕСЬ: общий обход детей для MemberAccess не выполняется
    // (analyzeNode для MemberAccess/ArrayAccess делает ранний return в AccessResolver), поэтому без
    // этого шага аргументы метода не получали бы ни резолва имён, ни типов (ни диагностик).
    // Порядок: сначала аргументы (нужны типы для проверок), затем сигнатура метода.
    if (call.m_args) {
        for (auto& arg : *call.m_args) {
            if (arg) {
                m_core.analyzeNode(arg);
            }
        }
    }
    // Многоточие `obj.m(a, ... expr ...)`/`obj.m(a, ...)`: разбор ЕДИНЫМ примитивом
    // (semantic/ellipsis), как у вызовов функций; арность проверяется с учётом числа позиций.
    const EllipsisInfo einfo = scanCallEllipsis(call);
    // Не-const: instantiateRangeMethod интернирует функциональный тип (мутирует реестр).
    TypeSystem& reg = m_actx.ctx().types();
    const TypeId objType = n.m_left ? reg.read().store().desugaredTypeId(m_actx.facts().exprType(*n.m_left)) : INVALID_TYPE_ID;
    if (objType == INVALID_TYPE_ID) {
        // Тип объекта неизвестен (напр. Any) - сигнатуру метода получить нельзя; многоточие не может
        // быть раскрыто (кодогенерация не должна получить нераскрытое многоточие).
        if (einfo.form != EllipsisForm::None) {
            m_actx.ctx().diag().report(Severity::Error, call.range(), "аргументы вызова: число параметров вызываемого метода неизвестно");
            discardEllipsisElements(*call.m_args, false);
            m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
            return;
        }
        // Не можем проверить метод; типизируем как Any.
        n.resultType = n.commonType = QualType::fromTypeId(m_actx.ctx().readTypes().getType(type_generic::Any));
        m_actx.facts().setExprState(&n, n.resultType);
        return;
    }
    // Тип объекта сохраняем в lhsType (для кодгена: нативное имя метода через findMethodInfo и
    // const_cast<const T&>). Кодген не может восстановить его сам (скоуп-стек сброшен к глобальному).
    n.lhsType = QualType::fromTypeId(objType);
    const auto methodInfo = reg.read().methods().findMethodInfo(objType, mname);
    if (!methodInfo) {
        const std::string tname = std::string(reg.read().printer().getTypeName(objType));
        m_actx.ctx().diag().report(Severity::Error, n.range(), "type '{}' has no method '{}'", tname, mname);
        if (call.m_args) {
            discardEllipsisElements(*call.m_args, false);
        }
        m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
        return;
    }
    // МЕТОД - НАБОР сигнатур (перегрузки). Эффективные сигнатуры (подстановка T→Elem для
    // контейнеров и типовых аргументов native-template) строит ЕДИНЫЙ примитив слоя types.
    // C++-суффикс метода берётся из methodInfo->entries по индексу кандидата: у инстанса
    // record-шаблона suffixes скопированы с шаблона (методы - те же объявления), поэтому
    // совпадают с именем члена в C++.
    const TypeId canonicalObj = reg.read().store().desugaredTypeId(objType);
    if (const RecordTypeData* rdata = reg.read().store().recordData(canonicalObj); rdata != nullptr && rdata->templateOf != INVALID_TYPE_ID) {
        const auto tmplMethods = reg.read().methods().findMethodInfo(rdata->templateOf, mname);
        EXPECT(tmplMethods.has_value() && tmplMethods->entries.size() == methodInfo->entries.size() &&
               "method set of a record template instance must be declared on its template");
    }
    const std::vector<TypeId> sigs = effectiveMethodSignatures(reg, objType, *methodInfo);
    if (reg.read().store().isNativeTemplateType(objType) && std::find(sigs.begin(), sigs.end(), INVALID_TYPE_ID) != sigs.end()) {
        m_actx.ctx().diag().report(Severity::Error, n.range(),
                                   "method '{}' of native template '{}' cannot be instantiated: multiple type parameters are not supported", mname,
                                   reg.read().printer().getTypeName(objType));
        m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
        return;
    }

    // Выбор сигнатуры. Перегрузку/конверсии аргументов разрешает АНАЛИЗАТОР ВСЕГДА (в т.ч. при
    // единственной сигнатуре и для нативных методов): ошибка типа аргумента диагностируется здесь,
    // а не C++-слоем. Многоточие (материализация аргументов) допустимо только при единственной
    // сигнатуре.
    const bool hasEllipsis = (einfo.form != EllipsisForm::None);
    TypeId funcType = INVALID_TYPE_ID;
    bool bindingDone = false;
    bool anyDefaults = false;
    for (std::size_t i = 0; i < sigs.size(); ++i) {
        if (const CallableParamInfo* info = methodParamInfoAt(*methodInfo, i); info != nullptr && info->hasAnyDefault()) {
            anyDefaults = true;
            break;
        }
    }
    const bool named = hasNamedArguments(call);
    if (named || anyDefaults) {
        if (hasEllipsis) {
            m_actx.ctx().diag().report(Severity::Error, call.range(), "named/default arguments are not supported with ellipsis for method '{}'", mname);
            discardEllipsisElements(*call.m_args, false);
            m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
            return;
        }
        const std::string what = std::format("method '{}' of type '{}'", mname, reg.read().printer().getTypeName(objType));
        const std::vector<AstNodePtr> emptyArgs;
        const auto& rawArgs = call.m_args ? *call.m_args : emptyArgs;
        BoundCandidateSet gathered =
            gatherBoundCandidates(m_actx, rawArgs, sigs, [&](std::size_t idx, TypeId) { return methodParamInfoAt(*methodInfo, idx); }, what);
        auto& candidates = gathered.candidates;
        auto& boundArgs = gathered.bound;
        if (candidates.empty()) {
            reportBoundCandidatesFailure(m_actx, call.range(), gathered, what);
            m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
            return;
        }
        const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
        if (r.chosen == INVALID_TYPE_ID) {
            reportBoundResolutionFailure(m_actx, call.range(), r, what);
            m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
            return;
        }
        funcType = r.chosen;
        // bound-аргументы и суффикс - ПО ИНДЕКСУ кандидата (два объявления могут делить TypeId).
        EXPECT(r.chosenIndex < boundArgs.size() && "method call: chosen candidate index is out of range");
        call.boundArgs = std::move(boundArgs[r.chosenIndex].values);
        if (reg.read().store().isRecordType(reg.read().store().desugaredTypeId(objType)) && !op::isOperatorFunctionName(mname)) {
            // C++-имя метода - identity-суффикс ЕГО объявления (у инстанса шаблона suffixes
            // скопированы с шаблона, поэтому совпадают с эмитированным именем члена).
            n.resolvedMethodSuffix = methodCppSuffix(*methodInfo, r.chosenIndex);
        }
        bindingDone = true;
    }
    if (!bindingDone) {
        if (sigs.size() == 1 && hasEllipsis) {
            // Многоточие: сигнатура известна до материализации аргументов (позиции не разрешаются
            // по типам). Проверка типов выполняется после разворота списка (ниже).
            funcType = sigs.front();
            if (reg.read().store().isRecordType(reg.read().store().desugaredTypeId(objType)) && !op::isOperatorFunctionName(mname)) {
                n.resolvedMethodSuffix = methodCppSuffix(*methodInfo, 0);
            }
        } else {
            if (hasEllipsis) {
                m_actx.ctx().diag().report(Severity::Error, call.range(), "method '{}' of type '{}' is overloaded; ellipsis in arguments is not supported",
                                           mname, reg.read().printer().getTypeName(objType));
                discardEllipsisElements(*call.m_args, false);
                m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
                return;
            }
            const std::vector<TypeId> argTypes = callArgTypes(m_actx, call);
            const OverloadResolution r = resolveCallOverload(m_actx, sigs, argTypes, call.range(),
                                                             std::format("method '{}' of type '{}'", mname, reg.read().printer().getTypeName(objType)));
            const TypeId chosen = r.chosen;
            if (chosen == INVALID_TYPE_ID) {
                m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
                return;
            }
            funcType = chosen;
            // C++-имя пользовательского Record-метода - identity-суффикс объявления, безусловно
            // (не зависит от перегруженности имени в скоупе). Нативные имена фиксированы внешней
            // библиотекой - суффикс не добавляется; операторы - `operator<sym>`.
            if (reg.read().store().isRecordType(reg.read().store().desugaredTypeId(objType)) && !op::isOperatorFunctionName(mname)) {
                n.resolvedMethodSuffix = methodCppSuffix(*methodInfo, r.chosenIndex);
            }
        }
    }

    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(funcType);
    EXPECT(fd && "handleMethodCall: method signature is not a function type");
    const size_t nargs = call.m_args ? call.m_args->size() : 0;
    if (einfo.form != EllipsisForm::None) {
        // ЕДИНЫЙ примитив (как у вызовов функций): структурные правила + capacity/типы +
        // МАТЕРИАЛИЗАЦИЯ списка аргументов (кодоген многоточия не видит).
        if (!validateEllipsis(einfo, call, m_actx, "аргументы вызова")) {
            discardEllipsisElements(*call.m_args, false);
            m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
            return;
        }
        if (!expandCallEllipsis(call, m_actx, einfo, fd->paramTypes, fd->variadicType != INVALID_TYPE_ID)) {
            m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
            return;
        }
    } else if (!bindingDone && sigs.size() == 1) {
        // Единственная сигнатура: арность проверяется здесь ТЕМ ЖЕ правилом, что и в
        // resolveCall (variadic допускает хвост, фиксированная сигнатура - строгое равенство);
        // у набора арность проверяет резолвер.
        const bool variadic = fd->variadicType != INVALID_TYPE_ID;
        const bool arityOk = variadic ? nargs >= fd->paramTypes.size() : nargs == fd->paramTypes.size();
        if (!arityOk) {
            m_actx.ctx().diag().report(Severity::Error, call.range(), "method '{}' of type '{}' expects {} argument(s), got {}", mname,
                                       reg.read().printer().getTypeName(objType), fd->paramTypes.size(), nargs);
            m_actx.facts().setExprType(&n, INVALID_TYPE_ID);
            return;
        }
    }
    n.resultType = n.commonType = QualType::fromTypeId(fd->returnType);
    m_actx.facts().setExprType(&n, fd->returnType);
}

} // namespace trust
