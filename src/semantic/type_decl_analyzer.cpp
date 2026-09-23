// Generated: src/semantic/decl_analyzer.cpp
#include "semantic/decl_analyzer.hpp"
#include "semantic/name_resolution.hpp"
#include "semantic/type_set.hpp"
#include "semantic/analysis_common.hpp"
#include "semantic/format_check.hpp"
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
#include "utils/trace.hpp"
#include <algorithm>
#include <format>
#include <string>

namespace trust {
void DeclAnalyzer::analyzeTypeDecl(Binary& binary_node) {
    auto* left = binary_node.m_left.get();
    // Имя типа слева от `::=`: `Name` (Ident) или `:Name` (TypeName, напр. в `:Point ::= :Struct{...}`).
    if (!left || (left->kind() != ParserToken::Kind::Ident && left->kind() != ParserToken::Kind::TypeName)) {
        m_actx.ctx().diag().report(Severity::Error, binary_node.range(), "type declaration must have a name on the left");
        return;
    }

    std::string type_name = std::string(left->text());

    auto* right = binary_node.m_right.get();
    if (!right) {
        m_actx.ctx().diag().report(Severity::Error, binary_node.range(), "type '{}' must have a definition", type_name);
        return;
    }

    // Forward-объявление (нативного) класса `Pair ::= %std::pair<T1,T2>{...};`:
    // RHS - ClassDecl (trust-имя слева, нативное C++-имя из RHS, члены-интерфейс).
    if (right->kind() == ParserToken::Kind::ClassDecl) {
        analyzeClassDecl(static_cast<ClassDecl&>(*right));
        return;
    }

    // Объявление пользовательского Record-типа (Struct/Class) `:Name ::= :Base{, :Base}{ ... };`:
    // RHS - RecordDecl. Struct vs Class определяется базой (семантика → Group TypeKind).
    if (right->kind() == ParserToken::Kind::StructDecl) {
        analyzeRecordDecl(binary_node);
        return;
    }

    // Enum/Variant-объявление (ПОСТФИКС `(...):Enum`/`(...):Variant`, НЕ префикс `:Enum(...)`):
    // правая часть - DictLiteral с аннотацией «Enum»/«Variant». Голые члены = безнарные (валидны).
    if (right->kind() == ParserToken::Kind::DictLiteral) {
        const auto& dl = static_cast<const DictLiteralNode&>(*right);
        if (!dl.prefix && dl.m_type && dl.m_type->text() == type_category::Enum) {
            analyzeEnumDecl(binary_node);
            return;
        }
        if (!dl.prefix && dl.m_type && dl.m_type->text() == type_category::Variant) {
            analyzeVariantDecl(binary_node);
            return;
        }
    }

    // Определяем базовый TypeId правой части (имя типа: алиас или встроенный).
    TypeId base_id = INVALID_TYPE_ID;
    if (right->kind() == ParserToken::Kind::TypeName) {
        // y ::= Int; - alias на существующий тип.
        base_id = m_actx.resolveTypeRef(*right).value_or(INVALID_TYPE_ID);
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
    TRUST_DEBUG("declare", "type '{}' depth={}", type_name, m_actx.symbols().depth());
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
            auto tid = m_actx.resolveTypeRef(*ta);
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
        // Тип из явных значений (exprType + join); если явных нет - минимальный Int по числу членов.
        std::vector<TypeId> explicitTypes;
        for (const auto& el : body) {
            if (!isMember(el)) {
                continue;
            }
            const AstNodePtr v = enumVariantMember(static_cast<const ArgNode&>(*el)).value;
            if (v) {
                explicitTypes.push_back(m_actx.exprType(*v));
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
    TRUST_DEBUG("declare", "type '{}' depth={}", enum_name, m_actx.symbols().depth());
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
// члена - СВОЙ (гетерогенный): выводится из значения (exprType), ординальный член без значения
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
            auto tid = m_actx.resolveTypeRef(*m.type);
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
            mtype = m_actx.exprType(*m.value); // тип из значения
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
    TRUST_DEBUG("declare", "type '{}' depth={}", variant_name, m_actx.symbols().depth());
    for (auto& hook : m_core.m_hooks) {
        hook->onDeclare(es);
    }

    // Классический метод count() -> Int64 (работа с variant идёт через имя типа).
    reg.addMethod(variant_id, "count", reg.getOrCreateFunctionType(reg.getType(type::Int64), {}));
}

} // namespace trust
