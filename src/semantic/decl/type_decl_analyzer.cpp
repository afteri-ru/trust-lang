#include "semantic/decl/decl_analyzer.hpp"
#include "semantic/core/ast_view.hpp"
#include "semantic/decl/exported_signature_check.hpp"
#include "semantic/core/name_resolution.hpp"
#include "semantic/typer/type_set.hpp"
#include "semantic/verify/format_check.hpp"
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
#include "utils/utils.hpp"
#include <algorithm>
#include <format>
#include <string>

namespace trust {

// -- RHS-имя типа: общая точка `::=` (новый тип) и `=` (второе имя существующего) --------
// TypeName резолвится через resolveTypeRef (материализует факт в узле, включая шаблонные
// инстанциации); Ident - имя типа из таблицы символов (ссылка на переменную справа - ошибка).
TypeId DeclAnalyzer::resolveAliasTargetType(const AstNodeBase& right, std::string_view opSpelling) {
    if (right.kind() == ParserToken::Kind::TypeName) {
        const TypeId id = m_actx.types().resolveTypeRef(right).value_or(INVALID_TYPE_ID);
        if (id == INVALID_TYPE_ID) {
            m_actx.ctx().diag().report(Severity::Error, right.range(), "type '{}' not found", requiredText(right));
        }
        return id;
    }
    if (right.kind() == ParserToken::Kind::Ident) {
        // Модульно-квалифицированная ссылка (`\lib::T`) - единый путь резолва типа
        // (resolveTypeRef ограничивает поиск экспорт-набором указанного модуля).
        if (const auto* ident = right.as<IdentName>(); ident != nullptr && ident->moduleRef.has_value()) {
            const TypeId id = m_actx.types().resolveTypeRef(right).value_or(INVALID_TYPE_ID);
            if (id == INVALID_TYPE_ID) {
                m_actx.ctx().diag().report(Severity::Error, right.range(), "type '{}' not found", requiredText(right));
            }
            return id;
        }
        // Голое имя типа без сигила (`:B ::- Int32;`): реестр типов + видимость объявления.
        if (const auto named = m_actx.types().lookupTypeName(requiredText(right)); named.has_value()) {
            return *named;
        }
        const Symbol* vs = m_actx.symbols().resolve(requiredText(right));
        if (!vs) {
            m_actx.ctx().diag().report(Severity::Error, right.range(), "undefined name '{}'", requiredText(right));
            return INVALID_TYPE_ID;
        }
        if (vs->decl->kind() != ParserToken::Kind::TypeDecl) {
            m_actx.ctx().diag().report(Severity::Error, right.range(), "'{}' right side must be a type, '{}' is not a type", opSpelling, requiredText(right));
            return INVALID_TYPE_ID;
        }
        const TypeId id = vs->type.getTypeId();
        if (id == INVALID_TYPE_ID) {
            m_actx.ctx().diag().report(Severity::Error, right.range(), "type of '{}' is not resolved", requiredText(right));
        }
        return id;
    }
    return INVALID_TYPE_ID;
}

void DeclAnalyzer::analyzeTypeDecl(Binary& binary_node) {
    // `=` (TYPE_DEFINE) - две формы: реализация/доопределение УЖЕ объявленного типа и ВТОРОЕ имя
    // существующего типа. `::-` (ALIAS_TYPE) - только второе имя. `::=` создаёт новый тип.
    if (binary_node.isTypeDefine() || binary_node.isTypeAlias()) {
        analyzeTypeDefine(binary_node);
        return;
    }

    auto* left = binary_node.m_left.get();
    // Имя типа слева от `::=`: `Name` (Ident) или `:Name` (TypeName, напр. в `:Point ::= :Struct{...}`).
    if (!left || (left->kind() != ParserToken::Kind::Ident && left->kind() != ParserToken::Kind::TypeName)) {
        m_actx.ctx().diag().report(Severity::Error, binary_node.range(), "type declaration must have a name on the left");
        return;
    }

    std::string type_name = std::string(requiredText(*left));
    // Материализация «имя объявления обозначает тип»: кодоген читает `resolvedTypeId`, не резолвит
    // имя в реестре. Регистрация могла не состояться (диагностика уже выдана) - тогда no-op.
    const auto recordDeclaredName = [&](TypeId id) {
        if (id != INVALID_TYPE_ID) {
            m_actx.types().recordTypeName(*left, id);
        }
    };
    const auto recordRegisteredName = [&]() {
        if (auto id = m_actx.types().lookupTypeName(type_name)) {
            recordDeclaredName(*id);
        }
    };
    if (op::isSystemName(utils::stripTypeSigil(type_name))) {
        m_actx.ctx().diag().report(Severity::Error, left->range(), "system name '{}' is reserved for compiler-generated entities", type_name);
        return;
    }

    // Встроенное имя предобъявлено: `::=` создаёт НОВЫЙ тип и потому запрещён.
    // Реализация определяется оператором `=` (analyzeTypeDefine).
    if (m_actx.ctx().readTypes().names().isBuiltinTypeName(type_name)) {
        m_actx.ctx().diag().report(Severity::Error, left->range(), "built-in type '{}' is pre-declared; use '=' to define its implementation", type_name);
        return;
    }

    auto* right = binary_node.m_right.get();
    if (!right) {
        m_actx.ctx().diag().report(Severity::Error, binary_node.range(), "type '{}' must have a definition", type_name);
        return;
    }

    // Forward-объявление (нативного) класса `Pair ::= %std::pair<T1,T2>{...};`:
    // RHS - ClassDecl (trust-имя слева, нативное C++-имя из RHS, члены-интерфейс).
    if (right->kind() == ParserToken::Kind::ClassDecl) {
        analyzeClassDecl(static_cast<ClassDecl&>(*right));
        recordRegisteredName();
        return;
    }

    // Объявление пользовательского Record-типа (Struct/Class) `:Name ::= :Base{, :Base}{ ... };`:
    // RHS - RecordDecl. Struct vs Class определяется базой (семантика → Group TypeKind).
    if (right->kind() == ParserToken::Kind::StructDecl) {
        analyzeRecordDecl(binary_node);
        recordRegisteredName();
        return;
    }

    // Enum/Variant-объявление (ПОСТФИКС `(...):Enum`/`(...):Variant`, НЕ префикс `:Enum(...)`):
    // правая часть - DictLiteral с аннотацией «Enum»/«Variant». Голые члены = безнарные (валидны).
    if (right->kind() == ParserToken::Kind::DictLiteral) {
        const auto& dl = static_cast<const DictLiteralNode&>(*right);
        if (!dl.prefix && dl.m_type && requiredText(*dl.m_type) == type_category::Enum) {
            analyzeEnumDecl(binary_node);
            recordRegisteredName();
            return;
        }
        if (!dl.prefix && dl.m_type && requiredText(*dl.m_type) == type_category::Variant) {
            analyzeVariantDecl(binary_node);
            recordRegisteredName();
            return;
        }
    }

    // Определяем базовый TypeId правой части (имя типа: алиас или встроенный).
    TypeId base_id = INVALID_TYPE_ID;
    if (right->kind() == ParserToken::Kind::TypeName || right->kind() == ParserToken::Kind::Ident) {
        // y ::= Int; / y ::= :Int32; - alias на существующий тип (новый ТИП).
        base_id = resolveAliasTargetType(*right, "::=");
        if (base_id == INVALID_TYPE_ID) {
            return;
        }
    } else if (right->kind() == ParserToken::Kind::TypeSet) {
        // Набор допустимых типов `Name ::= :A + :B;`: проверка комбинации (ветки/подтипы/составные
        // типы) - здесь, в анализаторе. Регистрация и разворот в N определений - отложены.
        if (!semantic::validateTypeSet(static_cast<const Sequence&>(*right), m_actx)) {
            return; // ошибки комбинации уже выданы
        }
        m_actx.ctx().diag().report(Severity::Error, right->range(), "type set definitions are not implemented yet");
        return;
    } else {
        m_actx.ctx().diag().report(Severity::Error, right->range(), "unsupported type alias definition");
        return;
    }

    // Регистрация алиаса в реестре типов (метаданные TypeId). Тип-алиас с trust-условиями
    // (непустой m_trust после имени) помечается битом trust в TypeKind - семантический
    // дифференциатор идентичности (и защита от авто-вывода типа, см. typeExpr).
    const auto alias_keys = m_actx.types().typeStorageKeys(binary_node, type_name);
    TypeId alias_id =
        m_actx.ctx().types().registrar().registerType(type_name, base_id, {}, right->range(), {}, !binary_node.m_trust.empty(), alias_keys.front());
    if (alias_id == INVALID_TYPE_ID) {
        return; // дубликат - диагностику сформировал реестр
    }
    if (semantic::isExportedTopLevel(m_actx)) {
        semantic::checkExportedTypeVisibility(m_actx, base_id, right->range(), "type alias '" + type_name + "'");
    }
    m_actx.types().aliasTypeKeys(alias_id, alias_keys);
    recordDeclaredName(alias_id);
    if (right->kind() == ParserToken::Kind::Ident) {
        m_actx.types().recordTypeName(*right, base_id); // база алиаса обозначает тип
    }

    // Биндинг имени алиаса в текущем скоупе (shadowing/коллизии через скоуп-стек).
    Symbol as;
    as.name = stableSymbolName(type_name);
    as.type = QualType::fromTypeId(alias_id);
    as.decl = &binary_node;
    const DeclResult decl_result = m_actx.symbols().declare(as);
    if (decl_result == DeclResult::Duplicate) {
        m_actx.ctx().diag().report(Severity::Error, left->range(), "duplicate declaration '{}'", type_name);
        return;
    }
    if (decl_result == DeclResult::AlreadyRegistered) {
        return; // идемпотентная повторная регистрация того же объявления (ромбовидный импорт)
    }
    TRUST_DEBUG("declare", "type '{}' depth={}", type_name, m_actx.symbols().depth());
    for (auto& hook : m_core.hooks()) {
        hook->onDeclare(as);
    }

    // Trust-условия типа: резолв имён + обработка по -Wsolver/--solver-mode (verify/MEMORY.md).
    m_core.trust().processTrustConditions(binary_node.m_trust, binary_node);
}

// -- Формы `=`: реализация типа и ВТОРОЕ имя существующего типа -----------------
void DeclAnalyzer::analyzeTypeDefine(Binary& binary_node) {
    auto* left = binary_node.m_left.get();
    if (!left || (left->kind() != ParserToken::Kind::Ident && left->kind() != ParserToken::Kind::TypeName)) {
        m_actx.ctx().diag().report(Severity::Error, binary_node.range(), "type definition must have a name on the left");
        return;
    }
    const std::string type_name{requiredText(*left)};

    auto* right = binary_node.m_right.get();
    if (!right) {
        m_actx.ctx().diag().report(Severity::Error, binary_node.range(), "type '{}' must have a definition", type_name);
        return;
    }

    // `:A ::- T;` / `:A ::- :T;` - ВТОРОЕ ИМЯ существующего типа (семантический алиас): новый TypeId
    // НЕ создаётся, `A` вводится как имя цели. Признак формы - RHS-имя типа; `::=` по-прежнему
    // создаёт НОВЫЙ номинальный тип. Поэтому LHS здесь обязан быть СВОБОДЕН (в отличие от
    // реализации/доопределения ниже, где LHS уже объявлен).
    if (right->kind() == ParserToken::Kind::TypeName || right->kind() == ParserToken::Kind::Ident) {
        const TypeId target = resolveAliasTargetType(*right, binary_node.isTypeAlias() ? "::-" : "=");
        if (target == INVALID_TYPE_ID) {
            return;
        }
        // Trust-условия - свойство РЕАЛИЗАЦИИ типа; у второго имени своей реализации нет.
        if (!binary_node.m_trust.empty()) {
            m_actx.ctx().diag().report(Severity::Error, left->range(),
                                       "trust conditions cannot be attached to a type alias '{}'; declare a new type with '::='", type_name);
            return;
        }
        const auto alias_keys = m_actx.types().typeStorageKeys(binary_node, type_name);
        if (m_actx.ctx().types().names().aliasTypeName(target, alias_keys.front(), type_name, left->range()) == INVALID_TYPE_ID) {
            return; // имя занято - диагностику сформировал reserveTypeName
        }
        if (semantic::isExportedTopLevel(m_actx)) {
            semantic::checkExportedTypeVisibility(m_actx, target, right->range(), "type alias '" + type_name + "'");
        }
        m_actx.types().aliasTypeKeys(target, alias_keys);
        m_actx.types().recordTypeName(*left, target);
        if (right->kind() == ParserToken::Kind::Ident) {
            m_actx.types().recordTypeName(*right, target); // база алиаса обозначает тип
        }

        Symbol alias_sym;
        alias_sym.name = stableSymbolName(type_name);
        alias_sym.type = QualType::fromTypeId(target);
        alias_sym.decl = &binary_node;
        const DeclResult alias_result = m_actx.symbols().declare(alias_sym);
        if (alias_result == DeclResult::Duplicate) {
            m_actx.ctx().diag().report(Severity::Error, left->range(), "duplicate declaration '{}'", type_name);
            return;
        }
        if (alias_result == DeclResult::AlreadyRegistered) {
            return; // идемпотентная повторная регистрация того же объявления
        }
        TRUST_DEBUG("declare", "type alias '{}' depth={}", type_name, m_actx.symbols().depth());
        for (auto& hook : m_core.hooks()) {
            hook->onDeclare(alias_sym);
        }
        return;
    }

    // Иначе `=` - реализация/доопределение УЖЕ объявленного типа.
    const auto type_id = m_actx.types().lookupTypeName(type_name);
    if (!type_id.has_value()) {
        m_actx.ctx().diag().report(Severity::Error, left->range(), "type '{}' is not declared; use '::=' to create a new type", type_name);
        return;
    }
    // Имя в `Name = ...` обозначает уже объявленный тип (материализация для кодогена).
    m_actx.types().recordTypeName(*left, *type_id);

    // Пользовательский forward-record: `:Name ::= :Base ...;` ... `Name = :Base { ... };`.
    if (right->kind() == ParserToken::Kind::StructDecl) {
        if (m_actx.ctx().readTypes().store().isBuiltinType(*type_id)) {
            m_actx.ctx().diag().report(Severity::Error, right->range(), "built-in type '{}' cannot be defined as a user record", type_name);
            return;
        }
        analyzeRecordDecl(binary_node);
        return;
    }

    if (right->kind() != ParserToken::Kind::ClassDecl) {
        m_actx.ctx().diag().report(Severity::Error, right->range(), "unsupported definition of type '{}': expected native class/template or record", type_name);
        return;
    }

    auto& ncd = static_cast<ClassDecl&>(*right);
    if (!m_actx.ctx().readTypes().store().isBuiltinType(*type_id)) {
        m_actx.ctx().diag().report(Severity::Error, left->range(), "'=' defines implementations of pre-declared built-in types; '{}' is not built-in",
                                   type_name);
        return;
    }
    if (!ncd.m_body.empty()) {
        m_actx.ctx().diag().report(Severity::Error, ncd.range(), "implementation members of built-in type '{}' are not supported yet", type_name);
        return;
    }
    if (ncd.m_nativeName.empty()) {
        m_actx.ctx().diag().report(Severity::Error, ncd.range(), "implementation of built-in type '{}' must have a native C++ name", type_name);
        return;
    }

    std::vector<std::string> includes;
    std::vector<std::string> libs;
    {
        const AttrPool& attrs = m_actx.ctx().attrs();
        if (const std::string* header = firstAttrArg(attrs, ncd, attr::Include)) {
            includes.push_back(includeDirective(*header));
        }
        if (const std::string* lib = firstAttrArg(attrs, ncd, attr::Link)) {
            libs.push_back(*lib);
        }
    }

    if (m_actx.ctx().types().builtins().define(m_actx.ctx().readTypes(), type_name, ncd.m_nativeName, std::move(includes), std::move(libs)) ==
        INVALID_TYPE_ID) {
        m_actx.ctx().diag().report(Severity::Error, left->range(), "failed to define implementation of built-in type '{}'", type_name);
    }
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
    const std::string enum_name = std::string(requiredText(*binary_node.m_left));
    TypeSystem& reg = m_actx.ctx().types();
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
            auto tid = m_actx.types().resolveTypeRef(*ta);
            if (!tid.has_value()) {
                m_actx.ctx().diag().report(Severity::Error, ta->range(), "enum '{}': unknown member type", enum_name);
                continue;
            }
            const TypeId c = reg.read().store().desugaredTypeId(*tid);
            if (valueType == INVALID_TYPE_ID) {
                valueType = c;
            } else if (valueType != c) {
                m_actx.ctx().diag().report(Severity::Error, ta->range(), "enum '{}' member types differ ('{}' vs '{}')", enum_name,
                                           reg.read().printer().getTypeName(valueType), reg.read().printer().getTypeName(c));
                return;
            }
        }
        if (valueType == INVALID_TYPE_ID) {
            valueType = reg.read().getType(type::Int64);
        }
    } else {
        // Тип из явных значений (exprType + join); если явных нет - минимальный Int по числу членов.
        std::vector<TypeId> explicitTypes;
        for (const auto& el : body) {
            if (!isMember(el)) {
                continue;
            }
            const AstNodePtr v = enumVariantMember(static_cast<const ArgNode&>(*el)).value;
            if (v) {
                explicitTypes.push_back(m_actx.facts().exprType(*v));
            }
        }
        if (explicitTypes.empty()) {
            valueType = intTypeForLiteral(reg, memberCount - 1);
        } else {
            TypeId common = INVALID_TYPE_ID;
            bool allSame = true;
            for (const TypeId vt : explicitTypes) {
                const TypeId c = (vt != INVALID_TYPE_ID) ? reg.read().store().desugaredTypeId(vt) : INVALID_TYPE_ID;
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
                    nat.push_back(m_core.typer().naturalRuntimeType(vt));
                }
                valueType = m_core.typer().joinElementTypes(nat);
                if (valueType == INVALID_TYPE_ID) {
                    valueType = reg.read().getType(type_generic::Any);
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
    const bool integerVT = getGroup(getTypeKind(reg.read().store().desugaredTypeId(valueType))) == Group::kIntegers;
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
            // Значение-член может быть составным (литерал/конструкция) - текста у него нет;
            // строковое значение нужно только для скалярной формы (Literal).
            if (const HasText* vt = v->as_text()) {
                vstr = std::string(vt->text());
                if (integerVT) {
                    unsigned long long parsed = 0;
                    if (parseDecimalUInt(vt->text(), parsed)) {
                        cur = parsed;
                        haveValue = true;
                    }
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
    const auto enum_keys = m_actx.types().typeStorageKeys(binary_node, enum_name);
    const TypeId enum_id = reg.registrar().registerEnumType(enum_name, valueType, std::move(md), decl_range, !binary_node.m_trust.empty(), enum_keys.front());

    if (enum_id == INVALID_TYPE_ID) {
        return;
    }
    if (semantic::isExportedTopLevel(m_actx)) {
        semantic::checkExportedTypeVisibility(m_actx, valueType, decl_range, "enum type '" + enum_name + "'");
    }
    m_actx.types().aliasTypeKeys(enum_id, enum_keys);

    // -- Биндинг имени enum-типа в текущем скоупе (shadowing через скоуп-стек).
    Symbol es;
    es.name = stableSymbolName(enum_name);
    es.type = QualType::fromTypeId(enum_id);
    es.decl = &binary_node;
    const DeclResult enum_decl = m_actx.symbols().declare(es);
    if (enum_decl == DeclResult::Duplicate) {
        m_actx.ctx().diag().report(Severity::Error, decl_range, "duplicate declaration '{}'", enum_name);
        return;
    }
    if (enum_decl == DeclResult::AlreadyRegistered) {
        return; // идемпотентная повторная регистрация того же объявления (ромбовидный импорт)
    }
    TRUST_DEBUG("declare", "type '{}' depth={}", enum_name, m_actx.symbols().depth());
    for (auto& hook : m_core.hooks()) {
        hook->onDeclare(es);
    }

    // -- Классические тип-уровневые методы (осознанное решение: работа ТОЛЬКО через тип).
    // count() -> Int64; fromName(name: StrChar) -> Enum; fromValue(value: Value) -> Enum.
    const TypeId int64Id = reg.read().getType(type::Int64);
    const TypeId strCharId = reg.read().getType(type::StrChar);
    auto ftype = [&](TypeId ret, std::vector<TypeId> args) { return reg.interner().getOrCreateFunctionType(ret, std::move(args)); };
    reg.methods().addMethod(enum_id, "count", ftype(int64Id, {}));
    reg.methods().addMethod(enum_id, "fromName", ftype(enum_id, {strCharId}));
    reg.methods().addMethod(enum_id, "fromValue", ftype(enum_id, {valueType}));
}

// -- Объявление Variant-типа (`Value ::= :Variant(RED:Int64=0, GREEN='g',)`) --
// TypeDecl(Binary): left = имя типа, right = DictLiteral с аннотацией m_type «Variant»; элементы
// m_body - Binary(AssignOp) (left=имя или пусто для бесзначённого, right=значение). Тип каждого
// члена - СВОЙ (гетерогенный): выводится из значения (exprType), ординальный член без значения
// → минимальный знаковый Int по позиции. Регистрирует Variant-тип, биндит имя, методы (count).
void DeclAnalyzer::analyzeVariantDecl(Binary& binary_node) {
    const std::string variant_name = std::string(requiredText(*binary_node.m_left));
    TypeSystem& reg = m_actx.ctx().types();
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
            auto tid = m_actx.types().resolveTypeRef(*m.type);
            if (tid.has_value()) {
                mtype = reg.read().store().desugaredTypeId(*tid);
            } else {
                // Явная аннотация типа члена не резолвится - ОШИБКА (симметрично enum), а не
                // тихий fallback на тип из значения/ординал: ниже член всё же получает тип,
                // но ошибка уже зафиксирована.
                m_actx.ctx().diag().report(Severity::Error, m.type->range(), "variant '{}': unknown member type", variant_name);
            }
        }
        if (mtype == INVALID_TYPE_ID && m.value) {
            mtype = m_actx.facts().exprType(*m.value); // тип из значения
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

    const auto variant_keys = m_actx.types().typeStorageKeys(binary_node, variant_name);
    const TypeId variant_id =
        reg.registrar().registerVariantType(variant_name, std::move(members), decl_range, !binary_node.m_trust.empty(), variant_keys.front());

    if (variant_id == INVALID_TYPE_ID) {
        return;
    }
    if (semantic::isExportedTopLevel(m_actx)) {
        semantic::checkExportedTypeVisibility(m_actx, variant_id, decl_range, "variant type '" + variant_name + "'");
    }
    m_actx.types().aliasTypeKeys(variant_id, variant_keys);

    // Биндинг имени Variant-типа в скоупе.
    Symbol es;
    es.name = stableSymbolName(variant_name);
    es.type = QualType::fromTypeId(variant_id);
    es.decl = &binary_node;
    const DeclResult variant_decl = m_actx.symbols().declare(es);
    if (variant_decl == DeclResult::Duplicate) {
        m_actx.ctx().diag().report(Severity::Error, decl_range, "duplicate declaration '{}'", variant_name);
        return;
    }
    if (variant_decl == DeclResult::AlreadyRegistered) {
        return; // идемпотентная повторная регистрация того же объявления (ромбовидный импорт)
    }
    TRUST_DEBUG("declare", "type '{}' depth={}", variant_name, m_actx.symbols().depth());
    for (auto& hook : m_core.hooks()) {
        hook->onDeclare(es);
    }

    // Классический метод count() -> Int64 (работа с variant идёт через имя типа).
    reg.methods().addMethod(variant_id, "count", reg.interner().getOrCreateFunctionType(reg.read().getType(type::Int64), {}));
}

} // namespace trust
