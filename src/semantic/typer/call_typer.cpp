// Типизация вызовов: формат-строки, выбор перегруженной/шаблонной сигнатуры,
// привязка аргументов и материализация многоточия. Выделено из ExprTyper как
// отдельная зона ответственности.
#include "types/type_read.hpp"
#include "semantic/verify/format_check.hpp"
#include "semantic/typer/call_typer.hpp"
#include "semantic/core/name_resolution.hpp"
#include "semantic/typer/ellipsis.hpp"
#include "semantic/verify/format_check.hpp"
#include "semantic/solver.hpp"
#include "semantic/typer/overload_call.hpp"
#include "semantic/typer/call_binding.hpp"
#include "symbols/symbol_table.hpp"
#include "semantic/typer/type_inference.hpp"
#include "types/call.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "semantic/ref/ref_kind.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "utils/strings.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include <algorithm>
#include <format>
#include <string>

namespace trust {

namespace {

bool isStaticMemberSymbol(const Symbol& sym) {
    return sym.storage == Storage::Static && sym.decl != nullptr && sym.decl->is<FuncDecl>() && utils::isStaticName(sym.name);
}

} // namespace

void CallTyper::failCall(CallExpr& call) const {
    // Единая точка отказа вызова: диагностику к этому моменту уже выдал вызывающий код.
    m_actx.facts().setExprType(&call, INVALID_TYPE_ID);
}

// Типизация вызова: проверка формата, результат функции/лямбды, чтение аргументов, многоточие.
void CallTyper::typeCallNode(CallExpr& call) {
    // Строка-формат `"{}"(args)` / `'{}'(args)`: callee - строковый литерал. Результат -
    // строка той же ширины (StrWide/StrChar), как у литерала. + компиляйт-тайм проверка.
    if (call.m_callee && (call.m_callee->kind() == ParserToken::Kind::StrWide || call.m_callee->kind() == ParserToken::Kind::StrChar)) {
        const bool wide = call.m_callee->kind() == ParserToken::Kind::StrWide;
        const TypeId t = m_actx.ctx().readTypes().getType(wide ? type::StrWide : type::StrChar);
        if (t != INVALID_TYPE_ID) {
            m_actx.facts().setExprState(&call, QualType::fromTypeId(t).withInferred());
        }
        checkFormatStringArgs(call);
        return;
    }
    // printf-формат (атрибут @[format]) - проверка типов аргументов (пост-порядково).
    checkFormatArgs(call);
    // Обычный вызов пользовательской функции: типизируем результат возвращаемым типом
    // сигнатуры (как handleMethodCall для методов). Это чинит `p := f(10)` → int32_t.
    // Метод-вызов obj.method(args) обрабатывается отдельно (analyzeAccess/handleMethodCall). Если тип не резолвится - остаётся Any.
    if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::Ident) {
        if (const Symbol* s = m_core.resolveSimple(call.m_callee.get(), requiredText(*call.m_callee))) {
            const FuncDecl* fdecl = (s->decl && s->decl->is<FuncDecl>()) ? s->decl->as<FuncDecl>() : nullptr;
            // Нативная функция (`%name` / импорт-алиас `name := %native` / нативный шаблон-тип):
            // перегрузку и конверсии аргументов разрешает C++-слой - типы заданы явно нативными,
            // имя символа фиксировано внешней библиотекой (не манглируется).
            // ВАЖНО: шаблонный native-import (`<T> run(...) := %...`) сюда НЕ входит - его
            // типовые параметры обязана вывести семантика (иначе результат останется `T`).
            const bool genericImport = fdecl != nullptr && fdecl->isNativeImport() && fdecl->m_templateParams.has_value();
            const bool nativeFunc = fdecl != nullptr && !genericImport && fdecl->isNativeImport();
            // Статический член нативного класса: имя фиксировано внешней библиотекой (не
            // манглируется), но арность/типы/выбор overload'а проверяет анализатор.
            if (isStaticMemberSymbol(*s)) {
                typeStaticMemberCall(call, *s);
                return;
            }
            if (nativeFunc) {
                if (typeNativeCall(call, *s, fdecl)) {
                    return;
                }
            }
            if (fdecl != nullptr && !nativeFunc) {
                if (typeTrustFunctionCall(call, *s, fdecl)) {
                    return;
                }
            } else if (const auto* fd = m_actx.ctx().readTypes().store().getTypeDataAs<FunctionTypeData>(s->type.getTypeId())) {
                // Нативная функция ИЛИ вызов ЗНАЧЕНИЯ функционального типа (переменная-лямбда):
                // результат - returnType (перегрузку/конверсии нативного - C++).
                m_actx.facts().setExprType(&call, fd->returnType);
            }
            // Конструктор по ИМЕНИ ТИПА (`Point(...)`, `MyStr(...)`): результат конструкции - сам тип.
            // Кодогенерация читает тип левого операнда доступа (`Point().field`) из
            // `CallExpr::resultState`; без материализации тип неизвестен, и доступ к полю уходил бы
            // в словарный путь (`.at()`). Шаблонные типы исключены: их тип-цель выводится контекстом
            // (`coerceTemplateCtor`) либо остаётся CTAD (`Box(...)`).
            TypeId denoted = call.m_callee->as<IdentName>()->resolvedTypeId;
            if (denoted == INVALID_TYPE_ID) {
                denoted = m_actx.types().typeNameOfSymbol(*s);
            }
            if (denoted != INVALID_TYPE_ID) {
                const TypeReadModel& reg = m_actx.ctx().readTypes();
                const TypeId d = reg.store().desugaredTypeId(denoted);
                if ((reg.store().isRecordType(d) && !reg.store().isRecordTemplate(d)) || reg.store().isNativeClassType(d)) {
                    m_actx.facts().setExprType(&call, d);
                }
            }
        }
    }
    // Немедленный вызов лямбды `( lambda )(args)`: callee - не идентификатор, а лямбда-выражение.
    // Тип результата - returnType функционального типа лямбды (типизирована пост-порядково).
    if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::FuncDecl) {
        const TypeId ct = m_actx.facts().exprType(*call.m_callee);
        if (const auto* fd = m_actx.ctx().readTypes().store().getTypeDataAs<FunctionTypeData>(ct)) {
            m_actx.facts().setExprType(&call, fd->returnType);
        }
    }
    if (typeCallOperator(call)) {
        return;
    }
    // Чтение значения в АРГУМЕНТЕ вызова: вариативные/слабо-типизированные цели (напр. `@print`)
    // не проверяют типы аргументов, поэтому чтение неинициализированной переменной-аргумента не
    // пришло бы через типизацию аргумента. Принудительно резолвим каждый аргумент-Ident как
    // value-read (репорт-дедуп на узле внутри exprType). Составные аргументы уже прочитаны
    // при своей типизации (вложенные операнды) — здесь только верхний слой-Ident.
    if (call.m_args) {
        for (const auto& a : *call.m_args) {
            if (a && a->kind() == ParserToken::Kind::Ident) {
                const TypeId _argRead = m_actx.facts().exprType(*a);
                (void)_argRead; // [[nodiscard]]; результат не нужен — важна проверка чтения
            }
        }
    }
    // Многоточие `f(a, ... expr ...)` / `f(a, ...)`: структурные правила + разворот списка
    // аргументов ЕДИНЫМ примитивом (semantic/ellipsis) - кодоген многоточия не видит.
    analyzeCallFilling(call);
    return;
}

