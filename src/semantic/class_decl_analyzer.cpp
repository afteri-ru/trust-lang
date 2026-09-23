// Generated: src/semantic/decl_analyzer.cpp
#include "semantic/decl_analyzer.hpp"
#include "semantic/name_resolution.hpp"
#include "semantic/analysis_common.hpp"
#include "semantic/format_check.hpp"
#include "semantic/operator_check.hpp"
#include "semantic/solver.hpp"
#include "analysis/symbol_table.hpp"
#include "semantic/type_inference.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/promotion.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "utils/strings.hpp"
#include <algorithm>
#include <format>
#include <set>
#include <string>

namespace trust {
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

    // preprocInclude из `@[include("header")@]` (голое имя → угловой инклуд), подтягивается
    // on-use при использовании типа (как у нативного шаблона-типа).
    std::string preprocInclude;
    {
        const AttrPool& attrs = m_actx.ctx().attrs();
        if (auto inc = attrs.lookup(attr::Include); inc.has_value() && ncd.has_attr(*inc)) {
            if (const auto* args = ncd.attr_args(*inc); args && !args->empty() && !args->at(0).empty()) {
                const std::string& header = args->at(0);
                if (header.front() == '@') {
                    // Рантайм-заголовок (извлекается из trust-runtime; см. TypeEmitter::recordRequiredInclude):
                    // пробрасывается как есть, как у встроенных типов (registerBuiltinType).
                    preprocInclude = header;
                } else if (header.front() == '\"' || header.front() == '<') {
                    preprocInclude = "#include " + header;
                } else {
                    preprocInclude = "#include <" + header + ">";
                }
            }
        }
    }

    // Регистрация типа: нативный класс (kNativeClass) или нативный шаблон-класс (kNativeTemplate).
    TypeId cls = INVALID_TYPE_ID;
    if (!ncd.m_templateParams.has_value()) {
        cls = m_actx.ctx().types().registerNativeClass(trustName, cppName, range, preprocInclude);
    } else {
        cls = m_actx.ctx().types().registerNativeTemplate(trustName, cppName, range, preprocInclude);
    }
    if (cls == INVALID_TYPE_ID) {
        return; // дубликат - диагностика сформирована реестром
    }

    // Биндинг trust-имени класса в скоупе.
    Symbol as;
    as.name = trustName;
    as.type = cls;
    as.decl = &ncd;
    if (m_actx.symbols().declare(as)) {
        for (auto& hook : m_core.m_hooks) {
            hook->onDeclare(as);
        }
    }

    // Класс-скоуп: нужен для currentClass()/@__CLASS__/@:: (namespacePath включает имя класса)
    // и для регистрации статических членов. Типовые параметры шаблона объявляются в этом же скоупе.
    m_core.enterScope(ncd);
    if (ncd.m_templateParams) {
        const TypeId tpl = m_actx.ctx().types().getType(type_category::TemplateParam);
        for (const auto& p : *ncd.m_templateParams) {
            if (!p) {
                continue;
            }
            Symbol ps;
            ps.name = std::string(p->text());
            ps.type = tpl;
            ps.decl = p.get();
            ps.storage = Storage::Local;
            m_actx.symbols().declare(ps);
        }
    }

    // Ключ члена: статический = ПОЛНОЕ имя `ns::Class::name` (содержит '::'); экземплярный = bare
    // (последний сегмент после '::', срез ведущего '.' и маркеров '%'/'^' через utils::bare_name).
    const auto memberKey = [](const std::string& expanded) -> std::string {
        const size_t p = expanded.rfind("::");
        std::string seg = (p == std::string::npos) ? expanded : expanded.substr(p + 2);
        if (!seg.empty() && seg.front() == '.') {
            seg.erase(0, 1);
        }
        return utils::bare_name(seg);
    };

    // Регистрация членов-интерфейса (forward, тела нет). Классификация: имя содержит '::' после
    // раскрытия @:: → ns::Class::name — СТАТИЧЕСКИЙ член; иначе — экземплярный метод/поле.
    // Правильная регистрация экземплярного — ведущая '.'; без неё (например %field) — диагностика
    // -Wclass-member-dot (по умолчанию ignore). Ключи: экземплярный — "%<bare>", статический — "@<bare>"
    // (маркер статики '@', bare_name срезает '@' → findMethodInfo(cls,"name") находит оба).
    // Символы объявленных операторов-методов: дубликат запрещён (addMethod делает FAULT).
    std::set<std::string> declaredOperators;

