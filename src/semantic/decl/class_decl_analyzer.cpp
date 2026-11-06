#include "semantic/decl/decl_analyzer.hpp"
#include "semantic/core/name_resolution.hpp"
#include "semantic/verify/format_check.hpp"
#include "semantic/typer/operator_check.hpp"
#include "semantic/typer/call_binding.hpp"
#include "semantic/solver.hpp"
#include "symbols/symbol_table.hpp"
#include "semantic/typer/type_inference.hpp"
#include "attrs/attr_args.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "location/module_name.hpp"
#include "semantic/diag.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "target/target.hpp"
#include "utils/strings.hpp"
#include <algorithm>
#include <format>
#include <string>

namespace trust {

namespace {

/// Тип поля нативного класса: аннотация обязательна, тип обязан резолвиться.
/// Диагностика выдаётся здесь; nullopt — объявление пропускается.
std::optional<TypeId> resolveClassFieldType(AnalysisContext& actx, VarDecl& vd, std::string_view bare) {
    if (!vd.m_type) {
        actx.ctx().diag().report(Severity::Error, vd.range(), "native field '{}' must have an explicit type in a forward declaration", bare);
        return std::nullopt;
    }
    const TypeId ftype = actx.types().resolveTypeRef(*vd.m_type).value_or(INVALID_TYPE_ID);
    if (ftype == INVALID_TYPE_ID) {
        actx.ctx().diag().report(Severity::Error, vd.m_type->range(), "unknown field type '{}'", requiredText(*vd.m_type));
        return std::nullopt;
    }
    return ftype;
}

/// Регистрация сигнатуры метода/оператора класса: дубль по параметрам — диагностика,
/// иначе addMethod. `binders` — типовые параметры КЛАССА (позиционная идентичность).
/// nullopt — объявление пропускается.
std::optional<TypeId> registerClassMethodSignature(AnalysisContext& actx, TypeId cls, const MethodKey& key, FuncDecl& f, const TemplateBinders& binders,
                                                   std::string_view what, std::string_view displayName) {
    const TypeId ft = actx.types().buildFuncType(f);
    if (ft == INVALID_TYPE_ID) {
        return std::nullopt;
    }
    TypeSystem& reg = actx.registry();
    if (reg.read().methods().hasOwnMethodSignature(cls, key, ft, binders)) {
        actx.ctx().diag().report(Severity::Error, f.range(), "{} '{}' with the same parameter types is already declared in this type", what, displayName);
        return std::nullopt;
    }
    reg.methods().addMethod(cls, key, ft, {}, callableParamInfo(f), binders, f.visibility, f.m_isOperator ? DeclId{} : f.declId());
    return ft;
}

} // namespace
void DeclAnalyzer::analyzeClassDecl(ClassDecl& ncd, ModuleIndex ownerModule) {
    MapperRange range = ncd.range();
    const std::string trustName{ncd.text()};
    const std::string cppName{m_actx.ctx().declCppFacts().cppNameOf(ncd.declId())};

    // `@[borrowed]` на классе помечает ВСЕ его производные типы (см. borrow_flow). Метка
    // регистрируется в TypeSystem, чтобы borrow-анализ видел классы, пришедшие ИМПОРТОМ
    // (их AST не входит в корневой AST программы).
    if (ncd.has_attr(m_actx.ctx().attrs(), attr::Borrowed)) {
        m_actx.registry().markBorrowedClass(trustName);
    }

    // Обобщённая форма `<T1,T2> Pair ::= <T1,T2> %std::pair { ... }` и явная регистрируются
    // одинаково (cppName `std::pair`); маппинг `Pair<A,B>`→`std::pair<A,B>` происходит при
    // инстанциации по типовым параметрам (m_templateArgs для обобщённой остаётся nullopt).
    if (trustName.empty()) {
        m_actx.ctx().diag().report(Severity::Error, range, "native class must have a non-empty name");
        return;
    }
    if (cppName.empty()) {
        m_actx.ctx().diag().report(Severity::Error, range, "native class '{}' must have a non-empty C++ name (use a '%'-prefixed name)", trustName);
        return;
    }

    // Pack-параметры в native template class: разрешены ТОЛЬКО как пустой type-synonym
    // (`<T...> StdTuple ::= %std::tuple<T...> { };`). Члены, зависящие от пакета, не
    // инстанцируются (у инстанциации нет substitution методов) — явная ошибка.
    bool hasPackParam = false;
    if (ncd.m_templateParams) {
        for (const auto& p : *ncd.m_templateParams) {
            if (p && p->is<ArgNode>() && p->as<ArgNode>()->isTypePack) {
                hasPackParam = true;
            }
        }
    }
    if (hasPackParam && !ncd.m_body.empty()) {
        m_actx.ctx().diag().report(Severity::Error, range, "type parameter packs in native template classes are supported only for empty-body type synonyms");
        return;
    }

    // preprocInclude из `@[include("header")@]` (голое имя → угловой инклуд), подтягивается
    // on-use при использовании типа (как у нативного шаблона-типа). '@'-префикс (пакет
    // stdlib.trust) пробрасывается как есть - заголовок доступен через -Istdlib/include.
    std::string preprocInclude;
    {
        const AttrPool& attrs = m_actx.ctx().attrs();
        if (const std::string* header = firstAttrArg(attrs, ncd, attr::Include)) {
            preprocInclude = includeDirective(*header);
        }
    }

    // Логические библиотеки из `@[link("name")]` - применяются ON-USE (recordUsedType).
    std::vector<std::string> linkLibs;
    {
        const AttrPool& attrs = m_actx.ctx().attrs();
        if (const std::string* lib = firstAttrArg(attrs, ncd, attr::Link)) {
            linkLibs.push_back(*lib);
        }
    }

    validateClassBases(ncd, trustName);

    // Регистрация типа: нативный класс (kNativeClass) или нативный шаблон-класс (kNativeTemplate).
    const auto native_keys = m_actx.types().typeStorageKeys(trustName);
    TypeId cls = INVALID_TYPE_ID;
    // (B): C++-имя генерируемого класса-обёртки = trust-имя класса (namespace — как в коде).
    // Обёртка создаётся, когда C++-имя класса ОТЛИЧАЕТСЯ от RHS (`MyStr ::= %std::string`).
    // Совпадение (`CountDel ::= %CountDel`) — чистый алиас на существующий C++-класс (обёртки нет).
    // Шаблоны `std::vector`/`std::array` — ИСКЛЮЧЕНИЕ: они унифицируются с `:Array` (спец-случай
    // в type_resolver), поэтому остаются проекцией (класс-обёртка сломала бы Array-семантику).
    const bool arrayUnifiedTemplate = trust::type::isBuiltinContainerCppName(cppName);
    const bool wrapperize = trustName != cppName && !arrayUnifiedTemplate;
    const std::string ownCppName = wrapperize ? trustName : cppName;
    // ГЕНЕРИРУЕМЫЙ класс-обёртка публикуется заголовком СВОЕГО модуля (заголовок модуля = интерфейс):
    // on-use include типа — этот заголовок. `@[include]` при этом остаётся инклудом БАЗИСА и
    // собирается кодогеном при эмиссии самой обёртки (collectInclude на объявлении).
    if (wrapperize) {
        if (ModuleResolver* resolver = m_actx.ctx().moduleResolver(); resolver != nullptr && ownerModule.isValid()) {
            const std::string encoded = moduleEncodedName(resolver->moduleLogicalName(ownerModule));
            if (!encoded.empty()) {
                preprocInclude = "#include \"" + moduleHeaderFileName(encoded) + "\"";
            }
        }
    }
    if (!ncd.m_templateParams.has_value()) {
        cls = m_actx.registry().registrar().registerNativeClass(trustName, range, native_keys.front());
    } else {
        cls = m_actx.registry().registrar().registerNativeTemplate(trustName, range, native_keys.front());
    }
    if (cls == INVALID_TYPE_ID) {
        return; // дубликат - диагностика сформирована реестром
    }
    // Целевые C++-факты типа (C++-имя/шаблон/include/libs) — в контракт цели (`target`), НЕ в
    // языковую модель (R1/R37). Ключ — канонический (`TypeStore::cppFactsKeyOf`).
    {
        CppTypeFacts facts;
        if (ncd.m_templateParams.has_value()) {
            facts.cppTemplate = ownCppName; // нативный шаблон: C++-имя шаблона (cppName пуст).
        } else {
            facts.cppName = ownCppName; // нативный класс: C++-имя класса.
        }
        if (!preprocInclude.empty()) {
            facts.preprocIncludes.push_back(preprocInclude);
        }
        facts.libs = std::move(linkLibs);
        const TypeId factsKey = m_actx.ctx().readTypes().store().cppFactsKeyOf(m_actx.ctx().readTypes().store().desugaredTypeId(cls));
        m_actx.ctx().target().typeCppFacts().set(factsKey, std::move(facts));
    }
    m_actx.types().aliasTypeKeys(cls, native_keys);
    // Идентичность типа на УЗЛЕ-объявлении (ClassDecl) — для проверки доступа к членам из
    // контекста тела класса (`NamespaceScope::currentTypeBodyId`).
    ncd.resolvedTypeId = cls;

    // Биндинг trust-имени класса в скоупе.
    Symbol as;
    as.name = stableSymbolName(trustName);
    as.type = QualType::fromTypeId(cls);
    as.decl = &ncd;
    if (m_actx.symbols().declare(as) == DeclResult::Inserted) {
        for (auto& hook : m_core.hooks()) {
            hook->onDeclare(as);
        }
    }

    // Класс-скоуп: нужен для currentClass()/@__CLASS__/@:: (namespacePath включает имя класса)
    // и для регистрации статических членов. Типовые параметры шаблона объявляются в этом же скоупе.
    m_core.enterScope(ncd);
    if (ncd.m_templateParams) {
        const TypeId tpl = m_actx.ctx().readTypes().getType(type_category::TemplateParam);
        for (const auto& p : *ncd.m_templateParams) {
            if (!p) {
                continue;
            }
            Symbol ps;
            ps.name = stableSymbolName(std::string(requiredText(*p)));
            ps.type = QualType::fromTypeId(tpl);
            ps.decl = p.get();
            ps.storage = Storage::Local;
            m_actx.symbols().declare(ps);
        }
    }

    // Ключ члена: статический = ПОЛНОЕ имя `ns::Class::name` (содержит '::'); экземплярный = последний
    // сегмент после '::'. Flags (native/static/const) — ПОЛЯ MethodKey; `^` разбирается из spelling.
    const auto memberKey = [](const std::string& expanded) -> MethodKey {
        std::string seg(utils::shortNameOf(expanded));
        if (!seg.empty() && seg.front() == '.') {
            seg.erase(0, 1);
        }
        MethodKey key = methodKeyFromSpelling(seg);
        key.isStatic = false;
        return key;
    };

    // Binder-контекст класса: методы/статические члены native-шаблона используют типовые
    // параметры КЛАССА (позиционная C++-идентичность FunctionIdentity).
    const TemplateBinders classBinders = bindersOf(ncd.m_templateParams);

    // Регистрация членов-интерфейса (forward, тела нет). Классификация: имя содержит '::' после
    // раскрытия @:: → ns::Class::name — СТАТИЧЕСКИЙ член; иначе — экземплярный метод/поле.
    // Правильная регистрация экземплярного — ведущая '.'; без неё (например %field) — диагностика
    // -Wclass-member-dot (по умолчанию ignore). Ключи: экземплярный — "%<bare>", статический — "@<bare>"
    // (маркер статики '@', bareName срезает '@' → findMethodInfo(cls,"name") находит оба).
    // Символы объявленных операторов-методов: дубликат запрещён (addMethod делает FAULT).
    // Регистрация членов-интерфейса (forward, тела нет).

    for (const auto& member : ncd.m_body) {
        if (!member) {
            continue;
        }
        // Раскрытие @:: в имени члена (VarDecl/FuncDecl - потомки IdentName): @::field → ns::Class::field.
        if (member->is<IdentName>()) {
            member->as<IdentName>()->expandQualified(m_actx.namespaces().namespacePath());
        }
        const std::string mname{requiredText(*member)};
        if (mname.empty()) {
            continue;
        }
        const bool isStatic = mname.find("::") != std::string::npos;
        const MethodKey memberKeyValue = memberKey(mname);
        const std::string bare = memberKeyValue.name;
        // Экспорт нативного имени — только для свободных функций/переменных: член класса
        // публикуется через проекцию владельца, отдельный C/C++-символ ему не задаётся.
        const Decl* memberDecl = member->is<FuncDecl>() ? static_cast<const Decl*>(member->as<FuncDecl>())
                                                        : (member->is<VarDecl>() ? static_cast<const Decl*>(member->as<VarDecl>()) : nullptr);
        if (memberDecl != nullptr && memberDecl->bindsNativeCpp()) {
            m_actx.ctx().diag().report(Severity::Error, member->range(),
                                       "native export is not applicable to class members ('{}'): use the owner type projection", bare);
            continue;
        }
        // Нативный класс `::=` описывает ИНТЕРФЕЙС: тело класса находится в C++-заголовке, поэтому
        // каждый член обязан быть предварительным объявлением. Тело/делегат метода и значение
        // поля не эмитятся и были бы молча потеряны.
        if (member->kind() == ParserToken::Kind::FuncDecl) {
            if (!static_cast<const FuncDecl&>(*member).isForwardDeclaration()) {
                m_actx.ctx().diag().report(Severity::Error, member->range(),
                                           "native class member '{}' must be a forward declaration (':= ...'): the class body is defined in the C++ header",
                                           bare);
                continue;
            }
        } else if (member->kind() == ParserToken::Kind::VarDecl) {
            if (static_cast<const VarDecl&>(*member).initKind() != VarInitKind::Forward) {
                m_actx.ctx().diag().report(Severity::Error, member->range(),
                                           "native class field '{}' must be a forward declaration (':= ...'): the class body is defined in the C++ header",
                                           bare);
                continue;
            }
        }
        MethodKey staticKey = methodKeyFromSpelling(mname);
        // '^' нормализуется AST в attr::ReadOnly — константность читаем из атрибута.
        const AstNodeAttr* memberAttr = member->as_attr();
        const bool memberIsConst = memberAttr != nullptr && memberAttr->has_attr(m_actx.ctx().attrs(), attr::ReadOnly);
        staticKey.isConst = memberIsConst;
        MethodKey instanceKey = memberKeyValue;
        instanceKey.isConst = memberIsConst;
        if (bare.empty()) {
            continue;
        }
        if (op::isSystemName(bare) && !op::isOperatorFunctionName(bare)) {
            m_actx.ctx().diag().report(Severity::Error, member->range(),
                                       "system name '{}' is reserved; only registered operator system functions may be declared", bare);
            continue;
        }
        // Оператор-метод (FuncDecl с m_isOperator): имя - СИМВОЛ (`==`, `()`, `[]`), правило
        // ведущей '.' к нему НЕ применяется (оператор не именованный член класса).
        const bool isOperatorMember = member->kind() == ParserToken::Kind::FuncDecl && static_cast<const FuncDecl&>(*member).m_isOperator;

        if (isStatic) {
            // Статический член: СТРУКТУРНЫЙ ключ с полным `ns::Class::name` (isStatic=true).
            // C++-имя = последний сегмент (bare). Регистрируем
            // как член типа (доступ Cls.field / Cls::field → cppName::name) и как статическую переменную
            // в скоупе (анализатор имён).
            if (member->kind() == ParserToken::Kind::FuncDecl) {
                auto& f = static_cast<FuncDecl&>(*member);
                // Статический член нативного класса: C++-объявление не эмитится, а C++-имя и
                // `@[include]` кодоген берёт из типа-владельца (`m_nativeOwner`). Отдельной
                // нативной привязки у члена НЕТ: проекция владельца — единственный источник.
                f.m_nativeOwner = cls;
                const auto ft = registerClassMethodSignature(m_actx, cls, staticKey, f, classBinders, "static member", bare);
                if (!ft) {
                    continue;
                }
                Symbol ss;
                ss.name = stableSymbolName(mname);
                ss.type = QualType::fromTypeId(*ft);
                ss.decl = member.get();
                ss.storage = Storage::Static;
                ss.overloads.push_back(SymbolOverload{*ft, callableParamInfo(f), member.get()});
                const DeclResult sr = m_actx.symbols().declareOrComplete(ss);
                if (sr == DeclResult::AlreadyRegistered) {
                    continue; // идемпотентная повторная регистрация (ромбовидный/повторный импорт)
                }
                if (sr == DeclResult::Duplicate || sr == DeclResult::SignatureMismatch) {
                    m_actx.ctx().diag().report(Severity::Error, f.range(), "static member '{}' is already declared in this scope", bare);
                    continue;
                }
                m_actx.symbols().declareGlobalOrComplete(ss); // персистентно: поиск по `... = ns`
            } else if (member->kind() == ParserToken::Kind::VarDecl) {
                auto& vd = static_cast<VarDecl&>(*member);
                const auto ftype = resolveClassFieldType(m_actx, vd, bare);
                if (!ftype) {
                    continue;
                }
                const TypeId memberFn = m_actx.registry().interner().getOrCreateFunctionType(*ftype, {});
                if (m_actx.ctx().readTypes().methods().hasOwnMemberName(cls, staticKey)) {
                    m_actx.ctx().diag().report(Severity::Error, vd.range(), "static member '{}' is already declared in this type", bare);
                    continue;
                }
                m_actx.registry().methods().addMethod(cls, staticKey, memberFn);
                Symbol ss;
                ss.name = stableSymbolName(mname);
                ss.type = QualType::fromTypeId(*ftype);
                ss.decl = member.get();
                ss.storage = Storage::Static;
                vd.m_nativeOwner = cls; // C++-имя и @[include] печатает кодоген по владельцу
                m_actx.symbols().declare(ss);
                m_actx.symbols().declareGlobal(ss); // персистентно: поиск по `... = ns`
            }
            continue;
        }

        // Экземплярный член: без ведущей '.' → -Wclass-member-dot (по умолчанию ignore).
        // Оператор-метод исключён: его имя - символ, а не именованный член.
        if (mname.front() != '.' && !isOperatorMember) {
            m_actx.ctx().report(member->range(), semantic::DiagId::ClassMemberDot, "class member '{}' should be registered with a leading '.' (use '.{}')",
                                mname, bare);
        }
        const MethodKey& ikey = instanceKey;
        if (member->kind() == ParserToken::Kind::FuncDecl) {
            auto& f = static_cast<FuncDecl&>(*member);
            // Оператор-метод нативного класса: та же валидация, что и у user-record (символ/арность/
            // member-form/тип возврата) - иначе объявление принималось бы МОЛЧА (асимметрия).
            if (f.m_isOperator && !semantic::validateOperatorDecl(m_actx.ctx(), f, /*isMember=*/true)) {
                continue;
            }
            if (!f.hasBody() && !f.m_type) {
                m_actx.ctx().diag().report(Severity::Error, f.range(), "native method '{}' must have a return type in a forward declaration", bare);
                continue;
            }
            const std::string_view what = f.m_isOperator ? "operator" : "method";
            const std::string displayName = f.m_isOperator ? std::string(op::symbolOfFunction(f.text())) : bare;
            if (!registerClassMethodSignature(m_actx, cls, ikey, f, classBinders, what, displayName)) {
                continue;
            }
        } else if (member->kind() == ParserToken::Kind::VarDecl) {
            auto& vd = static_cast<VarDecl&>(*member);
            const auto ftype = resolveClassFieldType(m_actx, vd, bare);
            if (!ftype) {
                continue;
            }
            const TypeId memberFn = m_actx.registry().interner().getOrCreateFunctionType(*ftype, {});
            if (m_actx.ctx().readTypes().methods().hasOwnMemberName(cls, ikey)) {
                m_actx.ctx().diag().report(Severity::Error, vd.range(), "member '{}' is already declared in this type", bare);
                continue;
            }
            m_actx.registry().methods().addMethod(cls, ikey, memberFn, {}, {}, {}, vd.visibility);
        }
    }

    m_core.exitScope();
}

void DeclAnalyzer::validateClassBases(ClassDecl& ncd, std::string_view typeName) {
    // Базисы ГЕНЕРИРУЕМОГО класса (СИНТАКСИС объявления): НАТИВНЫЕ (`%std::string` — готовое
    // C++-имя) либо ОБЫЧНЫЕ типы (`:Base` — резолвятся как record-классы). Смешивать нельзя:
    // порядок и правила разных доменов имён несовместимы.
    // Ошибки базисов НЕ прерывают анализ объявления: члены тела обязаны быть материализованы
    // (иначе HIR verify даёт ICE); кодоген на ошибках не запускается (errorCount > 0).
    if (!ncd.m_nativeBaseCppNames.empty() && !ncd.m_baseTypes.empty()) {
        m_actx.ctx().diag().report(Severity::Error, ncd.range(), "native class '{}' cannot mix native ('%name') and type (':Name') bases", typeName);
    }
    for (const NativeBase& nativeBase : ncd.m_nativeBaseCppNames) {
        if (nativeBase.cppName.empty()) {
            m_actx.ctx().diag().report(Severity::Error, ncd.range(), "native base class of '{}' must have a C++ name after '%'", typeName);
        }
    }
    for (const auto& base : ncd.m_baseTypes) {
        if (!base.type) {
            continue;
        }
        std::string baseText(requiredText(*base.type));
        if (!baseText.empty() && baseText.front() == ':') {
            baseText.erase(0, 1);
        }
        const auto tid = m_actx.types().resolveTypeRef(*base.type);
        if (!tid.has_value() || *tid == INVALID_TYPE_ID) {
            m_actx.ctx().diag().report(Severity::Error, base.type->range(), "unknown base type '{}'", baseText);
            continue;
        }
        if (!m_actx.ctx().readTypes().store().isRecordType(*tid)) {
            m_actx.ctx().diag().report(Severity::Error, base.type->range(), "base type '{}' of native class '{}' must be a Struct/Class", baseText, typeName);
        }
    }
}

} // namespace trust
