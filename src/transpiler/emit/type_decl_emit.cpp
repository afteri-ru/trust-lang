#include "transpiler/emit/decl_emit.hpp"
#include "transpiler/emit/decl_emit_common.hpp"
#include "analysis/builtin_class.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_dispatch.hpp"
#include "ast/token_type.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "types/types.hpp"
#include "types/inheritance.hpp"
#include "target/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/type_data.hpp"
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

namespace {

/// Out-of-class определение члена Enum/Variant: `const <memberCpp> <ownerCpp>::<cppName>{<payload>};`.
/// Имя — тем же кодеком `cppDeclName`, что и объявление (`emitStaticConstMember`). `payload` —
/// значение (Enum — `value, ordinal`; Variant — значение либо ordinal). Локально (`type_decl_emit`
/// в allowlist C++-домена: сборка `Owner::name` — C++-имя, не trust-имя).
void appendStaticConstDefinition(std::string& out, std::string_view memberCpp, std::string_view ownerCpp, std::string_view trustName,
                                 std::string_view payload) {
    out += "const " + std::string(memberCpp) + " " + std::string(ownerCpp) + "::" + utils::cppDeclName(trustName) + "{" + std::string(payload) + "};\n";
}

} // namespace

void DeclEmitter::generateTypeDeclToFile(const Binary& binary_node, MapperFile output_idx) {
    auto* left = binary_node.m_left.get();
    // Имя типа слева от `::=`: `Name` (Ident) или `:Name` (TypeName, напр. в `:Point ::= :Struct{...}`).
    if (!left || (left->kind() != ParserToken::Kind::Ident && left->kind() != ParserToken::Kind::TypeName)) {
        m_ectx.m_services.report(binary_node.range(), diag::DiagId::ParseError, "type declaration must have a name on the left");
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
                m_ectx.m_services.report(binary_node.range(), diag::DiagId::ParseError, "enum type '{}' is not resolved by semantic analysis", requiredText(*left));
                return;
            }
            // Отображение объявления (trust-range → cpp): MapperScope охватывает struct +
            // out-of-class определения; маппинг имени типа выполняет emitEnumStruct.
            std::unique_ptr<MapperScope> scope;
            if (!binary_node.range().begin.isInvalid()) {
                scope = std::make_unique<MapperScope>(m_ectx.m_services.source(), binary_node.range(), output_idx);
            }
            emitEnumStruct(requiredText(*left), dl, typeId, output_idx, left->range());
            return;
        }
        if (dl.m_type && requiredText(*dl.m_type) == type_category::Variant) {
            const TypeId typeId = left->as<IdentName>()->resolvedTypeId;
            if (typeId == INVALID_TYPE_ID) {
                m_ectx.m_services.report(binary_node.range(), diag::DiagId::ParseError, "variant type '{}' is not resolved by semantic analysis",
                                    requiredText(*left));
                return;
            }
            std::unique_ptr<MapperScope> scope;
            if (!binary_node.range().begin.isInvalid()) {
                scope = std::make_unique<MapperScope>(m_ectx.m_services.source(), binary_node.range(), output_idx);
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
            m_ectx.m_services.report(binary_node.range(), diag::DiagId::ParseError, "record type '{}' is not resolved by semantic analysis", requiredText(*left));
            return;
        }
        std::unique_ptr<MapperScope> recScope;
        if (!binary_node.range().begin.isInvalid()) {
            recScope = std::make_unique<MapperScope>(m_ectx.m_services.source(), binary_node.range(), output_idx);
        }
        emitRecordDecl(rec, typeId, output_idx, left->range());
        return;
    }
    // Forward-объявление НАТИВНОГО класса `MyStr ::= %std::string { ... };`: под (B) кодоген
    // ГЕНЕРИРУЕТ класс-обёртку `class MyStr : public std::string { ... };` (C++-имя = trust-имя).
    // Совпадение trust-имени и RHS (`CountDel ::= %CountDel`) — чистый алиас: класс определён
    // в C++-заголовке, C++-struct НЕ эмитится.
    if (right && right->kind() == ParserToken::Kind::ClassDecl) {
        emitNativeClassWrapper(binary_node, static_cast<const ClassDecl&>(*right), output_idx);
        return;
    }
    // База алиаса: TypeName (:Int32) или Ident (MyInt - существующий алиас/переменная).
    // TypeName рендерится из материализованного `resolvedTypeId`; Ident-ссылка на алиас
    // разрешается в реестре по имени (декларация типа, не аннотация).
    if (!right || (right->kind() != ParserToken::Kind::TypeName && right->kind() != ParserToken::Kind::Ident)) {
        m_ectx.m_services.report(binary_node.range(), diag::DiagId::ParseError, "unsupported type alias definition");
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
        m_ectx.m_services.report(right->range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", requiredText(*right));
        return;
    }

    MapperScope scope(m_ectx.m_services.source(), binary_node.range(), output_idx);
    const std::string using_prefix = "using ";
    std::string cpp_line = using_prefix + type_name + " = " + base_cpp + ";";
    m_ectx.m_services.source().output_append(output_idx, cpp_line);

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
    // Объявление эмитится в trust-namespace — имя модуля в C++-символ не входит.
    const std::string enum_cpp = utils::cppDeclName(enum_trust);
    const auto* ed = m_ectx.m_services.readTypes().store().getTypeDataAs<EnumTypeData>(enum_id);
    if (!ed) {
        m_ectx.m_services.report(dict.range(), diag::DiagId::ParseError, "enum '{}' has no member data", enum_trust);
        return;
    }

    auto vn = m_driver.m_type.emitTypeName(ed->valueType);
    if (!vn) {
        m_ectx.m_services.report(dict.range(), diag::DiagId::ParseError, "unable to generate C++ type for enum '{}' value type", enum_trust);
        return;
    }
    std::string valueCpp = std::move(*vn);
    const size_t N = ed->members.size();

    // Рантайм-шаблон trust::Enum: заголовок из trust-runtime (механизм «@»-заголовков).
    m_driver.m_type.recordRequiredInclude("@trust/enum.hpp");

    const std::string base = "trust::Enum<" + valueCpp + ", " + std::to_string(N) + ", " + enum_cpp + ">";
    // Единый список членов из AST (диапазоны/значения) + инвариант соответствия реестру:
    // значения членов ВЫЧИСЛЕНЫ семантикой (автоинкремент) и живут в реестре; AST несёт spelling.
    const std::vector<DictElement> elements = dictElements(dict);
    EXPECT(elements.size() == N && "enum: AST member list and registry member list must agree");
    // Каркас — единый `cppClassFrame`; базис trust::Enum печатается без спецификатора доступа.
    const CppClassFrame frame = cppClassFrame("struct", "", {}, enum_cpp, {CppBase{base, Visibility::Public, /*omitAccessKeyword=*/true}});
    std::string out = frame.open;
    out += "    using " + base + "::Enum;\n";
    // Декларации членов + name-маппинг (hover) — единый helper с Variant.
    for (size_t midx = 0; midx < N; ++midx) {
        emitStaticConstMember(out, output_idx, enum_cpp, ed->members[midx].name, elements[midx]);
    }
    // Таблица имя↔значение (для fromName/fromValue в trust::Enum); порядок = объявление (ordinal).
    // `static inline const` (C++17 inline-переменная): позволяет in-class инициализацию и для
    // НЕ-литеральных типов значений (Rational), инициализируемых в рантайме; fromName/fromValue
    // (не constexpr) читают её во время выполнения. Для литеральных типов тоже корректно.
    out += "    static inline const trust::EnumMember<" + valueCpp + "> kMembers[" + std::to_string(N) + "] = {";
    // Значения членов Enum — из реестра (memberValueCpp по типу значения); разбор значения-узла
    // (литеральность/безнарный член/диагностика «не реализовано») — единый helper с Variant.
    std::string defs;
    for (size_t idx = 0; idx < N; ++idx) {
        const AstNodeBase* valNode = nullptr;
        if (!resolveEnumVariantMemberValue(m_ectx, elements[idx], "enum", enum_trust, valNode)) {
            return;
        }
        const std::string val_str = memberValueCpp(m_ectx.m_services.readTypes(), m_ectx.cppFactsView(), ed->valueType, ed->members[idx].value);
        if (idx) {
            out += ", ";
        }
        out += "{" + val_str + ", \"" + ed->members[idx].name + "\"}";
        appendStaticConstDefinition(defs, enum_cpp, enum_cpp, ed->members[idx].name, val_str + ", " + std::to_string(idx));
    }
    out += "};\n";
    out += frame.close;
    out += defs;
    // Маппинг имени типа: оффсет из каркаса (сразу после "struct ").
    if (m_ectx.m_services.source().mappingActive() && !typeNameRange.begin.isInvalid()) {
        mapDeclaredName(output_idx, typeNameRange, frame.nameOffset, enum_trust, enum_cpp);
    }
    m_ectx.m_services.source().output_append(output_idx, out);
}