// Вызов TrustLang-функции по имени: набор перегрузок, named/default binding, многоточие,
// generic-унификация и метаданные узла. true - вызов обработан (успех или диагностика),
// вызывающему нужно вернуться; false - это не вызов TrustLang-функции.
bool CallTyper::typeTrustFunctionCall(CallExpr& call, const Symbol& s, const FuncDecl* fdecl) {
    // TrustLang-функция: перегрузку разрешает АНАЛИЗАТОР ВСЕГДА (в т.ч. при единственной
    // сигнатуре) - выбор сигнатуры, тип результата и допустимость аргументов определяет
    // компилятор, а не C++-слой. Набор из одной сигнатуры - вырожденный случай.
    std::vector<TypeId> sigs = overloadSignatures(s);
    const EllipsisInfo einfo = scanCallEllipsis(call);
    bool anyDefaults = false;
    for (std::size_t i = 0; i < sigs.size(); ++i) {
        if (const CallableParamInfo* info = overloadParamInfoAt(s, i); info != nullptr && info->hasAnyDefault()) {
            anyDefaults = true;
            break;
        }
    }
    const bool named = hasNamedArguments(call);
    if (named && fdecl->m_templateParams.has_value()) {
        m_actx.ctx().diag().report(Severity::Error, call.range(), "named arguments are not supported for generic function '{}'", requiredText(*call.m_callee));
        failCall(call);
        return true;
    }
    if (anyDefaults && fdecl->m_templateParams.has_value()) {
        const std::size_t nargs = call.m_args ? call.m_args->size() : 0;
        const auto* firstFd = m_actx.ctx().readTypes().store().getTypeDataAs<FunctionTypeData>(sigs.front());
        if (firstFd != nullptr && nargs < firstFd->paramTypes.size()) {
            m_actx.ctx().diag().report(Severity::Error, call.range(), "default arguments are not supported for generic function '{}'",
                                       requiredText(*call.m_callee));
            failCall(call);
            return true;
        }
    }
    if ((named || anyDefaults) && !fdecl->m_templateParams.has_value()) {
        // Binding именованных/default аргументов. Generic-функции отложены: для них нужна
        // унификация шаблонов по bound-аргументам (отдельный этап).
        if (einfo.form != EllipsisForm::None) {
            m_actx.ctx().diag().report(Severity::Error, call.range(), "named/default arguments are not supported with ellipsis for '{}'",
                                       requiredText(*call.m_callee));
            discardEllipsisElements(*call.m_args, false);
            failCall(call);
            return true;
        }
        const std::string what = std::format("function '{}'", requiredText(*call.m_callee));
        const std::vector<AstNodePtr> emptyArgs;
        const auto& rawArgs = call.m_args ? *call.m_args : emptyArgs;
        BoundCandidateSet gathered = gatherBoundCandidates(m_actx, rawArgs, sigs, [&](std::size_t idx, TypeId) { return overloadParamInfoAt(s, idx); }, what);
        auto& candidates = gathered.candidates;
        auto& boundArgs = gathered.bound;
        if (candidates.empty()) {
            reportBoundCandidatesFailure(m_actx, call.range(), gathered, what);
            failCall(call);
            return true;
        }
        const OverloadResolution r = resolveCall({m_actx.registry().read(), m_actx.registry().interner()}, candidates);
        if (r.chosen == INVALID_TYPE_ID) {
            reportBoundResolutionFailure(m_actx, call.range(), r, what);
            failCall(call);
            return true;
        }
        const TypeId chosen = r.chosen;
        // bound-аргументы выбираются ПО ИНДЕКСУ: два объявления могут делить один
        // TypeId (позиционная identity), поэтому сравнение сигнатур неоднозначно.
        EXPECT(r.chosenIndex < boundArgs.size() && "call: chosen candidate index is out of range");
        call.boundArgs = boundArgumentNodes(boundArgs[r.chosenIndex]);
        call.resolvedSignature = chosen;
        call.resolvedCalleeSuffix = declCppSuffix(s, r.chosenIndex, m_actx.ctx().declCppFacts());
        materializeQualifiedCallee(call, s, r.chosenIndex);
        const auto* chosenFd = m_actx.ctx().readTypes().store().getTypeDataAs<FunctionTypeData>(chosen);
        m_actx.facts().setExprType(&call, chosenFd != nullptr ? chosenFd->returnType : INVALID_TYPE_ID);
        return true;
    }
    if (einfo.form != EllipsisForm::None) {
        // Многоточие в аргументах: позиции заполняются/повторяются - разрешать по типам
        // аргументов нельзя. Допустимо только при ЕДИНСТВЕННОЙ сигнатуре; материализацию
        // списка выполняет analyzeCallFilling (по resolvedSignature ниже).
        if (fdecl->m_templateParams.has_value()) {
            m_actx.ctx().diag().report(Severity::Error, call.range(), "ellipsis in arguments is not supported for generic function '{}'",
                                       requiredText(*call.m_callee));
            discardEllipsisElements(*call.m_args, false);
            failCall(call);
        } else if (sigs.size() == 1) {
            call.resolvedSignature = sigs.front();
            call.resolvedCalleeSuffix = declCppSuffix(s, 0, m_actx.ctx().declCppFacts());
            materializeQualifiedCallee(call, s, 0);
            if (const auto* fd = m_actx.ctx().readTypes().store().getTypeDataAs<FunctionTypeData>(sigs.front())) {
                m_actx.facts().setExprType(&call, fd->returnType);
            }
        } else {
            m_actx.ctx().diag().report(Severity::Error, call.range(), "function '{}' is overloaded; ellipsis in arguments is not supported",
                                       requiredText(*call.m_callee));
            discardEllipsisElements(*call.m_args, false);
            failCall(call);
        }
    } else {
        const std::vector<TypeId> argTypes = callArgTypes(m_actx, call);
        OverloadResolution r =
            resolveCallOverloadWithTemplates(m_actx, sigs, argTypes, call.range(), std::format("function '{}'", requiredText(*call.m_callee)));
        if (r.chosen != INVALID_TYPE_ID) {
            // Эффективная сигнатура вызова: подставленные типы, пак развёрнут.
            // Имя/суффикс — от ОБЪЯВЛЕННОЙ (r.chosen).
            if (r.effectiveSig == INVALID_TYPE_ID) {
                m_actx.ctx().diag().report(
                    Severity::Error, call.range(),
                    "cannot substitute template parameters into the return type (a type parameter pack is not allowed in a single-type position)");
                failCall(call);
                return true;
            }
            call.resolvedSignature = r.effectiveSig;
            // C++-имя функции - ПОРОЖДЁННОЕ ИДЕНТИЧНОСТЬЮ объявления (не зависит от
            // перегруженности имени в скоупе): суффикс берётся у выбранного кандидата.
            call.resolvedCalleeSuffix = declCppSuffix(s, r.chosenIndex, m_actx.ctx().declCppFacts());
            materializeQualifiedCallee(call, s, r.chosenIndex);
            // Типовые аргументы в порядке объявления (для явного `c_f<T...>(...)`).
            if (fdecl->m_templateParams.has_value()) {
                for (const auto& tp : *fdecl->m_templateParams) {
                    if (!tp) {
                        continue;
                    }
                    const TypeId pid = m_actx.registry().interner().getOrCreateTemplateParamType(requiredText(*tp));
                    for (const auto& b : r.bindings) {
                        if (b.param == pid) {
                            if (b.isPack) {
                                call.resolvedTemplateArgs.insert(call.resolvedTemplateArgs.end(), b.types.begin(), b.types.end());
                            } else if (!b.types.empty()) {
                                call.resolvedTemplateArgs.push_back(b.types.front());
                            }
                            break;
                        }
                    }
                }
            }
            const auto* effectiveFd = m_actx.ctx().readTypes().store().getTypeDataAs<FunctionTypeData>(r.effectiveSig);
            EXPECT(effectiveFd != nullptr && "resolved call signature must be a function type");
            m_actx.facts().setExprType(&call, effectiveFd->returnType);
        } else {
            failCall(call);
        }
    }
    return false;
}

