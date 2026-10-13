// src/semantic/decl/var_decl_analyzer.cpp
// Анализ объявлений переменных.
#include "semantic/decl/var_decl_analyzer.hpp"
#include "semantic/core/ast_view.hpp"
#include "semantic/core/local_name.hpp"
#include "semantic/decl/exported_signature_check.hpp"
#include "semantic/decl/type_completeness.hpp"
#include "semantic/core/name_resolution.hpp"
#include "semantic/typer/type_set.hpp"
#include "semantic/verify/format_check.hpp"
#include "semantic/typer/operator_check.hpp"
#include "semantic/typer/call_binding.hpp"
#include "semantic/solver.hpp"
#include "symbols/symbol_table.hpp"
#include "types/call.hpp"
#include "semantic/typer/type_inference.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/token.hpp"
#include "ast/token_type.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "types/typekind.hpp"
#include "utils/strings.hpp"
#include "utils/utils.hpp"
#include <algorithm>
#include <format>
#include <string>
#include <unordered_set>

namespace trust {

void VarDeclAnalyzer::analyzeVarDecl(VarDecl& var_node) {
    std::string var_name{var_node.text()};
    MapperRange var_range = var_node.range();
    if (op::isSystemName(var_name)) {
        m_actx.ctx().diag().report(Severity::Error, var_range, "system name '{}' is reserved for compiler-generated entities", var_name);
        return;
    }

    // Резолв необязательной аннотации типа.
    TypeId var_type = INVALID_TYPE_ID;
    if (var_node.m_type) {
        auto type_id = m_actx.types().resolveTypeRef(*var_node.m_type);
        if (type_id.has_value()) {
            var_type = *type_id;
        } else {
            m_actx.ctx().diag().report(Severity::Error, var_node.m_type->range(), "unknown type '{}'", displayTextOrKind(*var_node.m_type));
        }
    }
    // Конструктор record-шаблона типизируется по типу-цели (`b : Box<Int32> := Box()`):
    // запоминаем инстанциацию на CallExpr, чтобы кодоген эмитил `c_Box<int32_t>(...)`.
    m_actx.types().coerceTemplateCtor(var_node.m_initializer.get(), var_type);
    // ПОЗИЦИЯ задания вида ссылки при объявлении. Умные ссылки (shared/unique/weak) задаются
    // РОВНО ОДНОЙ из двух позиций:
    //   * у ТИПА (каноническая): `x : @[reftype("shared")@] T` ИЛИ `x : &&T`;
    //   * у ПЕРЕМЕННОЙ (сокращённая), только при ОПУЩЕННОМ типе (авто-вывод): `&& x := ...`.
    // Вид в ОБЕИХ позициях с ОДИНАКОВЫМ видом - лишний квалификатор у переменной → -Wref-kind-dup;
    // с РАЗНЫМИ видами - конфликт (ошибка reference kind mismatch в applyRefAttrs).
    // Вид у переменной при ЯВНОМ обычном типе (`&& x : Int32`) - ошибка (у типа вида нет).
    const AttrPool& declAttrs = m_actx.ctx().attrs();
    const bool varHasRefSpec = var_node.has_attr(declAttrs, attr::Reftype);
    const std::optional<RefType> varKind = varHasRefSpec ? refKindOfAttr(declAttrs, var_node) : std::nullopt;
    const std::optional<RefType> typeKind = refKindOfTypeSpec(var_node.m_type.get(), declAttrs);
    // Вид у переменной (короткий маркер) ИЛИ вид-маркер в аннотации типа.
    const std::optional<RefType> sigilKind = refKindOfTypeNode(var_node.m_type.get());
    const std::optional<RefType> refKind = varKind.has_value() ? varKind : sigilKind;
    if (varKind.has_value() && typeKind.has_value()) {
        // Вид задан в ОБЕИХ позициях. Совпадение видов - лишний квалификатор у переменной
        // (предупреждение); расхождение - конфликт, ошибку выдаст applyRefAttrs (kind mismatch).
        if (*varKind == *typeKind) {
            m_actx.ctx().report(var_range, semantic::DiagId::RefKindDup,
                                "reference kind '{}' is already specified by the type annotation; the qualifier before variable '{}' is redundant",
                                refTypeName(*varKind), var_name);
        }
    } else if (varKind.has_value() && var_node.m_type) {
        // Вид у переменной, но у явного типа вида нет - неоднозначная форма: либо вид задаётся
        // в типе (после ':'), либо тип опускается для авто-вывода (см. модель выше).
        m_actx.ctx().diag().report(Severity::Error, var_range,
                                   "reference variable '{}' has an explicit type without a reference kind; specify the reference kind in the type "
                                   "(after ':'), or omit the type to auto-deduce it from the initializer",
                                   var_name);
        return;
    }
    // Trust-тип: переменная несёт ссылку на узел декларации типа (источник trust-условий;
    // nullptr для нетрастовых - условия на типе невозможны, см. typeExpr). Переживает таблицу символов.
    var_node.m_typeDecl = m_actx.types().trustTypeDeclOf(var_type);

    AstNodePtr init_node = var_node.m_initializer;

    // В `:=` справа должно быть ЗНАЧЕНИЕ, а не тип-имя: `x := :Int32` невалидно. Тип объявляется
    // через `::=` (`x ::= :Int32` → тип-алиас). Голый `:T` - TypeName; конструкция `:T(a)` - единый
    // узел DictLiteralNode (это выражение-значение, не затрагивается).
    if (init_node && init_node->kind() == ParserToken::Kind::TypeName) {
        m_actx.ctx().diag().report(Severity::Error, var_range, "cannot assign a type '{}' to a value variable '{}'; use '::=' to declare a type alias",
                                   requiredText(*init_node), var_name);
        return;
    }

    // Предварительное (forward) объявление `x:Type := ...;` - инициализатора нет.
    // Для нативного имени (%...) тип обязателен: имя напрямую транслируется в C++.
    if (!init_node && !var_name.empty() && var_name[0] == '%' && !var_node.m_type) {
        m_actx.ctx().diag().report(Severity::Error, var_range, "native variable '{}' must have a type in a forward declaration", var_name);
        return;
    }

    // Имя без сигила в локальном скоупе: нормализуем в локальную переменную с '$' префиксом
    // ($x) и предупреждаем (опция -Wsigil, default Warning). Локальный скоуп = внутри функции
    // (в стеке скоупов есть FuncDecl); уровень модуля/глобальный - НЕ локальный. Единый хелпер
    // normalizeLocalSigil используется и declareDestructureTarget (унификация sigil-логики).
    const bool isLocal = m_actx.namespaces().isInLocalScope();
    // Граница API: короткие (символьные) маркеры ссылки допустимы ТОЛЬКО в локальном скоупе.
    // На уровне модуля (экспортируемое имя) вид задаётся исключительно явным @[reftype(...)].
    if (!isLocal && ((sigilKind.has_value() && isSmartRefKind(*sigilKind)) || hasAutoRefKindAttr(var_node, declAttrs))) {
        m_actx.ctx().diag().report(Severity::Error, var_range,
                                   "short reference markers are only allowed in local scope; use the explicit attribute @[reftype(\"...\")] on the module API");
    }
    // Bare-имя ДО сигил-нормализации: параметры/внешние локали хранятся без '$', а локаль
    // `x` нормализуется в '$x' - для детекции shadowing (общее C++-имя `c_x`) сравниваем и то,
    // и другое (`$n` и `n` - одно локальное имя, см. name_resolution.cpp).
    const std::string bareName = var_name;
    var_name = normalizeLocalSigil(m_actx, var_node, var_node.nameRange(), isLocal);

    Symbol sym;
    sym.name = stableSymbolName(var_name);
    // Константность ('^' → attr::ReadOnly) и вид ссылки (@[reftype(...)]) - ортогональные
    // квалификаторы, применяемые единым хелпером (applyRefAttrs). Константность в типе даёт
    // `const T` в C++ (getCppTypeName) и попадает в прототипы функций. Для нетипизированной
    // переменной (var_type == INVALID) бит const выставляется позже, в typeExpr, когда тип
    // выводится из инициализатора.
    // Источник квалификаторов: И узел переменной (`@[reftype("shared")@] x`), и аннотация типа
    // (`x : @[reftype("shared")@] Int32`). Транспилятор (emitTypeNameForNode) для типизированной
    // переменной читает их с m_type, поэтому применяем из ОБОИХ мест - чтобы Symbol::type совпал
    // с C++-именем (иначе рассинхрон: семантика считает kShared, транспилятор - голый тип).
    // Квалификаторы аннотации (const/reftype/policy/impl) материализует resolveTypeRef в
    // IdentType::resolvedState; здесь применяются только квалификаторы УЗЛА переменной.
    QualType var_state = QualType::fromTypeId(var_type);
    if (var_node.m_type && var_node.m_type->kind() == ParserToken::Kind::TypeName) {
        const auto& annotation = static_cast<const IdentType&>(*var_node.m_type);
        if (annotation.resolvedState.isValid()) {
            var_state = annotation.resolvedState;
        }
    }
    var_state = m_core.applyRefAttrs(var_state, var_node, var_range);
    sym.type = var_state;
    // Кодоген рендерит ссылочную переменную из материализованного типа (без повторного reftype).
    if (getRefType(getTypeKind(var_state.getTypeId())) != RefType::kValue) {
        var_node.inferredType = QualType::fromTypeId(var_state.getTypeId());
    }
    // `^` на переменной и `@[readonly]` на типе уже применены applyRefAttrs к var_type;
    // QualType несёт const как type-state (identity — getTypeId()).
    // Признак «тип выведен» (inferred) закодирован битом в TypeId (withInferred) и
    // выставляется в typeExpr, когда тип выводится из инициализатора. Явная аннотация
    // `x:Type :=` даёт структурный тип (без бита) → фиксированный.
    sym.decl = &var_node;

    // Взятие слабой ссылки из shared-переменной (`w := & x`) требует, чтобы вид weak был задан
    // (в ЛЮБОЙ позиции: `&? w := & x` или `w : &?Int32 := & x`). Если вид weak не задан нигде -
    // ошибка + fixit с маркером у переменной.
    if (var_node.m_initializer && var_node.m_initializer->kind() == ParserToken::Kind::RefMakeExpr && !(refKind.has_value() && *refKind == RefType::kWeak)) {
        // Уточнение: заём у unique (borrow, `& u`) НЕ требует weak-маркера - это другой вид.
        bool operandIsUnique = false;
        const auto& mke = static_cast<const RefMakeExpr&>(*var_node.m_initializer);
        if (!mke.m_body.empty() && mke.m_body[0] && mke.m_body[0]->kind() == ParserToken::Kind::Ident) {
            const std::string on(requiredText(*mke.m_body[0]));
            const Symbol* s = m_actx.symbols().resolve(on);
            if (s == nullptr && !on.empty() && on[0] != '$') {
                s = m_actx.symbols().resolve("$" + on); // локальные имена нормализованы в '$name'
            }
            if (s != nullptr && s->type.getTypeId() != INVALID_TYPE_ID) {
                operandIsUnique = getRefType(getTypeKind(s->type.getTypeId())) == RefType::kUnique;
            }
        }
        if (!operandIsUnique) {
            // Исходное (bare) имя переменной для сообщения/fixit (без сигила '$').
            std::string bare(var_node.text());
            if (!bare.empty() && bare[0] == '$') {
                bare.erase(0, 1);
            }
            auto* entry = m_actx.ctx().diag().report(
                Severity::Error, var_range,
                "taking a weak reference from a shared variable requires the weak-reference marker; declare '&? {}' instead of '{}'", bare, bare);
            m_actx.ctx().diag().fixit(entry, var_node.nameRange(), "&? " + bare);
        }
    }

    // Месторасположение (физическая память): TLS → ThreadLocal; имя с '::' → Static
    // (переменная в области имён); локальный скоуп → Local (стек); иначе Global.
    if (var_node.has_attr(m_actx.ctx().attrs(), attr::ThreadLocal)) {
        sym.storage = Storage::ThreadLocal;
    } else if (utils::isQualifiedName(var_name)) {
        sym.storage = Storage::Static;
    } else if (isLocal) {
        sym.storage = Storage::Local;
    }

    // Переопределение имени: предупреждение, если объявление затеняет имя из ВНЕШНЕГО скоупа
    // (глобал, внешняя локаль или параметр функции). Точный дубликат в ТОМ ЖЕ скоупе остаётся
    // ошибкой ниже (declareOrComplete → Duplicate). Опция -Wshadow.
    if (m_actx.symbols().lookupCurrent(var_name) == nullptr) {
        const Symbol* outer = m_actx.symbols().resolve(var_name);
        if (!outer && var_name != bareName) {
            outer = m_actx.symbols().resolve(bareName); // `$x` ↔ параметр `x`
        }
        if (outer) {
            const bool isParam = outer->decl && outer->decl->kind() == ParserToken::Kind::ArgNode;
            const Severity sev = m_actx.ctx().opts().get(semantic::DiagId::Shadow);
            if (sev != Severity::Ignore) {
                if (isParam) {
                    m_actx.ctx().diag().report(sev, var_range, semantic::DiagId::Shadow, "declaration of '{}' shadows a function parameter", var_name);
                } else {
                    m_actx.ctx().diag().report(sev, var_range, semantic::DiagId::Shadow, "declaration of '{}' shadows an outer variable", var_name);
                }
            }
        }
    }

    // Регистрация в текущем скоупе (дубликат - ошибка). Forward-объявление (init == nullptr)
    // может быть завершено последующим определением того же имени (declareOrComplete → Completed).
    // S1: file-private тип в типе экспортируемой переменной недопустим.
    if (semantic::isExportedTopLevel(m_actx)) {
        semantic::checkExportedTypeVisibility(m_actx, sym.type.getTypeId(), var_range, "variable '" + var_name + "'");
    }
    // Неполный (forward) тип допустим только за невладеющей ссылкой. Поля record
    // анализируются этим же путём (VarDecl в теле типа) - текст диагностики различается.
    // Вывод типа (`h := make();`) проверяется по объявлению callee в VarTyper — здесь `sym.type`
    // ещё не содержит выведенного типа.
    {
        const std::string entity = std::string(m_actx.namespaces().insideTypeBody() ? "field '" : "variable '") + var_name + "'";
        semantic::requireCompleteValueType(m_actx, var_node.m_type.get(), sym.type, var_range, entity);
    }
    const DeclResult var_decl_result = m_actx.symbols().declareOrComplete(sym);
    if (var_decl_result == DeclResult::Duplicate) {
        m_actx.ctx().diag().report(Severity::Error, var_range, "duplicate declaration '{}'", var_name);
        return;
    }
    if (var_decl_result == DeclResult::AlreadyRegistered) {
        return; // идемпотентная повторная регистрация (ромбовидный/повторный импорт)
    }
    TRUST_DEBUG("declare", "var '{}' depth={}", var_name, m_actx.symbols().depth());
    for (auto& hook : m_core.hooks()) {
        hook->onDeclare(sym);
    }

    // `x := _;` не инициализирует переменную: flow-факт (read-before-init) вычисляет
    // отдельный CFG/dataflow-проход (semantic/definite_assignment) после обхода.

    // Статическая размерность из инициализатора-словаря (для статической проверки `d.1`)
    // и типы полей (для вывода типа `d.two`/`d.1`/`d[0]`). Копируются на символ переменной
    // (свойство Dims переменной; см. архитектуру). Иммутабельный случай.
    if (init_node && init_node->kind() == ParserToken::Kind::DictLiteral) {
        if (Symbol* s = m_actx.symbols().resolveMutable(var_name)) {
            const auto& dl = static_cast<const Sequence&>(*init_node);
            s->dims = static_cast<int64_t>(dl.m_body.size());
            for (const auto& el : dl.m_body) {
                if (!el) {
                    continue;
                }
                std::string fname;
                const AstNodeBase* valueNode = nullptr;
                collectionElementNameValue(el.get(), fname, valueNode);
                s->dictFieldTypes.emplace_back(fname, valueNode ? m_core.typer().dictElementType(valueNode) : QualType{});
            }
        }
    }

    // Инициализатор обходится общим механизмом (analyzeNode → children()).

    // Trust-условия переменной: резолв имён + обработка по -Wsolver/--solver-mode (verify/MEMORY.md).
    m_core.trust().processTrustConditions(var_node.m_trust, var_node);
}

} // namespace trust