// Эмиссия Variant-типа (гетерогенный): `struct c_Value { using Variant = std::variant<...>;
// static const <T> c_MEMBER; ... }` + out-of-class определения (значения из DictLiteral RHS).
// Каждый член - константа СВОЕГО типа (из VariantTypeData); `Value.RED` → c_Value::c_RED.
void DeclEmitter::emitVariantStruct(std::string_view variant_trust, const DictLiteralNode& dict, TypeId variant_id, MapperFile output_idx,
                                    MapperRange typeNameRange) {
    const std::string vcpp = utils::cppDeclName(variant_trust);
    const auto* vd = m_ectx.m_services.readTypes().store().getTypeDataAs<VariantTypeData>(variant_id);
    if (!vd) {
        m_ectx.m_services.report(dict.range(), diag::DiagId::ParseError, "variant '{}' has no member data", variant_trust);
        return;
    }

    m_driver.m_type.recordRequiredInclude("#include <variant>");

    // C++-имена типов членов (emitTypeName записывает их инклуды).
    std::vector<std::string> memberCpp;
    memberCpp.reserve(vd->members.size());
    for (const auto& m : vd->members) {
        auto n = m_driver.m_type.emitTypeName(m.type);
        if (!n) {
            m_ectx.m_services.report(dict.range(), diag::DiagId::ParseError, "unable to generate C++ type for variant '{}' member '{}'", variant_trust, m.name);
            return;
        }
        memberCpp.push_back(std::move(*n));
    }

    // Каркас — единый `cppClassFrame`.
    const CppClassFrame frame = cppClassFrame("struct", "", {}, vcpp, {});
    std::string out = frame.open;
    out += "    using Variant = std::variant<";
    for (size_t i = 0; i < vd->members.size(); ++i) {
        if (i) {
            out += ", ";
        }
        out += memberCpp[i];
    }
    out += ">;\n";
    // Единый список членов из AST (диапазоны/значения) + инвариант соответствия реестру.
    const std::vector<DictElement> elements = dictElements(dict);
    EXPECT(elements.size() == vd->members.size() && "variant: AST member list and registry member list must agree");
    // Декларации членов + name-маппинг (hover) — единый helper с Enum.
    for (size_t midx = 0; midx < vd->members.size(); ++midx) {
        emitStaticConstMember(out, output_idx, memberCpp[midx], vd->members[midx].name, elements[midx]);
    }
    out += "    static constexpr int count() { return " + std::to_string(vd->members.size()) + "; }\n";
    out += frame.close;
    // out-of-class определения: ИМЯ члена - из реестра (тот же источник, что у декларации выше),
    // значение - AST-литерал (`ArgNode::m_value`; реестр VariantMemberData хранит только тип);
    // без значения → ordinal. Форматирование значения по типу члена - единый memberValueCpp.
    for (size_t i = 0; i < vd->members.size(); ++i) {
        // Единый разбор значения члена с common-helper'ом Enum (диагностика «не реализовано»).
        const AstNodeBase* valNode = nullptr;
        if (!resolveEnumVariantMemberValue(m_ectx, elements[i], "variant", variant_trust, valNode)) {
            return;
        }
        std::string val_str = std::to_string(i);
        if (valNode != nullptr) {
            val_str = memberValueCpp(m_ectx.m_services.readTypes(), m_ectx.cppFactsView(), vd->members[i].type, std::string(requiredText(*valNode)));
        }
        appendStaticConstDefinition(out, memberCpp[i], vcpp, vd->members[i].name, val_str);
    }
    // Маппинг имени типа: оффсет из каркаса (сразу после "struct ").
    if (m_ectx.m_services.source().mappingActive() && !typeNameRange.begin.isInvalid()) {
        mapDeclaredName(output_idx, typeNameRange, frame.nameOffset, variant_trust, vcpp);
    }
    m_ectx.m_services.source().output_append(output_idx, out);
}