// Вызов значения record/native-класса с объявленным оператором `()` (набор сигнатур,
// перегрузка/арность/многоточие, результат). true - обработка ПРЕРВАНА диагностикой
// (вызывающий обязан вернуться); false - вызова оператора нет либо он успешно типизирован
// (общие проверки аргументов выполняются ниже).
bool CallTyper::typeCallOperator(CallExpr& call) {
    // Вызов ЗНАЧЕНИЯ record/native-класса с объявленным оператором `()`: результат = его
    // returnType, арность/многоточие проверяются как у метода. Без `()` - явная ошибка
    // (не откладываем на C++). Не перехватываем: (1) именованные функции/значения
    // функционального типа (обработаны выше), (2) вызов-КОНСТРУКТОР по имени типа (`MyStr(...)`).
    if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::Ident) {
        TypeSystem& reg = m_actx.registry();
        const Symbol* calleeSym = m_core.resolveSimple(call.m_callee.get(), requiredText(*call.m_callee));
        // Callee обозначает тип — материализованный факт (`IdentName::resolvedTypeId`); если обход
        // ещё не записал его, используем уже разрешённый символ (shadowing учитывается).
        TypeId calleeType = call.m_callee->as<IdentName>()->resolvedTypeId;
        if (calleeType == INVALID_TYPE_ID && calleeSym != nullptr) {
            calleeType = m_actx.types().typeNameOfSymbol(*calleeSym);
            if (calleeType != INVALID_TYPE_ID) {
                m_actx.types().recordTypeName(*call.m_callee, calleeType);
            }
        }
        const bool isTypeName = calleeType != INVALID_TYPE_ID;
        const bool isNamedCallable = calleeSym != nullptr && ((calleeSym->decl != nullptr && calleeSym->decl->kind() == ParserToken::Kind::FuncDecl) ||
                                                              reg.read().store().getTypeDataAs<FunctionTypeData>(calleeSym->type.getTypeId()) != nullptr);
        const TypeId ct = (calleeSym != nullptr) ? calleeSym->type.getTypeId() : INVALID_TYPE_ID;
        const TypeId ctc = (ct != INVALID_TYPE_ID) ? reg.read().store().desugaredTypeId(ct) : INVALID_TYPE_ID;
        const bool userLike = ctc != INVALID_TYPE_ID && (reg.read().store().isRecordType(ctc) || reg.read().store().isNativeClassType(ctc) ||
                                                         reg.read().store().isNativeTemplateType(ctc));
        if (userLike && !isNamedCallable && !isTypeName) {
            const auto opInfo = reg.read().methods().findMethodInfo(ctc, op::callFunctionName());
            if (!opInfo) {
                m_actx.ctx().diag().report(Severity::Error, call.range(), "type '{}' has no operator '()'", reg.read().printer().typeNameOf(ctc));
                failCall(call);
                return true;
            }
            // `()`: ВСЕГДА через единый конвейер; native single-signature не обходится.
            // Эффективные сигнатуры (включая типовые аргументы native-template) - общий примитив.
            const std::vector<TypeId> opSigs = effectiveMethodSignatures(reg, ctc, *opInfo);
            const bool hasEllipsisOp = scanCallEllipsis(call).form != EllipsisForm::None;
            TypeId opFuncType = INVALID_TYPE_ID;
            bool opBindingDone = false;
            bool opAnyDefaults = false;
            for (std::size_t i = 0; i < opSigs.size(); ++i) {
                if (const CallableParamInfo* info = methodParamInfoAt(*opInfo, i); info != nullptr && info->hasAnyDefault()) {
                    opAnyDefaults = true;
                    break;
                }
            }
            const bool opNamed = hasNamedArguments(call);
            if ((opNamed || opAnyDefaults) && !hasEllipsisOp) {
                const std::string what = std::format("operator '()' of type '{}'", reg.read().printer().typeNameOf(ctc));
                const std::vector<AstNodePtr> emptyArgs;
                const auto& rawArgs = call.m_args ? *call.m_args : emptyArgs;
                BoundCandidateSet gathered =
                    gatherBoundCandidates(m_actx, rawArgs, opSigs, [&](std::size_t idx, TypeId) { return methodParamInfoAt(*opInfo, idx); }, what);
                auto& candidates = gathered.candidates;
                auto& boundArgs = gathered.bound;
                if (candidates.empty()) {
                    reportBoundCandidatesFailure(m_actx, call.range(), gathered, what);
                    failCall(call);
                    return true;
                }
                const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
                if (r.chosen == INVALID_TYPE_ID) {
                    reportBoundResolutionFailure(m_actx, call.range(), r, what);
                    failCall(call);
                    return true;
                }
                opFuncType = r.chosen;
                // bound-аргументы выбираются ПО ИНДЕКСУ: два объявления могут делить один TypeId
                // (позиционная identity), поэтому сравнение сигнатур неоднозначно.
                EXPECT(r.chosenIndex < boundArgs.size() && "call-operator: chosen candidate index is out of range");
                call.boundArgs = boundArgumentNodes(boundArgs[r.chosenIndex]);
                opBindingDone = true;
            }
            if (!opBindingDone) {
                if (opSigs.size() == 1 && hasEllipsisOp) {
                    opFuncType = opSigs.front();
                } else {
                    const std::vector<TypeId> argTypes = callArgTypes(m_actx, call);
                    // Единственная сигнатура: точная диагностика арности - текст ПРИНАДЛЕЖИТ форме.
                    // Variadic допускает хвост, поэтому сравнивать нужно нижнюю границу.
                    if (opSigs.size() == 1) {
                        const auto* only = reg.read().store().getTypeDataAs<FunctionTypeData>(opSigs.front());
                        const bool variadicOnly = only != nullptr && only->variadicType != INVALID_TYPE_ID;
                        const bool arityMismatch =
                            only != nullptr && (variadicOnly ? argTypes.size() < only->paramTypes.size() : argTypes.size() != only->paramTypes.size());
                        if (arityMismatch) {
                            m_actx.ctx().diag().report(Severity::Error, call.range(), "operator '()' of type '{}' expects {} argument(s), got {}",
                                                       reg.read().printer().typeNameOf(ctc), only->paramTypes.size(), argTypes.size());
                            failCall(call);
                            return true;
                        }
                    }
                    const CallFailureText failure{
                        std::format("call to overloaded operator '()' of type '{}' is ambiguous", reg.read().printer().typeNameOf(ctc)),
                        std::format("no matching overload for operator '()' of type '{}'", reg.read().printer().typeNameOf(ctc)),
                        std::format("operator '()' of native template '{}' cannot be instantiated: multiple type parameters are not supported",
                                    reg.read().printer().typeNameOf(ctc)),
                    };
                    const auto resolvedOp = resolveCallablePositional(m_actx, opSigs, argTypes, call.range(), failure);
                    if (!resolvedOp.has_value()) {
                        failCall(call);
                        return true;
                    }
                    opFuncType = *resolvedOp;
                }
            }
            const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(opFuncType);
            EXPECT(fd != nullptr && "call-operator: '()' must have a function type");
            const EllipsisInfo einfo = scanCallEllipsis(call);
            const size_t nargs = call.m_args ? call.m_args->size() : 0;
            if (einfo.form != EllipsisForm::None) {
                if (!validateEllipsis(einfo, call, m_actx, "аргументы вызова")) {
                    discardEllipsisElements(*call.m_args, false);
                    failCall(call);
                    return true;
                }
                if (!expandCallEllipsis(call, m_actx, einfo, fd->paramTypes, fd->variadicType != INVALID_TYPE_ID)) {
                    failCall(call);
                    return true;
                }
            } else if (!opBindingDone && opInfo->entries.size() == 1 && nargs != fd->paramTypes.size()) {
                m_actx.ctx().diag().report(Severity::Error, call.range(), "operator '()' of type '{}' expects {} argument(s), got {}",
                                           reg.read().printer().typeNameOf(ctc), fd->paramTypes.size(), nargs);
                failCall(call);
                return true;
            }
            if (opInfo->entries.size() > 1) {
                call.resolvedOperatorSignature = getUnqualifiedType(opFuncType);
            }
            m_actx.facts().setExprType(&call, fd->returnType);
        }
    }
    return false;
}

