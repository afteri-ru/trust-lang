// Generated: src/semantic/decl_analyzer.cpp
#include "semantic/decl_analyzer.hpp"
#include "semantic/name_resolution.hpp"
#include "semantic/type_set.hpp"
#include "semantic/analysis_common.hpp"
#include "semantic/format_check.hpp"
#include "semantic/operator_check.hpp"
#include "semantic/solver.hpp"
#include "analysis/symbol_table.hpp"
#include "types/overload_resolve.hpp"
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
#include "types/typekind.hpp"
#include "utils/strings.hpp"
#include "utils/trace.hpp"
#include <algorithm>
#include <format>
#include <string>
#include <unordered_set>

namespace trust {

namespace {

// Авто-маркерный (короткий) вид ссылки на узле объявления: term_to_ast конвертирует символьный
// маркер ПЕРЕД именем (`&* x := ...`) в атрибут @[reftype] с manual=false; явный `@[reftype(...)]`
// имеет manual=true. Используется правилом «короткие маркеры запрещены на границе API».
bool hasAutoReftypeAttr(const AstNodeAttr& node, const AttrPool& pool) {
    const auto rid = pool.lookup(attr::Reftype);
    if (!rid.has_value()) {
        return false;
    }
    const AttrId idx = static_cast<AttrId>(*rid & detail::kAttrIndexMask);
    for (const AttrId id : node.attrs()) {
        if (static_cast<AttrId>(id & detail::kAttrIndexMask) == idx && !detail::is_manual(id)) {
            return true;
        }
    }
    return false;
}

} // namespace

// -- Объявления --

void DeclAnalyzer::analyzeVarDecl(VarDecl& var_node) {
    std::string var_name{var_node.text()};
    MapperRange var_range = var_node.range();

    // Резолв необязательной аннотации типа.
    TypeId var_type = INVALID_TYPE_ID;
    if (var_node.m_type) {
        auto type_id = m_actx.resolveTypeRef(*var_node.m_type);
        if (type_id.has_value()) {
            var_type = *type_id;
        } else {
            m_actx.ctx().diag().report(Severity::Error, var_node.m_type->range(), "unknown type '{}'", var_node.m_type->text());
        }
    }
    // Конструктор record-шаблона типизируется по типу-цели (`b : Box<Int32> := Box()`):
    // запоминаем инстанциацию на CallExpr, чтобы кодоген эмитил `c_Box<int32_t>(...)`.
    m_actx.coerceRecordTemplateCtor(var_node.m_initializer.get(), var_type);
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
    var_node.m_typeDecl = m_core.trustTypeDeclOf(var_type);

    AstNodePtr init_node = var_node.m_initializer;

    // В `:=` справа должно быть ЗНАЧЕНИЕ, а не тип-имя: `x := :Int32` невалидно. Тип объявляется
    // через `::=` (`x ::= :Int32` → тип-алиас). Голый `:T` - TypeName; конструкция `:T(a)` - единый
    // узел DictLiteralNode (это выражение-значение, не затрагивается).
    if (init_node && init_node->kind() == ParserToken::Kind::TypeName) {
        m_actx.ctx().diag().report(Severity::Error, var_range, "cannot assign a type '{}' to a value variable '{}'; use '::=' to declare a type alias",
                                   init_node->text(), var_name);
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
    const bool isLocal = m_core.isInLocalScope();
    // Граница API: короткие (символьные) маркеры ссылки допустимы ТОЛЬКО в локальном скоупе.
    // На уровне модуля (экспортируемое имя) вид задаётся исключительно явным @[reftype(...)].
    if (!isLocal && ((sigilKind.has_value() && isSmartRefKind(*sigilKind)) || hasAutoReftypeAttr(var_node, declAttrs))) {
        m_actx.ctx().diag().report(Severity::Error, var_range,
                                   "short reference markers are only allowed in local scope; use the explicit attribute @[reftype(\"...\")] on the module API");
    }
    // Bare-имя ДО сигил-нормализации: параметры/внешние локали хранятся без '$', а локаль
    // `x` нормализуется в '$x' - для детекции shadowing (общее C++-имя `c_x`) сравниваем и то,
    // и другое (`$n` и `n` - одно локальное имя, см. name_resolution.cpp).
    const std::string bare_name = var_name;
    var_name = normalizeLocalSigil(var_node, var_node.nameRange(), isLocal);

    Symbol sym;
    sym.name = var_name;
    // Константность ('^' → attr::ReadOnly) и вид ссылки (@[reftype(...)]) - ортогональные
    // квалификаторы, применяемые единым хелпером (applyRefAttrs). Константность в типе даёт
    // `const T` в C++ (getCppTypeName) и попадает в прототипы функций. Для нетипизированной
    // переменной (var_type == INVALID) бит const выставляется позже, в typeExpr, когда тип
    // выводится из инициализатора.
    // Источник квалификаторов: И узел переменной (`@[reftype("shared")@] x`), и аннотация типа
    // (`x : @[reftype("shared")@] Int32`). Транспилятор (emitTypeNameForNode) для типизированной
    // переменной читает их с m_type, поэтому применяем из ОБОИХ мест - чтобы Symbol::type совпал
    // с C++-именем (иначе рассинхрон: семантика считает kShared, транспилятор - голый тип).
    if (var_node.m_type && var_node.m_type->as_attr()) {
        var_type = m_core.applyRefAttrs(var_type, *var_node.m_type->as_attr(), var_node.m_type->range());
    }
    var_type = m_core.applyRefAttrs(var_type, var_node, var_range);
    sym.type = var_type;
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
            const std::string on(mke.m_body[0]->text());
            const Symbol* s = m_actx.symbols().resolve(on);
            if (s == nullptr && !on.empty() && on[0] != '$') {
                s = m_actx.symbols().resolve("$" + on); // локальные имена нормализованы в '$name'
            }
            if (s != nullptr && s->type != INVALID_TYPE_ID) {
                operandIsUnique = getRefType(getKindFromId(s->type)) == RefType::kUnique;
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
    } else if (var_name.find("::") != std::string::npos) {
        sym.storage = Storage::Static;
    } else if (isLocal) {
        sym.storage = Storage::Local;
    }

    // Переопределение имени: предупреждение, если объявление затеняет имя из ВНЕШНЕГО скоупа
    // (глобал, внешняя локаль или параметр функции). Точный дубликат в ТОМ ЖЕ скоупе остаётся
    // ошибкой ниже (declareOrComplete → Duplicate). Опция -Wshadow.
    if (m_actx.symbols().current().lookup(var_name) == nullptr) {
        const Symbol* outer = m_actx.symbols().resolve(var_name);
        if (!outer && var_name != bare_name) {
            outer = m_actx.symbols().resolve(bare_name); // `$x` ↔ параметр `x`
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
    if (m_actx.symbols().declareOrComplete(sym) == DeclResult::Duplicate) {
        m_actx.ctx().diag().report(Severity::Error, var_range, "duplicate declaration '{}'", var_name);
        return;
    }
    TRUST_DEBUG("declare", "var '{}' depth={}", var_name, m_actx.symbols().depth());
    for (auto& hook : m_core.m_hooks) {
        hook->onDeclare(sym);
    }

    // `x := _;` (без инициализатора): взводим пер-Symbol признак «не инициализирована».
    // Маркер `_` в позиции инициализатора - Ident с исходным термом; узлы без терма (ручные
    // тестовые) исключаем ДО вызова text() (text() требует m_term). Переменные с обычным
    // инициализатором остаются инициализированными (признак false).
    if (isLocal && isNoneMarker(init_node.get())) {
        if (Symbol* s = m_actx.symbols().resolveMutable(var_name)) {
            // Признак «не инициализирована» - ортогональный бит kUninitFlag на Symbol::type
            // (как пер-переменная константность); для нетипизированной переменной тип ещё не
            // известен (INVALID) - бит взведётся при выводе типа в typeExpr.
            if (s->type != INVALID_TYPE_ID) {
                s->type = setFlag(s->type, SymbolFlag::Uninit);
            }
        }
    }

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
                s->dictFieldTypes.emplace_back(fname, valueNode ? m_core.m_typer.dictElementType(valueNode) : INVALID_TYPE_ID);
            }
        }
    }

    // Инициализатор обходится общим механизмом (analyzeNode → children()).

    // Trust-условия переменной (`y @{ ... @} := ...`): резолв имён + обработка по -Wsolver/--solver-mode.
    m_core.m_trust.processTrustConditions(var_node.m_trust, var_node);
}

void DeclAnalyzer::analyzeFuncDecl(FuncDecl& func_node) {
    std::string func_name{func_node.text()};
    MapperRange func_range = func_node.range();

    // Набор допустимых типов в сигнатуре (`f(x:(:A + :B))`): проверка комбинации (ветки/подтипы/
    // составные типы) - здесь; разворот в N определений отложен. Явная диагностика.
    if (func_node.m_type && func_node.m_type->kind() == ParserToken::Kind::TypeSet) {
        if (semantic::validateTypeSet(static_cast<const Sequence&>(*func_node.m_type), m_actx)) {
            m_actx.ctx().diag().report(Severity::Error, func_node.m_type->range(), "type sets in a function return type are not implemented yet");
        }
        return;
    }
    if (func_node.m_params) {
        for (const auto& p : *func_node.m_params) {
            if (p && p->kind() == ParserToken::Kind::ArgNode) {
                const auto& pd = static_cast<const ArgNode&>(*p);
                if (pd.m_type && pd.m_type->kind() == ParserToken::Kind::TypeSet) {
                    if (semantic::validateTypeSet(static_cast<const Sequence&>(*pd.m_type), m_actx)) {
                        m_actx.ctx().diag().report(Severity::Error, pd.m_type->range(), "type sets in a function parameter type are not implemented yet");
                    }
                    return;
                }
            }
        }
    }

    // Перегружаемый оператор (лексема REFLECTION, имя-СИМВОЛ в обратных кавычках).
    // Оператор-МЕТОД регистрируется анализатором ТИПА (record/class analyzer) как метод типа
    // (TypeRegistry::addMethod) и в таблицу символов НЕ попадает: имя-символ не адресуемо, а
    // резолв использования member-оператора идёт по типу операнда (`findMethodInfo`).
    // Свободный оператор - обычный путь функции: валидация здесь + регистрация в скоупе (модуль),
    // откуда его и резолвит использование (`symbols().resolve(sym)` в типизации выражений).
    const bool isMemberOperator = func_node.m_isOperator && m_actx.insideTypeBody();
    if (func_node.m_isOperator && !isMemberOperator) {
        if (m_actx.currentFunc() != nullptr) {
            m_actx.ctx().diag().report(Severity::Error, func_range, "a free operator must be declared at module scope");
            return;
        }
        if (!semantic::validateOperatorDecl(m_actx.ctx(), func_node, /*isMember=*/false)) {
            return;
        }
    }

    // Нативная функция (%...) транслируется в C++ напрямую → в forward-объявлении
    // тип возврата обязателен (без типа вернули бы голую декларацию без типа).
    if (!func_node.m_body.has_value() && !func_name.empty() && func_name[0] == '%' && !func_node.m_type) {
        m_actx.ctx().diag().report(Severity::Error, func_range, "native function '{}' must have a return type in a forward declaration", func_name);
        return;
    }

    // Валидация атрибута контроля переполнения стека:
    //   @[stack_check@]      - без аргумента (limit) либо @[stack_check(N)@] с одним целым N >= 0.
    {
        const AttrPool& attrs = m_actx.ctx().attrs();
        if (auto sc = attrs.lookup(attr::StackCheck); sc.has_value() && func_node.has_attr(*sc)) {
            if (const auto* args = func_node.attr_args(*sc); args && !args->empty()) {
                if (args->size() != 1) {
                    m_actx.ctx().diag().report(Severity::Error, func_range, "@[stack_check@] expects zero or one integer argument (stack size in bytes)");
                } else {
                    const std::string& v = args->at(0);
                    bool ok = !v.empty();
                    for (const char c : v) {
                        if (c < '0' || c > '9') {
                            ok = false;
                            break;
                        }
                    }
                    if (!ok) {
                        m_actx.ctx().diag().report(Severity::Error, func_range,
                                                   "@[stack_check({})@]: argument must be a non-negative integer (stack size in bytes)", v);
                    }
                }
            }
        }
    }

    // Квалификаторы deleter/lifetime/pin применяются только к объявлениям переменных/параметров
    // (NameResolutionPass::applyRefAttrs). На возвращаемом типе они НЕ применяются (reftype на
    // возврате поддержан отдельно через resolveTypeRef) - явная диагностика вместо тихого игнора.
    if (func_node.m_type) {
        if (const AstNodeAttr* ta = func_node.m_type->as_attr()) {
            const AttrPool& attrs = m_actx.ctx().attrs();
            const auto reject = [&](std::string_view name) {
                const auto id = attrs.lookup(name);
                if (id.has_value() && ta->has_attr(*id)) {
                    m_actx.ctx().diag().report(
                        Severity::Error, ta->range(),
                        "attribute '{}' is not supported on a function return type (qualifiers apply to variable/parameter declarations only)", name);
                }
            };
            reject(attr::Deleter);
            reject(attr::Lifetime);
            reject(attr::Pin);
        }
    }

    // Граница API: короткие (символьные) маркеры в возвращаемом типе функции запрещены -
    // сигнатура является контрактом API; используйте явный @[reftype(...)]. Правило относится к
    // УМНЫМ маркерам (`&&`/`&*`/`&?`); нативные `%&`/`%*` - отдельная переходная ось.
    if (func_node.m_type) {
        if (const auto rk = refKindOfTypeNode(func_node.m_type.get()); rk.has_value() && isSmartRefKind(*rk)) {
            m_actx.ctx().diag().report(Severity::Error, func_node.m_type->range(),
                                       "short reference markers are not allowed in a function return type; use the explicit attribute @[reftype(\"...\")]");
        }
    }

    // Регистрация имени функции с функциональным типом сигнатуры (return + параметры).
    // Оператор-МЕТОД в таблицу символов НЕ кладётся: он живёт в таблице методов типа
    // (TypeRegistry::addMethod, регистрирует анализатор типа) - общие проверки объявления выше
    // уже выполнены, а вызывающий продолжит анализ тела.
    if (isMemberOperator) {
        return;
    }
    Symbol sym;
    sym.name = func_name;
    sym.type = m_actx.buildFuncType(func_node);
    sym.decl = &func_node;

    // Детерминированный C++-суффикс перегрузки по сигнатуре (кодоген применяет его ТОЛЬКО если
    // имя перегружено: `m_isOverloaded`). Ставим всегда - бесплатно и не зависит от порядка.
    func_node.m_overloadSuffix = overloadCppSuffix(m_actx.ctx().types(), structuralType(sym.type));

    // Регистрация в текущем скоупе (дубликат - ошибка). Forward-объявление (без тела) может
    // быть завершено последующим определением того же имени (declareOrComplete → Completed).
    if (m_actx.symbols().declareOrComplete(sym) == DeclResult::Duplicate) {
        m_actx.ctx().diag().report(Severity::Error, func_range, "duplicate declaration '{}'", func_name);
        return;
    }
    TRUST_DEBUG("declare", "func '{}' depth={}", func_name, m_actx.symbols().depth());
    for (auto& hook : m_core.m_hooks) {
        hook->onDeclare(sym);
    }
}

// Объявление нативного шаблона-ТИПА `<T> %std::vector() := ...;` - регистрирует абстрактный
// нативный шаблон (Group::kNativeTemplate) в реестре типов. trust-имя = последний компонент
// C++-имени (`std::vector` → "vector"), чтобы `vector<Int32>` резолвилось. preprocIncludes типа -
// из `@[include]` (мнемоника `@include`); инклуд подтягивается on-use при использовании типа.
// C++-имена встроенных контейнеров: `:Array` (mutable) → std::vector, `:Array^` (const) → std::array.
static bool isBuiltinArrayCppName(std::string_view cppName) noexcept {
    return cppName == "std::vector" || cppName == "std::array";
}
void DeclAnalyzer::analyzeNativeTemplateDecl(FuncDecl& func_node) {
    MapperRange func_range = func_node.range();
    std::string cppTemplate = func_node.m_nativeName; // "std::vector" (без '%')

    // trust-имя шаблона: последний компонент после '::'.
    std::string trustName = cppTemplate;
    const size_t pos = trustName.rfind("::");
    if (pos != std::string::npos) {
        trustName = trustName.substr(pos + 2);
    }
    if (trustName.empty()) {
        m_actx.ctx().diag().report(Severity::Error, func_range, "native template must have a non-empty C++ name");
        return;
    }

    // preprocInclude из `@[include("header")@]` (голое имя → угловой инклуд).
    std::string preprocInclude;
    {
        const AttrPool& attrs = m_actx.ctx().attrs();
        if (auto inc = attrs.lookup(attr::Include); inc.has_value() && func_node.has_attr(*inc)) {
            if (const auto* args = func_node.attr_args(*inc); args && !args->empty() && !args->at(0).empty()) {
                preprocInclude = "#include <" + args->at(0) + ">";
            }
        }
    }

    // Дубликат по C++-имени со встроенным контейнером (`std::vector`/`std::array` от `:Array`):
    // мягкая диагностика, тип инстанцируется через встроенный Array (повторной регистрации
    // дублирующего типа нет, `vector<Int32>` резолвится через `:Array`).
    if (isBuiltinArrayCppName(cppTemplate)) {
        m_actx.ctx().diag().report(Severity::Warning, func_range, "native template '{}' is a builtin container (use ':Array' / ':Array^' without declaration)",
                                   cppTemplate);
    }

    // Регистрация абстрактного нативного шаблона (дубликат имени - диагностика реестра).
    TypeId tpl = m_actx.ctx().types().registerNativeTemplate(trustName, cppTemplate, func_range, preprocInclude);
    if (tpl == INVALID_TYPE_ID) {
        return; // дубликат - диагностика сформирована реестром
    }
    // Биндинг имени в скоуп (shadowing/резолв имён; тип доступен и через реестр findType).
    Symbol as;
    as.name = trustName;
    as.type = tpl;
    as.decl = &func_node;
    if (m_actx.symbols().declare(as)) {
        for (auto& hook : m_core.m_hooks) {
            hook->onDeclare(as);
        }
    }
}

// Forward-объявление (нативного) класса `String ::= %std::string { ... };` (и шаблон-класса
// `Pair ::= %std::pair<T1,T2> { ... };`): регистрирует тип (trust-имя ↔ нативное C++-имя) +
// члены-интерфейс (методы/поля/конструкторы/статич-члены) как методы типа. Класс определён в
// C++-заголовке (инклуд из @[include] в preprocIncludes); C++-struct НЕ генерируется. Методы -
// forward (`:= ...`, тела нет): регистрируются сигнатуры.
// Регистрация параметров в текущем (функционном) скоупе - вызывается из analyzeNode
// ВНУТРИ enterScope() скоупа функции, чтобы имена в теле функции резолвились.
void DeclAnalyzer::declareFuncParams(FuncDecl& func_node) {
    if (!func_node.m_params) {
        return;
    }
    for (const auto& p : *func_node.m_params) {
        if (!p || p->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        auto& pd = static_cast<ArgNode&>(*p);
        // Граница API: короткие (символьные) маркеры в типе параметра запрещены - сигнатура
        // функции является контрактом API; используйте явный @[reftype(...)]. Правило относится к
        // УМНЫМ маркерам (`&&`/`&*`/`&?`); нативные `%&`/`%*` - отдельная переходная ось.
        if (const auto pk = refKindOfTypeNode(pd.m_type.get()); pk.has_value() && isSmartRefKind(*pk)) {
            m_actx.ctx().diag().report(Severity::Error, pd.m_type->range(),
                                       "short reference markers are not allowed in a function parameter type; use the explicit attribute @[reftype(\"...\")]");
        }
        Symbol ps;
        ps.name = std::string(pd.text());
        TypeId ptype = (pd.m_type) ? m_actx.resolveTypeRef(*pd.m_type).value_or(INVALID_TYPE_ID) : INVALID_TYPE_ID;
        // Константность и вид ссылки параметра - из атрибутов узла ТИПА параметра
        // (`fmt: @[reftype(ptr)@] StrChar^`): reftype → RefType, ReadOnly → const.
        if (pd.m_type && pd.m_type->as_attr()) {
            ptype = m_core.applyRefAttrs(ptype, *pd.m_type->as_attr(), pd.m_type->range());
        }
        ps.type = ptype;
        ps.decl = &pd;
        ps.storage = Storage::Local; // параметры функции - стек
        m_actx.symbols().declare(ps);
        for (auto& hook : m_core.m_hooks) {
            hook->onDeclare(ps);
        }
    }
}

// Лямбда-выражение: резолв захватов в ОБЪЁМЛЮЩЕМ скоупе (вызывается ДО enterScope лямбды).
// Захват - ТОЛЬКО имя переменной, ТОЛЬКО по значению (копия). Некопируемый (unique/move-only)
// захват по значению - ошибка (без молчаливого move).
void DeclAnalyzer::analyzeLambdaCaptures(FuncDecl& func_node) {
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
        if (s->type != INVALID_TYPE_ID && refAxisOf(getRefType(getKindFromId(s->type))) == RefAxis::Unique) {
            m_actx.ctx().diag().report(Severity::Error, c.range(),
                                       "cannot capture exclusive-ownership value '{}' by value; "
                                       "lambda capture-by-value requires a copyable value",
                                       name);
            continue;
        }
        c.resultType = s->type; // тип захваченной переменной (копия)
    }
}

// Общий подсчёт слотов-элементов и валидация rest-цели для деструктуризации (spread-словаря и
// кортежа). Единый источник идентичного цикла в analyzeDestructure / analyzeDestructureTuple.
bool DeclAnalyzer::collectDestructureSlots(const DestructureDecl& node, size_t& elementSlots, bool& hasRest) {
    elementSlots = 0;
    hasRest = false;
    const size_t cnt = node.m_targets.size();
    for (size_t i = 0; i < cnt; ++i) {
        auto* t = node.m_targets[i].get();
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        const bool isRest = i < node.m_targetIsRest.size() && node.m_targetIsRest[i];
        if (isRest) {
            hasRest = true;
            if (i + 1 != cnt) {
                m_actx.ctx().diag().report(Severity::Error, t->range(), "rest target '...' must be the last destructuring target");
                return false; // фатально: rest не последняя - цели не разбираем
            }
        } else {
            ++elementSlots;
        }
    }
    return true;
}

// Деструктуризация `t1, ..., tN := [... ]source;`.
// Без маркера - ТОЧНАЯ привязка (Python/Rust/Go/C++/Haskell): каждая цель - один элемент; для
// статически-известного размера число целей == числу элементов. Суффикс `...` у цели (`rest...`,
// C++-pack) - «остаток»: связывает оставшиеся элементы; `_...` - извлечь, остаток отбросить;
// одиночный `_` - пропустить ровно один элемент. Спред (`... source`, Dict) - цели извлекаются
// pop_front (точная привязка) / rest = остаток; кортеж (без `...`) - цели std::get<N> с проверкой арности.
void DeclAnalyzer::analyzeDestructure(DestructureDecl& node) {
    if (node.m_source) {
        m_core.analyzeNode(node.m_source);
    }
    if (node.m_targets.empty()) {
        return;
    }
    // Кортеж (структурный источник, НЕ spread): цели = элементы по индексу.
    if (!node.m_isSpread) {
        const TypeId src = node.m_source ? m_actx.exprType(*node.m_source) : INVALID_TYPE_ID;
        const bool isTuple = src != INVALID_TYPE_ID && m_actx.ctx().types().getTypeDataAs<TupleTypeData>(src) != nullptr;
        if (isTuple) {
            analyzeDestructureTuple(node, src);
            return;
        }
        m_actx.ctx().diag().report(Severity::Error, node.m_source ? node.m_source->range() : node.range(),
                                   "destructuring requires a tuple source (or use '...' for a dictionary spread)");
        return;
    }
    // spread (коллекция Dict). Проверка типа источника: допустим только словарь (Dict) - иначе
    // pop_front на не-коллекции упал бы лишь на этапе C++-компиляции (тихий fallback в семантике).
    const TypeRegistry& reg = m_actx.ctx().types();
    const TypeId srcType = node.m_source ? m_actx.exprType(*node.m_source) : INVALID_TYPE_ID;
    if (!isDictTypeId(reg, srcType)) {
        m_actx.ctx().diag().report(Severity::Error, node.m_source ? node.m_source->range() : node.range(),
                                   "spread destructuring source must be a dictionary (Dict), got a non-collection type");
        return;
    }
    const size_t cnt = node.m_targets.size();
    // Число целей-элементов (НЕ rest) и наличие rest-маркера (`rest...` / `_...`).
    size_t elementTargets = 0;
    bool hasRest = false;
    if (!collectDestructureSlots(node, elementTargets, hasRest)) {
        return; // rest не последняя - Error репортнут
    }
    // Статическая арность: без rest - elementTargets == размер (точная привязка); с rest -
    // elementTargets <= размер (остаток поглощается rest).
    const int64_t size = m_core.m_typer.dictSizeOf(node.m_source.get());
    if (size >= 0) {
        if (hasRest) {
            if (static_cast<int64_t>(elementTargets) > size) {
                m_actx.ctx().diag().report(Severity::Error, node.m_source->range(),
                                           "destructuring arity mismatch: {} element target(s) before rest but dictionary has {} element(s)", elementTargets,
                                           size);
                return;
            }
        } else if (static_cast<int64_t>(elementTargets) != size) {
            m_actx.ctx().diag().report(Severity::Error, node.m_source->range(),
                                       "destructuring arity mismatch: {} target(s) but dictionary has {} element(s); add '...' rest or adjust targets",
                                       elementTargets, size);
            return;
        }
    }
    // Типизация целей: per-element (как в кортеже) - каждая цель получает runtime-тип своего
    // элемента (Int8..Int64 → Int64, Float → Double, Bool, StrChar...). ВНУТРИ ЦИКЛА тип расширяется
    // до МАКСИМАЛЬНОГО среди элементов (Bool/Int8 → Integer, float → Double) - элемент перечитывается
    // и может меняться; Any - только если тип не выводим (внешний источник / несовместимые категории).
    node.m_targetTypes.assign(cnt, INVALID_TYPE_ID);
    node.m_targetDeclaredTypes.assign(cnt, INVALID_TYPE_ID);
    node.m_inLoop = m_core.isInLoop();
    const std::vector<TypeId> elemTypes = m_core.m_typer.dictElementTypes(node.m_source.get());
    std::vector<TypeId> naturalized;
    naturalized.reserve(elemTypes.size());
    for (const TypeId et : elemTypes) {
        naturalized.push_back(m_core.m_typer.naturalRuntimeType(et));
    }
    const TypeId joined = node.m_inLoop ? m_core.m_typer.joinElementTypes(naturalized) : INVALID_TYPE_ID;
    bool untyped = elemTypes.empty();
    for (const TypeId et : elemTypes) {
        if (et != INVALID_TYPE_ID && testFlag(et, SymbolFlag::Inferred)) {
            untyped = true;
            break;
        }
    }
    if (node.m_inLoop && untyped) {
        if (joined != INVALID_TYPE_ID) {
            m_actx.ctx().report(node.range(), semantic::DiagId::WidenAny, "destructured element type is not specified; widened to the maximum element type");
        } else {
            m_actx.ctx().report(node.range(), semantic::DiagId::WidenAny, "destructured element type is not inferable; widened to the generic type (Any)");
        }
    }
    size_t elemIdx = 0;
    for (size_t i = 0; i < cnt; ++i) {
        auto* t = node.m_targets[i].get();
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        const bool isRest = i < node.m_targetIsRest.size() && node.m_targetIsRest[i];
        if (node.m_isAssign) {
            // Присваивание в существующие цели: резолв + проверка const. Тип any_cast - natural
            // runtime тип ЭЛЕМЕНТА (соответствует хранению Dict), а не цель; значение присваивается
            // в существующую переменную с её собственным типом (C++-конверсия).
            // rest в присваивании: допустима только мутация-идиома (rest == источнику); прочее
            // переиспользование - Error (иначе кодген молча присвоил бы Dict в несовместимую цель).
            if (isRest && !restTargetNameAllowed(static_cast<HasText&>(*t), /*isSpreadDict=*/true, node.m_source.get())) {
                continue;
            }
            node.m_targetTypes[i] = isRest ? INVALID_TYPE_ID : (elemIdx < naturalized.size() ? naturalized[elemIdx] : INVALID_TYPE_ID);
            assignDestructureTarget(node, i, static_cast<HasText&>(*t), isRest);
        } else {
            // Объявление: m_targetTypes[i] - тип any_cast = natural runtime тип ЭЛЕМЕНТА (как хранит
            // Dict); declaredType - тип объявляемой переменной (явная аннотация `a:Int32` фиксирует
            // его, иначе - тот же выведенный). Разделение важно: Dict хранит int как int64_t, поэтому
            // any_cast<int32_t> по аннотации Int32 упал бы на элементе, хранимом как int64_t.
            if (isRest) {
                // Шаг 1: переиспользование имени rest-цели. Допустима только мутация-идиома
                // (rest == источнику); прочее переиспользование - Error (иначе кодген молча дал бы
                // C++-redefinition). При конфликте цель не связываем.
                if (!restTargetNameAllowed(static_cast<HasText&>(*t), /*isSpreadDict=*/true, node.m_source.get())) {
                    continue;
                }
                // Шаг 2: аннотация типа на rest-цели запрещена - кодген фиксирует rest как Dict и
                // аннотацию молча игнорировал бы. Явная диагностика вместо тихого игнора.
                if (i < node.m_targetTypeNodes.size() && node.m_targetTypeNodes[i]) {
                    m_actx.ctx().diag().report(Severity::Error, t->range(), "type annotation on a rest target '{}...' is not supported; rest type is inferred",
                                               t->text());
                    continue;
                }
            }
            const TypeId inferred =
                isRest ? INVALID_TYPE_ID : (node.m_inLoop ? joined : (elemIdx < naturalized.size() ? naturalized[elemIdx] : INVALID_TYPE_ID));
            // Вне цикла тип цели не расширяется (per-element типизация): если тип конкретного
            // элемента не выводится (naturalRuntimeType → INVALID), цель молча становится std::any.
            // Симметрично цикловому предупреждению WidenAny - явная диагностика вместо тихого
            // fallback на Any (AGENTS rule 5 «no silent fallback»).
            if (!node.m_inLoop && !isRest && inferred == INVALID_TYPE_ID && t->text() != "_") {
                m_actx.ctx().report(t->range(), semantic::DiagId::WidenAny,
                                    "destructured element type for target '{}' is not inferable; widened to the generic type (Any)", t->text());
            }
            const TypeId declaredType = explicitTargetType(node, i, inferred);
            node.m_targetTypes[i] = inferred;                                        // any_cast<T> = как хранит Dict
            node.m_targetDeclaredTypes[i] = isRest ? INVALID_TYPE_ID : declaredType; // тип переменной
            declareDestructureTarget(static_cast<HasText&>(*t), isRest, declaredType);
        }
        if (!isRest) {
            ++elemIdx; // и `_`, и именованная цель занимают один элемент (индекс)
        }
    }
}

void DeclAnalyzer::analyzeDestructureTuple(DestructureDecl& node, TypeId tupleType) {
    const auto* td = m_actx.ctx().types().getTypeDataAs<TupleTypeData>(tupleType);
    const size_t elemCount = td ? td->elements.size() : 0;
    node.m_sourceArity = elemCount; // для кортеж-rest в кодогенерации (скоуп сброшен к моменту codegen)
    const size_t cnt = node.m_targets.size();
    // Слоты-элементы (связывание + skip `_`) и наличие rest-цели (`rest...` / `_...`).
    size_t slots = 0;
    bool hasRest = false;
    if (!collectDestructureSlots(node, slots, hasRest)) {
        return; // rest не последняя - Error репортнут
    }
    // Арность: без rest - слоты == число элементов; с rest - слоты <= числа элементов (остаток).
    if (hasRest) {
        if (slots > elemCount) {
            m_actx.ctx().diag().report(Severity::Error, node.range(),
                                       "destructuring arity mismatch: {} element target(s) before rest but tuple has {} element(s)", slots, elemCount);
            return;
        }
    } else if (slots != elemCount) {
        m_actx.ctx().diag().report(Severity::Error, node.range(), "destructuring arity mismatch: {} target(s) but tuple has {} element(s)", slots, elemCount);
        return;
    }
    node.m_targetTypes.assign(cnt, INVALID_TYPE_ID);
    size_t idx = 0;
    for (size_t i = 0; i < cnt; ++i) {
        auto& t = node.m_targets[i];
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        const bool isRest = i < node.m_targetIsRest.size() && node.m_targetIsRest[i];
        auto& h = static_cast<HasText&>(*t);
        if (isNoneMarker(t.get()) && !isRest) {
            ++idx; // skip-элемент занимает индекс, но не связывается
            continue;
        }
        if (node.m_isAssign) {
            // Присваивание в существующие цели (std::get): резолв + проверка const.
            assignDestructureTarget(node, i, h, isRest);
            if (!isRest) {
                ++idx;
            }
            continue;
        }
        if (isRest) {
            // rest: `_...` - отброс (ничего не связываем); именованный `rest...` - остаток кортежа
            // (C++-тип выводится в кодогенерации через make_tuple; семантический тип - исходный кортеж).
            if (isNoneMarker(t.get())) {
                continue;
            }
            // Шаг 1: rest кортежа не может переиспользовать существующую переменную (в т.ч. сам
            // источник): кортежный rest - НЕ мутация (в отличие от spread-словаря), иначе кодген
            // молча дал бы `auto c_t = std::make_tuple(std::get<2>(c_t)...)` (переобъявление/UB).
            if (!restTargetNameAllowed(h, /*isSpreadDict=*/false, node.m_source.get())) {
                continue;
            }
            // Шаг 2: аннотация типа на rest-цели запрещена - кодген кортежа эмитит `auto` и
            // аннотацию молча игнорировал бы. Явная диагностика вместо тихого игнора.
            if (i < node.m_targetTypeNodes.size() && node.m_targetTypeNodes[i]) {
                m_actx.ctx().diag().report(Severity::Error, t->range(), "type annotation on a rest target '{}...' is not supported; rest type is inferred",
                                           t->text());
                continue;
            }
            const TypeId restType = explicitTargetType(node, i, tupleType);
            node.m_targetTypes[i] = restType;
            declareDestructureTarget(h, /*isRest=*/true, restType);
            continue;
        }
        const TypeId elemType = (td && idx < td->elements.size()) ? td->elements[idx].type : INVALID_TYPE_ID;
        const TypeId explicitType = explicitTargetType(node, i, INVALID_TYPE_ID);
        node.m_targetTypes[i] = explicitType; // INVALID → кодген кортежа эмитит `auto`
        declareDestructureTarget(h, /*isRest=*/false, (explicitType != INVALID_TYPE_ID) ? explicitType : elemType);
        ++idx;
    }
}

// Нормализация bare-имени в локальном скоупе (`x` → `$x`, опция -Wsigil): правит текст узла
// и репортит предупреждение. Возвращает имя для символа (с сигилом при нормализации).
std::string DeclAnalyzer::normalizeLocalSigil(HasText& node, MapperRange range, bool isLocal) {
    std::string name{node.text()};
    if (isLocal && isSimpleVarName(name)) {
        const std::string sigil = "$" + name;
        node.set_text(sigil);
        // Быстрый фикс: заменить bare-имя на сигнальное `$name`. Отчёт через
        // diag().report(...) (возвращает DiagnosticEntry*) вместо ctx().report(...)
        // (discard), чтобы прикрепить fixit к диагностике.
        const Severity sev = m_actx.ctx().opts().get(semantic::DiagId::NoSigil);
        if (sev != Severity::Ignore) {
            auto* entry = m_actx.ctx().diag().report(sev, range, semantic::DiagId::NoSigil, "creating a local variable '${}'", name);
            if (entry != nullptr && !range.isInvalid()) {
                m_actx.ctx().diag().fixit(entry, range, sigil);
            }
        }
        return sigil;
    }
    return name;
}

// Каноническое имя цели деструктуризации (сигил-нормализация БЕЗ мутации узла и без
// предупреждения): bare-имя в локальном скоупе → "$" + имя. Совпадает с логикой
// declareDestructureTarget / normalizeLocalSigil (для проверки rest-переиспользования).
std::string DeclAnalyzer::canonicalTargetName(const HasText& t) const {
    std::string name{t.text()};
    if (m_core.isInLocalScope() && isSimpleVarName(name)) {
        return "$" + name;
    }
    return name;
}

// Проверка переиспользования имени именованной rest-цели (`rest...`). Для словаря допустима
// мутация-идиома (rest-цель == самому источнику: pop'ы идут прямо в источник, объявление не
// создаётся - см. declareDestructureTarget). Переиспользование ЛЮБОЙ другой существующей
// переменной - Error: без этой проверки кодген молча сгенерировал бы C++-redefinition
// (`trust::Dict c_x = ...` поверх уже объявленного c_x) без диагностики. Для кортежа rest
// никогда не мутация, поэтому переиспользование (в т.ч. самого источника) всегда Error.
bool DeclAnalyzer::restTargetNameAllowed(HasText& t, bool isSpreadDict, const AstNodeBase* source) {
    const std::string name = canonicalTargetName(t);
    if (name.empty() || name == "_") {
        return true;
    }
    if (!m_actx.symbols().resolve(name)) {
        return true; // имя свободно - можно объявлять заново
    }
    bool isSourceReuse = false;
    if (isSpreadDict && source && source->kind() == ParserToken::Kind::Ident) {
        isSourceReuse = (canonicalTargetName(static_cast<const HasText&>(*source)) == name);
    }
    if (isSourceReuse) {
        return true; // мутация-идиома `item, dict... := ... dict`
    }
    if (isSpreadDict) {
        m_actx.ctx().diag().report(Severity::Error, t.range(),
                                   "rest target '{}...' reuses an existing variable; only the source itself may be reused (mutation idiom)", name);
    } else {
        m_actx.ctx().diag().report(Severity::Error, t.range(),
                                   "tuple rest target '{}...' cannot reuse an existing variable; tuple rest is not a mutation (unlike dictionary spread)",
                                   name);
    }
    return false;
}

// Объявление одной цели деструктуризации. `_` - skip. isRest + уже объявленное имя (= источник)
// → «остаток» через мутацию pop_front, отдельного объявления нет.
void DeclAnalyzer::declareDestructureTarget(HasText& t, bool isRest, TypeId type) {
    std::string name{t.text()};
    if (name == "_") {
        return; // skip: элемент потребляется, переменная не создаётся
    }
    const MapperRange range = t.range();
    // Нормализация сигила (единый хелпер с analyzeVarDecl): bare-имя в локальном скоупе → $name.
    const bool isLocal = m_core.isInLocalScope();
    name = normalizeLocalSigil(t, range, isLocal);
    // «Остаток» (isRest): если имя уже объявлено (== источник) - это мутация pop_front, не объявляем.
    if (isRest && m_actx.symbols().resolve(name)) {
        return;
    }
    Symbol sym;
    sym.name = name;
    sym.type = (type != INVALID_TYPE_ID) ? type : (isRest ? m_actx.ctx().types().getType(type::Dict) : m_actx.ctx().types().getType(type_generic::Any));
    sym.decl = &t;
    sym.storage = Storage::Local;
    if (m_actx.symbols().declareOrComplete(sym) == DeclResult::Duplicate) {
        m_actx.ctx().diag().report(Severity::Error, range, "duplicate declaration '{}'", name);
        return;
    }
    for (auto& hook : m_core.m_hooks) {
        hook->onDeclare(sym);
    }
}

// Явный тип цели из аннотации (`a:Int32`, node.m_targetTypeNodes[i]); INVALID - аннотации нет
// (возвращается fallback) или тип не резолвится (диагностируется).
TypeId DeclAnalyzer::explicitTargetType(const DestructureDecl& node, size_t i, TypeId fallback) {
    if (i >= node.m_targetTypeNodes.size() || !node.m_targetTypeNodes[i]) {
        return fallback;
    }
    auto resolved = m_actx.resolveTypeRef(*node.m_targetTypeNodes[i]);
    if (!resolved) {
        m_actx.ctx().diag().report(Severity::Error, node.m_targetTypeNodes[i]->range(), "unknown type '{}'", node.m_targetTypeNodes[i]->text());
        return INVALID_TYPE_ID;
    }
    return *resolved;
}

// Цель деструктуризации-ПРИСВАИВАНИЯ (`a, b = ... source`): резолв существующей переменной,
// проверка на константность; объявление не создаётся. Тип any_cast задаёт вызывающий (цикл кладёт
// natural runtime тип элемента в node.m_targetTypes[i]). `_` - skip; rest == источник - мутация
// pop_front (присвоения нет).
void DeclAnalyzer::assignDestructureTarget(DestructureDecl& node, size_t i, HasText& t, bool isRest) {
    (void)node;
    (void)i;
    std::string name{t.text()};
    if (name == "_") {
        return; // skip: элемент потребляется, переменная не связывается
    }
    const MapperRange range = t.range();
    const bool isLocal = m_core.isInLocalScope();
    name = normalizeLocalSigil(t, range, isLocal);
    // «Остаток» (isRest): если имя уже объявлено (== источник) - это мутация pop_front, присвоения нет.
    if (isRest && m_actx.symbols().resolve(name)) {
        return;
    }
    Symbol* s = m_actx.symbols().resolveMutable(name);
    if (!s) {
        m_actx.ctx().diag().report(Severity::Error, range, "destructuring assignment target '{}' is not declared (use ':=' to create a variable)", name);
        return;
    }
    if (testFlag(s->type, SymbolFlag::Const)) {
        m_actx.ctx().diag().report(Severity::Error, range, "cannot assign to constant destructuring target '{}'", name);
        return;
    }
}
} // namespace trust