// Декларация `static const <memberCpp> c_NAME;` члена Enum/Variant + name-маппинг (тип/имя — из
// реестра, диапазон — из AST-элемента того же порядка).
void DeclEmitter::emitStaticConstMember(std::string& out, MapperFile output_idx, std::string_view memberCpp, std::string_view trustName,
                                        const DictElement& element) {
    const std::string cname = utils::cppDeclName(trustName);
    out += "    static const ";
    out += memberCpp;
    out += " ";
    const auto nameOff = static_cast<uint32_t>(out.size());
    out += cname;
    out += ";\n";
    if (m_ectx.m_services.source().mappingActive() && !element.range.begin.isInvalid()) {
        mapDeclaredName(output_idx, element.range, nameOff, trustName, cname);
    }
}

// Эмиссия пользовательского Record-типа (Struct/Class): единый `struct Name [: public Base...] {
// поля; методы };`. Struct (Group::kStructs) дополнительно получает static_assert POD; Class
// (Group::kClassDefs) - нет (виртуальные/наследование допустимы). Все члены публичные.
void DeclEmitter::emitRecordDecl(const RecordDecl& rec, TypeId rec_id, MapperFile output_idx, MapperRange typeNameRange) {
    const std::string rec_cpp = utils::cppDeclName(rec.text());

    // Пользовательский шаблон-класс (`<T> :Box ::= :Class{...}`): заголовок
    // `template <typename T, ...>` и активные имена параметров на время эмиссии членов
    // (аннотации `: T` рендерятся как `T`; см. TypeEmitter::emitTypeNameForNode).
    // Источник параметров — объявление (единый helper с функциями и обёртками нативных классов);
    // реестр хранит те же имена, поэтому `:T` и шапка совпадают.
    const CppTemplateParams tpl = cppTemplateParams(rec.m_templateParams);
    const std::vector<std::string>& paramNames = tpl.names;
    const std::string tplHeader = tpl.header.empty() ? std::string() : tpl.header + "\n";

    // Предварительное объявление (все члены - forward): C++ - incomplete type `struct c_Name;`
    // (полное определение будет в другом месте). Полное определение - ниже.
    if (!rec.hasBody()) {
        // "struct " — тот же текст-префикс используется и для эмиссии, и для offset имени (source-map).
        const std::string_view structKeyword = "struct ";
        const std::string fwd = tplHeader + std::string(structKeyword) + rec_cpp + ";\n";
        m_ectx.m_services.source().output_append(output_idx, fwd);
        if (m_ectx.m_services.source().mappingActive() && !typeNameRange.begin.isInvalid()) {
            mapDeclaredName(output_idx, typeNameRange, static_cast<uint32_t>(tplHeader.size() + structKeyword.size()), rec.text(), rec_cpp);
        }
        return;
    }

    const std::vector<TupleElementData>* fields = m_ectx.m_services.readTypes().store().fieldsOf(rec_id);
    if (fields == nullptr) {
        m_ectx.m_services.report(rec.range(), diag::DiagId::ParseError, "record '{}' has no member data", rec.text());
        return;
    }
    const bool isPod = m_ectx.m_services.readTypes().store().isStructType(rec_id);
    const bool recordIsTemplate = !paramNames.empty();

    // C++-имена типов полей (инклуды записывает emitTypeName). Тип поля - QualType:
    // квалификатор `const` объявленного поля уже сохранён семантикой. Ключ записи — `DeclId`
    // объявления поля (связь с членом AST по ключу, не по индексу; R14).
    std::vector<CppClassSpec::CppFieldSpec> fieldSpecs;
    fieldSpecs.reserve(fields->size());
    for (const auto& f : *fields) {
        auto tn = m_driver.m_type.emitTypeName(f.type);
        if (!tn) {
            m_ectx.m_services.report(rec.range(), diag::DiagId::ParseError, "unable to generate C++ type for field '{}' of record '{}'", f.name, rec.text());
            return;
        }
        fieldSpecs.push_back(CppClassSpec::CppFieldSpec{f.declId, std::move(*tn), f.name});
    }
    // Базовые классы: C++-имена + вариант наследования (абстрактные маркеры `:Struct`/`:Class`
    // семантика в baseClasses не кладёт). Дефолт record-типа (C++ `struct`) — public.
    std::vector<CppBase> baseCpp;
    for (const BaseClass& b : m_ectx.m_services.readTypes().store().basesOf(rec_id)) {
        auto bn = m_driver.m_type.resolveCppTypeId(b.type);
        if (!bn) {
            m_ectx.m_services.report(rec.range(), diag::DiagId::ParseError, "unable to generate C++ base type for record '{}'", rec.text());
            return;
        }
        baseCpp.push_back(CppBase{std::move(bn->first), b.access});
    }

    // Активные имена типовых параметров - на время эмиссии членов (RAII: снимаются на ЛЮБОМ
    // выходе, не затирая объемлющий контекст).
    std::optional<ActiveTemplateParamsGuard> templateGuard;
    if (!paramNames.empty()) {
        templateGuard.emplace(m_ectx, paramNames);
    }

    // Каркас + члены — ЕДИНЫЙ генератор (`emitCppClass`): без namespace-сегментов (пользовательский
    // тип живёт в области имён программиста, не в C++-имени). Поля берутся из РЕЕСТРА (порядок
    // объявления), обход членов — в порядке AST; секции доступа печатает генератор.
    CppClassSpec spec;
    spec.keyword = "struct";
    spec.templateHeader = tplHeader;
    spec.className = rec_cpp;
    spec.bases = std::move(baseCpp);
    spec.recordIsTemplate = recordIsTemplate;
    spec.ownerTrustName = rec.text();
    spec.members = *rec.body();
    spec.fields = std::move(fieldSpecs);
    spec.podStruct = isPod;
    // POD-проверка - только для КОНКРЕТНЫХ Struct (не для шаблона: static_assert над шаблоном
    // невыразим/зависит от T). Для Struct-шаблонов assert эмитится на инстанциациях.
    if (isPod && paramNames.empty()) {
        m_driver.m_type.recordRequiredInclude("#include <type_traits>");
        spec.trailer = podAssertText(rec_cpp, rec.text()) + "\n";
    }
    spec.typeNameRange = typeNameRange;
    emitCppClass(spec, output_idx);
}

