// Generated: src/transpiler/type_emit.cpp
#include "transpiler/type_emit.hpp"
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

void TypeEmitter::collectLinkLib(const AstNodeAttr& node) {
    const AttrPool& pool = m_ectx.m_ctx.attrs();
    auto link_id = pool.lookup(attr::Link);
    if (!link_id.has_value() || !node.has_attr(*link_id)) {
        return;
    }
    const std::vector<std::string>* args = node.attr_args(*link_id);
    // @[link("имя")] - ровно один аргумент (имя библиотеки).
    if (!args || args->size() != 1 || (*args)[0].empty()) {
        return;
    }
    m_ectx.m_linkLibs.insert("-l" + (*args)[0]);
}

// Вставка preprocessor-инклуда (если требуется типом) в начало выходного файла.
// Инклуд рантайм-типа помечается ведущим '@' (см. TypeRegistry::preprocInclude):
// '@' срезается, путь заголовка запоминается в m_runtimeHeaders (ТОЛЬКО реально
// использованные), а в файл пишется настоящая директива #include "<path>".
void TypeEmitter::recordRequiredInclude(std::string_view include) const {
    if (include.empty()) {
        return;
    }
    if (include.front() == '@') {
        include.remove_prefix(1);
        const std::string bare(include);
        if (m_ectx.m_runtimeHeaders.count(bare)) {
            return; // уже записан
        }
        m_ectx.m_runtimeHeaders.insert(bare);
        m_ectx.m_requiredIncludes.insert("#include \"" + bare + "\"");
        return;
    }
    m_ectx.m_requiredIncludes.insert(std::string(include));
}

// -- МЕХАНИЗМ №1 - ПО ТИПУ (TypeRegistry): сбор ТИПОВ во время обхода AST, инклуды ПОСЛЕ --
// Во время эмиссии resolveCppTypeId/recordUsedType только отмечают использованные типы
// (канонические TypeId) в m_usedTypes. Сами директивы инклудов из типов формируются
// ПОСЛЕ полного обхода AST (collectTypeIncludes → emitCollectedIncludes).
void TypeEmitter::recordUsedType(TypeId type_id) const {
    m_ectx.m_usedTypes.insert(m_ectx.m_ctx.types().getCanonicalTypeId(type_id));
}

void TypeEmitter::collectTypeIncludes() const {
    for (TypeId id : m_ectx.m_usedTypes) {
        for (const auto& inc : m_ectx.m_ctx.types().getPreprocIncludes(id)) {
            recordRequiredInclude(inc);
        }
        // std::shared_ptr/std::weak_ptr/std::unique_ptr (виды ссылок) требуют <memory>.
        const RefType rt = getRefType(getKindFromId(id));
        if (rt == RefType::kShared || rt == RefType::kWeak || rt == RefType::kUnique) {
            recordRequiredInclude("#include <memory>");
        }
    }
}

std::optional<std::string> TypeEmitter::emitTypeName(TypeId type_id, std::string_view displayName) {
    // МЕХАНИЗМ №1 - ПО ТИПУ: resolveCppTypeId только отмечает тип как использованный
    // (recordUsedType → m_ectx.m_usedTypes); инклуды из типов формируются ПОСЛЕ обхода AST.
    auto resolved = resolveCppTypeId(type_id, displayName);
    if (!resolved) {
        return std::nullopt;
    }
    return std::move(resolved->first);
}

