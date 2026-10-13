// src/semantic/decl/func_decl_analyzer.cpp
// Анализ объявлений функций: сигнатура (проверки - semantic/func_signature_check.*), параметры,
// defaults, лямбда-захваты и шаблонные native-импорты (функции-алиасы).
//
// Зоны `analyzeFuncDecl` (порядок вызовов = порядок проверок). `fatal` - при ошибке объявление не
// регистрируется и дальнейшие зоны НЕ выполняются; `report-only` - объявление продолжается;
// `control` - не проверка. Реализация проверок сигнатуры - src/semantic/decl/func_signature_check.cpp.
//   Z1  системное имя                                   fatal
//   Z2  generic-методы (заглушка)                        fatal
//   Z3  type-set в типе возврата (заглушка)              fatal
//   Z4  type-set в типе параметра (заглушка)             fatal
//   Z5  форма оператора (member/free)                    fatal, вычисляет isMemberOperator
//   Z6  нативный forward без типа возврата               fatal
//   Z7  аргумент @[stack_check]                          report-only
//   Z8  deleter/lifetime/pin на типе возврата             report-only (порядок = порядок атрибутов)
//   Z9  короткие ref-маркеры на возврате                  report-only
//   Z10 member-оператор: выход без регистрации            control
//   Z11 несколько pack-параметров / конфликт с `...`      fatal
//   Z12 форма pack-слота (trailing, объявлен, один)       fatal (ДО buildFuncType)
//   Z13 buildFuncType + типовой параметр в сигнатуре      предусловие + fatal
//   Z14 свободный оператор: пользовательский операнд     fatal
//   Z15 использование pack в типах сигнатуры             fatal
//   Z16 символ/cppSuffix/declareOrComplete/onDeclare     регистрация
// Порядок закреплён lit-кейсами test/lit/pipeline/diag/funcdecl/order_*.src.
#include "semantic/decl/func_decl_analyzer.hpp"
#include "semantic/core/ast_view.hpp"
#include "semantic/decl/exported_signature_check.hpp"
#include "semantic/core/name_resolution.hpp"
#include "semantic/decl/func_signature_check.hpp"
#include "semantic/decl/type_completeness.hpp"
#include "semantic/typer/call_binding.hpp"
#include "symbols/symbol_table.hpp"
#include "session/module_resolver.hpp"
#include "types/call.hpp"
#include "attrs/attr_args.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/typekind.hpp"
#include "utils/utils.hpp"
#include "utils/error.hpp"
#include "utils/strings.hpp"
#include <algorithm>
#include <string>
#include <unordered_set>