void DeclEmitter::emitAccessSection(std::string& buffer, MapperFile output_idx, Visibility level, Visibility& current) const {
    if (level == current) {
        return;
    }
    // Спецификатор печатается с НАЧАЛА строки: предыдущий член мог закрыться без '\n'.
    const std::string_view tailSoFar = buffer.empty() ? m_ectx.m_services.source().output_body(output_idx) : std::string_view(buffer);
    if (!outputAtLineStart(tailSoFar)) {
        buffer += "\n";
    }
    buffer += "  ";
    buffer += visibilityKeyword(level);
    buffer += ":\n";
    current = level;
}

uint32_t DeclEmitter::declaredNameOffset(MapperFile output_idx, std::string_view buffer) const {
    // База — фактический конец вывода минус точка mapStart: буфер сливается порциями
    // (перед каждым методом), поэтому абсолютное смещение от начала `out` недостаточно.
    const auto& src = m_ectx.m_services.source();
    const uint32_t base = static_cast<uint32_t>(src.output_body(output_idx).size()) - src.mapStackTop().outputBegin.offset();
    return base + static_cast<uint32_t>(buffer.size());
}

void DeclEmitter::flushOutput(MapperFile output_idx, std::string& buffer) const {
    if (!buffer.empty()) {
        m_ectx.m_services.source().output_append(output_idx, buffer);
        buffer.clear();
    }
}

