#pragma once

// Сбор C++-заголовков, link-библиотек и POD-проверок, требуемых использованными типами.
// Выделен из TypeEmitter: побочные эффекты включений отделены от рендеринга имён типов.

#include "transpiler/emit/emit_ctx.hpp"
#include "ast/ast_nodes.hpp"
#include "location/location.hpp"
#include "target/runtime_symbols.hpp"
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
    /// POD-проверка конкретной инстанциации Struct-шаблона (при первом использовании типа).
    void emitStructPodAsserts(MapperFile output_idx) const;
    void recordRuntimeSymbolHeaders(RuntimeSymbolId id) const;
    void recordRuntimeSymbolsInText(std::string_view text) const;
    void emitCollectedIncludes(MapperFile output_idx);

  private:
    /// Регистрация POD-проверки при первом использовании типа (R42: порядок объявлений).
    void recordStructPodAssert(TypeId id) const;

    CppEmitContext& m_ectx;
};

} // namespace trust
