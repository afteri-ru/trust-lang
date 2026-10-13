// src/types/registry/builtin_registry.cpp
// Зона встроенных определений и рантайм-символов.
#include "types/registry/builtin_registry.hpp"

#include "types/group.hpp"
#include "types/names/storage_key.hpp"
#include "types/names/type_names.hpp"
#include "types/registry/type_store.hpp"
#include "types/type_read.hpp"
#include "utils/error.hpp"

#include <variant>

namespace trust {

namespace {

/// Заголовок C++-представления функционального значения (`std::function<...>`).
constexpr std::string_view kFunctionalInclude = "#include <functional>";

} // namespace

TypeId BuiltinRegistry::define(const TypeReadModel& read, std::string_view name, std::string_view cppName, std::vector<std::string> preprocIncludes,
                               std::vector<std::string> libs) {
    // Имя резолвится чтением; определение привязывается к существующему предобъявленному типу.
    const auto id = read.findType(name);
    if (!id.has_value() || !read.store().isBuiltinType(*id)) {
        return INVALID_TYPE_ID;
    }
    define(*id, cppName, std::move(preprocIncludes), std::move(libs));
    return *id;
}

void BuiltinRegistry::define(TypeId id, std::string_view cppName, std::vector<std::string> preprocIncludes, std::vector<std::string> libs) {
    EXPECT(m_store.isBuiltinType(id) && "BuiltinRegistry::define: only a predeclared builtin type can be defined");
    BuiltinDefinition def;
    def.cppName = std::string(cppName);
    def.preprocIncludes = std::move(preprocIncludes);
    def.libs = std::move(libs);
    // Храним на КАНОНИЧЕСКОМ типе: алиас (`String`) и цель (`StrChar`) дают одно определение.
    m_definitions[m_store.desugaredTypeId(id)] = std::move(def);
}

const BuiltinDefinition* BuiltinRegistry::getBuiltinDefinition(TypeId id) const noexcept {
    // Определение хранится на КАНОНИЧЕСКОМ предобъявленном типе; id может нести
    // пер-Symbol флаги (Inferred/Const) или быть алиасом (String→StrChar).
    const TypeId canonical = m_store.desugaredTypeId(id);
    const auto it = m_definitions.find(canonical);
    return it == m_definitions.end() ? nullptr : &it->second;
}

void BuiltinRegistry::registerRuntimeSymbol(RuntimeSymbolId id) {
    const std::string_view sym = runtimeSymbolName(id);
    const auto headers = runtimeSymbolHeaders(id);

    // Инвариант: символ НЕ должен дублировать тип, зарегистрированный через
    // registerBuiltinType (его заголовки уже покрываются по-типу, механизм №1).
    for (const auto& desc : m_store.userDescriptors()) {
        EXPECT(desc.cppName != sym && "runtime symbol duplicates a builtin type; headers already come from the type");
    }

    std::vector<std::string> hs(headers.begin(), headers.end());
    m_runtime.push_back({std::string(sym), std::move(hs)});
}

const std::vector<RuntimeSymbol>& BuiltinRegistry::runtimeSymbols() const noexcept {
    return m_core != nullptr ? m_core->runtimeSymbols : m_runtime;
}

void BuiltinRegistry::reset() {
    m_definitions.clear();
    m_runtime.clear();
}

std::vector<RuntimeSymbol> BuiltinRegistry::takeRuntimeSymbols() {
    std::vector<RuntimeSymbol> out = std::move(m_runtime);
    m_runtime.clear();
    return out;
}

const std::vector<std::string>& BuiltinRegistry::preprocIncludesOf(TypeId id) const noexcept {
    static const std::vector<std::string> kEmpty;
    static const std::vector<std::string> kFunctional{std::string(kFunctionalInclude)};
    if (m_store.getTypeDataAs<FunctionTypeData>(id) != nullptr) {
        return kFunctional;
    }
    if (const BuiltinDefinition* def = getBuiltinDefinition(id); def != nullptr) {
        return def->preprocIncludes;
    }
    if (const TypeDescriptor* desc = m_store.descriptorOf(id); desc != nullptr) {
        return desc->preprocIncludes;
    }
    return kEmpty;
}

std::string_view BuiltinRegistry::preprocIncludeOf(TypeId id) const noexcept {
    const std::vector<std::string>& includes = preprocIncludesOf(id);
    return includes.empty() ? std::string_view{} : std::string_view(includes.front());
}

std::vector<std::string> BuiltinRegistry::linkLibsOf(TypeId id) const {
    const TypeId canonical = m_store.desugaredTypeId(id);
    // Конкретный тензор: libs объявлены на абстрактном предобъявленном типе реализацией
    // провайдера (`Tensor = %trust::Tensor @[link("tensor")]`), а не на структурном узле.
    if (m_store.isTensorType(canonical)) {
        const TypeId base = m_store.builtinIdByName(m_store.isSparseTensorType(canonical) ? type::SparseTensor : type::Tensor);
        if (const BuiltinDefinition* def = getBuiltinDefinition(base); def != nullptr) {
            return def->libs;
        }
        return {};
    }
    const TypeDescriptor* desc = m_store.descriptorOf(canonical);
    if (desc == nullptr) {
        return {};
    }
    if (!desc->libs.empty()) {
        return desc->libs;
    }
    // Инстанциация нативного шаблона: libs хранятся на шаблонном дескрипторе (args пуст).
    const auto* tpl = m_store.getTypeDataAs<NativeTemplateTypeData>(canonical);
    if (tpl == nullptr || tpl->args.empty()) {
        return {};
    }
    for (const TypeDescriptor& candidate : m_store.userDescriptors()) {
        if (candidate.libs.empty() || !candidate.data.has_value()) {
            continue;
        }
        if (!std::holds_alternative<NativeTemplateTypeData>(*candidate.data)) {
            continue;
        }
        const auto& data = std::get<NativeTemplateTypeData>(*candidate.data);
        if (data.args.empty() && data.cppTemplate == tpl->cppTemplate) {
            return candidate.libs;
        }
    }
    return {};
}

} // namespace trust
