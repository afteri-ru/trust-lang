// src/semantic/record_decl_analyzer.cpp
// Семантика объявления пользовательского Record-типа (Struct/Class):
//   :Name ::= :Struct{ поля };            - POD Struct (Group::kStructs, static_assert в C++)
//   :Name ::= :Class { поля; методы };    - Class (Group::kClassDefs)
//   :Derived ::= :Base{ ... };            - наследование Class (C++-наследование, baseClasses)
// Резолв баз, регистрация полей/методов и типа (registerRecordType), привязка имени типа.
#include "semantic/decl_analyzer.hpp"
#include "semantic/name_resolution.hpp"
#include "semantic/analysis_common.hpp"
#include "analysis/symbol_table.hpp"
#include "semantic/diag.hpp"
#include "semantic/operator_check.hpp"
#include "diag/diag.hpp"
#include "ast/token.hpp"
#include "ast/token_base.hpp"
#include "attrs/attr_builtin.hpp"
#include "types/group.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/overload_resolve.hpp"
#include "types/type_names.hpp"
#include "utils/strings.hpp"
#include "utils/trace.hpp"
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace trust {

namespace {

// Атрибуты полиморфизма (@[virtual]/@[override]) НЕ реализованы: явная ошибка на носителе
// (объявление или его член), чтобы не игнорировать их молча (AGENTS п.5). Обычная функция (без лямбд).
void reportUnimplementedPolymorphismAttrs(AnalysisContext& actx, const AstNodeBase& node) {
    const AstNodeAttr* attrNode = node.as_attr();
    if (!attrNode) {
        return;
    }
    const AttrPool& attrs = actx.ctx().attrs();
    if (auto vid = attrs.lookup(attr::Virtual); vid.has_value() && attrNode->has_attr(*vid)) {
        actx.ctx().diag().report(Severity::Error, attrNode->range(), "attribute '@[{}]' (polymorphism) is not implemented yet", attr::Virtual);
    }
    if (auto oid = attrs.lookup(attr::Override); oid.has_value() && attrNode->has_attr(*oid)) {
        actx.ctx().diag().report(Severity::Error, attrNode->range(), "attribute '@[{}]' (polymorphism) is not implemented yet", attr::Override);
    }
}

} // namespace

void DeclAnalyzer::analyzeRecordDecl(Binary& binary_node) {
    auto* right = binary_node.m_right.get();
    EXPECT(right && right->kind() == ParserToken::Kind::StructDecl && "analyzeRecordDecl: RHS must be RecordDecl");
    auto& rec = static_cast<RecordDecl&>(*right);
    const MapperRange decl_range = binary_node.range();
    const std::string rec_name = std::string(rec.text());
    if (rec_name.empty()) {
        m_actx.ctx().diag().report(Severity::Error, decl_range, "record type must have a non-empty name");
        return;
    }

    TypeRegistry& reg = m_actx.ctx().types();

    // -- Класс-скоуп открывается ДО резолва баз: типовые параметры шаблона (`<T>`) должны быть
    //    видны и в базах (`:Base<:T>`), и в членах. Закрывается после регистрации/методов.
    m_core.enterScope(rec);

    // -- Типовые параметры пользовательского шаблон-класса (`<T>` / `<T,U>`): связываются в
    //    класс-скоупе отдельными идентификаторами (Group::kTemplateParam), чтобы поля/методы
    //    `: T` резолвились, а при инстанциации подставлялись конкретным аргументом.
    const bool isTemplate = rec.m_templateParams.has_value();
    std::vector<TypeId> templateParams;
    if (isTemplate) {
        for (const auto& p : *rec.m_templateParams) {
            if (!p) {
                continue;
            }
            const TypeId pt = reg.getOrCreateTemplateParamType(p->text());
            templateParams.push_back(pt);
            Symbol ps;
            ps.name = std::string(p->text());
            ps.type = pt;
            ps.decl = p.get();
            ps.storage = Storage::Local;
            m_actx.symbols().declare(ps);
        }
    }

    // -- Резолв баз. Struct: строго маркер `:Struct` (POD, наследование запрещено). Class: маркер
    // `:Class` (корень) и/или пользовательские Class-типы (наследование). Абстрактные маркеры
    // (:Struct/:Class/:Any) в baseClasses НЕ попадают (в C++ не эмитятся). Шаблонные базы
    // (`:Base<:T>`) резолвятся через resolveTypeRef (инстанциация с типовым параметром в аргументе).
    Group group = Group::kClassDefs;
    bool structMarker = false;
    bool classMarker = false;
    std::vector<TypeId> bases;
    for (const auto& b : rec.m_baseTypes) {
        if (!b) {
            continue;
        }
        std::string base_name(b->text());
        if (!base_name.empty() && base_name.front() == ':') {
            base_name.erase(0, 1);
        }
        if (base_name == type_category::Struct) {
            structMarker = true;
            continue;
        }
        if (base_name == type_category::Class) {
            classMarker = true;
            continue;
        }
        auto tid = m_actx.resolveTypeRef(*b);
        if (!tid.has_value() || *tid == INVALID_TYPE_ID) {
            m_actx.ctx().diag().report(Severity::Error, b->range(), "unknown base type '{}'", base_name);
            m_core.exitScope();
            return;
        }
        if (!reg.isRecordType(*tid)) {
            m_actx.ctx().diag().report(Severity::Error, b->range(), "base type '{}' must be a Struct/Class", base_name);
            m_core.exitScope();
            return;
        }
        if (!reg.isClassType(*tid)) {
            m_actx.ctx().diag().report(Severity::Error, b->range(), "Struct '{}' is POD and cannot be used as a base", base_name);
            m_core.exitScope();
            return;
        }
        bases.push_back(*tid);
    }
    if (structMarker) {
        if (classMarker || !bases.empty()) {
            m_actx.ctx().diag().report(Severity::Error, decl_range, "Struct '{}' is POD and cannot inherit", rec_name);
            m_core.exitScope();
            return;
        }
        group = Group::kStructs;
    }

    // -- Объявление типа ДО анализа тела: имя record-типа становится видимым внутри собственного
    //    тела (self-ссылка `:Node ::= :Class{ next : shared<Node> }`) и служит точкой
    //    доопределения для forward-объявлений (`:B ::= :Class;` ... `:B ::= :Class{...}`).
    //    Тип остаётся неполным (complete=false) до defineRecord ниже.
    const bool hasTrust = !binary_node.m_trust.empty();
    const TypeId rec_id = reg.declareRecord(rec_name, group, isTemplate ? templateParams : std::vector<TypeId>{}, decl_range, hasTrust);
    if (rec_id == INVALID_TYPE_ID) {
        m_core.exitScope();
        return; // дубликат/конфликт имени - диагностику сформировал реестр
    }

    // -- Тело объявления (как FuncDecl::m_body): nullopt - ПРЕДВАРИТЕЛЬНОЕ (forward) объявление
    //    (`:Name ::= :Base ...;` → C++ incomplete `struct c_Name;`, членов нет); engaged - полное
    //    определение. `members` - единый доступ к членам (пустой для forward).
    std::vector<AstNodePtr> noMembers;
    std::vector<AstNodePtr>& members = rec.m_body ? *rec.m_body : noMembers;

    // -- Атрибуты полиморфизма (@[virtual]/@[override]) НЕ реализованы - явная ошибка на любом
    //    носителе (объявление или член), чтобы не игнорировать их молча (AGENTS п.5).
    reportUnimplementedPolymorphismAttrs(m_actx, rec);
    for (const auto& member : members) {
        if (member) {
            reportUnimplementedPolymorphismAttrs(m_actx, *member);
        }
    }

    // -- Поля (VarDecl): правила значения по базе.
    //    Struct (POD): default ЗАПРЕЩЁН (NSDMI делает тип нетривиальным) - допустимо только
    //      `:= _` (без значения) → `T c_field;`; `:= ...` для поля недопустим (поля не
    //      предобъявляются).
    //    Class: значение НЕ проверяется - `:= <value>` (default) и `:= _` (без значения) допустимы.
    const bool isStruct = (group == Group::kStructs);
    std::vector<TupleElementData> fields;
    for (auto& member : members) {
        if (!member || member->kind() != ParserToken::Kind::VarDecl) {
            continue;
        }
        auto& vd = static_cast<VarDecl&>(*member);
        if (vd.m_initializer == nullptr) {
            m_actx.ctx().diag().report(Severity::Error, vd.range(), "field '{}' cannot be a forward declaration; use ':=' with no value ('{} := _')", vd.text(),
                                       vd.text());
            continue;
        }
        const bool noDefault = isNoneMarker(vd.m_initializer.get());
        if (isStruct && !noDefault) {
            m_actx.ctx().diag().report(Severity::Error, vd.range(), "Struct field '{}' must not have a default value (POD); use ':=' with no value ('{} := _')",
                                       vd.text(), vd.text());
            continue;
        }
        m_core.analyzeNode(member);
        const Symbol* s = m_actx.symbols().current().lookup(vd.text());
        const TypeId ft = (s != nullptr) ? structuralType(s->type) : INVALID_TYPE_ID;
        if (ft == INVALID_TYPE_ID) {
            m_actx.ctx().diag().report(Severity::Error, vd.range(), "field '{}' of '{}' must have a resolvable type", vd.text(), rec_name);
            continue;
        }
        fields.push_back(TupleElementData{std::string(vd.text()), ft});
    }

    // -- Доопределение Record-типа: поля/базы. Forward-объявление (без тела) остаётся неполным
    //    (`struct c_Name;`), доопределяемое более поздним телом. Обычный путь без forward -
    //    declareRecord выше создал неполный тип, здесь он становится определённым.
    if (rec.m_body.has_value()) {
        if (reg.defineRecord(rec_name, group, std::move(fields), std::move(bases), decl_range, hasTrust) == INVALID_TYPE_ID) {
            m_core.exitScope();
            return; // дубликат/расхождение - диагностику сформировал реестр
        }
    }

    // -- Методы (FuncDecl): анализ (тело + сигнатура), регистрация как методов типа.
    //    Операторы (имя-СИМВОЛ в обратных кавычках): дубликат символа в типе запрещён (в C++
    //    это была бы повторная регистрация - addMethod делает FAULT, поэтому проверяем заранее).
    std::set<std::string> declaredOperators;
    std::vector<std::tuple<std::string, TypeId, FuncDecl*>> methodRegs; // (bare, signature, decl)
    for (auto& member : members) {
        if (!member || member->kind() != ParserToken::Kind::FuncDecl) {
            continue;
        }
        m_core.analyzeNode(member);
        auto& f = static_cast<FuncDecl&>(*member);
        TypeId ft = INVALID_TYPE_ID;
        if (f.m_isOperator) {
            // Оператор-МЕТОД: в таблицу символов не попадает (см. analyzeFuncDecl) - валидация и
            // регистрация здесь. Дубликат символа в типе запрещён (addMethod делает FAULT).
            if (!declaredOperators.insert(std::string(f.text())).second) {
                m_actx.ctx().diag().report(Severity::Error, f.range(), "operator '{}' is already declared in this type", f.text());
                continue;
            }
            if (!semantic::validateOperatorDecl(m_actx.ctx(), f, /*isMember=*/true)) {
                continue;
            }
            ft = m_actx.buildFuncType(f);
        } else {
            // Обычный метод: сигнатура строится НАПРЯМУЮ из объявления. `lookup().type` здесь
            // непригоден: у ПЕРЕГРУЖЕННОГО имени `Symbol::type` - лишь представитель набора (первая
            // сигнатура), а нужна сигнатура ИМЕННО этого объявления.
            ft = m_actx.buildFuncType(f);
        }
        if (ft == INVALID_TYPE_ID) {
            continue; // диагностику выдал анализ функции
        }
        const std::string bare = utils::bare_name(f.text());
        reg.addMethod(rec_id, bare, ft);
        methodRegs.emplace_back(bare, ft, &f);
    }
    // Перегруженные ПОЛЬЗОВАТЕЛЬСКИЕ методы: уникальные C++-имена (иначе C++ выберет не ту
    // перегрузку). Суффикс по сигнатуре; кодоген применяет его и в объявлении, и в вызове
    // (`Binary::resolvedMethodSuffix` ставит семантика при разрешении метода).
    {
        std::map<std::string, int> countByName;
        for (const auto& [bare, ft, decl] : methodRegs) {
            (void)ft;
            (void)decl;
            ++countByName[bare];
        }
        for (const auto& [bare, ft, decl] : methodRegs) {
            if (countByName[bare] > 1) {
                decl->m_isOverloaded = true;
                decl->m_overloadSuffix = overloadCppSuffix(reg, ft);
            }
        }
    }

    m_core.exitScope();

    // -- Привязка имени типа в ВНЕШНЕМ скоупе. Завершение ранее объявленного forward тем же
    //    TypeId - не дубликат (forward + определение); имя, занятое ДРУГИМ символом, - дубликат.
    Symbol ts;
    ts.name = rec_name;
    ts.type = rec_id;
    ts.decl = &binary_node;
    const Symbol* existing = m_actx.symbols().current().lookup(rec_name);
    if (existing != nullptr && existing->type != rec_id) {
        m_actx.ctx().diag().report(Severity::Error, decl_range, "duplicate declaration '{}'", rec_name);
        return;
    }
    if (existing == nullptr) {
        m_actx.symbols().declare(ts);
    }
    TRUST_DEBUG("declare", "type '{}' depth={}", rec_name, m_actx.symbols().depth());
    for (auto& hook : m_core.m_hooks) {
        hook->onDeclare(ts);
    }
}

} // namespace trust
