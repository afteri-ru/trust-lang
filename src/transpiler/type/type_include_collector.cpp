#include "transpiler/type/type_include_collector.hpp"
#include "transpiler/type/cpp_type_renderer.hpp"
#include "transpiler/type/type_cpp_facts_view.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_args.hpp"
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
#include "target/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/call.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"
#include <format>
#include <memory>
#include <set>
#include <vector>

namespace trust {

void TypeIncludeCollector::collectLinkLib(const AstNodeAttr& node) {
    // @[link("имя")] - ТРЕБУЕТСЯ ровно один аргумент (имя библиотеки); иначе атрибут не применяется.
    const auto link_id = m_ectx.m_services.attrs().lookup(attr::Link);
    if (!link_id.has_value() || !node.has_attr(*link_id)) {
        return;
    }
    const std::vector<std::string>* args = node.attr_args(*link_id);
    if (args == nullptr || args->size() != 1 || args->at(0).empty()) {
        return;
    }
    m_ectx.m_accumulators.linkLibs.insert("-l" + args->at(0));
}

// Сбор зависимых C++-заголовков из @[include("header")@] на нативной декларации/типе (все
// аргументы). Форма директивы - единая (attrs::includeDirective, контракт attr::Include);
// пакетный '@'-заголовок доходит до recordRequiredInclude в исходной форме (он сам срезает '@').
void TypeIncludeCollector::collectInclude(const AstNodeAttr& node) {
    const auto inc_id = m_ectx.m_services.attrs().lookup(attr::Include);
    if (!inc_id.has_value() || !node.has_attr(*inc_id)) {
        return;
    }
    const std::vector<std::string>* args = node.attr_args(*inc_id);
    if (args == nullptr) {
        return;
    }
    for (const std::string& header : *args) {
        if (!header.empty()) {
            recordRequiredInclude(includeDirective(header));
        }
    }
}

// Вставка preprocessor-инклуда (если требуется типом) в начало выходного файла.
// Инклуд рантайм-типа помечается ведущим '@' (см. TypeSystem::preprocInclude):
// '@' срезается, путь заголовка запоминается в m_accumulators.packageHeaders (ТОЛЬКО реально
// использованные), а в файл пишется настоящая директива #include "<path>".

void TypeIncludeCollector::recordRequiredInclude(std::string_view include) const {
    if (include.empty()) {
        return;
    }
    // Собственный заголовок модуля: при генерации его же интерфейса include типа «из себя»
    // не эмитится (одна единица трансляции/заголовок; `#pragma once` не нужен).
    if (!m_ectx.m_output.ownHeaderInclude.empty() && include == m_ectx.m_output.ownHeaderInclude) {
        return;
    }
    if (include.front() == '@') {
        include.remove_prefix(1);
        const std::string bare(include);
        if (m_ectx.m_accumulators.packageHeaders.count(bare)) {
            return; // уже записан
        }
        m_ectx.m_accumulators.packageHeaders.insert(bare);
        m_ectx.m_accumulators.requiredIncludes.insert("#include \"" + bare + "\"");
        return;
    }
    m_ectx.m_accumulators.requiredIncludes.insert(std::string(include));
}

// Во время эмиссии resolveCppTypeId/recordUsedType только отмечают использованные типы
// (канонические TypeId) в m_accumulators.usedTypes. Сами директивы инклудов из типов формируются
// ПОСЛЕ полного обхода AST (collectTypeIncludes → emitCollectedIncludes).

void TypeIncludeCollector::recordUsedType(TypeId type_id) const {
    const TypeId canonical = m_ectx.m_services.readTypes().store().desugaredTypeId(type_id);
    // Порядок первого использования — порядок обхода HIR (порядок объявлений), поэтому
    // производные POD-проверки байт-стабильны и не зависят от индексной identity `TypeId` (R42).
    if (m_ectx.m_accumulators.usedTypes.insert(canonical).second) {
        recordStructPodAssert(canonical);
    }
    // Нативный класс/шаблон может нести @[link(...)] на объявлении, реализация builtin —
    // `@[link]` в `.src`. Применяется ON-USE: импорт модуля сам по себе не добавляет link-опции.
    // `linkLibsOf` — единый аккумулятор целевых библиотек (R1).
    for (const std::string& lib : m_ectx.cppFactsView().linkLibsOf(canonical)) {
        if (!lib.empty()) {
            m_ectx.m_accumulators.linkLibs.insert("-l" + lib);
        }
    }
}

void TypeIncludeCollector::collectTypeIncludes() const {
    // Обход с рекурсией в структурные ссылочные узлы (RefTypeData): у узла пустые
    // preprocIncludes, а инклуды его детей (pointee/deleter) должны попасть в вывод
    // (напр. unique<T,D>: T и D - нативные классы с @[include]/@stdlib/resource.hpp).
    std::vector<TypeId> work(m_ectx.m_accumulators.usedTypes.begin(), m_ectx.m_accumulators.usedTypes.end());
    std::set<TypeId> seen;
    while (!work.empty()) {
        const TypeId id = work.back();
        work.pop_back();
        if (!seen.insert(id).second) {
            continue;
        }
        for (const auto& inc : m_ectx.cppFactsView().preprocIncludesOf(id)) {
            recordRequiredInclude(inc);
        }
        // Рантайм-заголовки вида ссылки - единый источник (refTypeRuntimeIncludes); sync-обёртка
        // определяется наличием РЕАЛЬНОГО типа политики в узле (RefTypeData::accessPolicyType).
        const auto* rd = m_ectx.m_services.readTypes().store().getTypeDataAs<RefTypeData>(id);
        const bool hasSync = rd != nullptr && rd->accessPolicyType != INVALID_TYPE_ID;
        const RefType rt = typeRefOf(id);
        for (std::string_view inc : refTypeRuntimeIncludes(rt, hasSync)) {
            recordRequiredInclude(inc);
        }
        // Структурный ссылочный узел: инклуды pointee, deleter и политики доступа.
        if (rd != nullptr) {
            if (rd->pointeeType != INVALID_TYPE_ID) {
                work.push_back(rd->pointeeType);
            }
            if (rd->deleterType != INVALID_TYPE_ID) {
                work.push_back(rd->deleterType);
            }
            if (rd->accessPolicyType != INVALID_TYPE_ID) {
                work.push_back(rd->accessPolicyType);
            }
        }
    }
}

// POD-проверка для КОНКРЕТНОЙ инстанциации Struct-шаблона (сам шаблон assert НЕ получает:
// static_assert над шаблоном зависит от T). Регистрируется при ПЕРВОМ использовании типа, поэтому
// порядок проверок = порядок объявлений (R42).

void TypeIncludeCollector::recordStructPodAssert(TypeId id) const {
    if (!m_ectx.m_services.readTypes().store().isStructType(id)) {
        return;
    }
    const auto instance = m_ectx.m_services.readTypes().store().instanceOf(id);
    if (!instance) {
        return; // обычный Struct - assert уже эмитирован при объявлении
    }
    auto cpp = CppTypeRenderer(m_ectx.m_services.readTypes(), m_ectx.cppFactsView()).render(id);
    if (!cpp) {
        return;
    }
    recordRequiredInclude("#include <type_traits>");
    const std::string tmplName(m_ectx.m_services.readTypes().printer().typeNameOf(instance->templateOf));
    m_ectx.m_accumulators.structPodAsserts.push_back(podAssertText(*cpp, tmplName));
}

void TypeIncludeCollector::emitStructPodAsserts(MapperFile output_idx) const {
    for (const std::string& a : m_ectx.m_accumulators.structPodAsserts) {
        m_ectx.m_services.source().output_append(output_idx, a + "\n");
    }
}

void TypeIncludeCollector::recordRuntimeSymbolHeaders(RuntimeSymbolId id) const {
    for (const auto& h : runtimeSymbolHeaders(id)) {
        recordRequiredInclude(h);
    }
}

// Скан текста EMBED-вставки ({% %}) на имена рантайм-символов (substring) и запись их
// заголовков через recordRuntimeSymbolHeaders(id). Для EMBED типа нет - это единственный
// способ определить нужные заголовки. Отдельный хелпер, не перегрузка записи.

void TypeIncludeCollector::recordRuntimeSymbolsInText(std::string_view text) const {
    for (size_t i = 0; i < static_cast<size_t>(RuntimeSymbolId::kCount); ++i) {
        const auto id = static_cast<RuntimeSymbolId>(i);
        const std::string_view sym = runtimeSymbolName(id);
        if (text.find(sym) != std::string_view::npos) {
            recordRuntimeSymbolHeaders(id);
        }
    }
}

void TypeIncludeCollector::emitCollectedIncludes(MapperFile output_idx) {
    for (const auto& inc : m_ectx.m_accumulators.requiredIncludes) {
        m_ectx.m_services.source().output_prepend(output_idx, inc);
    }
}

} // namespace trust