namespace trust {

namespace {

bool referencesParameter(const AstNodeBase* node, const std::vector<std::string>& params) {
    if (node == nullptr) {
        return false;
    }
    if (node->kind() == ParserToken::Kind::Ident) {
        return std::find(params.begin(), params.end(), std::string(requiredText(*node))) != params.end();
    }
    for (const auto& child : node->children()) {
        if (child && referencesParameter(child.get(), params)) {
            return true;
        }
    }
    return false;
}

} // namespace

// АЛИАС функции: `f = \path::g;`. Имя `f` адресует НАБОР перегрузок цели; объявление-цель
// остаётся единственной идентичностью (вызовы нормализуются на него, C++-функция не эмитится).
void FuncDeclAnalyzer::analyzeNamespaceAlias(FuncDecl& func_node) {
    EXPECT(func_node.aliasKind() == AliasKind::Namespace && "analyzeNamespaceAlias requires a namespace alias");
    // Имя-алиас приходит с завершающим `::` (`ns::`, `::ns::`) - это namespace-алиас.
    const std::string_view alias = utils::namespaceAliasName(func_node.text());
    const AstNodeBase* target_node = func_node.aliasTarget().get();
    const auto* target = target_node != nullptr && target_node->is<IdentName>() ? target_node->as<IdentName>() : nullptr;
    if (target == nullptr || !target->moduleRef.has_value()) {
        m_actx.ctx().diag().report(Severity::Error, func_node.range(), "namespace alias target must name its module: write '{}:: :- \\path::ns::;'", alias);
        return;
    }
    const ModuleIndex module = *target->moduleRef;
    const std::string_view path = target->text();
    if (alias.empty() || path.empty()) {
        m_actx.ctx().diag().report(Severity::Error, func_node.range(), "namespace alias requires both an alias and a target path");
        return;
    }
    if (!m_actx.symbols().hasNamespaceInModule(module, path)) {
        m_actx.ctx().diag().report(Severity::Error, target->range(), "namespace '{}' not found in the target module", path);
        return;
    }
    if (module == m_actx.symbols().currentModule() && alias == path) {
        m_actx.ctx().diag().report(Severity::Error, func_node.range(), "namespace alias '{}::' aliases itself", alias);
        return;
    }
    const NamespaceAliasRecord record{m_actx.symbols().currentModule(),     internText(alias), module, internText(path),
                                      m_actx.symbols().currentScopeRange(), AliasOrigin::Local};
    if (!m_actx.symbols().aliases().recordAlias(m_actx.symbols().current(), record)) {
        m_actx.ctx().diag().report(Severity::Error, func_node.range(), "namespace alias '{}::' is already bound in this scope", alias);
        return;
    }
}

void FuncDeclAnalyzer::analyzeModuleAlias(FuncDecl& func_node) {
    EXPECT(func_node.aliasKind() == AliasKind::Module && "analyzeModuleAlias requires a module alias");
    if (m_actx.namespaces().currentFunc() != nullptr || m_actx.symbols().depth() != 2) {
        m_actx.ctx().diag().report(Severity::Error, func_node.range(), "module alias is allowed only at module level");
        return;
    }
    const AstNodeBase* target_node = func_node.aliasTarget().get();
    const auto* target = target_node != nullptr && target_node->is<IdentName>() ? target_node->as<IdentName>() : nullptr;
    if (target == nullptr) {
        m_actx.ctx().diag().report(Severity::Error, func_node.range(), "module alias target must name an already loaded module");
        return;
    }
    ModuleResolver* resolver = m_actx.ctx().moduleResolver();
    EXPECT(resolver != nullptr && "module alias requires a module resolver");
    if (!target->moduleRef.has_value()) {
        m_actx.ctx().diag().report(Severity::Error, func_node.range(), "module alias target must name an already loaded module");
        return;
    }
    const ModuleIndex owner = m_actx.symbols().currentModule();
    // Единственный ПИСАТЕЛЬ привязки — парсер (ссылки `\m::name` резолвятся до семантики);
    // здесь только валидация: привязка обязана уже указывать на эту цель.
    if (!resolver->hasModuleAlias(owner, func_node.text(), *target->moduleRef)) {
        m_actx.ctx().diag().report(Severity::Error, func_node.range(), "module alias '{}' is already bound in this module", func_node.text());
    }
}

void FuncDeclAnalyzer::analyzeNameAlias(FuncDecl& func_node) {
    EXPECT(func_node.aliasKind() == AliasKind::Name && "analyzeNameAlias requires a name alias");
    const AstNodeBase* target_node = func_node.aliasTarget().get();
    EXPECT(target_node != nullptr && "function alias must carry its target");
    if (!target_node->is<IdentName>()) {
        m_actx.ctx().diag().report(Severity::Error, func_node.range(), "function alias requires a qualified target name '\\path::name'");
        return;
    }
    const auto& target = *target_node->as<IdentName>();
    const Symbol* target_sym = nullptr;
    if (target.moduleRef.has_value()) {
        target_sym = m_actx.types().resolveModuleRef(target);
        if (target_sym == nullptr) {
            m_actx.types().reportModuleRefError(target);
            return;
        }
    } else {
        // Цель в ТЕКУЩЕМ модуле: qualified-имя области (`ns::h`) либо простое имя. C++-имя вызова
        // берётся из выбранной перегрузки (`SymbolOverload::namespacePath`), поэтому путь области
        // не требуется в узле.
        target_sym = m_actx.symbols().resolve(requiredText(target));
        if (target_sym == nullptr) {
            m_actx.ctx().diag().report(Severity::Error, target.range(), "undefined name '{}' in alias target", requiredText(target));
            return;
        }
    }
    // Алиас имени — второе имя ФУНКЦИИ или ЗНАЧЕНИЯ/КОНСТАНТЫ. Для типов используется `::-`.
    const bool valueTarget = target_sym->decl != nullptr && target_sym->decl->is<VarDecl>();
    if (target_sym->decl == nullptr || (!target_sym->decl->is<FuncDecl>() && !valueTarget)) {
        m_actx.ctx().diag().report(Severity::Error, target.range(),
                                   "name alias supports functions and values only; '{}' is not a value or function (use '::-' for types)",
                                   requiredText(target));
        return;
    }
    if (target_sym->decl != nullptr && target_sym->decl->is<FuncDecl>()) {
        const auto& fn = *target_sym->decl->as<FuncDecl>();
        if (fn.m_templateParams.has_value() && !fn.m_templateParams->empty()) {
            m_actx.ctx().diag().report(Severity::Error, func_node.range(),
                                       "function alias cannot target generic function '{}': template arguments are resolved at the call site", fn.text());
            return;
        }
    }
    if (!m_actx.symbols().bindAlias(func_node.text(), *target_sym, func_node)) {
        m_actx.ctx().diag().report(Severity::Error, func_node.range(), "alias name '{}' is already declared in this scope", func_node.text());
        return;
    }
    // Хуки видят АЛИАС как объявление: имя/диапазон/документация — узла-алиаса, тип и набор
    // перегрузок — цели (сам символ в таблице остаётся привязанным к объявлению-цели: ссылка
    // нормализуется на неё). Без уведомления имя-алиас отсутствует в SymbolIndex (LSP).
    Symbol alias_sym = *target_sym;
    alias_sym.name = stableSymbolName(func_node.text());
    alias_sym.decl = &func_node;
    for (auto& hook : m_core.hooks()) {
        hook->onDeclare(alias_sym);
    }
}

void FuncDeclAnalyzer::analyzeFuncDecl(FuncDecl& func_node) {
    switch (func_node.aliasKind()) {
    case AliasKind::None:
        break;
    case AliasKind::Name:
        analyzeNameAlias(func_node);
        return;
    case AliasKind::Namespace:
        analyzeNamespaceAlias(func_node);
        return;
    case AliasKind::Module:
        analyzeModuleAlias(func_node);
        return;
    }
    // Драйверное состояние вычисляется ОДИН раз и передаётся проверкам значением.
    const bool in_type_body = m_actx.namespaces().insideTypeBody();

    if (!semantic::validateReservedFuncName(m_actx.ctx(), func_node)) {
        return;
    }
    if (!semantic::validateGenericMethodDecl(m_actx.ctx(), func_node, in_type_body)) {
        return;
    }
    if (!semantic::validateTypeSetSignature(m_actx, func_node)) {
        return;
    }

    // Перегружаемый оператор (системное dunder-имя: `__eq__`, `__getitem__`, ...).
    const bool isMemberOperator = func_node.m_isOperator && in_type_body;
    if (!semantic::validateOperatorForm(m_actx.ctx(), func_node, in_type_body, m_actx.namespaces().currentFunc() != nullptr)) {
        return;
    }

    if (!semantic::validateNativeForwardDecl(m_actx.ctx(), func_node)) {
        return;
    }

    semantic::checkStackCheckAttr(m_actx.ctx(), func_node);
    semantic::checkReturnTypeQualifiers(m_actx.ctx(), func_node);
    semantic::checkReturnRefMarkers(m_actx.ctx(), func_node);

    // Оператор-МЕТОД регистрируется анализатором ТИПА как метод типа (MethodTable::addMethod) и в
    // таблицу символов НЕ попадает: системное имя не адресуемо как значение, а резолв использования
    // member-оператора идёт по типу операнда (findMethodInfo). Свободный оператор - обычный путь
    // функции: валидация выше + регистрация в скоупе (модуль), откуда его резолвит типизация.
    if (isMemberOperator) {
        return;
    }

    // -- Type parameter packs (function templates) ------------------------------------------
    const semantic::PackParamInfo pack_info = semantic::collectPackParams(func_node);
    if (!semantic::validatePackDeclCount(m_actx.ctx(), func_node, pack_info)) {
        return;
    }
    if (!semantic::validatePackSlotShape(m_actx.ctx(), func_node, pack_info)) {
        return;
    }
    const TypeId func_type = m_actx.types().buildFuncType(func_node);
    EXPECT(func_type != INVALID_TYPE_ID && "buildFuncType must always produce a function type");
    if (!semantic::validateTemplateParamsUsed(m_actx.ctx(), func_node, func_type)) {
        return;
    }
    if (!semantic::validateFreeOperatorOperand(m_actx.ctx(), func_node, func_type)) {
        return;
    }
    if (!semantic::validatePackUsage(m_actx.ctx(), func_node, func_type, pack_info)) {
        return;
    }
    // S1: file-private тип в сигнатуре экспортируемой функции недопустим (в TU импортёра
    // forward-decl ссылается на C++-имя из anonymous namespace).
    if (semantic::isExportedTopLevel(m_actx)) {
        semantic::checkExportedTypeVisibility(m_actx, func_type, func_node.range(), "function '" + std::string(func_node.text()) + "'");
    }
    // Неполный (forward) тип возврата: значение передаётся как объявлено, поэтому полнота
    // обязательна в ОПРЕДЕЛЕНИИ (объявление-прототип легально; параметры - в declareFuncParams).
    if (func_node.m_body.has_value()) {
        if (const auto* signature = m_actx.ctx().readTypes().store().getTypeDataAs<FunctionTypeData>(func_type)) {
            semantic::requireCompleteValueType(m_actx, func_node.m_type.get(), QualType::fromTypeId(signature->returnType), func_node.range(),
                                               "return type of function '" + std::string(func_node.text()) + "'");
        }
    }
    registerFuncSymbol(func_node, func_type);
}

// Z16: символ функции в текущем скоупе + уведомление хуков. Ошибки дубля/несовпадения forward-
// объявления решает ЕДИНЫЙ код SymbolTable::declareOrComplete (с binder-контекстом конкретного
// FuncDecl), поэтому повторных проверок здесь нет.
void FuncDeclAnalyzer::registerFuncSymbol(FuncDecl& func_node, TypeId func_type) {
    const std::string func_name{func_node.text()};
    Symbol sym;
    sym.name = stableSymbolName(func_name);
    sym.type = QualType::fromTypeId(func_type);
    sym.decl = &func_node;
    sym.overloads.push_back(SymbolOverload{func_type, callableParamInfo(func_node), &func_node});

    // Детерминированный C++-суффикс по идентичности объявления (позиционные binder'ы, алиасы
    // раскрыты). Ставим всегда: C++-имя функции - чистая функция от её identity и не зависит
    // от того, перегружено ли имя в текущем скоупе (иначе имена рассинхронизируются между
    // раздельно компилируемыми модулями).
    const FunctionIdentity& identity = m_actx.ctx().types().identity();
    func_node.m_cppSuffix = identity.cppSuffix(sym.type.getTypeId(), bindersOf(func_node));

    // Forward-объявление (без тела) может быть завершено последующим определением.
    switch (m_actx.symbols().declareOrComplete(sym)) {
    case DeclResult::AlreadyRegistered:
        return; // идемпотентная повторная регистрация (ромбовидный/повторный импорт)
    case DeclResult::Duplicate:
        m_actx.ctx().diag().report(Severity::Error, func_node.range(),
                                   "duplicate declaration '{}' (indistinguishable in C++: the same generated C++ name or the same native C++ "
                                   "signature; C++ cannot overload on return type)",
                                   func_name);
        return;
    case DeclResult::SignatureMismatch:
        m_actx.ctx().diag().report(Severity::Error, func_node.range(),
                                   "definition of '{}' does not match the forward declaration: parameter types/template positions, "
                                   "return type or trust conditions differ",
                                   func_name);
        return;
    default:
        break;
    }
    TRUST_DEBUG("declare", "func '{}' depth={}", func_name, m_actx.symbols().depth());
    for (auto& hook : m_core.hooks()) {
        hook->onDeclare(sym);
    }
}

// Проверка параметров со значениями по умолчанию после анализа тела: defaults только в хвосте,
// default не ссылается на другой параметр и его тип совместим с типом параметра.
void FuncDeclAnalyzer::validateFuncDefaults(const FuncDecl& func_node) {
    if (!func_node.m_params) {
        return;
    }
    std::vector<std::string> param_names;
    forEachArgParam(func_node.m_params, [&](const ArgNode& arg, std::size_t) {
        if (!isVariadicParamMarker(&arg)) {
            param_names.emplace_back(arg.text());
        }
    });
    bool sawDefault = false;
    forEachArgParam(func_node.m_params, [&](const ArgNode& pd, std::size_t) {
        if (isVariadicParamMarker(&pd) || pd.isTypePack) {
            return;
        }
        if (!pd.m_value) {
            if (sawDefault) {
                m_actx.ctx().diag().report(Severity::Error, pd.range(), "parameter '{}' without a default value follows a parameter with a default value",
                                           pd.text());
            }
            return;
        }
        sawDefault = true;
        if (referencesParameter(pd.m_value.get(), param_names)) {
            m_actx.ctx().diag().report(
                Severity::Error, pd.m_value->range(),
                "default value for parameter '{}' must not reference another parameter (C++ default arguments cannot depend on parameters)", pd.text());
            return;
        }
        const TypeId defaultType = m_actx.facts().exprType(*pd.m_value);
        const Symbol* param = m_actx.symbols().resolve(pd.text());
        if (param == nullptr || param->type.getTypeId() == INVALID_TYPE_ID) {
            return; // отсутствие/невалидность типа параметра уже диагностированы
        }
        if (defaultType == INVALID_TYPE_ID) {
            m_actx.ctx().diag().report(Severity::Error, pd.m_value->range(), "cannot determine the type of the default value for parameter '{}'", pd.text());
            return;
        }
        if (overloadConversionRank(m_actx.ctx().types(), defaultType, param->type.getTypeId()) < 0) {
            m_actx.ctx().diag().report(Severity::Error, pd.m_value->range(), "default value type '{}' is not convertible to parameter '{}' of type '{}'",
                                       m_actx.ctx().readTypes().printer().getTypeName(defaultType), pd.text(),
                                       m_actx.ctx().readTypes().printer().getTypeName(param->type.getTypeId()));
        }
    });
}

// Регистрация параметров в текущем (функционном) скоупе - вызывается из analyzeNode
// ВНУТРИ enterScope() скоупа функции, чтобы имена в теле функции резолвились.
void FuncDeclAnalyzer::declareFuncParams(FuncDecl& func_node) {
    if (!func_node.m_params) {
        return;
    }
    for (const auto& p : *func_node.m_params) {
        if (!p || p->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        auto& pd = static_cast<ArgNode&>(*p);
        if (op::isSystemName(pd.text())) {
            m_actx.ctx().diag().report(Severity::Error, p->range(), "system name '{}' is reserved for compiler-generated entities", pd.text());
            // Продолжаем объявление, чтобы не давать каскад «undefined name» в теле функции.
        }
        // Граница API: короткие (символьные) маркеры в типе параметра запрещены - сигнатура
        // функции является контрактом API; используйте явный @[reftype(...)]. Правило относится к
        // УМНЫМ маркерам (`&&`/`&*`/`&?`); нативные `%&`/`%*` - отдельная переходная ось.
        if (const auto pk = refKindOfTypeNode(pd.m_type.get()); pk.has_value() && isSmartRefKind(*pk)) {
            m_actx.ctx().diag().report(Severity::Error, pd.m_type->range(),
                                       "short reference markers are not allowed in a function parameter type; use the explicit attribute @[reftype(\"...\")]");
        }
        Symbol ps;
        ps.name = stableSymbolName(std::string(pd.text()));
        TypeId ptype = INVALID_TYPE_ID;
        if (pd.m_type) {
            if (const auto resolved = m_actx.types().resolveTypeRef(*pd.m_type); resolved.has_value()) {
                ptype = *resolved;
            } else {
                m_actx.ctx().diag().report(Severity::Error, pd.m_type->range(), "type '{}' not found", requiredText(*pd.m_type));
            }
        }
        // Квалификаторы аннотации (const/reftype/policy/impl) уже материализованы resolveTypeRef.
        QualType pstate = QualType::fromTypeId(ptype);
        if (pd.m_type && pd.m_type->kind() == ParserToken::Kind::TypeName) {
            const auto& annotation = static_cast<const IdentType&>(*pd.m_type);
            if (annotation.resolvedState.isValid()) {
                pstate = annotation.resolvedState;
            }
        }
        // Параметр-пакет: тип символа - маркер PackExpansion(T). Как значение он недопустим
        // (диагностика в lookupOrError); раскрытие в `T... xs` делает кодоген.
        if (pd.isTypePack && pstate.isValid()) {
            ptype = m_actx.ctx().types().interner().getOrCreatePackExpansionType(pstate.getTypeId());
            pstate = pstate.withIdentity(ptype);
        }
        ps.type = pstate;
        ps.decl = &pd;
        ps.storage = Storage::Local; // параметры функции - стек
        m_actx.symbols().declare(ps);
        // Неполный (forward) тип параметра допустим только за невладеющей ссылкой
        // (вид ссылки уже применён в `pstate`). Проверяется только ОПРЕДЕЛЕНИЕ: объявление
        // (C++ prototype) с неполным параметром легально, ошибка возникает при определении.
        if (!pd.isTypePack && func_node.m_body.has_value()) {
            semantic::requireCompleteValueType(m_actx, pd.m_type.get(), pstate, pd.range(),
                                               "parameter '" + std::string(pd.text()) + "' of '" + std::string(func_node.text()) + "'");
        }
        for (auto& hook : m_core.hooks()) {
            hook->onDeclare(ps);
        }
    }
}

// Лямбда-выражение: резолв захватов в ОБЪЁМЛЮЩЕМ скоупе (вызывается ДО enterScope лямбды).
// Захват - ТОЛЬКО имя переменной, ТОЛЬКО по значению (копия). Некопируемый (unique/move-only)
// захват по значению - ошибка (без молчаливого move).
void FuncDeclAnalyzer::analyzeLambdaCaptures(FuncDecl& func_node) {
    if (!func_node.m_captures) {
        return;
    }
    std::unordered_set<std::string> seen;
    for (const auto& cap : *func_node.m_captures) {
        if (!cap || cap->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        auto& c = static_cast<ArgNode&>(*cap);
        const std::string name(c.text());
        if (name.empty()) {
            continue;
        }
        // Локальные имена нормализованы в '$name' - резолвим оба варианта.
        const Symbol* s = m_actx.symbols().resolve(name);
        if (s == nullptr && name[0] != '$') {
            s = m_actx.symbols().resolve("$" + name);
        }
        if (s == nullptr) {
            m_actx.ctx().diag().report(Severity::Error, c.range(), "captured name '{}' is not declared in the enclosing scope", name);
            continue;
        }
        if (!seen.insert(name).second) {
            m_actx.ctx().diag().report(Severity::Error, c.range(), "duplicate lambda capture '{}'", name);
            continue;
        }
        // Эксклюзивное владение (unique/move-only) нельзя захватить по значению.
        if (s->type.getTypeId() != INVALID_TYPE_ID && refAxisOf(getRefType(getTypeKind(s->type.getTypeId()))) == RefAxis::Unique) {
            m_actx.ctx().diag().report(Severity::Error, c.range(),
                                       "cannot capture exclusive-ownership value '{}' by value; "
                                       "lambda capture-by-value requires a copyable value",
                                       name);
            continue;
        }
        c.resultType = s->type;   // тип захваченной переменной (копия)
        c.resolvedDecl = s->decl; // стабильная ссылка для пост-проходного DA
    }
}

} // namespace trust