void CallTyper::materializeQualifiedCallee(CallExpr& call, const Symbol& symbol, std::size_t chosenIndex) {
    if (!call.m_callee || !call.m_callee->is<IdentName>()) {
        return;
    }
    EXPECT(chosenIndex < symbol.overloads.size() && "call: chosen overload index is out of range");
    const SymbolOverload& overload = symbol.overloads[chosenIndex];
    // ВЫБРАННОЕ объявление материализуется в узле независимо от наличия namespace-пути:
    // у модульного уровня путь пуст, но C++-имя/модуль берутся из `resolvedDecl`.
    if (overload.decl != nullptr) {
        auto* ident = call.m_callee->as<IdentName>();
        ident->resolvedDecl = overload.decl;
        if (overload.decl->module.isValid() && overload.decl->module != m_actx.symbols().currentModule()) {
            ident->moduleRef = overload.decl->module;
        }
    }
    if (overload.namespacePath.empty() || symbol.name.empty()) {
        return;
    }
    HasText* text = call.m_callee->as_text();
    EXPECT(text != nullptr && "Ident node must carry text");
    // Имя берём у ВЫБРАННОГО объявления (у алиаса `Symbol::name` — имя-алиас, а не цель);
    // путь достраивается единственной доменной функцией.
    const HasText* declText = overload.decl != nullptr ? overload.decl->as_text() : nullptr;
    const std::string_view entityName = declText != nullptr && !declText->text().empty() ? declText->text() : symbol.name;
    text->set_text_interned(utils::qualifyTrustName(overload.namespacePath, utils::shortNameOf(entityName)));
}

