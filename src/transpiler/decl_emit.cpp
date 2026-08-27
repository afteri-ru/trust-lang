// Generated: src/transpiler/decl_emit.cpp
#include "transpiler/decl_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_visitor.hpp"
#include "ast/token_type.hpp"
#include "diag/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "semantic/symbol_table.hpp"
#include "semantic/solver.hpp"
#include "syntax/term.h"
#include "types/registry.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "transpiler/diag.hpp"
#include "utils/operators.hpp"
#include "utils/strings.hpp"
#include <format>
#include <memory>

namespace trust {

void DeclEmitter::generateVarDeclToFile(const VarDecl& var_node, MapperFile output_idx) {
    // Флаг линковки нативной библиотеки из @[link("имя")].
    m_driver.m_type.collectLinkLib(var_node);

    // Манглинг trust-имени в C++-идентификатор (срез '%' у нативных имён).
    std::string var_name = utils::name_to_cpp(var_node.text());
    if (var_name.empty()) {
        return;
    }

    // Determine type: типизированное имя → тип аннотации; нетипизированное → выведенный
    // анализатором конкретный тип (inferred join по истории присвоений), иначе std::any.
    std::string cpp_type;
    const bool typed = var_node.m_type && var_node.m_type->kind() == ParserToken::Kind::TypeName;
    if (typed) {
        // Резолв по имени узла-типа + запись инклудов. Нерезолвящееся имя → emitTypeNameForNode
        // сам выводит диагностику и возвращает "".
        cpp_type = m_driver.m_type.emitTypeNameForNode(var_node.m_type.get());
        if (cpp_type.empty()) {
            return;
        }
    } else {
        // Нетипизированная переменная: выведенный семантикой конкретный тип, либо ЯВНО помеченный
        // std::any (семантика маркирует Any для тип-less инициализаторов - тип-имя, embed, вызов
        // с неизвестным результатом, отрицательный литерал - и для forward-объявлений без типа).
        // Any - обычный выводимый тип → эмитим единообразно emitTypeName(inferred). INVALID у
        // переменной - ошибка вывода: тихий fallback на std::any запрещён (AGENTS rule 5).
        TypeId inferred = var_node.inferredType;
        // Страховка: forward-объявление, чей тип семантика не пометила (напр. stdlib/Any не
        // зарегистрирован), мог быть завершён последующим определением - добиваем по символу.
        if (inferred == INVALID_TYPE_ID && m_ectx.m_resolvedTypes) {
            if (const Symbol* s = m_ectx.m_resolvedTypes->resolve(var_node.text())) {
                inferred = s->type;
            }
        }
        if (inferred == INVALID_TYPE_ID) {
            if (var_node.m_initializer) {
                m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "unable to infer type for variable '{}'", var_node.text());
            } else {
                m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", type_generic::Any);
            }
            return;
        }
        std::optional<std::string> name = m_driver.m_type.emitTypeName(inferred, var_node.text());
        if (!name || name->empty()) {
            m_ectx.m_ctx.report(var_node.range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", type_generic::Any);
            return;
        }
        cpp_type = std::move(*name);
    }

    // Инклуды типа не нужны здесь: emitTypeName отметил тип (m_ectx.m_usedTypes), инклуды будут
    // сформированы из них ПОСЛЕ обхода AST (collectTypeIncludes).

    MapperScope scope(m_ectx.m_ctx.source(), var_node.range(), output_idx);
    // Константность ОБЪЯВЛЕНИЯ переменной - attr::ReadOnly на узле ('^' на имени или
    // @[readonly]@). НЕ берётся из бита Symbol::type: переменная может стать константной
    // позже (became-const, `x := 42; x^ += 1;`), но её ДЕКЛАРАЦИЯ обязана остаться не-const
    // (переменная мутировалась до финализации). Признак на узле = const «в типе» объявления.
    // Префикс влияет на смещение имени в выводе (source-map): имя идёт после "<prefix> <cpp_type> ".
    std::string prefix;
    if (var_node.has_attr(m_ectx.m_ctx.attrs(), attr::ReadOnly)) {
        prefix += "const ";
    }
    if (var_node.has_attr(m_ectx.m_ctx.attrs(), attr::ThreadLocal)) {
        prefix += "thread_local ";
    }
    // Forward-объявление `x:Type := ...;` → C++ extern-декларация переменной (объявление без
    // определения); иначе - определение с инициализатором. Смещение имени в выводе зависит
    // от наличия префикса "extern " (source-map).
    uint32_t namePrefixLen = static_cast<uint32_t>(prefix.length()) + static_cast<uint32_t>(cpp_type.length()) + 1;
    if (!var_node.m_initializer || m_ectx.m_forwardDeclOnly) {
        m_ectx.m_ctx.source().output_append(output_idx, "extern " + prefix + cpp_type + " " + var_name + ";");
        namePrefixLen += 7; // strlen("extern ")
    } else {
        m_ectx.m_ctx.source().output_append(output_idx, prefix + cpp_type + " " + var_name + " = ");
        m_driver.emitExpr(var_node.m_initializer.get());
        m_ectx.m_ctx.source().output_append(output_idx, ";");
        // Trust-условия переменной (--solver-mode=assert): проверка сразу после объявления/инициализации.
        if (!var_node.m_trust.empty()) {
            m_ectx.m_ctx.source().output_append(output_idx, "\n"); // проверка - на отдельной строке
            m_driver.m_contract.emitTrustChecks(var_node.m_trust);
        }
        // Тип-условия (тип с trust_assert): при создании значения типа (объявление переменной
        // этого типа) проверяем значение - имя типа подставляется как значение переменной.
        // Источник условий - узел декларации типа (VarDecl::m_typeDecl, ставит семантика);
        // trust-имя типа - из аннотации переменной. Без копий и без карт.
        if (var_node.m_typeDecl && var_node.m_initializer && !var_node.m_typeDecl->m_trust.empty()) {
            m_driver.m_contract.emitTypeTrustChecks(var_node.m_typeDecl->m_trust, var_node.m_type->text(), var_name);
        }
    }

    // Добавляем маппинг имени переменной для hover-ссылок.
    // Диапазон trust-имени: nameRange() берёт диапазон реального имени из m_term->m_left
    // (важно при макро-раскрытии, где range() самого узла - оператор). Fallback - имя
    // в начале range() (когда range() покрывает всю строку, m_term->m_left отсутствует).
    MapperRange trustNameRange = var_node.nameRange();
    if (trustNameRange.isInvalid()) {
        MapperLocation trustNameBegin = m_ectx.m_ctx.source().makeLoc(var_node.range().begin.fileIdx(), var_node.range().begin.offset());
        MapperLocation trustNameEnd = m_ectx.m_ctx.source().makeLoc(
            trustNameBegin.fileIdx(), trustNameBegin.offset() + static_cast<uint32_t>(utils::strip_native_prefix(var_node.text()).size()));
        trustNameRange = MapperRange(trustNameBegin, trustNameEnd);
    }
    // Имя выводится сразу после префикса "<cpp_type> " (или "extern <cpp_type> " для forward).
    // trust-имя в маппинге - исходное (var_node.text(), для нативных с '%'), cpp-имя - манглированное.
    mapDeclaredName(output_idx, trustNameRange, namePrefixLen, var_node.text(), var_name);

    // Экспортируются ОПРЕДЕЛЕНИЯ на верхнем уровне модуля в НЕ анонимной области имён
    // (глобальная '::' и именованные 'ns::' - с квалификацией, напр. "ns::x").
    // Локальные (внутри функций/блоков кода), скрытая область '_' и forward-объявления
    // (нет инициализатора → нет определения, `&::name` не связался бы) - не экспортируются.
    if (var_node.m_initializer && !var_name.empty() && !m_ectx.m_inCppBlock && m_ectx.m_hiddenNamespaceDepth == 0) {
        m_ectx.m_exports.push_back({std::string(var_node.text()), m_ectx.qualifiedCppName(var_name), buildTrustForwardDecl(var_node)});
    }
}

void DeclEmitter::generateTypeDeclToFile(const Binary& binary_node, MapperFile output_idx) {
    auto* left = binary_node.m_left.get();
    if (!left || left->kind() != ParserToken::Kind::Ident) {
        m_ectx.m_ctx.report(binary_node.range(), diag::DiagId::ParseError, "type declaration must have a name on the left");
        return;
    }

    // Имя типа-алиаса: манглинг trust-имени в C++-идентификатор (MyInt → c_MyInt).
    std::string type_name = utils::name_to_cpp(left->text());

    auto* right = binary_node.m_right.get();
    // Enum/Variant-объявление: `Color ::= :Enum(...)` / `(...):Enum`, `Value ::= :Variant(...)` /
    // `(...):Variant` - правая часть DictLiteral с аннотацией типа «Enum»/«Variant».
    if (right && right->kind() == ParserToken::Kind::DictLiteral) {
        const auto& dl = static_cast<const DictLiteralNode&>(*right);
        if (dl.m_type && dl.m_type->text() == "Enum") {
            // Тип обязан быть зарегистрирован семантикой (analyzeEnumDecl). Если его нет -
            // инвариантное нарушение: без диагностики молча ничего не эмитим.
            auto tid = m_ectx.m_ctx.types().findType(left->text());
            if (!tid) {
                m_ectx.m_ctx.report(binary_node.range(), diag::DiagId::ParseError, "enum type '{}' is not registered", left->text());
                return;
            }
            // Отображение объявления (trust-range → cpp): MapperScope охватывает struct +
            // out-of-class определения. Маппинг имени типа - внутри emitEnumStruct (оффсет
            // вычисляется из фактического вывода, а не из магической константы).
            std::unique_ptr<MapperScope> scope;
            if (!binary_node.range().begin.isInvalid()) {
                scope = std::make_unique<MapperScope>(m_ectx.m_ctx.source(), binary_node.range(), output_idx);
            }
            emitEnumStruct(left->text(), dl, *tid, output_idx, left->range());
            return;
        }
        if (dl.m_type && dl.m_type->text() == "Variant") {
            auto tid = m_ectx.m_ctx.types().findType(left->text());
            if (!tid) {
                m_ectx.m_ctx.report(binary_node.range(), diag::DiagId::ParseError, "variant type '{}' is not registered", left->text());
                return;
            }
            std::unique_ptr<MapperScope> scope;
            if (!binary_node.range().begin.isInvalid()) {
                scope = std::make_unique<MapperScope>(m_ectx.m_ctx.source(), binary_node.range(), output_idx);
            }
            emitVariantStruct(left->text(), dl, *tid, output_idx, left->range());
            return;
        }
    }
    // База алиаса: TypeName (:Int32) или Ident (MyInt - существующий алиас/переменная).
    // Оба разрешаются по имени через resolveCppType.
    if (!right || (right->kind() != ParserToken::Kind::TypeName && right->kind() != ParserToken::Kind::Ident)) {
        m_ectx.m_ctx.report(binary_node.range(), diag::DiagId::ParseError, "unsupported type alias definition");
        return;
    }

    // Resolve base type (canonical chain + cpp name + инклуды через emitTypeName).
    // Правая часть всегда тип (семантика '::=' отклоняет переменную справа), поэтому fallback
    // на переменную не нужен: при невозможности вывода базы - явная ошибка.
    std::string base_cpp;
    if (auto tid = m_ectx.m_ctx.types().findType(right->text())) {
        if (auto n = m_driver.m_type.emitTypeName(*tid, right->text())) {
            base_cpp = std::move(*n);
        }
    }
    if (base_cpp.empty()) {
        m_ectx.m_ctx.report(right->range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", right->text());
        return;
    }

    MapperScope scope(m_ectx.m_ctx.source(), binary_node.range(), output_idx);
    const std::string using_prefix = "using ";
    std::string cpp_line = using_prefix + type_name + " = " + base_cpp + ";";
    m_ectx.m_ctx.source().output_append(output_idx, cpp_line);

    // Add name mapping for the type name (hover links): name starts right after the "using " prefix.
    // trust-имя в маппинге - исходное (left->text()), cpp-имя - манглированное (type_name).
    mapDeclaredName(output_idx, left->range(), static_cast<uint32_t>(using_prefix.length()), left->text(), type_name);
}

// Эмиссия enum-типа: `struct c_Color : trust::Enum<ValueCpp, N> { using ...; static const члены; };`
// + out-of-class `const c_Color c_Color::c_MEMBER{value, ordinal};`. Generic-логика (значение/
// ординал, конструкторы, операторы сравнения по ординалу, count()) - в рантайм-шаблоне trust::Enum;
// кодогенерация эмитит только данные члена. Члены - static const (out-of-class), т.к.
// static constexpr собственного типа невозможен (неполный тип в точке объявления).
void DeclEmitter::emitEnumStruct(std::string_view enum_trust, const DictLiteralNode& dict, TypeId enum_id, MapperFile output_idx, MapperRange typeNameRange) {
    // Значения членов читаются из EnumTypeData (вычислены семантикой с автоинкрементом);
    // AST-словарь dict нужен только для диапазона диагностики.
    const std::string enum_cpp = utils::name_to_cpp(enum_trust);
    const auto* ed = m_ectx.m_ctx.types().getTypeDataAs<EnumTypeData>(enum_id);
    if (!ed) {
        m_ectx.m_ctx.report(dict.range(), diag::DiagId::ParseError, "enum '{}' has no member data", enum_trust);
        return;
    }

    auto vn = m_driver.m_type.emitTypeName(ed->valueType, std::string(enum_trust) + ".Value");
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
    const uint32_t typeNameOff = static_cast<uint32_t>(out.size());
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
            const std::string cname = utils::name_to_cpp(ed->members[midx].name);
            out += "    static const " + enum_cpp + " ";
            const uint32_t nameOff = static_cast<uint32_t>(out.size());
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
        const std::string val_str = memberValueCpp(m_ectx.m_ctx.types(), ed->valueType, ed->members[idx].value);
        const std::string cname = utils::name_to_cpp(ed->members[idx].name);
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
    const std::string vcpp = utils::name_to_cpp(variant_trust);
    const auto* vd = m_ectx.m_ctx.types().getTypeDataAs<VariantTypeData>(variant_id);
    if (!vd) {
        m_ectx.m_ctx.report(dict.range(), diag::DiagId::ParseError, "variant '{}' has no member data", variant_trust);
        return;
    }

    m_driver.m_type.recordRequiredInclude("#include <variant>");

    // C++-имена типов членов (emitTypeName записывает их инклуды).
    std::vector<std::string> memberCpp;
    memberCpp.reserve(vd->members.size());
    for (const auto& m : vd->members) {
        auto n = m_driver.m_type.emitTypeName(m.type, "");
        if (!n) {
            m_ectx.m_ctx.report(dict.range(), diag::DiagId::ParseError, "unable to generate C++ type for variant '{}' member '{}'", variant_trust, m.name);
            return;
        }
        memberCpp.push_back(std::move(*n));
    }

    std::string out;
    out += "struct ";
    const uint32_t typeNameOff = static_cast<uint32_t>(out.size());
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
            const std::string cname = utils::name_to_cpp(vd->members[midx].name);
            out += "    static const " + memberCpp[midx] + " ";
            const uint32_t nameOff = static_cast<uint32_t>(out.size());
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
            mname = std::string(valNode->text());
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
            val_str = memberValueCpp(m_ectx.m_ctx.types(), vd->members[i].type, std::string(valNode->text()));
        }
        out += "const " + memberCpp[i] + " " + vcpp + "::" + utils::name_to_cpp(mname) + "{" + val_str + "};\n";
        ++i;
    }
    // Маппинг имени типа: оффсет из фактического вывода (сразу после "struct "), а не магическая
    // константа. Для синтетических узлов без исходного range маппинг пропускается.
    if (m_ectx.m_ctx.source().mappingActive() && !typeNameRange.begin.isInvalid()) {
        mapDeclaredName(output_idx, typeNameRange, typeNameOff, variant_trust, vcpp);
    }
    m_ectx.m_ctx.source().output_append(output_idx, out);
}

