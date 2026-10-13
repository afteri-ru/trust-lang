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
#include "semantic/diag.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
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
std::optional<TypeId> registerClassMethodSignature(AnalysisContext& actx, TypeId cls, std::string_view key, FuncDecl& f, const TemplateBinders& binders,
                                                   std::string_view what, std::string_view displayName) {
    const TypeId ft = actx.types().buildFuncType(f);
    if (ft == INVALID_TYPE_ID) {
        return std::nullopt;
    }
    TypeSystem& reg = actx.ctx().types();
    if (reg.read().methods().hasOwnMethodSignature(cls, key, ft, binders)) {
        actx.ctx().diag().report(Severity::Error, f.range(), "{} '{}' with the same parameter types is already declared in this type", what, displayName);
        return std::nullopt;
    }
    reg.methods().addMethod(cls, key, ft, {}, callableParamInfo(f), binders);
    return ft;
}

} // namespace
void DeclAnalyzer::analyzeClassDecl(ClassDecl& ncd) {
    MapperRange range = ncd.range();
    const std::string trustName{ncd.text()};
    const std::string cppName = ncd.m_nativeName; // "std::string" / "std::pair" (без '%')

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

    // Регистрация типа: нативный класс (kNativeClass) или нативный шаблон-класс (kNativeTemplate).
    const auto native_keys = m_actx.types().typeStorageKeys(trustName);
    TypeId cls = INVALID_TYPE_ID;
    if (!ncd.m_templateParams.has_value()) {
        cls = m_actx.ctx().types().registrar().registerNativeClass(trustName, cppName, range, preprocInclude, std::move(linkLibs), native_keys.front());
    } else {
        cls = m_actx.ctx().types().registrar().registerNativeTemplate(trustName, cppName, range, preprocInclude, std::move(linkLibs), native_keys.front());
    }
    if (cls == INVALID_TYPE_ID) {
        return; // дубликат - диагностика сформирована реестром
    }
    m_actx.types().aliasTypeKeys(cls, native_keys);

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

    // Ключ члена: статический = ПОЛНОЕ имя `ns::Class::name` (содержит '::'); экземплярный = bare
    // (последний сегмент после '::', срез ведущего '.' и маркеров '%'/'^' через utils::bareName).
    const auto memberKey = [](const std::string& expanded) -> std::string {
        std::string seg(utils::shortNameOf(expanded));
        if (!seg.empty() && seg.front() == '.') {
            seg.erase(0, 1);
        }
        return utils::bareName(seg);
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
        const std::string bare = memberKey(mname);
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
            // Статический член: КЛЮЧ = ПОЛНОЕ имя `ns::Class::name` (содержит '::' — isStaticName
            // проверяет по зарегистрированному ключу). C++-имя = последний сегмент (bare). Регистрируем
            // как член типа (доступ Cls.field / Cls::field → cppName::name) и как статическую переменную
            // в скоупе (анализатор имён).
            const std::string& skey = mname;
            if (member->kind() == ParserToken::Kind::FuncDecl) {
                auto& f = static_cast<FuncDecl&>(*member);
                const std::string& skey = mname;
                // Статический член нативного класса: C++-объявление не эмитится (m_isNativeImport),
                // а C++-имя и `@[include]` кодоген берёт из типа-владельца.
                f.m_isNativeImport = true;
                f.m_nativeOwner = cls;
                const auto ft = registerClassMethodSignature(m_actx, cls, skey, f, classBinders, "static member", bare);
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
                const TypeId memberFn = m_actx.ctx().types().interner().getOrCreateFunctionType(*ftype, {});
                if (m_actx.ctx().readTypes().methods().hasOwnMemberName(cls, skey)) {
                    m_actx.ctx().diag().report(Severity::Error, vd.range(), "static member '{}' is already declared in this type", bare);
                    continue;
                }
                m_actx.ctx().types().methods().addMethod(cls, skey, memberFn);
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
        const std::string& ikey = bare;
        if (member->kind() == ParserToken::Kind::FuncDecl) {
            auto& f = static_cast<FuncDecl&>(*member);
            // Оператор-метод нативного класса: та же валидация, что и у user-record (символ/арность/
            // member-form/тип возврата) - иначе объявление принималось бы МОЛЧА (асимметрия).
            if (f.m_isOperator && !semantic::validateOperatorDecl(m_actx.ctx(), f, /*isMember=*/true)) {
                continue;
            }
            if (!f.m_body.has_value() && !f.m_type) {
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
            const TypeId memberFn = m_actx.ctx().types().interner().getOrCreateFunctionType(*ftype, {});
            if (m_actx.ctx().readTypes().methods().hasOwnMemberName(cls, ikey)) {
                m_actx.ctx().diag().report(Severity::Error, vd.range(), "member '{}' is already declared in this type", bare);
                continue;
            }
            m_actx.ctx().types().methods().addMethod(cls, ikey, memberFn);
        }
    }

    m_core.exitScope();
}

// Объявление нативного шаблона-ТИПА `<T> %std::vector() := ...;` - регистрирует абстрактный
// нативный шаблон (Group::kNativeTemplate) в реестре типов. trust-имя = последний компонент
// C++-имени (`std::vector` → "vector"), чтобы `vector<Int32>` резолвилось. preprocIncludes типа -
// из `@[include]` (мнемоника `@include`); инклуд подтягивается on-use при использовании типа.
// C++-имена встроенных контейнеров: `:Array` (mutable) → std::vector, `:Array^` (const) → std::array.
void DeclAnalyzer::analyzeNativeTemplateDecl(FuncDecl& func_node) {
    MapperRange func_range = func_node.range();
    std::string cppTemplate = func_node.m_nativeName; // "std::vector" (без '%')

    // Pack-параметры в этой форме (`<T...> %std::tuple() := ...;`) не поддержаны:
    // C++-tuple задаётся native class template `<T...> StdTuple ::= %std::tuple<T...> { };`.
    if (func_node.m_templateParams) {
        for (const auto& p : *func_node.m_templateParams) {
            if (p && p->is<ArgNode>() && p->as<ArgNode>()->isTypePack) {
                m_actx.ctx().diag().report(
                    Severity::Error, p->range(),
                    "type parameter packs in native template type declarations are not implemented; use '<T...> Name ::= %Cpp<T...> {{ }};'");
                return;
            }
        }
    }

    // trust-имя шаблона: последний компонент после '::'.
    std::string trustName(utils::shortNameOf(cppTemplate));
    if (trustName.empty()) {
        m_actx.ctx().diag().report(Severity::Error, func_range, "native template must have a non-empty C++ name");
        return;
    }

    // preprocInclude из `@[include("header")@]` - форма директивы задана контрактом attr::Include
    // (единый attrs::includeDirective), включая пакетный '@'-префикс.
    std::string preprocInclude;
    {
        const AttrPool& attrs = m_actx.ctx().attrs();
        if (const std::string* header = firstAttrArg(attrs, func_node, attr::Include)) {
            preprocInclude = includeDirective(*header);
        }
    }

    // Дубликат по C++-имени со встроенным контейнером (`std::vector`/`std::array` от `:Array`):
    // мягкая диагностика, тип инстанцируется через встроенный Array (повторной регистрации
    // дублирующего типа нет, `vector<Int32>` резолвится через `:Array`).
    if (type::isBuiltinContainerCppName(cppTemplate)) {
        m_actx.ctx().diag().report(Severity::Warning, func_range, "native template '{}' is a builtin container (use ':Array' / ':Array^' without declaration)",
                                   cppTemplate);
    }

    // Регистрация абстрактного нативного шаблона (дубликат имени - диагностика реестра).
    const auto tpl_keys = m_actx.types().typeStorageKeys(trustName);
    TypeId tpl = m_actx.ctx().types().registrar().registerNativeTemplate(trustName, cppTemplate, func_range, preprocInclude, {}, tpl_keys.front());
    if (tpl == INVALID_TYPE_ID) {
        return; // дубликат - диагностика сформирована реестром
    }
    m_actx.types().aliasTypeKeys(tpl, tpl_keys);
    // Биндинг имени в скоуп (shadowing/резолв имён; тип доступен и через реестр findType).
    Symbol as;
    as.name = stableSymbolName(trustName);
    as.type = QualType::fromTypeId(tpl);
    as.decl = &func_node;
    if (m_actx.symbols().declare(as) == DeclResult::Inserted) {
        for (auto& hook : m_core.hooks()) {
            hook->onDeclare(as);
        }
    }
}

} // namespace trust
