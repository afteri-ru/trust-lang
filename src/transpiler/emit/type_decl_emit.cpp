#include "transpiler/emit/decl_emit.hpp"
#include "transpiler/emit/decl_emit_common.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_dispatch.hpp"
#include "ast/token_type.hpp"
#include "session/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "types/types.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace trust {

void DeclEmitter::generateTypeDeclToFile(const Binary& binary_node, MapperFile output_idx) {
    auto* left = binary_node.m_left.get();
    // Имя типа слева от `::=`: `Name` (Ident) или `:Name` (TypeName, напр. в `:Point ::= :Struct{...}`).
    if (!left || (left->kind() != ParserToken::Kind::Ident && left->kind() != ParserToken::Kind::TypeName)) {
        m_ectx.m_ctx.report(binary_node.range(), diag::DiagId::ParseError, "type declaration must have a name on the left");
        return;
    }

    // Имя типа-алиаса: манглинг trust-имени в C++-идентификатор (MyInt → c_MyInt).
    std::string type_name = utils::cppDeclName(requiredText(*left));

    auto* right = binary_node.m_right.get();
    // Enum/Variant-объявление: `Color ::= :Enum(...)` / `(...):Enum`, `Value ::= :Variant(...)` /
    // `(...):Variant` - правая часть DictLiteral с аннотацией типа «Enum»/«Variant».
    if (right && right->kind() == ParserToken::Kind::DictLiteral) {
        const auto& dl = static_cast<const DictLiteralNode&>(*right);
        if (dl.m_type && requiredText(*dl.m_type) == type_category::Enum) {
            // Тип объявления материализован семантикой на имени (`IdentName::resolvedTypeId`) —
            // кодоген не резолвит имя типа в реестре.
            const TypeId typeId = left->as<IdentName>()->resolvedTypeId;
            if (typeId == INVALID_TYPE_ID) {
                m_ectx.m_ctx.report(binary_node.range(), diag::DiagId::ParseError, "enum type '{}' is not resolved by semantic analysis", requiredText(*left));
                return;
            }
            // Отображение объявления (trust-range → cpp): MapperScope охватывает struct +
            // out-of-class определения. Маппинг имени типа - внутри emitEnumStruct (оффсет
            // вычисляется из фактического вывода, а не из магической константы).
            std::unique_ptr<MapperScope> scope;
            if (!binary_node.range().begin.isInvalid()) {
                scope = std::make_unique<MapperScope>(m_ectx.m_ctx.source(), binary_node.range(), output_idx);
            }
            emitEnumStruct(requiredText(*left), dl, typeId, output_idx, left->range());
            return;
        }
        if (dl.m_type && requiredText(*dl.m_type) == type_category::Variant) {
            const TypeId typeId = left->as<IdentName>()->resolvedTypeId;
            if (typeId == INVALID_TYPE_ID) {
                m_ectx.m_ctx.report(binary_node.range(), diag::DiagId::ParseError, "variant type '{}' is not resolved by semantic analysis",
                                    requiredText(*left));
                return;
            }
            std::unique_ptr<MapperScope> scope;
            if (!binary_node.range().begin.isInvalid()) {
                scope = std::make_unique<MapperScope>(m_ectx.m_ctx.source(), binary_node.range(), output_idx);
            }
            emitVariantStruct(requiredText(*left), dl, typeId, output_idx, left->range());
            return;
        }
    }
    // Объявление пользовательского Record-типа (Struct/Class): RHS - RecordDecl. Struct vs Class
    // уже закодирован группой зарегистрированного типа (kStructs/kClassDefs) - emitRecordDecl
    // различает по ней (static_assert только для Struct).
    if (right && right->kind() == ParserToken::Kind::StructDecl) {
        const auto& rec = static_cast<const RecordDecl&>(*right);
        const TypeId typeId = left->as<IdentName>()->resolvedTypeId;
        if (typeId == INVALID_TYPE_ID) {
            m_ectx.m_ctx.report(binary_node.range(), diag::DiagId::ParseError, "record type '{}' is not resolved by semantic analysis", requiredText(*left));
            return;
        }
        std::unique_ptr<MapperScope> recScope;
        if (!binary_node.range().begin.isInvalid()) {
            recScope = std::make_unique<MapperScope>(m_ectx.m_ctx.source(), binary_node.range(), output_idx);
        }
        emitRecordDecl(rec, typeId, output_idx, left->range());
        return;
    }
    // Forward-объявление НАТИВНОГО класса `MyStr ::= %std::string { ... };`: C++-struct НЕ
    // эмитится (класс определён в C++-заголовке); конкретное C++-имя (`std::string`) эмитится
    // on-use при использовании типа (resolveCppTypeId), инклуд - on-use из preprocIncludes.
    if (right && right->kind() == ParserToken::Kind::ClassDecl) {
        return;
    }
    // База алиаса: TypeName (:Int32) или Ident (MyInt - существующий алиас/переменная).
    // TypeName рендерится из материализованного `resolvedTypeId`; Ident-ссылка на алиас
    // разрешается в реестре по имени (декларация типа, не аннотация).
    if (!right || (right->kind() != ParserToken::Kind::TypeName && right->kind() != ParserToken::Kind::Ident)) {
        m_ectx.m_ctx.report(binary_node.range(), diag::DiagId::ParseError, "unsupported type alias definition");
        return;
    }

    // `:A ::- T;` - ВТОРОЕ имя существующего типа: нового C++-типа нет (проекция printType
    // всегда даёт имя цели), поэтому `using c_A` не эмитится. Признак синонима - LHS и RHS
    // разрешились в ОДИН TypeId; у `::=` LHS - новый номинальный тип и отличается от цели.
    {
        const TypeId alias_id = static_cast<const IdentName&>(*left).resolvedTypeId;
        const TypeId target_id = static_cast<const IdentName&>(*right).resolvedTypeId;
        if (alias_id != INVALID_TYPE_ID && alias_id == target_id) {
            return;
        }
    }

    // Resolve base type (canonical chain + cpp name + инклуды через emitTypeName).
    // Правая часть всегда тип (семантика '::=' отклоняет переменную справа), поэтому fallback
    // на переменную не нужен: при невозможности вывода базы - явная ошибка.
    std::string base_cpp;
    if (right->kind() == ParserToken::Kind::TypeName) {
        // Полный тип-узел (в т.ч. нативный шаблон `:vector<Int32>` и размерности `[...]`):
        // emitTypeNameForNode рендерит материализованный `resolvedTypeId` (инстанциация/размерности).
        base_cpp = m_driver.m_type.emitTypeNameForNode(right);
    } else if (right->kind() == ParserToken::Kind::Ident) {
        // База-алиас обозначает тип: факт материализован семантикой на имени.
        const TypeId baseId = right->as<IdentName>()->resolvedTypeId;
        if (baseId != INVALID_TYPE_ID) {
            if (auto n = m_driver.m_type.emitTypeName(baseId)) {
                base_cpp = std::move(*n);
            }
        }
    }
    if (base_cpp.empty()) {
        m_ectx.m_ctx.report(right->range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", requiredText(*right));
        return;
    }

    MapperScope scope(m_ectx.m_ctx.source(), binary_node.range(), output_idx);
    const std::string using_prefix = "using ";
    std::string cpp_line = using_prefix + type_name + " = " + base_cpp + ";";
    m_ectx.m_ctx.source().output_append(output_idx, cpp_line);

    // Add name mapping for the type name (hover links): name starts right after the "using " prefix.
    // trust-имя в маппинге - исходное (requiredText(*left)), cpp-имя - манглированное (type_name).
    mapDeclaredName(output_idx, left->range(), static_cast<uint32_t>(using_prefix.length()), requiredText(*left), type_name);
}

// Эмиссия enum-типа: `struct c_Color : trust::Enum<ValueCpp, N> { using ...; static const члены; };`
// + out-of-class `const c_Color c_Color::c_MEMBER{value, ordinal};`. Generic-логика (значение/
// ординал, конструкторы, операторы сравнения по ординалу, count()) - в рантайм-шаблоне trust::Enum;
// кодогенерация эмитит только данные члена. Члены - static const (out-of-class), т.к.
// static constexpr собственного типа невозможен (неполный тип в точке объявления).
void DeclEmitter::emitEnumStruct(std::string_view enum_trust, const DictLiteralNode& dict, TypeId enum_id, MapperFile output_idx, MapperRange typeNameRange) {
    // Значения членов читаются из EnumTypeData (вычислены семантикой с автоинкрементом);
    // AST-словарь dict нужен только для диапазона диагностики.
    // Объявление эмитится в namespace модуля — префикс не нужен.
    const std::string enum_cpp = utils::cppDeclName(enum_trust);
    const auto* ed = m_ectx.m_ctx.readTypes().store().getTypeDataAs<EnumTypeData>(enum_id);
    if (!ed) {
        m_ectx.m_ctx.report(dict.range(), diag::DiagId::ParseError, "enum '{}' has no member data", enum_trust);
        return;
    }

    auto vn = m_driver.m_type.emitTypeName(ed->valueType);
    if (!vn) {
        m_ectx.m_ctx.report(dict.range(), diag::DiagId::ParseError, "unable to generate C++ type for enum '{}' value type", enum_trust);
        return;
    }
    std::string valueCpp = std::move(*vn);
    const size_t N = ed->members.size();

    // Рантайм-шаблон trust::Enum: заголовок из trust-runtime (механизм «@»-заголовков).
    m_driver.m_type.recordRequiredInclude("@trust/enum.hpp");

    const std::string base = "trust::Enum<" + valueCpp + ", " + std::to_string(N) + ", " + enum_cpp + ">";
    std::string out;
    out += "struct ";
    const auto typeNameOff = static_cast<uint32_t>(out.size());
    out += enum_cpp + " : " + base + " {\n";
    out += "    using " + base + "::Enum;\n";
    // Декларации членов + их name-маппинг (hover): член выводится как `static const c_Level c_NAME;`
    // - имя сразу после "<enum_cpp> ". Источник диапазона члена - ArgNode из dict.m_body (тот же
    // порядок, что и ed->members). Для синтетических/без-range узлов маппинг пропускается.
    {
        size_t midx = 0;
        for (const auto& el : dict.m_body) {
            if (!el || el->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            if (midx >= ed->members.size()) {
                break;
            }
            const std::string cname = utils::cppDeclName(ed->members[midx].name);
            out += "    static const " + enum_cpp + " ";
            const auto nameOff = static_cast<uint32_t>(out.size());
            out += cname + ";\n";
            if (m_ectx.m_ctx.source().mappingActive() && !el->range().begin.isInvalid()) {
                mapDeclaredName(output_idx, el->range(), nameOff, ed->members[midx].name, cname);
            }
            ++midx;
        }
    }
    // Таблица имя↔значение (для fromName/fromValue в trust::Enum); порядок = объявление (ordinal).
    // `static inline const` (C++17 inline-переменная): позволяет in-class инициализацию и для
    // НЕ-литеральных типов значений (Rational), инициализируемых в рантайме; fromName/fromValue
    // (не constexpr) читают её во время выполнения. Для литеральных типов тоже корректно.
    out += "    static inline const trust::EnumMember<" + valueCpp + "> kMembers[" + std::to_string(N) + "] = {";
    // Значения членов: скалярные литералы (ast::is_literal_kind) форматируются по типу
    // (memberValueCpp). Составные литералы (массив/словарь/диапазон) НЕ реализованы - «не
    // реализовано» выдаётся ЗДЕСЬ, по факту невозможности сгенерировать C++-литерал значения.
    std::string defs;
    size_t idx = 0;
    for (const auto& el : dict.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        if (idx >= ed->members.size()) {
            break;
        }
        const auto& a = static_cast<const ArgNode&>(*el);
        const AstNodeBase* valNode = a.m_value.get();
        if (a.text().empty() && valNode && valNode->kind() == ParserToken::Kind::Ident) {
            valNode = nullptr; // безнарный член - имя в значении, значения нет
        }
        if (valNode && !is_literal_kind(valNode->kind())) {
            m_ectx.m_ctx.report(el->range(), diag::DiagId::ParseError, "значение члена enum '{}' (массив/словарь/диапазон) ещё не реализовано", enum_trust);
            return;
        }
        const std::string val_str = memberValueCpp(m_ectx.m_ctx.readTypes(), ed->valueType, ed->members[idx].value);
        const std::string cname = utils::cppDeclName(ed->members[idx].name);
        if (idx) {
            out += ", ";
        }
        out += "{" + val_str + ", \"" + ed->members[idx].name + "\"}";
        defs += "const " + enum_cpp + " " + enum_cpp + "::" + cname + "{" + val_str + ", " + std::to_string(idx) + "};\n";
        ++idx;
    }
    out += "};\n";
    out += "};\n";
    out += defs;
    // Маппинг имени типа: оффсет из фактического вывода (сразу после "struct "), а не магическая
    // константа. Для синтетических узлов без исходного range маппинг пропускается.
    if (m_ectx.m_ctx.source().mappingActive() && !typeNameRange.begin.isInvalid()) {
        mapDeclaredName(output_idx, typeNameRange, typeNameOff, enum_trust, enum_cpp);
    }
    m_ectx.m_ctx.source().output_append(output_idx, out);
}

// Эмиссия Variant-типа (гетерогенный): `struct c_Value { using Variant = std::variant<...>;
// static const <T> c_MEMBER; ... }` + out-of-class определения (значения из DictLiteral RHS).
// Каждый член - константа СВОЕГО типа (из VariantTypeData); `Value.RED` → c_Value::c_RED.
void DeclEmitter::emitVariantStruct(std::string_view variant_trust, const DictLiteralNode& dict, TypeId variant_id, MapperFile output_idx,
                                    MapperRange typeNameRange) {
    const std::string vcpp = utils::cppDeclName(variant_trust);
    const auto* vd = m_ectx.m_ctx.readTypes().store().getTypeDataAs<VariantTypeData>(variant_id);
    if (!vd) {
        m_ectx.m_ctx.report(dict.range(), diag::DiagId::ParseError, "variant '{}' has no member data", variant_trust);
        return;
    }

    m_driver.m_type.recordRequiredInclude("#include <variant>");

    // C++-имена типов членов (emitTypeName записывает их инклуды).
    std::vector<std::string> memberCpp;
    memberCpp.reserve(vd->members.size());
    for (const auto& m : vd->members) {
        auto n = m_driver.m_type.emitTypeName(m.type);
        if (!n) {
            m_ectx.m_ctx.report(dict.range(), diag::DiagId::ParseError, "unable to generate C++ type for variant '{}' member '{}'", variant_trust, m.name);
            return;
        }
        memberCpp.push_back(std::move(*n));
    }

    std::string out;
    out += "struct ";
    const auto typeNameOff = static_cast<uint32_t>(out.size());
    out += vcpp + " {\n";
    out += "    using Variant = std::variant<";
    for (size_t i = 0; i < vd->members.size(); ++i) {
        if (i) {
            out += ", ";
        }
        out += memberCpp[i];
    }
    out += ">;\n";
    // Декларации членов + их name-маппинг (hover): `static const <T> c_NAME;` - имя сразу после
    // "<T> ". Источник диапазона члена - ArgNode из dict.m_body (тот же порядок, что и vd->members).
    {
        size_t midx = 0;
        for (const auto& el : dict.m_body) {
            if (!el || el->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            if (midx >= vd->members.size()) {
                break;
            }
            const std::string cname = utils::cppDeclName(vd->members[midx].name);
            out += "    static const " + memberCpp[midx] + " ";
            const auto nameOff = static_cast<uint32_t>(out.size());
            out += cname + ";\n";
            if (m_ectx.m_ctx.source().mappingActive() && !el->range().begin.isInvalid()) {
                mapDeclaredName(output_idx, el->range(), nameOff, vd->members[midx].name, cname);
            }
            ++midx;
        }
    }
    out += "    static constexpr int count() { return " + std::to_string(vd->members.size()) + "; }\n";
    out += "};\n";
    // out-of-class определения: значения из ArgNode (имя в text(), значение в m_value); без
    // значения → ordinal. Форматирование значения по типу члена - единый memberValueCpp.
    // Значение члена Variant - AST-выражение (источник истины - ArgNode.m_value); реестр хранит
    // только разрешённый тип (VariantMemberData).
    size_t i = 0;
    for (const auto& el : dict.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        if (i >= vd->members.size()) {
            break;
        }
        const auto& a = static_cast<const ArgNode&>(*el);
        std::string mname = std::string(a.text());
        AstNodePtr valNode = a.m_value;
        // Безнарный член `x` (имя="" и значение-Ident) - имя лежит в значении, значение отбрасываем.
        if (mname.empty() && valNode && valNode->kind() == ParserToken::Kind::Ident) {
            mname = std::string(requiredText(*valNode));
            valNode = nullptr;
        }
        // Значение члена Variant: скалярные литералы (ast::is_literal_kind) форматируются по типу.
        // Составные литералы (массив/словарь/диапазон) НЕ реализованы - «не реализовано» выдаётся
        // ЗДЕСЬ, по факту невозможности сгенерировать C++-литерал значения.
        if (valNode && !is_literal_kind(valNode->kind())) {
            m_ectx.m_ctx.report(el->range(), diag::DiagId::ParseError, "значение члена variant '{}' (массив/словарь/диапазон) ещё не реализовано",
                                variant_trust);
            return;
        }
        std::string val_str = std::to_string(i);
        if (valNode) {
            val_str = memberValueCpp(m_ectx.m_ctx.readTypes(), vd->members[i].type, std::string(requiredText(*valNode)));
        }
        out += "const " + memberCpp[i] + " " + vcpp + "::" + utils::cppDeclName(mname) + "{" + val_str + "};\n";
        ++i;
    }
    // Маппинг имени типа: оффсет из фактического вывода (сразу после "struct "), а не магическая
    // константа. Для синтетических узлов без исходного range маппинг пропускается.
    if (m_ectx.m_ctx.source().mappingActive() && !typeNameRange.begin.isInvalid()) {
        mapDeclaredName(output_idx, typeNameRange, typeNameOff, variant_trust, vcpp);
    }
    m_ectx.m_ctx.source().output_append(output_idx, out);
}

// Эмиссия пользовательского Record-типа (Struct/Class): единый `struct c_Name [: public c_Base...] {
// поля; методы };`. Struct (Group::kStructs) дополнительно получает static_assert POD; Class
// (Group::kClassDefs) - нет (виртуальные/наследование допустимы). Все члены публичные.
void DeclEmitter::emitRecordDecl(const RecordDecl& rec, TypeId rec_id, MapperFile output_idx, MapperRange typeNameRange) {
    const std::string rec_cpp = utils::cppDeclName(rec.text());

    // Пользовательский шаблон-класс (`<T> :Box ::= :Class{...}`): заголовок
    // `template <typename T, ...>` и активные имена параметров на время эмиссии членов
    // (аннотации `: T` рендерятся как `T`; см. TypeEmitter::emitTypeNameForNode). Параметры
    // берём из реестра (доступны и для объявленного-но-не-определённого шаблона).
    std::vector<std::string> paramNames;
    std::string tplHeader;
    for (const TypeId p : m_ectx.m_ctx.readTypes().store().recordTemplateParams(rec_id)) {
        const std::string_view pn = m_ectx.m_ctx.readTypes().store().templateParamName(p);
        paramNames.emplace_back(pn);
    }
    if (!paramNames.empty()) {
        tplHeader = "template <";
        for (size_t i = 0; i < paramNames.size(); ++i) {
            if (i) {
                tplHeader += ", ";
            }
            tplHeader += "typename " + paramNames[i];
        }
        tplHeader += ">\n";
    }

    // Предварительное объявление (все члены - forward): C++ - incomplete type `struct c_Name;`
    // (полное определение будет в другом месте). Полное определение - ниже.
    if (!rec.m_body.has_value()) {
        const std::string fwd = tplHeader + "struct " + rec_cpp + ";\n";
        m_ectx.m_ctx.source().output_append(output_idx, fwd);
        if (m_ectx.m_ctx.source().mappingActive() && !typeNameRange.begin.isInvalid()) {
            mapDeclaredName(output_idx, typeNameRange, static_cast<uint32_t>(tplHeader.size() + 7) /* "struct " */, rec.text(), rec_cpp);
        }
        return;
    }

    const auto* rd = m_ectx.m_ctx.readTypes().store().recordData(rec_id);
    if (!rd) {
        m_ectx.m_ctx.report(rec.range(), diag::DiagId::ParseError, "record '{}' has no member data", rec.text());
        return;
    }
    const bool isPod = m_ectx.m_ctx.readTypes().store().isStructType(rec_id);
    const bool recordIsTemplate = !paramNames.empty();

    // C++-имена типов полей (инклуды записывает emitTypeName). Тип поля - QualType:
    // квалификатор `const` объявленного поля уже сохранён семантикой.
    std::vector<std::string> fieldCpp;
    fieldCpp.reserve(rd->fields.size());
    for (const auto& f : rd->fields) {
        auto tn = m_driver.m_type.emitTypeName(f.type);
        if (!tn) {
            m_ectx.m_ctx.report(rec.range(), diag::DiagId::ParseError, "unable to generate C++ type for field '{}' of record '{}'", f.name, rec.text());
            return;
        }
        fieldCpp.push_back(std::move(*tn));
    }
    // Базовые классы: C++-имена (абстрактные маркеры `:Struct`/`:Class` семантика в baseClasses не кладёт).
    std::vector<std::string> baseCpp;
    for (const TypeId b : m_ectx.m_ctx.readTypes().store().baseClasses(rec_id)) {
        auto bn = m_driver.m_type.resolveCppTypeId(b);
        if (!bn) {
            m_ectx.m_ctx.report(rec.range(), diag::DiagId::ParseError, "unable to generate C++ base type for record '{}'", rec.text());
            return;
        }
        baseCpp.push_back(std::move(bn->first));
    }

    // Активные имена типовых параметров - на время эмиссии членов (снимаются в конце).
    m_ectx.m_activeTemplateParams = paramNames;

    std::string out;
    out += tplHeader;
    out += "struct ";
    const auto typeNameOff = static_cast<uint32_t>(out.size());
    out += rec_cpp;
    if (!baseCpp.empty()) {
        out += " : public " + baseCpp.front();
        for (size_t i = 1; i < baseCpp.size(); ++i) {
            out += ", public " + baseCpp[i];
        }
    }
    out += " {\n";
    // Поля: `<T> c_<field>{};` + name-маппинг (источник диапазона - VarDecl-члены rec.m_body в
    // порядке rd->fields). Методы пропускаются (эмитятся ниже).
    {
        size_t fi = 0;
        for (const auto& m : *rec.m_body) {
            if (!m || m->kind() != ParserToken::Kind::VarDecl) {
                continue;
            }
            if (fi >= rd->fields.size()) {
                break;
            }
            const std::string cname = utils::cppDeclName(rd->fields[fi].name);
            out += "    " + fieldCpp[fi] + " ";
            const auto nameOff = static_cast<uint32_t>(out.size());
            // Struct - строго POD: NSDMI (`{...}`) делает тип нетривиальным → поле БЕЗ инициализатора
            // (анализатор запретил default у Struct-полей, допустимо только `:= _`).
            // Class: `:= <literal>` → NSDMI `{<value>}`; `:= _` → без инициализатора.
            const auto& vd = static_cast<const VarDecl&>(*m);
            std::string init;
            if (!isPod && vd.m_initializer && !isNoneMarker(vd.m_initializer.get())) {
                const ParserToken::Kind ik = vd.m_initializer->kind();
                if (ik == ParserToken::Kind::IntLiteral || ik == ParserToken::Kind::FloatLiteral) {
                    init = "{" + std::string(requiredText(*vd.m_initializer)) + "};";
                } else {
                    init = "{};";
                }
            } else {
                init = ";";
            }
            out += cname + init + "\n";
            if (m_ectx.m_ctx.source().mappingActive() && !m->range().begin.isInvalid()) {
                mapDeclaredName(output_idx, m->range(), nameOff, rd->fields[fi].name, cname);
            }
            ++fi;
        }
    }
    m_ectx.m_ctx.source().output_append(output_idx, out);

    // Методы: переиспользуем генерацию функции (метод - член struct). Заголовок модуля отдаёт
    // ТОЛЬКО прототип, КРОМЕ случаев, где тело обязано быть видно в заголовке (шаблон,
    // `@[constexpr]`) - тогда тело печатается здесь же. Остальные тела выносит `.cppt`
    // (`emitPublishedRecordMembers`): файл-приватные (`__::`) сущности доступны только там.
    for (const auto& m : *rec.m_body) {
        if (!m || !m->is<FuncDecl>()) {
            continue;
        }
        const auto& method = static_cast<const FuncDecl&>(*m);
        if (!m_ectx.m_mode.forwardDeclOnly || methodBodyMustBeInHeader(method, recordIsTemplate, m_ectx.m_ctx.attrs())) {
            if (m_ectx.m_mode.forwardDeclOnly && bodyUsesNonPublishedEntity(method, m_ectx.m_declFacts.headerPublishedDecls, m_ectx.m_output.currentModule)) {
                m_ectx.m_ctx.report(method.range(), diag::DiagId::ParseError,
                                    "member '{}::{}' references a module-level entity that the module header does not publish "
                                    "(e.g. a '__::' helper): a template or constexpr member must be defined in the header; "
                                    "move the helper out of '__::' or drop the template",
                                    rec.text(), method.text());
                continue;
            }
            std::optional<BodyEmitInHeaderGuard> bodyGuard;
            if (m_ectx.m_mode.forwardDeclOnly) {
                bodyGuard.emplace(m_ectx);
            }
            generateFuncDeclToFile(method, output_idx, /*isMember=*/true);
            continue;
        }
        generateFuncDeclToFile(method, output_idx, /*isMember=*/true);
    }

    std::string tail = "};\n";
    // POD-проверка - только для КОНКРЕТНЫХ Struct (не для шаблона: static_assert над шаблоном
    // невыразим/зависит от T). Для Struct-шаблонов assert эмитится на инстанциациях.
    if (isPod && paramNames.empty()) {
        m_driver.m_type.recordRequiredInclude("#include <type_traits>");
        tail += "static_assert(std::is_trivial_v<" + rec_cpp + "> && std::is_standard_layout_v<" + rec_cpp + ">, \"trust: Struct '" + std::string(rec.text()) +
                "' must be POD\");\n";
    }
    if (m_ectx.m_ctx.source().mappingActive() && !typeNameRange.begin.isInvalid()) {
        mapDeclaredName(output_idx, typeNameRange, typeNameOff, rec.text(), rec_cpp);
    }
    m_ectx.m_ctx.source().output_append(output_idx, tail);
    m_ectx.m_activeTemplateParams.clear();
}

void DeclEmitter::emitPublishedRecordMembers(const Binary& typeDecl) {
    // Заголовок модуля опубликовал только ПРОТОТИПЫ методов (forward-режим), поэтому тела,
    // которые нельзя выразить в заголовке (ссылка на `__::`), определяются здесь - в исполняющем
    // артефакте модуля (`.cppt`), где anonymous namespace доступен. Шаблонные и `@[constexpr]`
    // методы остаются inline в заголовке и сюда не попадают.
    if (m_ectx.m_mode.forwardDeclOnly || !typeDecl.m_right || !typeDecl.m_right->is<RecordDecl>()) {
        return;
    }
    const auto& rec = static_cast<const RecordDecl&>(*typeDecl.m_right);
    if (!rec.m_body.has_value()) {
        return;
    }
    TypeId recId = INVALID_TYPE_ID;
    if (typeDecl.m_left && typeDecl.m_left->is<IdentName>()) {
        recId = typeDecl.m_left->as<IdentName>()->resolvedTypeId;
    }
    const bool recordIsTemplate = recId != INVALID_TYPE_ID && !m_ectx.m_ctx.readTypes().store().recordTemplateParams(recId).empty();
    // Владелец печатается КОРОТКО: определение эмитится в той же лексической области, что и
    // объявление типа (namespace модуля/область имён) — квалификация добавила бы дубль.
    const std::string owner = utils::cppDeclName(rec.text());
    bool emitted = false;
    for (const auto& member : *rec.m_body) {
        if (!member || !member->is<FuncDecl>()) {
            continue;
        }
        const auto& method = static_cast<const FuncDecl&>(*member);
        if (!method.m_body.has_value() || method.aliasKind() != AliasKind::None || method.m_isNativeImport || method.m_isNativeTemplateImport ||
            method.m_isNativeTemplateCtor) {
            continue;
        }
        if (methodBodyMustBeInHeader(method, recordIsTemplate, m_ectx.m_ctx.attrs())) {
            continue; // определение живёт в заголовке
        }
        // Default-аргументы уже выведены в прототипе заголовка - повтор в определении запрещён C++.
        generateFuncDeclToFile(method, m_ectx.m_output.out, /*isMember=*/true, owner, /*suppressDefaults=*/true);
        emitted = true;
    }
    if (emitted) {
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "\n");
    }
}

void DeclEmitter::mapDeclaredName(MapperFile output_idx, MapperRange trustRange, uint32_t prefixLen, std::string_view name, std::string_view cppName) {
    // Подавленный маппинг (генерация заголовка модуля): mapStart не пушил стек, маппить нечего.
    if (m_ectx.m_ctx.source().mappingSuppressed()) {
        return;
    }
    // Невалидный trust-range (напр. промежуточный токен раскрытого макроса) или пустой стек
    // маппинга (mapStart не выполнен для невалидного range) - имя НЕ регистрируем. Валидность
    // проверяется ДО обращения к мапперу, сам маппер остаётся строгим (EXPECT/FAULT).
    if (!m_ectx.m_ctx.source().mappingActive() || trustRange.isInvalid()) {
        return;
    }
    // Оффсет всегда от mapStackTop().outputBegin (инклуды output_prepend сдвигают начало
    // вывода - нельзя предполагать, что вывод начинается с offset 1). См. memory (transpiler).
    const auto stackEntry = m_ectx.m_ctx.source().mapStackTop();
    const uint32_t nameOffset = stackEntry.outputBegin.offset() + prefixLen;
    MapperLocation nameBegin = m_ectx.m_ctx.source().makeLoc(output_idx, nameOffset);
    MapperLocation nameEnd = m_ectx.m_ctx.source().makeLoc(output_idx, nameOffset + static_cast<uint32_t>(cppName.length()));
    MapperRange cppNameRange(nameBegin, nameEnd);
    m_ectx.m_ctx.source().addNameMapping(trustRange, cppNameRange, name, cppName);
}

} // namespace trust