void CallTyper::typeStaticMemberCall(CallExpr& call, const Symbol& s) {
    if (hasNamedArguments(call)) {
        m_actx.ctx().diag().report(Severity::Error, call.range(), "named arguments are not supported for static member '{}'", requiredText(*call.m_callee));
        failCall(call);
        return;
    }
    TypeSystem& sreg = m_actx.registry();
    const std::vector<TypeId> argTypes = callArgTypes(m_actx, call);
    const std::vector<TypeId> sigs = overloadSignatures(s);
    const CallFailureText failure{std::format("call to overloaded static member '{}' is ambiguous", requiredText(*call.m_callee)),
                                  std::format("no matching overload for static member '{}' with {} argument(s)", requiredText(*call.m_callee), argTypes.size()),
                                  std::format("static member '{}' cannot be instantiated", requiredText(*call.m_callee)),
                                  /*ambiguousCount=*/true};
    const auto resolved = resolveCallablePositional(m_actx, sigs, argTypes, call.range(), failure);
    if (!resolved.has_value()) {
        failCall(call);
        return;
    }
    call.resolvedSignature = *resolved;
    call.resolvedNativeOverload = sigs.size() > 1;
    // C++-имя статического члена кодоген печатает по типу-владельцу (`m_nativeOwner`);
    // `resolvedDecl` фиксирует выбранное объявление (у перегрузок — один узел-член).
    if (call.m_callee->is<IdentName>()) {
        call.m_callee->as<IdentName>()->resolvedDecl = s.decl;
    }
    const auto* staticFd = sreg.read().store().getTypeDataAs<FunctionTypeData>(call.resolvedSignature);
    EXPECT(staticFd != nullptr && "static member signature must be a function type");
    m_actx.facts().setExprType(&call, staticFd->returnType);
    return;
}