/// ЕДИНЫЙ генератор C++-класса: каркас, пролог, обход членов в ПОРЯДКЕ ОБЪЯВЛЕНИЯ и секции
/// доступа. Различия Record/обёртки/builtin-класса — в `CppClassSpec` (данные), не в коде.
void DeclEmitter::emitCppClass(const CppClassSpec& spec, MapperFile output_idx) {
    const CppClassFrame frame = cppClassFrame(spec.keyword, spec.templateHeader, spec.namespaces, spec.className, spec.bases);
    std::string out = frame.open;
    out += spec.preamble;
    // Старт — Public (C++-default `struct`; у обёртки пролог уже вывел `public:`).
    Visibility currentAccess = Visibility::Public;

    for (const AstNodePtr& member : spec.members) {
        if (!member) {
            continue;
        }
        if (member->is<VarDecl>()) {
            const auto& vd = *member->as<VarDecl>();
            // Связь «член AST ↔ поле формы» — по КЛЮЧУ (`DeclId`), не по индексу (R14).
            const CppClassSpec::CppFieldSpec* fs = nullptr;
            for (const CppClassSpec::CppFieldSpec& cand : spec.fields) {
                if (cand.declId == vd.declId()) {
                    fs = &cand;
                    break;
                }
            }
            EXPECT(fs != nullptr && "class: AST field has no matching form entry (DeclId link)");
            emitAccessSection(out, output_idx, vd.visibility, currentAccess);
            const std::string& trustName = fs->trustName;
            const std::string cname = utils::cppDeclName(trustName);
            out += "    " + fs->cppType + " ";
            uint32_t nameOff = 0;
            if (m_ectx.m_services.source().mappingActive() && !vd.range().begin.isInvalid()) {
                nameOff = declaredNameOffset(output_idx, out);
            }
            // Struct (POD): поле БЕЗ инициализатора (NSDMI ломает тривиальность). Class/обёртка:
            // `:= <value>` → NSDMI `{<value>}` (тем же путём, что default-аргумент функции);
            // `:= _` → без инициализатора.
            std::string init = ";";
            if (!spec.podStruct && vd.initKind() == VarInitKind::Initialized && vd.m_initializer) {
                init = "{" + m_driver.renderExprToString(vd.m_initializer.get()) + "};";
            }
            out += cname + init + "\n";
            if (nameOff != 0) {
                mapDeclaredName(output_idx, vd.range(), nameOff, trustName, cname);
            }
            continue;
        }
        if (!spec.emitMethods || !member->is<FuncDecl>()) {
            continue;
        }
        const auto& method = *member->as<FuncDecl>();
        // Заголовочный режим: тело остаётся здесь только если его нельзя вынести out-of-line
        // (член шаблона/шаблонный метод/`@[constexpr]`); иначе — прототип (тело даст `.cppt`).
        if (m_ectx.m_mode.forwardDeclOnly && !methodBodyMustBeInHeader(method, spec.recordIsTemplate, m_ectx.m_services.attrs())) {
            emitAccessSection(out, output_idx, method.visibility, currentAccess);
            flushOutput(output_idx, out);
            generateFuncDeclToFile(method, output_idx, /*isMember=*/true);
            continue;
        }
        if (m_ectx.m_mode.forwardDeclOnly && headerBodyUnpublishable(m_ectx, method, spec.ownerTrustName)) {
            continue;
        }
        std::optional<BodyEmitInHeaderGuard> bodyGuard;
        if (m_ectx.m_mode.forwardDeclOnly) {
            bodyGuard.emplace(m_ectx);
        }
        emitAccessSection(out, output_idx, method.visibility, currentAccess);
        flushOutput(output_idx, out);
        generateFuncDeclToFile(method, output_idx, /*isMember=*/true);
    }
    flushOutput(output_idx, out);

    std::string tail = frame.close;
    tail += spec.trailer;
    if (m_ectx.m_services.source().mappingActive() && !spec.typeNameRange.begin.isInvalid()) {
        mapDeclaredName(output_idx, spec.typeNameRange, frame.nameOffset, spec.ownerTrustName, spec.className);
    }
    m_ectx.m_services.source().output_append(output_idx, tail);
}