    for (const auto& member : ncd.m_body) {
        if (!member) {
            continue;
        }
        // Раскрытие @:: в имени члена (VarDecl/FuncDecl - потомки IdentName): @::field → ns::Class::field.
        if (member->is<IdentName>()) {
            member->as<IdentName>()->expandQualified(m_actx.namespacePath());
        }
        const std::string mname{member->text()};
        if (mname.empty()) {
            continue;
        }
        const bool isStatic = mname.find("::") != std::string::npos;
        const std::string bare = memberKey(mname);
        if (bare.empty()) {
            continue;
        }
        // Оператор-метод (FuncDecl с m_isOperator): имя - СИМВОЛ (`==`, `()`, `[]`), правило
        // ведущей '.' к нему НЕ применяется (оператор не именованный член класса).
        const bool isOperatorMember = member->kind() == ParserToken::Kind::FuncDecl && static_cast<const FuncDecl&>(*member).m_isOperator;

        if (isStatic) {
            // Статический член: КЛЮЧ = ПОЛНОЕ имя `ns::Class::name` (содержит '::' — is_static_name
            // проверяет по зарегистрированному ключу). C++-имя = последний сегмент (bare). Регистрируем
            // как член типа (доступ Cls.field / Cls::field → cppName::name) и как статическую переменную
            // в скоупе (анализатор имён).
            const std::string& skey = mname;
            if (member->kind() == ParserToken::Kind::FuncDecl) {
                auto& f = static_cast<FuncDecl&>(*member);
                const TypeId ft = m_actx.buildFuncType(f);
                m_actx.ctx().types().addMethod(cls, skey, ft);
                Symbol ss;
                ss.name = mname;
                ss.type = ft;
                ss.decl = member.get();
                ss.storage = Storage::Static;
                m_actx.symbols().declare(ss);
                m_actx.symbols().declareGlobal(ss); // персистентно: поиск по `... = ns`
            } else if (member->kind() == ParserToken::Kind::VarDecl) {
                auto& vd = static_cast<VarDecl&>(*member);
                if (!vd.m_type) {
                    m_actx.ctx().diag().report(Severity::Error, vd.range(), "native field '{}' must have an explicit type in a forward declaration", bare);
                    continue;
                }
                TypeId ftype = m_actx.resolveTypeRef(*vd.m_type).value_or(INVALID_TYPE_ID);
                if (ftype == INVALID_TYPE_ID) {
                    m_actx.ctx().diag().report(Severity::Error, vd.m_type->range(), "unknown field type '{}'", vd.m_type->text());
                    continue;
                }
                const TypeId memberFn = m_actx.ctx().types().getOrCreateFunctionType(ftype, {});
                m_actx.ctx().types().addMethod(cls, skey, memberFn);
                Symbol ss;
                ss.name = mname;
                ss.type = ftype;
                ss.decl = member.get();
                ss.storage = Storage::Static;
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
            // Оператор-метод: дубликат символа в типе запрещён (addMethod делает FAULT) - проверяем заранее.
            if (f.m_isOperator && !declaredOperators.insert(std::string(f.text())).second) {
                m_actx.ctx().diag().report(Severity::Error, f.range(), "operator '{}' is already declared in this type", f.text());
                continue;
            }
            if (!f.m_body.has_value() && !f.m_type) {
                m_actx.ctx().diag().report(Severity::Error, f.range(), "native method '{}' must have a return type in a forward declaration", bare);
                continue;
            }
            const TypeId ft = m_actx.buildFuncType(f);
            m_actx.ctx().types().addMethod(cls, ikey, ft);
        } else if (member->kind() == ParserToken::Kind::VarDecl) {
            auto& vd = static_cast<VarDecl&>(*member);
            if (!vd.m_type) {
                m_actx.ctx().diag().report(Severity::Error, vd.range(), "native field '{}' must have an explicit type in a forward declaration", bare);
                continue;
            }
            TypeId ftype = m_actx.resolveTypeRef(*vd.m_type).value_or(INVALID_TYPE_ID);
            if (ftype == INVALID_TYPE_ID) {
                m_actx.ctx().diag().report(Severity::Error, vd.m_type->range(), "unknown field type '{}'", vd.m_type->text());
                continue;
            }
            const TypeId memberFn = m_actx.ctx().types().getOrCreateFunctionType(ftype, {});
            m_actx.ctx().types().addMethod(cls, ikey, memberFn);
        }
    }

    m_core.exitScope();
}

} // namespace trust