bool CallTyper::typeNativeCall(CallExpr& call, const Symbol& s, const FuncDecl* fdecl) {
    if (hasNamedArguments(call)) {
        m_actx.ctx().diag().report(Severity::Error, call.range(), "named arguments are not supported for native function '{}'", requiredText(*call.m_callee));
        failCall(call);
        return true;
    }
    if (const CallableParamInfo* info = overloadParamInfoAt(s, 0); info != nullptr && info->hasAnyDefault()) {
        const std::size_t nargs = call.m_args ? call.m_args->size() : 0;
        if (nargs < info->slots.size()) {
            m_actx.ctx().diag().report(Severity::Error, call.range(), "default arguments are not supported for native function '{}'",
                                       requiredText(*call.m_callee));
            failCall(call);
            return true;
        }
    }
    // Перегрузка нативной функции. Символ C-линковки (`extern "C"`, имя без '::')
    // перегружать НЕЛЬЗЯ: у всех сигнатур одно C++-имя. C++-символ (`%lib::f`)
    // перегружается естественно: резолвим ради ТИПА РЕЗУЛЬТАТА и проверки аргументов,
    // имя символа при этом не манглируется.
    if (s.overloads.size() > 1) {
        // C++-имя символа: целевой факт объявления (импорт `name := %lib::f`; `DeclCppFacts`, R1).
        std::string nativeSymbolStorage;
        if (fdecl != nullptr) {
            nativeSymbolStorage = std::string(m_actx.ctx().declCppFacts().cppNameOf(fdecl->declId()));
        }
        if (nativeSymbolStorage.empty()) {
            nativeSymbolStorage = s.name;
        }
        const std::string_view nativeSymbol = nativeSymbolStorage;
        if (!utils::isQualifiedName(nativeSymbol)) {
            m_actx.ctx().diag().report(Severity::Error, call.range(),
                                       "overloaded native function '{}' is not supported: a C-linkage symbol cannot be overloaded (declare distinct names)",
                                       requiredText(*call.m_callee));
            failCall(call);
            return true;
        }
        const std::vector<TypeId> argTypes = callArgTypes(m_actx, call);
        const std::vector<TypeId> nativeSigs = overloadSignatures(s);
        const OverloadResolution r =
            resolveCallOverloadWithTemplates(m_actx, nativeSigs, argTypes, call.range(), std::format("native function '{}'", requiredText(*call.m_callee)));
        if (r.chosen == INVALID_TYPE_ID) {
            failCall(call);
            return true;
        }
        if (r.effectiveSig == INVALID_TYPE_ID) {
            m_actx.ctx().diag().report(
                Severity::Error, call.range(),
                "cannot substitute template parameters into the return type (a type parameter pack is not allowed in a single-type position)");
            failCall(call);
            return true;
        }
        call.resolvedSignature = r.effectiveSig;
        call.resolvedNativeOverload = true;
        const auto* effectiveFd = m_actx.ctx().readTypes().store().getTypeDataAs<FunctionTypeData>(r.effectiveSig);
        EXPECT(effectiveFd != nullptr && "native call signature must be a function type");
        m_actx.facts().setExprType(&call, effectiveFd->returnType);
        return true;
    }
    return false;
}