/// ЕДИНЫЙ цикл out-of-line тел методов объявления, опубликованного заголовком (`.cppt`).
void DeclEmitter::emitOutOfLineMethods(const std::vector<AstNodePtr>& members, std::string_view ownerCpp, bool recordIsTemplate,
                                       const std::vector<std::string>& namespaces) {
    for (const std::string& ns : namespaces) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, cppNamespaceOpen(ns, /*inlineForm=*/true));
    }
    bool emitted = false;
    for (const auto& member : members) {
        if (!member || !member->is<FuncDecl>()) {
            continue;
        }
        const auto& method = *member->as<FuncDecl>();
        if (!method.hasBody() || method.aliasKind() != AliasKind::None || method.bindsNativeCpp()) {
            continue;
        }
        if (methodBodyMustBeInHeader(method, recordIsTemplate, m_ectx.m_services.attrs())) {
            continue; // определение живёт в заголовке
        }
        // Default-аргументы уже выведены в прототипе заголовка - повтор в определении запрещён C++.
        generateFuncDeclToFile(method, m_ectx.m_output.out, /*isMember=*/true, ownerCpp, /*suppressDefaults=*/true);
        emitted = true;
    }
    if (emitted) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, "\n");
    }
    for (std::size_t i = 0; i < namespaces.size(); ++i) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, cppNamespaceClose(/*inlineForm=*/true));
    }
}

