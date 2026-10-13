#pragma once

// include/transpiler/type/type_include_collector.hpp
// Сбор C++-заголовков, link-библиотек и POD-проверок, требуемых использованными типами.
// Выделен из TypeEmitter: побочные эффекты включений отделены от рендеринга имён типов.

#include "transpiler/emit/emit_ctx.hpp"
#include "ast/ast_nodes.hpp"
#include "location/location.hpp"
#include "types/runtime_symbols.hpp"
#include "types/type_id.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace trust {

class TypeIncludeCollector {
  public:
    explicit TypeIncludeCollector(CppEmitContext& ectx)
    : m_ectx(ectx) {}

    void collectLinkLib(const AstNodeAttr& node);
    void collectInclude(const AstNodeAttr& node);
    void recordRequiredInclude(std::string_view include) const;
    void recordUsedType(TypeId type_id) const;
    void collectTypeIncludes() const;
    /// POD-проверки для КОНКРЕТНЫХ инстанциаций Struct-шаблонов из m_accumulators.usedTypes.
    void collectStructPodAsserts() const;
    void emitStructPodAsserts(MapperFile output_idx) const;
    void recordRuntimeSymbolHeaders(RuntimeSymbolId id) const;
    void recordRuntimeSymbolsInText(std::string_view text) const;
    void emitCollectedIncludes(MapperFile output_idx);
    /// Тип по имени из СТРОКОВОГО атрибута (`@[reftype("shared", Policy)]`, `@[deleter(D)]`).
    /// Аннотации типов сюда не попадают: их тип материализован в узле (`IdentName::resolvedTypeId`).
    std::optional<TypeId> findRegisteredType(std::string_view trustName) const;

  private:
    CppEmitContext& m_ectx;
};

} // namespace trust