void CallTyper::checkFormatArgs(CallExpr& call) {
    if (!call.m_callee || call.m_callee->kind() != ParserToken::Kind::Ident) {
        return;
    }
    const Symbol* sym = m_core.resolveSimple(call.m_callee.get(), requiredText(*call.m_callee));
    if (!sym || !sym->decl || sym->decl->kind() != ParserToken::Kind::FuncDecl) {
        return;
    }
    const auto& f = static_cast<const FuncDecl&>(*sym->decl);
    const AttrPool& attrs = m_actx.ctx().attrs();
    auto fmt_id = attrs.lookup(attr::Format);
    if (!fmt_id.has_value() || !f.has_attr(*fmt_id)) {
        return;
    }
    const std::vector<std::string>* fargs = f.attr_args(*fmt_id);
    // @[format("printf", string_index, first_to_check)] - ровно три параметра.
    if (!fargs || fargs->size() != 3 || fargs->at(0) != attr::FormatPrintf) {
        return; // поддерживается только printf-архетип; параметры валидирует matches_params
    }
    int stringIdx = 0;
    int firstToCheck = 0;
    try {
        stringIdx = std::stoi(fargs->at(1));
        firstToCheck = std::stoi(fargs->at(2));
    } catch (...) {
        return;
    }
    if (stringIdx < 1 || firstToCheck < 1 || !call.m_args || static_cast<int>(call.m_args->size()) < stringIdx) {
        return;
    }
    // Формат-строка - аргумент stringIdx-1 (индексы 1-based); обязана быть строковым литералом.
    const auto& fmtArg = (*call.m_args)[stringIdx - 1];
    if (!fmtArg || fmtArg->kind() != ParserToken::Kind::StrChar) {
        m_actx.ctx().report(fmtArg ? fmtArg->range() : call.range(), semantic::DiagId::Format, "format string is not a string literal");
        return;
    }
    const std::string fmt(requiredText(*fmtArg));
    std::vector<format_check::Conversion> convs;
    if (!format_check::parse_printf_format(fmt, convs)) {
        m_actx.ctx().report(fmtArg->range(), semantic::DiagId::Format, "invalid printf format string '{}'", fmt);
        return;
    }
    const TypeReadModel& reg = m_actx.registry();
    for (std::size_t j = 0; j < convs.size(); ++j) {
        const int argPos = firstToCheck - 1 + static_cast<int>(j);
        if (argPos >= static_cast<int>(call.m_args->size())) {
            m_actx.ctx().report(call.range(), semantic::DiagId::Format,
                                "format string requires more arguments than provided (missing argument for conversion '%{}')", std::string(1, convs[j].conv));
            return;
        }
        const auto& arg = (*call.m_args)[argPos];
        if (!arg) {
            continue;
        }
        const TypeId argType = m_actx.facts().exprType(*arg);
        if (argType == INVALID_TYPE_ID) {
            continue;
        }
        bool ok = format_check::arg_matches_expect(reg, argType, convs[j].expect);
        // %s: StrChar-ЛИТЕРАЛ уже const char* в C++ (emitExpr → "..."), поэтому допустим,
        // хотя тип StrChar (std::string). Переменная StrChar требует .c_str() → CString.
        if (!ok && convs[j].expect == format_check::Expect::StrChar && reg.store().desugaredTypeId(argType) == reg.getType(type::StrChar) &&
            arg->kind() == ParserToken::Kind::StrChar) {
            ok = true;
        }
        if (!ok) {
            const TypeId c = reg.store().desugaredTypeId(argType);
            std::string typeName = (c != INVALID_TYPE_ID) ? std::string(reg.printer().typeNameOf(c)) : "?";
            m_actx.ctx().report(arg->range(), semantic::DiagId::Format, "format argument {} expects {} (conversion '{}') but argument has type '{}'",
                                argPos + 1, format_expect_name(convs[j].expect), std::string(1, convs[j].conv), typeName);
        }
    }
}