void DeclEmitter::emitNativeClassWrapper(const Binary& typeDecl, const ClassDecl& cls, MapperFile output_idx) {
    const std::string_view base = m_ectx.m_services.declCppFacts().cppNameOf(cls.declId());
    // Семантика отклоняет нативный класс без непустого C++-имени (semantic/decl/class_decl_analyzer.cpp):
    // нарушение контракта AST — ICE, а не тихий пропуск объявленного типа.
    EXPECT(!base.empty() && "native class must carry a non-empty C++ name (semantic contract)");
    // `std::vector`/`std::array` унифицируются с `:Array` — остаются проекцией, не обёрткой.
    if (trust::type::isBuiltinContainerCppName(base)) {
        return;
    }
    TypeId clsId = INVALID_TYPE_ID;
    if (typeDecl.m_left && typeDecl.m_left->is<IdentName>()) {
        clsId = typeDecl.m_left->as<IdentName>()->resolvedTypeId;
    }
    EXPECT(clsId != INVALID_TYPE_ID && "native class type must be resolved by semantic analysis");
    const AttrPool& attrs = m_ectx.m_services.attrs();
    const bool builtinImpl = m_ectx.m_services.readTypes().store().isBuiltinType(clsId);
    // Реализация builtin, описывающая СВОЙ класс (базисы в синтаксисе либо метод с телом) —
    // генерируется; реализация только из нативных привязок — проекция на внешний C++-тип.
    const bool generatedBuiltin = builtinImpl && analysis::builtinImplGeneratesCppClass(cls, attrs);

    CppQualifiedParts classCpp;
    std::vector<CppBase> baseCppList;
    if (generatedBuiltin) {
        // C++-имя реализации (`trust::StrChar`) задаёт namespace и локальное имя класса; базисы —
        // синтаксис объявления (`Name = %cpp, %std::string {...}`): нативный (`%name`) или обычный
        // тип-класс (резолвится семантикой).
        classCpp = splitCppName(base);
        EXPECT(!classCpp.name.empty() && "generated builtin class: native C++ name must have a local name");
        // Нативные базисы — готовые C++-имена (в порядке объявления), затем обычные типы-классы.
        for (const NativeBase& nativeBase : cls.m_nativeBaseCppNames) {
            baseCppList.push_back(CppBase{nativeBase.cppName, nativeBase.access});
        }
        for (const auto& baseNode : cls.m_baseTypes) {
            if (!baseNode.type) {
                continue;
            }
            const TypeId baseId = baseNode.type->is<IdentName>() ? baseNode.type->as<IdentName>()->resolvedTypeId : INVALID_TYPE_ID;
            if (baseId == INVALID_TYPE_ID) {
                // Диагностику базы уже выдала семантика; класс не эмитится (без тихой подмены).
                m_ectx.m_services.report(baseNode.type->range(), diag::DiagId::ParseError, "base type of class '{}' is not resolved by semantic analysis",
                                    cls.text());
                return;
            }
            auto baseName = m_driver.m_type.resolveCppTypeId(baseId);
            if (!baseName.has_value()) {
                return; // диагностика выдана единым резолвером типов
            }
            baseCppList.push_back(CppBase{std::move(baseName->first), baseNode.access});
        }
    } else {
        if (builtinImpl) {
            return; // C++-тип/заголовок приходят извне (`@[include]`), дублировать класс нельзя
        }
        if (base == cls.text()) {
            return; // алиас на существующий C++-класс: определение — в C++-заголовке
        }
        classCpp.name = utils::cppDeclName(cls.text());
        // Прозрачная обёртка нативного типа: базис `%Native` — IDENTITY-mapping (не пользовательская
        // база); доступ — общий дефолт public (иначе унаследованные члены стали бы приватными и
        // обёртка потеряла бы смысл). Явный вариант нужен только при БАЗИСАХ в синтаксисе
        // (см. generatedBuiltin выше).
        baseCppList.push_back(CppBase{std::string(base), Visibility::Public});
    }
    // Инклуд/линковка объявления реализации — до определения класса. recordUsedType отмечает тип
    // (его include — заголовок модуля-владельца; собственный отфильтрован collector'ом).
    m_driver.m_type.collectInclude(cls);
    m_driver.m_type.collectLinkLib(cls);
    m_driver.m_type.recordUsedType(clsId);

    // Типовые параметры (`<T1,T2>` / packs): заголовок `template <typename T1, typename T2>`,
    // единый helper (общий с функциями и record-классами).
    const CppTemplateParams tpl = cppTemplateParams(cls.m_templateParams);
    const std::vector<std::string>& paramNames = tpl.names;
    const bool recordIsTemplate = !paramNames.empty();
    // База-обёртка с типовыми аргументами (`trust::Iterator<T>`): только у обёртки нативного типа —
    // у генерируемого класса базисы заданы синтаксисом (нативным C++-именем или типом).
    if (!generatedBuiltin && !paramNames.empty() && !baseCppList.empty()) {
        std::string withArgs = baseCppList.front().name + "<";
        for (std::size_t i = 0; i < paramNames.size(); ++i) {
            if (i != 0) {
                withArgs += ", ";
            }
            withArgs += paramNames[i];
            if (tpl.packs[i]) {
                withArgs += "...";
            }
        }
        withArgs += ">";
        baseCppList.front().name = std::move(withArgs);
    }

    // Активные типовые параметры — на время эмиссии членов (RAII: снимаются на любом выходе).
    std::optional<ActiveTemplateParamsGuard> templateGuard;
    if (recordIsTemplate) {
        templateGuard.emplace(m_ectx, paramNames);
    }

    // Каркас + члены — ЕДИНЫЙ генератор (`emitCppClass`): шапка шаблона, namespace-сегменты
    // C++-имени, `class Name : public Base {`; пролог (`public:` + using/ctor) — данные спеки.
    const std::string tplHeader = recordIsTemplate ? tpl.header + "\n" : std::string();
    std::string preamble = "  public:\n";
    for (const CppBase& cppBase : baseCppList) {
        // Наследование конструкторов базиса (`using Base::Base;`): локальное имя — БЕЗ типовых
        // аргументов (`using B<T>::B<T>;` невалиден C++).
        std::string ctorName = splitCppName(cppBase.name).name;
        if (const std::size_t lt = ctorName.find('<'); lt != std::string::npos) {
            ctorName.erase(lt);
        }
        preamble += "    using " + cppBase.name + "::" + ctorName + ";\n";
    }
    if (!baseCppList.empty()) {
        const std::string& primaryBase = baseCppList.front().name;
        // Конвертирующий конструктор из ПЕРВОГО базиса: код, возвращающий базовый C++-тип,
        // должен неявно давать обёртку. Constraint `U == Base` не перехватывает посторонние типы
        // и не инстанцируется для некопируемых баз (ThreadPool).
        preamble += "    template <typename U>\n";
        preamble += "    requires std::is_same_v<std::remove_cvref_t<U>, " + primaryBase + ">\n";
        preamble += "    " + classCpp.name + "(U&& b) : " + primaryBase + "(std::forward<U>(b)) { }\n";
        m_driver.m_type.recordRequiredInclude("#include <type_traits>");
        m_driver.m_type.recordRequiredInclude("#include <utility>");
    }

    // Объявление класса — объявление типа: диапазон охватывает весь класс (hover/definition);
    // в forward-режиме заголовка маппинг подавлен самим маппером.
    std::unique_ptr<MapperScope> scope;
    if (!typeDecl.range().begin.isInvalid()) {
        scope = std::make_unique<MapperScope>(m_ectx.m_services.source(), typeDecl.range(), output_idx);
    }
    CppClassSpec spec;
    spec.keyword = "class";
    spec.templateHeader = tplHeader;
    spec.namespaces = classCpp.namespaces;
    spec.className = classCpp.name;
    spec.bases = std::move(baseCppList);
    spec.preamble = std::move(preamble);
    // Члены эмитятся ТОЛЬКО у сгенерированного класса реализации builtin: обёртка нативного типа
    // (`::= %Native`) интерфейсом не владеет (её члены объявлены внешним C++-заголовком).
    spec.emitMethods = generatedBuiltin;
    spec.recordIsTemplate = recordIsTemplate;
    spec.ownerTrustName = cls.text();
    if (generatedBuiltin) {
        spec.members = cls.m_body;
        // Генерируемый builtin-класс не объявляет собственных полей: его состояние — из нативных
        // базисов. Поле VarDecl здесь нарушает контракт семантики — внутренняя ошибка с явным
        // сообщением (типы полей `emitCppClass` берёт из реестра, у обёртки их нет).
        for (const AstNodePtr& member : cls.m_body) {
            if (member && member->is<VarDecl>()) {
                FAULT("generated builtin class '{}' declares field '{}': fields of a builtin "
                      "implementation are not supported (state comes from its native bases)",
                      cls.text(), member->as<VarDecl>()->text());
            }
        }
    }
    if (typeDecl.m_left) {
        spec.typeNameRange = typeDecl.m_left->range();
    }
    emitCppClass(spec, output_idx);
}