void DeclEmitter::mapDeclaredName(MapperFile output_idx, MapperRange trustRange, uint32_t prefixLen, std::string_view name, std::string_view cppName) {
    // Подавленный маппинг (forward-decl на сайте импорта): mapStart не пушил стек, маппить нечего.
    if (m_ectx.m_ctx.source().mappingSuppressed()) {
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

void DeclEmitter::generateFuncDeclToFile(const FuncDecl& func_node, MapperFile output_idx) {
    // Нативный импорт `<name>(...) := %native...;` - алиас: C++-функция НЕ эмитится.
    // Регистрируем trust-имя → нативное C++-имя; вызовы name(...) будут переписаны в native(...).
    if (func_node.m_isNativeImport) {
        m_ectx.m_nativeImports[std::string(func_node.text())] = func_node.m_nativeName;
        return;
    }
    m_ectx.m_ctx.source().mapStart(func_node.range(), output_idx);

    // Флаг линковки нативной библиотеки из @[link("имя")].
    m_driver.m_type.collectLinkLib(func_node);

    // Точка входа модуля: DSL-макрос `@main` раскрывается в `<имя_модуля>__main__`. Pipeline
    // генерирует `_main.cppt` с `extern int <имя_модуля>__main__(); int main(){ return …; }`,
    // поэтому entry-функция эмитится с СЫРЫМ именем (без манглинга `c_`) и типом возврата `int`.
    const std::string trust_name = std::string(func_node.text());
    const bool isEntry = func_node.m_body && !m_ectx.m_inCppBlock && m_ectx.m_hiddenNamespaceDepth == 0 && trust_name.ends_with("__main__");
    // Function name: манглинг trust-имени в C++-идентификатор (срез '%' у нативных функций).
    // Entry - без манглинга, иначе не совпадёт с `extern int <имя>__main__()` в _main.cppt.
    std::string name = isEntry ? std::string(utils::strip_native_prefix(trust_name)) : utils::name_to_cpp(trust_name);

    // Return type (инклуды типа записываются через emitTypeName). None/Void → "void"; нет аннотации → "void".
    // Явный, но нерезолвящийся тип → emitTypeNameForNode выводит диагностику, прерываем функцию.
    std::string ret_type = "void";
    if (func_node.m_type && func_node.m_type->kind() == ParserToken::Kind::TypeName) {
        const std::string_view rt = func_node.m_type->text();
        if (rt == "Void" || rt == "None") {
            ret_type = "void";
        } else {
            ret_type = m_driver.m_type.emitTypeNameForNode(func_node.m_type.get());
            if (ret_type.empty()) {
                return; // emitTypeNameForNode уже вывел диагностику
            }
        }
    }
    // Entry-функция без явного типа возврата должна быть `int` (иначе не слинкуется
    // `extern int <имя>__main__()` из _main.cppt).
    if (isEntry && func_node.m_type == nullptr) {
        ret_type = "int";
    }

    // Квалификаторы функции из атрибутов. Лидирующие (до типа возврата): FuncConst ->
    // __attribute__((const)), FuncPure -> __attribute__((pure)), FuncConstexpr -> constexpr.
    // Завершающий (после ')'): NoExcept -> noexcept. ReadOnly у функций не обрабатывается.
    std::string lead;
    if (func_node.has_attr(m_ectx.m_ctx.attrs(), attr::FuncConst)) {
        lead += "__attribute__((const)) ";
    }
    if (func_node.has_attr(m_ectx.m_ctx.attrs(), attr::FuncPure)) {
        lead += "__attribute__((pure)) ";
    }
    if (func_node.has_attr(m_ectx.m_ctx.attrs(), attr::FuncConstexpr)) {
        lead += "constexpr ";
    }
    std::string trail;
    if (func_node.has_attr(m_ectx.m_ctx.attrs(), attr::NoExcept)) {
        trail += " noexcept";
    }

    // Нативная декларация (`%...`): правило линковки - без '::' линкуется как C-символ
    // (extern "C", напр. libc/libm `sqrt`, `open`, `abs`); с '::' - C++-линковка (`std::...`).
    // Импорт-алиасы (`name(...) := %sym...;`) вернулись выше - сюда попадают только
    // forward-decl и определения.
    // extern "C" добавляется ТОЛЬКО forward-декларациям (нет тела): это настоящие C-символы.
    // Определения (с телом) - пользовательские C++-функции; extern "C" для них неверен
    // (напр. auto-возврат кортежа несовместим с C-линковкой) и не нужен - при наличии
    // forward-объявления определение наследует C-линковку по правилу [dcl.link].
    if (trust_name.starts_with('%') && !func_node.m_body.has_value() && utils::strip_native_prefix(trust_name).find("::") == std::string::npos) {
        lead = "extern \"C\" " + lead;
    }

    // Parameters
    std::string params_str;
    std::vector<std::pair<const ArgNode*, uint32_t>> param_name_positions; // (node, name offset within signature)
    if (func_node.m_params) {
        const uint32_t sig_prefix =
            static_cast<uint32_t>(lead.length()) + static_cast<uint32_t>(ret_type.length()) + 1 + static_cast<uint32_t>(name.length()) + 1;
        for (size_t i = 0; i < func_node.m_params->size(); ++i) {
            if (i > 0) {
                params_str += ", ";
            }
            auto* param_node = static_cast<const ArgNode*>((*func_node.m_params)[i].get());
            if (!param_node || param_node->kind() != ParserToken::Kind::ArgNode) {
                params_str += "std::any"; // дефектный узел (семантика отсекает) - тип Any
                continue;
            }
            if (param_node->text() == "...") {
                // Вариативность: trust `...` - свойство компилятора (произвольное число
                // аргументов); в C++ это чистая variadic-метка `...` (без имени и типа).
                // C++ требует `...` последним параметром, что и гарантируется грамматикой.
                params_str += "...";
                continue;
            }
            // Param type (инклуды записываются через emitTypeName); None/Void → "void";
            // без аннотации типа → :Any (std::any). Явный, но нерезолвящийся тип → ошибка.
            std::string param_type;
            if (param_node->m_type && param_node->m_type->kind() == ParserToken::Kind::TypeName) {
                const std::string_view pt = param_node->m_type->text();
                if (pt == "Void" || pt == "None") {
                    param_type = "void";
                } else {
                    param_type = m_driver.m_type.emitTypeNameForNode(param_node->m_type.get());
                    if (param_type.empty()) {
                        return; // emitTypeNameForNode уже вывел диагностику - функция невалидна
                    }
                }
            } else if (auto aid = m_ectx.m_ctx.types().findType(type_generic::Any)) {
                // Нетипизированный параметр - тип :Any (std::any); инклуд записывает emitTypeName.
                auto anyName = m_driver.m_type.emitTypeName(*aid, type_generic::Any);
                EXPECT(anyName.has_value() && "untyped parameter: Any type must have a C++ name");
                param_type = std::move(*anyName);
            } else {
                EXPECT(false && "untyped parameter: Any type must be registered");
            }
            // Param name: манглинг trust-имени параметра в C++-идентификатор (a → c_a).
            // Безымянный параметр (только тип) эмитится без имени - C++ это допускает
            // (void f(int32_t)), и к нему нельзя обратиться из тела.
            std::string param_name = utils::name_to_cpp(param_node->text());
            if (param_name.empty()) {
                params_str += param_type;
            } else {
                // Name offset within signature: params_str already holds everything emitted so far
                // (separators, previous params), so the type size is taken directly.
                uint32_t name_pos = sig_prefix + static_cast<uint32_t>(params_str.size()) + static_cast<uint32_t>(param_type.length()) + 1;
                params_str += param_type + " " + param_name;
                param_name_positions.emplace_back(param_node, name_pos);
            }
        }
    }

    // Emit function signature
    std::string sig = std::format("{}{} {}({}){}", lead, ret_type, name, params_str, trail);
    m_ectx.m_ctx.source().output_append(output_idx, sig);

    // Add name mappings (function name + parameter names) for hover links.
    // Имя функции выводится сразу после "<lead>ret_type " (offset = lead.length()+ret_type.length()+1).
    if (!name.empty()) {
        MapperLocation trustFnBegin = m_ectx.m_ctx.source().makeLoc(func_node.range().begin.fileIdx(), func_node.range().begin.offset());
        MapperLocation trustFnEnd = m_ectx.m_ctx.source().makeLoc(
            trustFnBegin.fileIdx(), trustFnBegin.offset() + static_cast<uint32_t>(utils::strip_native_prefix(func_node.text()).size()));
        MapperRange trustFnNameRange(trustFnBegin, trustFnEnd);
        mapDeclaredName(output_idx, trustFnNameRange, static_cast<uint32_t>(lead.length()) + static_cast<uint32_t>(ret_type.length()) + 1, func_node.text(),
                        name);
    }

    // Parameter names: name_pos - оффсет имени от начала сигнатуры (= начала mapStart).
    for (const auto& [param_node, name_pos] : param_name_positions) {
        std::string raw_param = std::string(param_node->text());
        if (raw_param.empty()) {
            continue; // placeholder argN is not backed by a real source name
        }
        std::string cpp_param = utils::name_to_cpp(raw_param);
        mapDeclaredName(output_idx, param_node->range(), name_pos, raw_param, cpp_param);
    }

    // Сигнатура (src [имя, оператор]) смапплена - закрываем её отдельно от тела.
    m_ectx.m_ctx.source().mapStop(func_node.range());

    // Body or forward declaration
    if (func_node.m_body && !m_ectx.m_forwardDeclOnly) {
        // Зеркалируем раскладку исходника: '{' и '}' размещаются по строкам блока,
        // переносы между '{' и первым оператором / последним оператором и '}' зависят
        // от того, на одной ли они строке исходника.
        MapperRange blockRange = func_node.blockRange();

        // Тело функции: convertSeq уже развернул SEQUENCE-контейнер тела, поэтому
        // func_node.m_body - плоский список операторов; пользовательские блоки остаются
        // ScopeBlock-узлами и оборачиваются visit_ScopeBlock. Отдельного сплющивания не нужно.
        // Entry-функция без явного `return` в конце получает `return 0;` перед '}' (иначе
        // «control reaches end of non-void function»).
        std::string beforeClose;
        if (isEntry && !bodyEndsWithReturn(*func_node.m_body)) {
            beforeClose = "return 0;";
        }
        // Trust-контракты функции (--solver-mode=assert): пред-условия (kind=Pre) и утверждение на
        // функции (kind=Assert) - проверяются при входе; пост-условия (kind=Post) - перед каждым
        // `return <value>` (не-void) со связыванием возвращаемого значения, либо в конце тела (void).
        std::vector<AstNodePtr> preTrust, postTrust;
        for (const auto& t : func_node.m_trust) {
            if (!t) {
                continue;
            }
            const auto* tc = dynamic_cast<const TrustContract*>(t.get());
            if (tc && tc->kind == PropertyKind::Post) {
                postTrust.push_back(t);
            } else {
                preTrust.push_back(t);
            }
        }
        // Не-void функция: пост-условия эмитятся visit_ReturnStmt перед каждым return (имя функции
        // = возвращаемое значение; ReturnStmt сам знает свою функцию через m_funcDecl).
        // Void-функция: пост-условие эмитится в конце тела (перед '}').
        const bool isVoidFunc = (ret_type == "void");
        m_ectx.m_scopeStack.push_back({m_ectx.indentLevel()});
        m_driver.m_stmt.emitBlockBodyToFile(*func_node.m_body, blockRange, output_idx, /*mapBlock=*/true, beforeClose, /*afterOpen=*/"",
                                            preTrust.empty() ? nullptr : &preTrust, (isVoidFunc && !postTrust.empty()) ? &postTrust : nullptr);
        m_ectx.m_scopeStack.pop_back();
    } else {
        // Forward declaration
        m_ectx.m_ctx.source().output_append(output_idx, ";");
    }

    // Экспортируются ОПРЕДЕЛЕНИЯ функций на верхнем уровне модуля в НЕ анонимной области имён
    // (квалифицированно для 'ns::'); из '_' и локальных, а также forward-объявления
    // (нет тела → нет определения, `&::name` не связался бы) - не экспортируются.
    if (func_node.m_body && !name.empty() && !m_ectx.m_inCppBlock && m_ectx.m_hiddenNamespaceDepth == 0) {
        m_ectx.m_exports.push_back({std::string(func_node.text()), m_ectx.qualifiedCppName(name), buildTrustForwardDecl(func_node)});
    }
}

void DeclEmitter::visit_ModuleDecl(const ModuleNode& n) {
    if (n.isImport()) {
        // Сайт импорта `\module(mod, masks)`: вместо полного тела - только forward-decl
        // экспортируемого интерфейса (прототипы функций / extern переменных / алиасы типов).
        // Определения живут в отдельном .cppt модуля и связываются линковщиком.
        emitModuleImportDecls(n);
        return;
    }
    // Корневой модуль (главный файл) - полное тело.
    m_driver.emitSequenceBody(n, m_ectx.m_out);
}

// -- Эмиссия forward-decl экспортов на сайте импорта модуля --

void DeclEmitter::emitModuleImportDecls(const ModuleNode& n) {
    // Множество экспортируемых термов (из отфильтрованного интерфейса сайта импорта).
    // Сопоставление по указателям: m_ectx.m_exports содержат ТЕ ЖЕ TermPtr, что и узел-декларация
    // в m_body, поэтому forward-decl эмитится ровно для отобранных экспортов.
    std::set<const Term*> exportTerms;
    for (const auto& t : n.exports()) {
        if (t) {
            exportTerms.insert(t.get());
        }
    }
    if (exportTerms.empty()) {
        return; // ничего не импортируется (или модуль без экспортов)
    }
    // В forward-режиме объявления подавляют определения (extern/прототип).
    // ВЕСЬ импорт мапится ОДНИМ фрагментом - на месте оператора загрузки модуля (`\module(...)`,
    // n.range()), как при раскрытии макроса. Внутренние forward-decl эмитятся внутри этого одного
    // маппинга; их собственные per-node маппинги подавляются, чтобы не заявлять диапазоны объявлений
    // модуля - эти диапазоны мапит отдельный `.cppt` модуля (иначе коллизия trustKey).
    MapperScope importScope(m_ectx.m_ctx.source(), n.range(), m_ectx.m_out);
    m_ectx.m_forwardDeclOnly = true;
    m_ectx.m_ctx.source().suppressMapping();
    emitImportScope(n.m_body, exportTerms, m_ectx.m_out);
    m_ectx.m_ctx.source().resumeMapping();
    m_ectx.m_forwardDeclOnly = false;
}

void DeclEmitter::emitImportScope(const std::vector<AstNodePtr>& body, const std::set<const Term*>& terms, MapperFile out) {
    for (const auto& node : body) {
        if (!node) {
            continue;
        }
        if (node->kind() == ParserToken::Kind::ScopeBlock) {
            const auto& sb = static_cast<const ScopeBlock&>(*node);
            // Анонимная область `_` и безымянный кодовый блок - не экспортируются.
            if (sb.is_hidden() || sb.is_anonymous()) {
                continue;
            }
            const std::string_view text = sb.text();
            // Глобальная область `::` - содержимое без обёртки.
            if (text == "::") {
                emitImportScope(sb.m_body, terms, out);
                continue;
            }
            // Именованная область `ns::` - оборачиваем в `namespace ns { ... }`.
            const std::string nsName = utils::name_to_cpp(m_ectx.namespaceCppName(text));
            m_ectx.m_namespaceStack.push_back(nsName);
            m_ectx.m_scopeStack.push_back({m_ectx.indentLevel() + 1});
            m_ectx.m_ctx.source().output_append(out, m_ectx.indentPrefix() + "namespace " + nsName + " {\n");
            emitImportScope(sb.m_body, terms, out);
            m_ectx.m_scopeStack.pop_back();
            m_ectx.m_ctx.source().output_append(out, m_ectx.indentPrefix() + "}\n");
            m_ectx.m_namespaceStack.pop_back();
            continue;
        }

        // Forward-decl только для отобранных экспортов (сопоставление по терму-источнику).
        const TermPtr& srcTerm = node->term();
        if (!srcTerm || terms.find(srcTerm.get()) == terms.end()) {
            continue;
        }
        m_ectx.m_ctx.source().output_append(out, m_ectx.indentPrefix());
        switch (node->kind()) {
        case ParserToken::Kind::VarDecl:
            generateVarDeclToFile(static_cast<const VarDecl&>(*node), out);
            break;
        case ParserToken::Kind::FuncDecl:
            generateFuncDeclToFile(static_cast<const FuncDecl&>(*node), out);
            break;
        case ParserToken::Kind::TypeDecl:
            generateTypeDeclToFile(static_cast<const Binary&>(*node), out);
            break;
        default:
            break; // прочие экспорт-формы пока не эмитятся
        }
        m_ectx.m_ctx.source().output_append(out, "\n");
    }
}

// -- Реконструкция Trust-синтаксиса предварительного объявления экспортируемого узла --

std::string DeclEmitter::buildTrustForwardDecl(const AstNodeBase& node) const {
    switch (node.kind()) {
    case ParserToken::Kind::VarDecl: {
        const auto& v = static_cast<const VarDecl&>(node);
        std::string s(v.text());
        if (v.m_type) {
            s += ":";
            s += std::string(v.m_type->text()); // e.g. "Int32" → ":Int32"
        }
        s += " := ...;";
        return s;
    }
    case ParserToken::Kind::FuncDecl: {
        const auto& f = static_cast<const FuncDecl&>(node);
        std::string s(f.text()); // e.g. "%func"
        s += "(";
        if (f.m_params) {
            bool first = true;
            for (const auto& p : *f.m_params) {
                if (!p || p->kind() != ParserToken::Kind::ArgNode) {
                    continue;
                }
                const auto& pd = static_cast<const ArgNode&>(*p);
                if (!first) {
                    s += ", ";
                }
                first = false;
                s += std::string(pd.text());
                if (pd.m_type) {
                    s += ":";
                    s += std::string(pd.m_type->text());
                }
            }
        }
        s += ")";
        if (f.m_type) {
            s += ":";
            s += std::string(f.m_type->text());
        }
        s += " := ...;";
        return s;
    }
    case ParserToken::Kind::TypeDecl: {
        const auto& b = static_cast<const Binary&>(node);
        std::string s = (b.m_left) ? std::string(b.m_left->text()) : std::string(node.text());
        s += " ::= ...;";
        return s;
    }
    default:
        return std::string(node.text()) + " := ...;";
    }
}

// Объявления.
void DeclEmitter::visit_VarDecl(const VarDecl& n) {
    generateVarDeclToFile(n, m_ectx.m_out);
}

void DeclEmitter::visit_FuncDecl(const FuncDecl& n) {
    generateFuncDeclToFile(n, m_ectx.m_out);
}

// Binary: TypeDecl → объявление типа; прочие → expression statement.
void DeclEmitter::visit_TypeDecl(const Binary& n) {
    generateTypeDeclToFile(n, m_ectx.m_out);
}

// Бинарные statement/expression kinds - единая генерация (m_exprDepth различает контекст).
void DeclEmitter::visit_NameDecl(const Binary& n) {
    m_driver.m_expr.emitBinaryStmtOrExpr(n);
}

void DeclEmitter::visit_EnumDecl(const Sequence&) {
    // Enum-объявления теперь - TypeDecl с Enum-аннотированным DictLiteral-RHS; реальная
    // эмиссия - в generateTypeDeclToFile → emitEnumStruct. Узел EnumDecl не производится.
}

void DeclEmitter::visit_EnumMember(const Sequence&) {
}

void DeclEmitter::visit_StructDecl(const Sequence&) {
}

void DeclEmitter::visit_StructField(const Sequence&) {
}
} // namespace trust