std::string TypeEmitter::emitTypeNameForNode(const AstNodeBase* type_node) {
    if (!type_node || type_node->kind() != ParserToken::Kind::TypeName) {
        return ""; // нет типа-аннотации - caller решает (напр. параметр без типа → auto)
    }
    // Резолвим TypeId, затем применяем ортогональные квалификаторы из атрибутов узла ТИПА
    // (`fmt: @[reftype(ptr)]@ StrChar^`): ReadOnly → const, reftype → вид ссылки. Так тип
    // параметра/переменной с атрибутами получает то же C++-имя, что вычислила семантика.
    auto type_id = resolveTypeIdByName(type_node->text());
    if (!type_id.has_value()) {
        m_ectx.m_ctx.report(type_node->range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", type_node->text());
        return "";
    }
    TypeId applied = *type_id;
    // Тип-определение массива `:Elem[3]`/`:Elem[3,4]`: размерности из `[...]` (IdentType::dims)
    // превращают базовый тип в структурный Array<Elem,dims> - так семантика (resolveType) и
    // кодогенерация согласованы (N-D - без кодогенерации: диагностика «не реализовано» ниже).
    if (const auto* it = dynamic_cast<const IdentType*>(type_node)) {
        if (it->dims() && !it->dims()->empty()) {
            std::vector<uint64_t> dims;
            for (const auto& d : *it->dims()) {
                if (!d || d->kind() != ParserToken::Kind::IntLiteral) {
                    continue;
                }
                unsigned long long v = 0;
                try {
                    v = std::stoull(std::string(d->text()), nullptr, 0);
                } catch (...) {
                    v = 0;
                }
                dims.push_back(v);
            }
            if (!dims.empty()) {
                // Определение типа массива `:Elem[3]`: изменяемый массив (std::vector) с известной
                // размерностью (согласовано с семантикой resolveType). N-D - без кодогенерации.
                applied = m_ectx.m_ctx.types().getOrCreateArrayType(*type_id, std::move(dims));
            }
        }
    }
    if (const AstNodeAttr* a = type_node->as_attr()) {
        const AttrPool& pool = m_ectx.m_ctx.attrs();
        if (a->has_attr(pool, attr::ReadOnly)) {
            applied = withConst(applied);
        }
        if (auto rid = pool.lookup(attr::Reftype); rid.has_value() && a->has_attr(*rid)) {
            if (const std::vector<std::string>* args = a->attr_args(*rid); args && !args->empty()) {
                if (auto rk = refTypeFromString(args->front())) {
                    applied = m_ectx.m_ctx.types().applyRefType(applied, *rk);
                }
            }
        }
    }
    // Многомерное определение массива `:Bool[3,4]`: тип регистрируется (семантика), но генерация
    // C++ не реализована («не реализовано» - только на кодогенерации, как у вложенных литералов).
    if (isMultiDimArray(applied, m_ectx.m_ctx.types())) {
        m_ectx.m_ctx.report(type_node->range(), diag::DiagId::ParseError, "многомерные массивы пока не реализованы: транслируются как тензоры (LibTorch)");
        return "";
    }
    // resolveCppTypeId (m_ectx.m_resolvedTypes-устойчивый резолв по имени) записывает все инклуды типа.
    if (auto resolved = resolveCppTypeId(applied, type_node->text())) {
        return std::move(resolved->first);
    }
    // Fallback запрещён: нерезолвящееся имя типа - ВСЕГДА ошибка с обязательной диагностикой.
    m_ectx.m_ctx.report(type_node->range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", type_node->text());
    return "";
}

// Рантайм-символ по типизированному идентификатору: заголовки из компайлтайм-таблицы
// (types/runtime_symbols.hpp). Enum вместо строки - опечатка в имени символа невозможна.
// ЕДИНСТВЕННЫЙ способ записи заголовков рантайм-символа.
void TypeEmitter::recordRuntimeSymbolHeaders(RuntimeSymbolId id) const {
    for (const auto& h : runtimeSymbolHeaders(id)) {
        recordRequiredInclude(h);
    }
}

// Скан текста EMBED-вставки ({% %}) на имена рантайм-символов (substring) и запись их
// заголовков через recordRuntimeSymbolHeaders(id). Для EMBED типа нет - это единственный
// способ определить нужные заголовки. Отдельный хелпер, не перегрузка записи.
void TypeEmitter::recordRuntimeSymbolsInText(std::string_view text) const {
    for (size_t i = 0; i < static_cast<size_t>(RuntimeSymbolId::kCount); ++i) {
        const auto id = static_cast<RuntimeSymbolId>(i);
        const std::string_view sym = runtimeSymbolName(id);
        if (text.find(sym) != std::string_view::npos) {
            recordRuntimeSymbolHeaders(id);
        }
    }
}

void TypeEmitter::emitCollectedIncludes(MapperFile output_idx) {
    for (const auto& inc : m_ectx.m_requiredIncludes) {
        m_ectx.m_ctx.source().output_prepend(output_idx, inc);
    }
}

std::optional<TypeId> TypeEmitter::resolveTypeIdByName(std::string_view trustName) const {
    // Если задана разрешённая семантикой таблица символов - используем её TypeId как
    // единый источник с анализом (скоуп-стек к моменту кодогенерации сброшен к глобальному;
    // для builtin-имён в скоупе записи нет → fallback на реестр).
    if (m_ectx.m_resolvedTypes) {
        if (const Symbol* s = m_ectx.m_resolvedTypes->resolve(trustName)) {
            if (s->type != INVALID_TYPE_ID) {
                return s->type;
            }
        }
    }
    return m_ectx.m_ctx.types().findType(trustName);
}

std::optional<std::pair<std::string, std::string_view>> TypeEmitter::resolveCppType(std::string_view trustName) const {
    auto type_id = resolveTypeIdByName(trustName);
    if (!type_id.has_value()) {
        return std::nullopt;
    }
    return resolveCppTypeId(*type_id, trustName);
}

// Разрешение по уже известному TypeId. displayName - trust-имя, сохраняемое для пользовательского
// алиаса; встроенные типы и встроенные алиасы - каноническое C++-имя.
std::optional<std::pair<std::string, std::string_view>> TypeEmitter::resolveCppTypeId(TypeId type_id, std::string_view displayName) const {
    TypeId canonical = m_ectx.m_ctx.types().getCanonicalTypeId(type_id);
    // Кортеж - структурный/компайлтайм-тип без единого runtime-представления: всегда конкретный
    // std::tuple, тип которого выводится из инициализатора (std::make_tuple). Голого C++-имени
    // у типа `:Tuple` нет → объявление переменной эмитится как `auto`. Плоский `:Tuple` и
    // структурный кортеж (TupleTypeData) - оба.
    if (canonical == m_ectx.m_ctx.types().getType(type_category::Tuple) || m_ectx.m_ctx.types().isTypeDataKind(canonical, TypeDataKind::kTuple)) {
        return std::make_pair(std::string("auto"), std::string_view{});
    }
    // Параметризованный Range<Elem> (структурный, TypeDataKind::kRange): конкретный C++-шаблон
    // `trust::Range<ElemCpp>`, где ElemCpp - элементный тип из RangeTypeData (рекурсивно через
    // emitTypeName). В отличие от абстрактного `:Range` (ветка ниже → auto) здесь имя конкретно.
    if (m_ectx.m_ctx.types().isRangeType(canonical)) {
        const TypeId elem = m_ectx.m_ctx.types().rangeElementType(canonical);
        std::string elemCpp;
        if (elem != INVALID_TYPE_ID) {
            if (auto en = resolveCppTypeId(elem, "Range.Element")) { // рекурсивно (const-метод)
                elemCpp = std::move(en->first);
            }
        }
        if (elemCpp.empty()) {
            elemCpp = "std::any";
        }
        recordUsedType(canonical); // включит @trust/range.hpp + dict/rational (getPreprocIncludes)
        return std::make_pair("trust::Range<" + elemCpp + ">", m_ectx.m_ctx.types().getPreprocInclude(canonical));
    }
    // Диапазон `:Range` - абстрактный универсальный тип (как :Dict), конкретное C++-представление
    // `trust::Range<Elem>` (шаблон по элементному типу) выводится из инициализатора-литерала
    // `..` в visit_RangeExpr. Голого C++-имени у `:Range` нет → объявление переменной эмитится
    // как `auto`, инициализатор задаёт конкретный `trust::Range<Elem>`. (Модель: кортеж `:Tuple`.)
    if (canonical == m_ectx.m_ctx.types().getType(type_category::Range)) {
        return std::make_pair(std::string("auto"), std::string_view{});
    }
    // Параметризованный Array<Elem> (структурный, ArrayTypeData): конкретный C++-шаблон
    // `std::vector<ElemCpp>` (mutable) или `std::array<ElemCpp,N>` (константная/фиксированная).
    // ElemCpp - элементный тип из ArrayTypeData (рекурсивно через resolveCppTypeId).
    if (m_ectx.m_ctx.types().isArrayType(canonical)) {
        const TypeId elem = m_ectx.m_ctx.types().arrayElementType(canonical);
        std::string elemCpp;
        if (elem != INVALID_TYPE_ID) {
            if (auto en = resolveCppTypeId(elem, "Array.Element")) { // рекурсивно (const-метод)
                elemCpp = std::move(en->first);
            }
        }
        if (elemCpp.empty()) {
            elemCpp = "std::any";
        }
        const bool cst = typeIsConst(type_id); // kConstFlag-бит в TypeId (`:Array^`), как у любых типов
        if (cst) {
            uint64_t n = 0;
            const auto& dims = m_ectx.m_ctx.types().arrayDimensions(canonical);
            if (!dims.empty()) {
                n = dims.front();
            }
            recordRequiredInclude("#include <array>");
            return std::make_pair("std::array<" + elemCpp + ", " + std::to_string(n) + ">", std::string_view{});
        }
        recordRequiredInclude("#include <vector>");
        return std::make_pair("std::vector<" + elemCpp + ">", std::string_view{});
    }
    // Массив `:Array` - абстрактный универсальный тип (как :Dict/:Range): конкретное
    // C++-представление `std::vector<Elem>` выводится из инициализатора-литерала (visit_ArrayInit).
    // Голого C++-имени нет → объявление переменной эмитится как `auto`.
    if (canonical == m_ectx.m_ctx.types().getType(type::Array)) {
        return std::make_pair(std::string("auto"), std::string_view{});
    }
    // Константность (kConstFlag) - ортогональный квалификатор, учитывается здесь: префикс `const `
    // добавляется к базовому имени (каноника снимает бит, поэтому getCppTypeName(canonical) его
    // не видит). Для пользовательских алиасов const добавляется к trust-имени.
    const bool isConst = typeIsConst(type_id);

    // Спецправило: узкая строка как указатель на константные данные
    // (`fmt: @[reftype(ptr)]@ StrChar^`) - это C-строка `const char*` (а НЕ `const std::string*`).
    // Такой тип совместим с C-функциями, принимающими форматную строку (printf и др.); при
    // вызове StrChar-аргумент конвертируется в .c_str() (см. visit_CallExpr).
    if (isConst && getRefType(getKindFromId(canonical)) == RefType::kPtr) {
        TypeKind baseKind = withRefType(getKindFromId(canonical), RefType::kValue);
        TypeId baseId = (static_cast<uint64_t>(baseKind) << 32) | (static_cast<uint32_t>(getIndexFromId(canonical)) & 0xFFFFFFFFu);
        if (m_ectx.m_ctx.types().getCanonicalTypeId(baseId) == m_ectx.m_ctx.types().getType(type::StrChar)) {
            return std::make_pair(std::string("const char*"), m_ectx.m_ctx.types().getPreprocInclude(canonical));
        }
    }

    // Enum-тип (Group::kEnums, EnumTypeData): C++-имя - манглинг trust-имени (самодостаточная
    // struct, объявленная visit_EnumDecl); у типа нет единого preproc-include.
    if (m_ectx.m_ctx.types().isTypeDataKind(canonical, TypeDataKind::kEnum)) {
        std::string name = utils::name_to_cpp(displayName);
        if (isConst) {
            name = "const " + name;
        }
        recordUsedType(canonical);
        return std::make_pair(std::move(name), std::string_view{});
    }
    // Variant-тип (Group::kVariants, VariantTypeData): C++-имя - манглинг trust-имени (struct c_Value).
    if (m_ectx.m_ctx.types().isTypeDataKind(canonical, TypeDataKind::kVariant)) {
        std::string name = utils::name_to_cpp(displayName);
        if (isConst) {
            name = "const " + name;
        }
        recordUsedType(canonical);
        return std::make_pair(std::move(name), std::string_view{});
    }

    auto cpp_name = m_ectx.m_ctx.types().getCppTypeName(canonical);
    if (!cpp_name) {
        return std::nullopt;
    }
    std::string_view include = m_ectx.m_ctx.types().getPreprocInclude(canonical);

    // МЕХАНИЗМ №1 - ПО ТИПУ: отмечаем тип как использованный (не файлы!). Инклуды из собранных
    // типов формируются ПОСЛЕ обхода AST (collectTypeIncludes), а не в момент резолва.
    recordUsedType(canonical);

    // Пользовательский алиас (зарегистрирован семантикой) сохраняет своё trust-имя в коде;
    // встроенные типы и встроенные алиасы (Integer, String, Char...) маппятся на каноническое
    // C++-имя (int64_t, std::string...). Include всегда берётся у канонического (базового) типа.
    // Признак пользовательского типа - явный (isUserDefinedType), а не по sourceRange.
    if (m_ectx.m_ctx.types().isUserDefinedType(type_id)) {
        // Пользовательский алиас сохраняет своё trust-имя в C++-коде, но в виде корректного
        // C++-идентификатора (манглинг: MyInt → c_MyInt), чтобы совпадать с объявлением `using c_MyInt = ...`.
        std::string name = utils::name_to_cpp(displayName);
        if (isConst) {
            name = "const " + name;
        }
        return std::make_pair(std::move(name), include);
    }
    if (isConst) {
        return std::make_pair(std::string("const ") + *cpp_name, include);
    }
    return std::make_pair(std::move(*cpp_name), include);
}
} // namespace trust