// callee - строковый литерал (StrWide/StrChar). Сверяем число плейсхолдеров `{}` с числом
// аргументов ({{ / }} - литеральные скобки, аргумент не потребляют) и баланс фигурных скобок.
void CallTyper::checkFormatStringArgs(CallExpr& call) {
    const auto* fmtNode = call.m_callee.get();
    if (!fmtNode || (fmtNode->kind() != ParserToken::Kind::StrChar && fmtNode->kind() != ParserToken::Kind::StrWide)) {
        return;
    }
    const std::string fmt(requiredText(*fmtNode));
    const size_t nArgs = call.m_args ? call.m_args->size() : 0;
    size_t placeholders = 0;
    int depth = 0;
    for (size_t i = 0; i < fmt.size(); ++i) {
        const char c = fmt[i];
        if (c == '{') {
            if (i + 1 < fmt.size() && fmt[i + 1] == '{') { // `{{` - литеральная скобка
                ++i;
                continue;
            }
            ++placeholders;
            ++depth;
            // Явный индекс `{N}` / `{N:spec}`: сверяем N с числом аргументов (иначе
            // std::format сгенерирует сломанный C++, а не понятную диагностику).
            size_t j = i + 1;
            if (j < fmt.size() && fmt[j] >= '0' && fmt[j] <= '9') {
                size_t idx = 0;
                while (j < fmt.size() && fmt[j] >= '0' && fmt[j] <= '9') {
                    idx = idx * 10 + static_cast<size_t>(fmt[j] - '0');
                    ++j;
                }
                if (idx >= nArgs) {
                    m_actx.ctx().report(fmtNode->range(), semantic::DiagId::Format,
                                        "format string '{}' references argument index {} but only {} argument(s) provided", fmt, idx, nArgs);
                }
            }
        } else if (c == '}') {
            if (i + 1 < fmt.size() && fmt[i + 1] == '}') { // `}}` - литеральная скобка
                ++i;
                continue;
            }
            --depth;
        }
    }
    if (depth != 0) {
        m_actx.ctx().report(fmtNode->range(), semantic::DiagId::Format, "unbalanced braces in format string '{}'", fmt);
        return;
    }
    if (placeholders != nArgs) {
        m_actx.ctx().report(fmtNode->range(), semantic::DiagId::Format, "format string '{}' has {} placeholder(s) but {} argument(s) provided", fmt,
                            placeholders, nArgs);
    }
}

// Многоточие в аргументах ВЫЗОВА ФУНКЦИИ (`... expr ...` / `...`): резолв сигнатуры + ЕДИНЫЙ

void CallTyper::analyzeCallFilling(CallExpr& call) {
    const EllipsisInfo info = scanCallEllipsis(call);
    if (info.form == EllipsisForm::None) {
        return;
    }
    // Структурные правила (единственное, последнее) - ДО резолва сигнатуры: форма проверяется
    // независимо от вызываемого.
    if (!validateEllipsis(info, call, m_actx, "аргументы вызова")) {
        discardEllipsisElements(*call.m_args, false);
        return;
    }
    // Сигнатура: именованная функция или значение функционального типа.
    const Symbol* sym = nullptr;
    if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::Ident) {
        sym = m_core.resolveSimple(call.m_callee.get(), requiredText(*call.m_callee));
    }
    // Сигнатура: для ПЕРЕГРУЖЕННОГО имени - выбранная семантикой (call.resolvedSignature);
    // иначе - единственная сигнатура символа.
    const FunctionTypeData* fd = nullptr;
    if (call.resolvedSignature != INVALID_TYPE_ID) {
        fd = m_actx.ctx().readTypes().store().getTypeDataAs<FunctionTypeData>(call.resolvedSignature);
    } else if (sym && sym->type.getTypeId() != INVALID_TYPE_ID) {
        fd = m_actx.ctx().readTypes().store().getTypeDataAs<FunctionTypeData>(sym->type.getTypeId());
    }
    if (fd == nullptr) {
        m_actx.ctx().diag().report(Severity::Error, call.range(), "аргументы вызова: число параметров вызываемой функции неизвестно");
        discardEllipsisElements(*call.m_args, false);
        return;
    }
    (void)expandCallEllipsis(call, m_actx, info, fd->paramTypes, fd->variadicType != INVALID_TYPE_ID);
}

} // namespace trust
