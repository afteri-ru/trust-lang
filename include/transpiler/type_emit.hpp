#pragma once

// include/transpiler/type_emit.hpp
// Компонент кодогенерации: TypeEmitter. Разделяет CppEmitContext с драйвером CppTranspiler;
// рекурсия/вызовы других компонентов идут через драйвер (friend).

#include "transpiler/emit_ctx.hpp"
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
    explicit TypeEmitter(CppEmitContext& ectx, CppTranspiler& driver)
    : m_ectx(ectx)
    , m_driver(driver) {}
    void collectLinkLib(const AstNodeAttr& node);
    void recordRequiredInclude(std::string_view include) const;
    void recordUsedType(TypeId type_id) const;
    void collectTypeIncludes() const;
    std::optional<std::string> emitTypeName(TypeId type_id, std::string_view displayName);
    std::string emitTypeNameForNode(const AstNodeBase* type_node);
    void recordRuntimeSymbolHeaders(RuntimeSymbolId id) const;
    void recordRuntimeSymbolsInText(std::string_view text) const;
    void emitCollectedIncludes(MapperFile output_idx);
    std::optional<TypeId> resolveTypeIdByName(std::string_view trustName) const;
    std::optional<std::pair<std::string, std::string_view>> resolveCppType(std::string_view trustName) const;
    std::optional<std::pair<std::string, std::string_view>> resolveCppTypeId(TypeId type_id, std::string_view displayName) const;

  private:
    CppEmitContext& m_ectx;
    CppTranspiler& m_driver;
};

} // namespace trust
