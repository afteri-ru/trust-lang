#pragma once

// include/transpiler/type/type_emit.hpp
// Фасад кодогенерации типов: рендеринг C++-имени (TypeNameResolver) и сбор include/link/
// POD-проверок (TypeIncludeCollector). Вызывающий код использует единый TypeEmitter.

#include "transpiler/emit/emit_ctx.hpp"
#include "transpiler/type/type_include_collector.hpp"
#include "transpiler/type/type_name_resolver.hpp"
#include "ast/ast_nodes.hpp"
#include "location/location.hpp"
#include "types/type_id.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

class CppTranspiler;

class TypeEmitter {
  public:
    TypeEmitter(CppEmitContext& ectx, CppTranspiler& /*driver*/)
    : m_includes(ectx)
    , m_names(ectx, m_includes) {}

    void collectLinkLib(const AstNodeAttr& node);
    void collectInclude(const AstNodeAttr& node);
    void recordRequiredInclude(std::string_view include) const;
    void recordUsedType(TypeId type_id) const;
    void collectTypeIncludes() const;
    void collectStructPodAsserts() const;
    void emitStructPodAsserts(MapperFile output_idx) const;
    std::optional<std::string> emitTypeName(TypeId type_id);
    std::optional<std::string> emitTypeName(QualType state);
    /// Рантайм-форма типа для ABI-границы `trust::TypedValue` (см. TypeNameResolver::toRuntimeType).
    QualType toRuntimeType(QualType state) const;
    std::string emitTypeNameForNode(const AstNodeBase* type_node);
    bool isRecordTemplateAnnotation(const AstNodeBase* type_node) const;
    void recordRuntimeSymbolHeaders(RuntimeSymbolId id) const;
    void recordRuntimeSymbolsInText(std::string_view text) const;
    void emitCollectedIncludes(MapperFile output_idx);
    std::optional<TypeId> findRegisteredType(std::string_view trustName) const;
    std::optional<std::pair<std::string, std::string_view>> resolveCppTypeId(TypeId type_id) const;

  private:
    TypeIncludeCollector m_includes;
    TypeNameResolver m_names;
};

} // namespace trust