void DeclEmitter::emitPublishedRecordMembers(const Binary& typeDecl) {
    // Заголовок модуля опубликовал только ПРОТОТИПЫ членов (forward-режим), поэтому тела,
    // которые нельзя выразить в заголовке, определяются здесь - в исполняющем артефакте модуля
    // (`.cppt`). Шаблонные и `@[constexpr]` тела остаются inline в заголовке и сюда не попадают.
    if (m_ectx.m_mode.forwardDeclOnly || !typeDecl.m_right) {
        return;
    }
    const bool record = typeDecl.m_right->is<RecordDecl>();
    const bool nativeClass = typeDecl.m_right->is<ClassDecl>();
    if (!record && !nativeClass) {
        return;
    }
    TypeId recId = INVALID_TYPE_ID;
    if (typeDecl.m_left && typeDecl.m_left->is<IdentName>()) {
        recId = typeDecl.m_left->as<IdentName>()->resolvedTypeId;
    }
    std::string owner;
    std::vector<std::string> ownerNamespaces;
    bool recordIsTemplate = false;
    const std::vector<AstNodePtr>* members = nullptr;
    if (record) {
        const auto& rec = static_cast<const RecordDecl&>(*typeDecl.m_right);
        if (!rec.hasBody()) {
            return;
        }
        // Шаблонность record — из объявления (AST), как в emitRecordDecl: реестр не второй источник.
        recordIsTemplate = !cppTemplateParams(rec.m_templateParams).names.empty();
        owner = utils::cppDeclName(rec.text());
        members = &(*rec.body());
    } else {
        const auto& cls = static_cast<const ClassDecl&>(*typeDecl.m_right);
        // Out-of-line тела есть только у СГЕНЕРИРОВАННОГО класса реализации builtin.
        const bool generated = recId != INVALID_TYPE_ID && m_ectx.m_services.readTypes().store().isBuiltinType(recId) &&
                               analysis::builtinImplGeneratesCppClass(cls, m_ectx.m_services.attrs());
        if (!generated) {
            return;
        }
        // Шаблонность класса — тот же критерий, что у record (непустые типовые имена).
        recordIsTemplate = !cppTemplateParams(cls.m_templateParams).names.empty();
        const std::string_view native = m_ectx.m_services.declCppFacts().cppNameOf(cls.declId());
        const CppQualifiedParts parts = splitCppName(native);
        owner = parts.name;
        ownerNamespaces = parts.namespaces;
        members = &cls.m_body;
    }
    // Владелец печатается КОРОТКО: определение эмитится в той же лексической области, что и
    // объявление типа (та же область имён) — квалификация добавила бы дубль.
    emitOutOfLineMethods(*members, owner, recordIsTemplate, ownerNamespaces);
}

void DeclEmitter::mapDeclaredName(MapperFile output_idx, MapperRange trustRange, uint32_t prefixLen, std::string_view name, std::string_view cppName) {
    // Подавленный маппинг (генерация заголовка модуля): mapStart не пушил стек, маппить нечего.
    if (m_ectx.m_services.source().mappingSuppressed()) {
        return;
    }
    // Невалидный trust-range (напр. промежуточный токен раскрытого макроса) или пустой стек
    // маппинга (mapStart не выполнен для невалидного range) - имя НЕ регистрируем. Валидность
    // проверяется ДО обращения к мапперу, сам маппер остаётся строгим (EXPECT/FAULT).
    if (!m_ectx.m_services.source().mappingActive() || trustRange.isInvalid()) {
        return;
    }
    // Оффсет всегда от mapStackTop().outputBegin (инклуды output_prepend сдвигают начало
    // вывода - нельзя предполагать, что вывод начинается с offset 1). См. memory (transpiler).
    const auto stackEntry = m_ectx.m_services.source().mapStackTop();
    const uint32_t nameOffset = stackEntry.outputBegin.offset() + prefixLen;
    MapperLocation nameBegin = m_ectx.m_services.source().makeLoc(output_idx, nameOffset);
    MapperLocation nameEnd = m_ectx.m_services.source().makeLoc(output_idx, nameOffset + static_cast<uint32_t>(cppName.length()));
    MapperRange cppNameRange(nameBegin, nameEnd);
    m_ectx.m_services.source().addNameMapping(trustRange, cppNameRange, name, cppName);
}

} // namespace trust
