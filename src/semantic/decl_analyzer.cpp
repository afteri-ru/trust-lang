// Generated: src/semantic/decl_analyzer.cpp
#include "semantic/decl_analyzer.hpp"
#include "semantic/name_resolution.hpp"
#include "semantic/analysis_common.hpp"
#include "semantic/format_check.hpp"
#include "semantic/solver.hpp"
#include "semantic/symbol_table.hpp"
#include "semantic/type_inference.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/promotion.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "utils/operators.hpp"
#include "utils/strings.hpp"
#include <algorithm>
#include <format>
#include <string>

namespace trust {

// -- Объявления --

void DeclAnalyzer::analyzeVarDecl(VarDecl& var_node) {
    std::string var_name{var_node.text()};
    MapperRange var_range = var_node.range();

    // Резолв необязательной аннотации типа.
    TypeId var_type = INVALID_TYPE_ID;
    if (var_node.m_type) {
        auto type_id = m_actx.resolveType(*var_node.m_type);
        if (type_id.has_value()) {
            var_type = *type_id;
        } else {
            m_actx.ctx().diag().report(Severity::Error, var_node.m_type->range(), "unknown type '{}'", var_node.m_type->text());
        }
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
    var_name = normalizeLocalSigil(var_node, var_node.nameRange(), isLocal);

    Symbol sym;
    sym.name = var_name;
    // Константность ('^' → attr::ReadOnly) и вид ссылки (@[reftype(...)]) - ортогональные
    // квалификаторы, применяемые единым хелпером (applyRefAttrs). Константность в типе даёт
    // `const T` в C++ (getCppTypeName) и попадает в прототипы функций. Для нетипизированной
    // переменной (var_type == INVALID) бит const выставляется позже, в typeExpr, когда тип
    // выводится из инициализатора.
    var_type = m_core.applyRefAttrs(var_type, var_node, var_range);
    sym.type = var_type;
    // Признак «тип выведен» (inferred) закодирован битом в TypeId (withInferred) и
    // выставляется в typeExpr, когда тип выводится из инициализатора. Явная аннотация
    // `x:Type :=` даёт структурный тип (без бита) → фиксированный.
    sym.decl = &var_node;

    // Месторасположение (физическая память): TLS → ThreadLocal; имя с '::' → Static
    // (переменная в области имён); локальный скоуп → Local (стек); иначе Global.
    if (var_node.has_attr(m_actx.ctx().attrs(), attr::ThreadLocal)) {
        sym.storage = Storage::ThreadLocal;
    } else if (var_name.find("::") != std::string::npos) {
        sym.storage = Storage::Static;
    } else if (isLocal) {
        sym.storage = Storage::Local;
    }

    // Регистрация в текущем скоупе (дубликат - ошибка). Forward-объявление (init == nullptr)
    // может быть завершено последующим определением того же имени (declareOrComplete → Completed).
    if (m_actx.symbols().declareOrComplete(sym) == DeclResult::Duplicate) {
        m_actx.ctx().diag().report(Severity::Error, var_range, "duplicate declaration '{}'", var_name);
        return;
    }
    for (auto& hook : m_core.m_hooks) {
        hook->onDeclare(sym);
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

void DeclAnalyzer::analyzeTypeDecl(Binary& binary_node) {
    auto* left = binary_node.m_left.get();
    if (!left || left->kind() != ParserToken::Kind::Ident) {
        m_actx.ctx().diag().report(Severity::Error, binary_node.range(), "type declaration must have a name on the left");
        return;
    }

    std::string type_name = std::string(left->text());

    auto* right = binary_node.m_right.get();
    if (!right) {
        m_actx.ctx().diag().report(Severity::Error, binary_node.range(), "type '{}' must have a definition", type_name);
        return;
    }

    // Enum/Variant-объявление (ПОСТФИКС `(...):Enum`/`(...):Variant`, НЕ префикс `:Enum(...)`):
    // правая часть - DictLiteral с аннотацией «Enum»/«Variant». Голые члены = безнарные (валидны).
    if (right->kind() == ParserToken::Kind::DictLiteral) {
        const auto& dl = static_cast<const DictLiteralNode&>(*right);
        if (!dl.prefix && dl.m_type && dl.m_type->text() == "Enum") {
            analyzeEnumDecl(binary_node);
            return;
        }
        if (!dl.prefix && dl.m_type && dl.m_type->text() == "Variant") {
            analyzeVariantDecl(binary_node);
            return;
        }
    }

    // Определяем базовый TypeId правой части (имя типа: алиас или встроенный).
    TypeId base_id = INVALID_TYPE_ID;
    if (right->kind() == ParserToken::Kind::TypeName) {
        // y ::= Int; - alias на существующий тип.
        base_id = m_actx.resolveType(*right).value_or(INVALID_TYPE_ID);
        if (base_id == INVALID_TYPE_ID) {
            m_actx.ctx().diag().report(Severity::Error, right->range(), "type '{}' not found", right->text());
            return;
        }
    } else if (right->kind() == ParserToken::Kind::Ident) {
        // y ::= MyInt; - правая часть - имя ТИПА (пользовательский алиас). Оператор '::='
        // создаёт ТОЛЬКО типы: ссылка на переменную справа - ошибка (не «алиас на переменную»).
        const Symbol* vs = m_actx.symbols().resolve(right->text());
        if (!vs) {
            m_actx.ctx().diag().report(Severity::Error, right->range(), "undefined name '{}'", right->text());
            return;
        }
        if (vs->decl->kind() != ParserToken::Kind::TypeDecl) {
            m_actx.ctx().diag().report(Severity::Error, right->range(), "'::=' right side must be a type, '{}' is not a type", right->text());
            return;
        }
        base_id = vs->type;
        if (base_id == INVALID_TYPE_ID) {
            m_actx.ctx().diag().report(Severity::Error, right->range(), "type of '{}' is not resolved", right->text());
            return;
        }
    } else {
        m_actx.ctx().diag().report(Severity::Error, right->range(), "unsupported type alias definition");
        return;
    }

    // Регистрация алиаса в реестре типов (метаданные TypeId). Тип-алиас с trust-условиями
    // (непустой m_trust после имени) помечается битом trust в TypeKind - семантический
    // дифференциатор идентичности (и защита от авто-вывода типа, см. typeExpr).
    TypeId alias_id = m_actx.ctx().types().registerType(type_name, base_id, {}, right->range(), {}, !binary_node.m_trust.empty());
    if (alias_id == INVALID_TYPE_ID) {
        return; // дубликат - диагностику сформировал реестр
    }

    // Биндинг имени алиаса в текущем скоупе (shadowing/коллизии через скоуп-стек).
    Symbol as;
    as.name = type_name;
    as.type = alias_id;
    as.decl = &binary_node;
    if (!m_actx.symbols().declare(as)) {
        m_actx.ctx().diag().report(Severity::Error, left->range(), "duplicate declaration '{}'", type_name);
        return;
    }
    for (auto& hook : m_core.m_hooks) {
        hook->onDeclare(as);
    }

    // Trust-условия типа (`MyInt ::= Int32 @{ ... @}`): резолв имён + обработка по -Wsolver/--solver-mode.
    m_core.m_trust.processTrustConditions(binary_node.m_trust, binary_node);
}

// -- Единый сбор членов `(name=value / name:Type=value / bare name)` из DictLiteral RHS --
// Контракт: элементы m_body - ArgNode (имя в text(), явный тип в m_type, значение в m_value),
// строятся term_to_ast::appendDictElementsFromArgs. Чтение (имя/тип/значение) - НАПРЯМУЮ из
// ArgNode, без обёрток и без разворачивания. Значение члена Variant - AST-выражение (источник -
// ArgNode.m_value); в реестре - только разрешённый тип члена.

// -- Объявление enum-типа (`Color ::= :Enum(RED=1, GREEN=2,)` / `(RED=1, GREEN=2,):Enum`) --
// TypeDecl(Binary): left = имя типа, right = DictLiteral с аннотацией m_type «Enum»; элементы
// m_body - ArgNode (имя, явный тип, значение). Регистрирует enum-тип, вычисляет единый тип
// значений (по общим правилам, предупреждение WidenAny при повышении до Any), биндит имя и
// регистрирует классические методы.
void DeclAnalyzer::analyzeEnumDecl(Binary& binary_node) {
    const std::string enum_name = std::string(binary_node.m_left->text());
    TypeRegistry& reg = m_actx.ctx().types();
    const MapperRange decl_range = binary_node.range();

    auto* right = binary_node.m_right.get();
    EXPECT(right && right->kind() == ParserToken::Kind::DictLiteral && "analyzeEnumDecl: RHS must be Enum-annotated DictLiteral");
    auto& dict = static_cast<DictLiteralNode&>(*right);

    // -- Члены: (имя, значение|null, явный тип|null) - напрямую из элементов m_body (ArgNode).
    const auto& body = dict.m_body;
    const auto isMember = [](const AstNodePtr& el) { return el && el->kind() == ParserToken::Kind::ArgNode; };

    size_t memberCount = 0;
    for (const auto& el : body) {
        if (isMember(el)) {
            ++memberCount;
        }
    }
    if (memberCount == 0) {
        m_actx.ctx().diag().report(Severity::Error, decl_range, "enum '{}' must have at least one member", enum_name);
        return;
    }

    std::vector<EnumMemberData> md;
    md.reserve(memberCount);
    TypeId valueType = INVALID_TYPE_ID;

    // -- Проход 1: тип - из ЯВНЫХ аннотаций члена (`A:Rational`); иначе из значений --
    bool haveExplicitType = false;
    for (const auto& el : body) {
        if (isMember(el) && static_cast<const ArgNode&>(*el).m_type) {
            haveExplicitType = true;
            break;
        }
    }
    if (haveExplicitType) {
        for (const auto& el : body) {
            if (!isMember(el)) {
                continue;
            }
            const auto& a = static_cast<const ArgNode&>(*el);
            const AstNodePtr ta = a.m_type;
            if (!ta) {
                continue;
            }
            auto tid = m_actx.resolveType(*ta);
            if (!tid.has_value()) {
                m_actx.ctx().diag().report(Severity::Error, ta->range(), "enum '{}': unknown member type", enum_name);
                continue;
            }
            const TypeId c = reg.getCanonicalTypeId(*tid);
            if (valueType == INVALID_TYPE_ID) {
                valueType = c;
            } else if (valueType != c) {
                m_actx.ctx().diag().report(Severity::Error, ta->range(), "enum '{}' member types differ ('{}' vs '{}')", enum_name,
                                           reg.getFullTypeName(valueType), reg.getFullTypeName(c));
                return;
            }
        }
        if (valueType == INVALID_TYPE_ID) {
            valueType = reg.getType(type::Int64);
        }
    } else {
        // Тип из явных значений (resolvedType + join); если явных нет - минимальный Int по числу членов.
        std::vector<TypeId> explicitTypes;
        for (const auto& el : body) {
            if (!isMember(el)) {
                continue;
            }
            const AstNodePtr v = enumVariantMember(static_cast<const ArgNode&>(*el)).value;
            if (v) {
                explicitTypes.push_back(m_actx.resolvedType(*v));
            }
        }
        if (explicitTypes.empty()) {
            valueType = intTypeForLiteral(reg, memberCount - 1);
        } else {
            TypeId common = INVALID_TYPE_ID;
            bool allSame = true;
            for (const TypeId vt : explicitTypes) {
                const TypeId c = (vt != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(vt) : INVALID_TYPE_ID;
                if (common == INVALID_TYPE_ID) {
                    common = c;
                } else if (c != INVALID_TYPE_ID && common != c) {
                    allSame = false;
                }
            }
            if (allSame && common != INVALID_TYPE_ID) {
                valueType = common;
            } else {
                std::vector<TypeId> nat;
                nat.reserve(explicitTypes.size());
                for (const TypeId vt : explicitTypes) {
                    nat.push_back(m_core.m_typer.naturalRuntimeType(vt));
                }
                valueType = m_core.m_typer.joinElementTypes(nat);
                if (valueType == INVALID_TYPE_ID) {
                    valueType = reg.getType(type_generic::Any);
                    m_actx.ctx().report(decl_range, semantic::DiagId::WidenAny, "enum '{}' members have incompatible value types; value type widened to Any",
                                        enum_name);
                }
            }
        }
    }
    // valueType всегда разрешён выше (тип из аннотаций / значений / JOIN → Any с предупреждением
    // WidenAny). Ветка INVALID здесь невозможна - молча не подменяем, а ловим инвариантом.
    EXPECT(valueType != INVALID_TYPE_ID && "analyzeEnumDecl: value type must be resolved");

    // -- Проход 2: значения членов (автоинкремент для целого типа, иначе ординал) --
    const bool integerVT = getGroup(getKindFromId(reg.getCanonicalTypeId(valueType))) == Group::kIntegers;
    unsigned long long cur = 0;
    bool haveValue = false;
    size_t ordinal = 0;
    for (const auto& el : body) {
        if (!isMember(el)) {
            continue;
        }
        const EnumVariantMember m = enumVariantMember(static_cast<const ArgNode&>(*el));
        const AstNodePtr v = m.value;
        std::string vstr;
        if (v) {
            vstr = v->text();
            if (integerVT) {
                unsigned long long parsed = 0;
                if (parseDecimalUInt(v->text(), parsed)) {
                    cur = parsed;
                    haveValue = true;
                }
            }
        } else if (integerVT) {
            // Автоинкремент: безнарный член = предыдущее значение + 1 (первый = 0).
            cur = haveValue ? (cur + 1) : 0;
            haveValue = true;
            vstr = std::to_string(cur);
        } else {
            // Не-целый тип: безнарный член = ординал (позиция).
            vstr = std::to_string(ordinal);
        }
        md.push_back(EnumMemberData{m.name, std::move(vstr)});
        ++ordinal;
    }

    // -- Регистрация enum-типа в реестре (EnumTypeData; дубликат → диагностика реестра).
    const TypeId enum_id = reg.registerEnumType(enum_name, valueType, std::move(md), decl_range, !binary_node.m_trust.empty());

    if (enum_id == INVALID_TYPE_ID) {
        return;
    }

    // -- Биндинг имени enum-типа в текущем скоупе (shadowing через скоуп-стек).
    Symbol es;
    es.name = enum_name;
    es.type = enum_id;
    es.decl = &binary_node;
    if (!m_actx.symbols().declare(es)) {
        m_actx.ctx().diag().report(Severity::Error, decl_range, "duplicate declaration '{}'", enum_name);
        return;
    }
    for (auto& hook : m_core.m_hooks) {
        hook->onDeclare(es);
    }

    // -- Классические тип-уровневые методы (осознанное решение: работа ТОЛЬКО через тип).
    // count() -> Int64; fromName(name: StrChar) -> Enum; fromValue(value: Value) -> Enum.
    const TypeId int64Id = reg.getType(type::Int64);
    const TypeId strCharId = reg.getType(type::StrChar);
    auto ftype = [&](TypeId ret, std::vector<TypeId> args) { return reg.getOrCreateFunctionType(ret, std::move(args)); };
    reg.addMethod(enum_id, "count", ftype(int64Id, {}));
    reg.addMethod(enum_id, "fromName", ftype(enum_id, {strCharId}));
    reg.addMethod(enum_id, "fromValue", ftype(enum_id, {valueType}));
}

// -- Объявление Variant-типа (`Value ::= :Variant(RED:Int64=0, GREEN='g',)`) --
// TypeDecl(Binary): left = имя типа, right = DictLiteral с аннотацией m_type «Variant»; элементы
// m_body - Binary(AssignOp) (left=имя или пусто для бесзначённого, right=значение). Тип каждого
// члена - СВОЙ (гетерогенный): выводится из значения (resolvedType), ординальный член без значения
// → минимальный знаковый Int по позиции. Регистрирует Variant-тип, биндит имя, методы (count).
void DeclAnalyzer::analyzeVariantDecl(Binary& binary_node) {
    const std::string variant_name = std::string(binary_node.m_left->text());
    TypeRegistry& reg = m_actx.ctx().types();
    const MapperRange decl_range = binary_node.range();

    auto* right = binary_node.m_right.get();
    EXPECT(right && right->kind() == ParserToken::Kind::DictLiteral && "analyzeVariantDecl: RHS must be Variant-annotated DictLiteral");
    auto& dict = static_cast<DictLiteralNode&>(*right);

    std::vector<VariantMemberData> members;
    // Единый сбор из m_body (ArgNode): тип члена - из ЯВНОЙ аннотации (m.type), иначе из
    // значения (m.value), иначе минимальный знаковый Int по позиции.
    size_t ordinal = 0;
    for (const auto& el : dict.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        const EnumVariantMember m = enumVariantMember(static_cast<const ArgNode&>(*el));
        TypeId mtype = INVALID_TYPE_ID;
        if (m.type) {
            auto tid = m_actx.resolveType(*m.type);
            if (tid.has_value()) {
                mtype = reg.getCanonicalTypeId(*tid);
            } else {
                // Явная аннотация типа члена не резолвится - ОШИБКА (симметрично enum), а не
                // тихий fallback на тип из значения/ординал: ниже член всё же получает тип,
                // но ошибка уже зафиксирована.
                m_actx.ctx().diag().report(Severity::Error, m.type->range(), "variant '{}': unknown member type", variant_name);
            }
        }
        if (mtype == INVALID_TYPE_ID && m.value) {
            mtype = m_actx.resolvedType(*m.value); // тип из значения
        }
        if (mtype == INVALID_TYPE_ID) {
            mtype = intTypeForLiteral(reg, ordinal);
        }
        members.push_back(VariantMemberData{m.name, mtype});
        ++ordinal;
    }
    if (members.empty()) {
        m_actx.ctx().diag().report(Severity::Error, decl_range, "variant '{}' must have at least one member", variant_name);
        return;
    }

    const TypeId variant_id = reg.registerVariantType(variant_name, std::move(members), decl_range, !binary_node.m_trust.empty());

    if (variant_id == INVALID_TYPE_ID) {
        return;
    }

    // Биндинг имени Variant-типа в скоупе.
    Symbol es;
    es.name = variant_name;
    es.type = variant_id;
    es.decl = &binary_node;
    if (!m_actx.symbols().declare(es)) {
        m_actx.ctx().diag().report(Severity::Error, decl_range, "duplicate declaration '{}'", variant_name);
        return;
    }
    for (auto& hook : m_core.m_hooks) {
        hook->onDeclare(es);
    }

    // Классический метод count() -> Int64 (работа с variant идёт через имя типа).
    reg.addMethod(variant_id, "count", reg.getOrCreateFunctionType(reg.getType(type::Int64), {}));
}

void DeclAnalyzer::analyzeFuncDecl(FuncDecl& func_node) {
    std::string func_name{func_node.text()};
    MapperRange func_range = func_node.range();

    // Нативная функция (%...) транслируется в C++ напрямую → в forward-объявлении
    // тип возврата обязателен (без типа вернули бы голую декларацию без типа).
    if (!func_node.m_body.has_value() && !func_name.empty() && func_name[0] == '%' && !func_node.m_type) {
        m_actx.ctx().diag().report(Severity::Error, func_range, "native function '{}' must have a return type in a forward declaration", func_name);
        return;
    }

    // Регистрация имени функции с функциональным типом сигнатуры (return + параметры).
    Symbol sym;
    sym.name = func_name;
    sym.type = m_actx.buildFuncType(func_node);
    sym.decl = &func_node;

    // Регистрация в текущем скоупе (дубликат - ошибка). Forward-объявление (без тела) может
    // быть завершено последующим определением того же имени (declareOrComplete → Completed).
    if (m_actx.symbols().declareOrComplete(sym) == DeclResult::Duplicate) {
        m_actx.ctx().diag().report(Severity::Error, func_range, "duplicate declaration '{}'", func_name);
        return;
    }
    for (auto& hook : m_core.m_hooks) {
        hook->onDeclare(sym);
    }
}

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
        Symbol ps;
        ps.name = std::string(pd.text());
        TypeId ptype = (pd.m_type) ? m_actx.resolveType(*pd.m_type).value_or(INVALID_TYPE_ID) : INVALID_TYPE_ID;
        // Константность и вид ссылки параметра - из атрибутов узла ТИПА параметра
        // (`fmt: @[reftype(ptr)]@ StrChar^`): reftype → RefType, ReadOnly → const.
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
        const TypeId src = node.m_source ? m_actx.resolvedType(*node.m_source) : INVALID_TYPE_ID;
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
    const TypeId srcType = node.m_source ? m_actx.resolvedType(*node.m_source) : INVALID_TYPE_ID;
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
        if (et != INVALID_TYPE_ID && typeIsInferred(et)) {
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
        if (h.text() == "_" && !isRest) {
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
            if (h.text() == "_") {
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
        auto sev = m_actx.ctx().opts().get(semantic::DiagId::NoSigil);
        if (sev.has_value()) {
            auto* entry = m_actx.ctx().diag().report(*sev, range, semantic::DiagId::NoSigil, "creating a local variable '${}'", name);
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
    auto resolved = m_actx.resolveType(*node.m_targetTypeNodes[i]);
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
    if (typeIsConst(s->type)) {
        m_actx.ctx().diag().report(Severity::Error, range, "cannot assign to constant destructuring target '{}'", name);
        return;
    }
}
} // namespace trust
