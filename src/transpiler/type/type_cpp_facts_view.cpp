// Порт чтения целевых C++-фактов типа для кодогена (см. заголовок).
#include "transpiler/type/type_cpp_facts_view.hpp"

#include "types/names.hpp"
#include "types/type_data.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {

std::string_view TypeCppFactsView::declaredCppName(TypeId id) const noexcept {
    return m_facts.cppNameOf(m_types.store().cppFactsKeyOf(id));
}

std::string_view TypeCppFactsView::nativeTemplateCppName(TypeId id) const noexcept {
    const auto* td = m_types.store().getTypeDataAs<NativeTemplateTypeData>(m_types.store().desugaredTypeId(id));
    if (td == nullptr) {
        return {};
    }
    // Факт хранится на абстрактном шаблоне (маркер) либо на его TypeId для инстанциации.
    const TypeId owner = (td->templateOf != INVALID_TYPE_ID) ? m_types.store().desugaredTypeId(td->templateOf) : m_types.store().desugaredTypeId(id);
    const CppTypeFacts* facts = m_facts.factsOf(m_types.store().cppFactsKeyOf(owner));
    return facts != nullptr ? std::string_view(facts->cppTemplate) : std::string_view{};
}

const CppTypeFacts* TypeCppFactsView::effective(TypeId id) const noexcept {
    const TypeId canonical = m_types.store().desugaredTypeId(id);
    if (const CppTypeFacts* facts = m_facts.factsOf(m_types.store().cppFactsKeyOf(canonical)); facts != nullptr) {
        return facts;
    }
    // Инстанциация нативного шаблона: целевые факты (include/libs/cppTemplate) объявлены на
    // абстрактном шаблоне — языковой ключ `templateOf` (R1: один владелец на факт).
    if (const auto* nt = m_types.store().getTypeDataAs<NativeTemplateTypeData>(canonical); nt != nullptr && nt->templateOf != INVALID_TYPE_ID) {
        if (const CppTypeFacts* facts = m_facts.factsOf(m_types.store().cppFactsKeyOf(nt->templateOf)); facts != nullptr) {
            return facts;
        }
    }
    // Структурные контейнеры не несут своей реализации: она объявлена на абстрактном builtin-типе.
    const TypeId abstractId = m_types.builtins().abstractBuiltinOf(canonical);
    return abstractId == INVALID_TYPE_ID ? nullptr : m_facts.factsOf(m_types.store().cppFactsKeyOf(abstractId));
}

const std::vector<std::string>& TypeCppFactsView::preprocIncludesOf(TypeId id) const noexcept {
    static const std::vector<std::string> kEmpty;
    static const std::vector<std::string> kFunctional{std::string("#include <functional>")};
    if (m_types.store().getTypeDataAs<FunctionTypeData>(id) != nullptr) {
        return kFunctional;
    }
    if (const CppTypeFacts* facts = effective(id); facts != nullptr && !facts->preprocIncludes.empty()) {
        return facts->preprocIncludes;
    }
    return kEmpty;
}

std::string_view TypeCppFactsView::preprocIncludeOf(TypeId id) const noexcept {
    const std::vector<std::string>& includes = preprocIncludesOf(id);
    return includes.empty() ? std::string_view{} : std::string_view(includes.front());
}

std::vector<std::string> TypeCppFactsView::linkLibsOf(TypeId id) const {
    if (const CppTypeFacts* facts = effective(id); facts != nullptr && !facts->libs.empty()) {
        return facts->libs;
    }
    return {};
}

bool TypeCppFactsView::isImplemented(TypeId id) const noexcept {
    return m_facts.hasUser(m_types.store().cppFactsKeyOf(id));
}

std::string_view TypeCppFactsView::anyCppName() const {
    const std::string_view name = declaredCppName(m_types.store().builtinIdByName(type_generic::Any));
    EXPECT(!name.empty() && "Any implementation is not registered: provider-gate must run before codegen");
    return name;
}

} // namespace trust