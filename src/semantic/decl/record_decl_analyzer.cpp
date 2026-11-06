// Семантика объявления пользовательского Record-типа (Struct/Class):
//   :Name ::= :Struct{ поля };            - POD Struct (Group::kStructs, static_assert в C++)
//   :Name ::= :Class { поля; методы };    - Class (Group::kClassDefs)
//   :Derived ::= :Base{ ... };            - наследование Class (C++-наследование, baseClasses)
// Резолв баз, регистрация полей/методов и типа (registerRecordType), привязка имени типа.
#include "semantic/decl/decl_analyzer.hpp"
#include "semantic/decl/exported_signature_check.hpp"
#include "semantic/decl/pod_predicate.hpp"
#include "semantic/core/name_resolution.hpp"
#include "symbols/symbol_table.hpp"
#include "semantic/diag.hpp"
#include "semantic/typer/operator_check.hpp"
#include "semantic/typer/call_binding.hpp"
#include "diag/diag.hpp"
#include "ast/token.hpp"
#include "ast/token_base.hpp"
#include "attrs/attr_builtin.hpp"
#include "types/group.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/call.hpp"
#include "types/names.hpp"
#include "utils/strings.hpp"
#include "utils/utils.hpp"
#include <map>
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
    // Экспортируемость вычисляется ДО входа в класс-скоуп (внутри он уже не top-level).
    const bool exported = semantic::isExportedTopLevel(m_actx);
    const std::string rec_name = std::string(rec.text());
    if (rec_name.empty()) {
        m_actx.ctx().diag().report(Severity::Error, decl_range, "record type must have a non-empty name");
        return;
    }

    TypeSystem& reg = m_actx.registry();
    // Storage-ключ имени типа вычисляется ДО входа в класс-скоуп: private-контекст `__::`
    // относится к объявлению, а не к вложенному scope тела record.
    const auto rec_storage_keys = m_actx.types().typeStorageKeys(binary_node, rec_name);
    const StorageKey rec_storage_key = rec_storage_keys.front();
    // Binder-контекст класса: методы record-шаблона используют типовые параметры КЛАССА.
    const TemplateBinders classBinders = bindersOf(rec.m_templateParams);

    // Класс-скоуп открывается ДО резолва баз: типовые параметры шаблона (`<T>`) должны быть
    // видны и в базах (`:Base<:T>`), и в членах. Закрывается после регистрации/методов.
    m_core.enterScope(rec);

    // Типовые параметры класса регистрируются в класс-скоупе отдельными идентификаторами
    // (Group::kTemplateParam), чтобы поля/методы `: T` резолвились, а при инстанциации
    // подставлялись конкретным аргументом.
    const bool isTemplate = rec.m_templateParams.has_value();
    // Pack-параметры пользовательских record/class-шаблонов не поддержаны: поля/методы
    // с пакетом не инстанцируются. Явная ошибка (до регистрации параметров в скоупе).
    if (isTemplate) {
        for (const auto& p : *rec.m_templateParams) {
            if (p && p->is<ArgNode>() && p->as<ArgNode>()->isTypePack) {
                m_actx.ctx().diag().report(Severity::Error, p->range(), "type parameter packs in class templates are not implemented yet");
                m_core.exitScope();
                return;
            }
        }
    }
    // Группа (Struct/Class) — по маркерам баз: маркеры распознаются по тексту имени
    // (резолв не требуется). Struct (POD) не может наследовать — ни класс-маркер, ни базовый тип.
    Group group = Group::kClassDefs;
    {
        bool structMarker = false;
        bool classMarker = false;
        bool hasNonMarkerBase = false;
        for (const auto& b : rec.m_baseTypes) {
            if (!b.type) {
                continue;
            }
            std::string base_name(requiredText(*b.type));
            if (!base_name.empty() && base_name.front() == ':') {
                base_name.erase(0, 1);
            }
            if (base_name == type_category::Struct) {
                structMarker = true;
            } else if (base_name == type_category::Class) {
                classMarker = true;
            } else {
                hasNonMarkerBase = true;
            }
        }
        if (structMarker && (classMarker || hasNonMarkerBase)) {
            m_actx.ctx().diag().report(Severity::Error, decl_range, "Struct '{}' is POD and cannot inherit", rec_name);
            m_core.exitScope();
            return;
        }
        group = structMarker ? Group::kStructs : Group::kClassDefs;
    }

    // Record-тип регистрируется неполным до разбора тела (owner-scoping, C++-модель
    // TemplateTypeParmDecl). Имя record-типа видно в собственном теле (self-ссылка
    // `:Node ::= :Class{ next : shared<Node> }`) и служит точкой доопределения для forward'ов.
    // Тип неполон (complete=false) до defineRecord ниже.
    const bool hasTrust = !binary_node.m_trust.empty();
    const TypeId rec_id = reg.registrar().declareRecord(rec_name, group, isTemplate, decl_range, hasTrust, rec_storage_key);
    if (rec_id == INVALID_TYPE_ID) {
        m_core.exitScope();
        return; // дубликат/конфликт имени - диагностику сформировал реестр
    }
    m_actx.types().aliasTypeKeys(rec_id, rec_storage_keys);
    // Идентичность типа на УЗЛЕ-объявлении (RecordDecl) — для проверки доступа к членам из
    // контекста тела класса (`NamespaceScope::currentTypeBodyId`).
    rec.resolvedTypeId = rec_id;

    // Типовые параметры объявляются в класс-скоупе (одно имя в РАЗНЫХ шаблонах не конфликтует),
    // чтобы поля/методы `: T` резолвились, а при инстанциации подставлялись аргументами по позиции.
    std::vector<TypeId> templateParams;
    if (isTemplate) {
        templateParams.reserve(rec.m_templateParams->size());
        std::size_t idx = 0;
        for (const auto& p : *rec.m_templateParams) {
            const std::size_t paramIndex = idx++;
            if (!p) {
                continue;
            }
            const TypeId pt = reg.interner().getOrCreateTemplateParamType(requiredText(*p), /*owner=*/rec_id, /*depth=*/0, /*index=*/paramIndex);
            templateParams.push_back(pt);
            Symbol ps;
            ps.name = stableSymbolName(std::string(requiredText(*p)));
            ps.type = QualType::fromTypeId(pt);
            ps.decl = p.get();
            ps.storage = Storage::Local;
            m_actx.symbols().declare(ps);
        }
        reg.registrar().setRecordTemplateParams(rec_id, std::move(templateParams));
    }

    // Базовые типы: `:Class` (корень) и/или пользовательские Class-типы (наследование). Абстрактные маркеры
    //    (:Struct/:Class/:Any) в baseClasses НЕ попадают (в C++ не эмитятся). Шаблонные базы
    //    (`:Base<:T>`) резолвятся через resolveTypeRef (инстанциация с типовым параметром в аргументе).
    std::vector<BaseClass> bases;
    for (const auto& b : rec.m_baseTypes) {
        if (!b.type) {
            continue;
        }
        std::string base_name(requiredText(*b.type));
        if (!base_name.empty() && base_name.front() == ':') {
            base_name.erase(0, 1);
        }
        if (base_name == type_category::Struct || base_name == type_category::Class) {
            continue; // маркер - уже обработан в пре-скане определения группы
        }
        auto tid = m_actx.types().resolveTypeRef(*b.type);
        if (!tid.has_value() || *tid == INVALID_TYPE_ID) {
            m_actx.ctx().diag().report(Severity::Error, b.type->range(), "unknown base type '{}'", base_name);
            m_core.exitScope();
            return;
        }
        if (!reg.read().store().isRecordType(*tid)) {
            m_actx.ctx().diag().report(Severity::Error, b.type->range(), "base type '{}' must be a Struct/Class", base_name);
            m_core.exitScope();
            return;
        }
        if (!reg.read().store().isClassType(*tid)) {
            m_actx.ctx().diag().report(Severity::Error, b.type->range(), "Struct '{}' is POD and cannot be used as a base", base_name);
            m_core.exitScope();
            return;
        }
        bases.push_back(BaseClass{*tid, b.access});
    }

    // Определение record-типа: forward (`:Name ::= :Base ...;` → C++ incomplete `struct c_Name;`,
    // членов нет); engaged - полное определение. `members` - единый доступ к членам (пустой для forward).
    std::vector<AstNodePtr> noMembers;
    std::vector<AstNodePtr>& members = rec.body() ? *rec.body() : noMembers;

    // Атрибуты полиморфизма (@virtual/@override) проверяются на каждом носителе (объявление или
    // член), чтобы не игнорировать их молча (AGENTS п.5).
    reportUnimplementedPolymorphismAttrs(m_actx, rec);
    for (const auto& member : members) {
        if (member) {
            reportUnimplementedPolymorphismAttrs(m_actx, *member);
        }
    }

    // Значения по умолчанию полей.
    // Struct (POD): default ЗАПРЕЩЁН (NSDMI делает тип нетривиальным) - допустимо только
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
        if (vd.bindsNativeCpp()) {
            m_actx.ctx().diag().report(Severity::Error, vd.range(), "native export is not applicable to record fields ('{}'): use the owner type projection",
                                       vd.text());
            continue;
        }
        if (vd.initKind() == VarInitKind::Forward) {
            m_actx.ctx().diag().report(Severity::Error, vd.range(), "field '{}' cannot be a forward declaration; use ':=' with no value ('{} := _')", vd.text(),
                                       vd.text());
            continue;
        }
        const bool noDefault = vd.initKind() != VarInitKind::Initialized;
        if (isStruct && !noDefault) {
            m_actx.ctx().diag().report(Severity::Error, vd.range(), "Struct field '{}' must not have a default value (POD); use ':=' with no value ('{} := _')",
                                       vd.text(), vd.text());
            continue;
        }
        m_core.analyzeNode(member);
        const Symbol* s = m_actx.symbols().lookupCurrent(vd.text());
        const QualType fstate = (s != nullptr) ? s->type : QualType{};
        const TypeId ft = fstate.getTypeId();
        if (ft == INVALID_TYPE_ID) {
            m_actx.ctx().diag().report(Severity::Error, vd.range(), "field '{}' of '{}' must have a resolvable type", vd.text(), rec_name);
            continue;
        }
        // Константность ОБЪЯВЛЕННОГО поля сохраняется в QualType (TupleElementData::type) и
        // доезжает до типизации доступа/кодогенерации. Ограничения C++-представления:
        //   * Struct (POD) с const-членом не является тривиально конструируемым -> запрет;
        //   * const-поле Class обязано иметь инициализатор (NSDMI), иначе C++ не соберётся;
        //   * const-массив-поле не имеет формы NSDMI в текущем кодогене -> запрет.
        if (fstate.isConst()) {
            if (isStruct) {
                m_actx.ctx().diag().report(Severity::Error, vd.range(), "Struct field '{}' cannot be constant (POD); use a Class or remove '^'", vd.text());
                continue;
            }
            if (noDefault) {
                m_actx.ctx().diag().report(Severity::Error, vd.range(), "constant field '{}' requires a default value", vd.text());
                continue;
            }
            if (reg.read().store().isArrayType(ft)) {
                m_actx.ctx().diag().report(Severity::Error, vd.range(), "constant array field '{}' is not supported", vd.text());
                continue;
            }
        }
        // Struct — строго POD: поле с ЗАВЕДОМО не-POD типом (класс, ссылка, строка, Any, массив)
        // отвергается семантикой в точке объявления (R13) вместо ошибки C++ static_assert. Предикат
        // частичный (R4): неизвестные/нативные типы остаются вердикту целевого static_assert.
        if (isStruct && semantic::isDefinitelyNonPodFieldType(ft, reg.read())) {
            m_actx.ctx().diag().report(Severity::Error, vd.range(),
                                       "Struct field '{}' has a non-POD type; a POD Struct requires trivial standard-layout fields (use a Class)", vd.text());
            continue;
        }
        fields.push_back(TupleElementData{std::string(vd.text()), fstate, vd.visibility, vd.declId()});
    }

    // defineRecord: forward (`struct c_Name;`), доопределяемое более поздним телом. Обычный путь
    // без forward - declareRecord выше создал неполный тип, здесь он становится определённым.
    if (rec.hasBody()) {
        if (reg.registrar().defineRecord(rec_name, group, std::move(fields), std::move(bases), decl_range, hasTrust, rec_storage_key) == INVALID_TYPE_ID) {
            m_core.exitScope();
            return; // дубликат/расхождение - диагностику сформировал реестр
        }
    }

    if (exported) {
        semantic::checkExportedTypeVisibility(m_actx, rec_id, decl_range, "record type '" + rec_name + "'");
    }

    // Системное имя текущего объекта `$0` (лексер: токен ARGUMENT) в телах методов. Тип - сам
    // record-тип; кодоген эмитит приёмник метода (`(*this)`). Вне метода `$0` недопустим.
    {
        Symbol selfSym;
        selfSym.name = stableSymbolName("$0");
        selfSym.type = QualType::fromTypeId(rec_id);
        selfSym.storage = Storage::Local;
        m_actx.symbols().declare(selfSym);
    }

    // Методы и операторы record-типа. Операторы (системные dunder-имена): перегрузка — набор
    // сигнатур; дубль определяется по эффективной сигнатуре (dunder-имя + типы параметров) до addMethod.
    for (auto& member : members) {
        if (!member || member->kind() != ParserToken::Kind::FuncDecl) {
            continue;
        }
        m_core.analyzeNode(member);
        auto& f = static_cast<FuncDecl&>(*member);
        if (f.bindsNativeCpp()) {
            m_actx.ctx().diag().report(Severity::Error, f.range(), "native export is not applicable to record methods ('{}'): use the owner type projection",
                                       f.text());
            continue;
        }
        TypeId ft = INVALID_TYPE_ID;
        MethodKey methodKey = methodKeyFromSpelling(f.text());
        // '^' в имени нормализуется AST в attr::ReadOnly (spelling уже без '^') — читаем атрибут.
        methodKey.isConst = f.has_attr(m_actx.ctx().attrs(), attr::ReadOnly);
        const std::string bare = methodKey.name;
        if (op::isSystemName(bare) && !op::isOperatorFunctionName(bare)) {
            continue; // диагностику уже выдал analyzeFuncDecl (системное имя зарезервировано)
        }
        if (f.m_isOperator) {
            // Оператор-МЕТОД: в таблицу символов не попадает (см. analyzeFuncDecl) - валидация и
            // регистрация здесь. Перегрузка - обычный НАБОР сигнатур; дубль определяется по
            // ЭФФЕКТИВНОЙ сигнатуре (prefix/postfix + типы параметров), а не по символу.
            if (!semantic::validateOperatorDecl(m_actx.ctx(), f, /*isMember=*/true)) {
                continue;
            }
            ft = m_actx.types().buildFuncType(f);
            if (ft == INVALID_TYPE_ID) {
                continue;
            }
            if (reg.read().methods().hasOwnMethodSignature(rec_id, methodKey, ft, classBinders)) {
                m_actx.ctx().diag().report(Severity::Error, f.range(), "operator '{}' with the same parameter types is already declared in this type",
                                           op::symbolOfFunction(f.text()));
                continue;
            }
        } else {
            // Обычный метод: сигнатура строится НАПРЯМУЮ из объявления. `lookup().type` здесь
            // непригоден: у ПЕРЕГРУЖЕННОГО имени `Symbol::type` - лишь представитель набора (первая
            // сигнатура), а нужна сигнатура ИМЕННО этого объявления.
            ft = m_actx.types().buildFuncType(f);
            if (ft == INVALID_TYPE_ID) {
                continue;
            }
            // Точный дубль сигнатуры - диагностика ДО addMethod (тот на дубле делает EXPECT).
            if (reg.read().methods().hasOwnMethodSignature(rec_id, methodKey, ft, classBinders)) {
                m_actx.ctx().diag().report(Severity::Error, f.range(), "method '{}' with the same parameter types is already declared in this type", bare);
                continue;
            }
        }
        reg.methods().addMethod(rec_id, methodKey, ft, {}, callableParamInfo(f), classBinders, f.visibility, f.m_isOperator ? DeclId{} : f.declId());
        // C++-имя метода - чистая функция от identity объявления (как у свободных функций):
        // ставим ВСЕГДА, а не только при перегруженном имени. Операторы не манглируются
        // (C++-имя фиксировано `operator<sym>`).
        if (!f.m_isOperator && f.declId().isValid()) {
            // C++-суффикс — целевой факт объявления (вне языковой модели, R1): `DeclCppFacts` по `declId()`.
            m_actx.ctx().declCppFacts().setCppSuffix(f.declId(), reg.identity().cppSuffix(ft, classBinders));
        }
    }

    m_core.exitScope();

    // Регистрация имени record-типа в текущем скоупе: тот же TypeId - не дубликат (forward +
    // определение); имя, занятое ДРУГИМ символом, - дубликат.
    Symbol ts;
    ts.name = stableSymbolName(rec_name);
    ts.type = QualType::fromTypeId(rec_id);
    ts.decl = &binary_node;
    const Symbol* existing = m_actx.symbols().lookupCurrent(rec_name);
    // Объявление ДРУГОГО модуля с тем же spelling — не дубль: голое имя станет неоднозначным
    // (диагностика на сайте использования), а `declare` зарегистрирует идентичность модуля.
    const bool foreign = existing != nullptr && m_actx.symbols().isForeignDeclaration(*existing);
    if (existing != nullptr && !foreign && existing->type.getTypeId() != rec_id) {
        m_actx.ctx().diag().report(Severity::Error, decl_range, "duplicate declaration '{}'", rec_name);
        return;
    }
    if (existing != nullptr && !foreign) {
        // Завершение forward'а определением: обновляет и текущий скоуп (владелец/сайт импорта),
        // и экспорт-поверхность; для реализации за forward'ом интерфейса поверхность не меняется.
        m_actx.symbols().completeTypeDeclaration(ts, rec_storage_key);
    } else {
        m_actx.symbols().declare(ts);
    }
    TRUST_DEBUG("declare", "type '{}' depth={}", rec_name, m_actx.symbols().depth());
    // Идемпотентная повторная регистрация (ромбовидный/повторный импорт): объявление уже
    // связано с тем же TypeId - хуки повторно НЕ уведомляются (иначе дубли в SymbolIndex).
    if (existing != nullptr && !foreign) {
        return;
    }
    for (auto& hook : m_core.hooks()) {
        hook->onDeclare(ts);
    }
}

} // namespace trust
