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
    /// Собирает зависимые C++-заголовки из @[include("header")@] на нативной декларации/типе
    /// (через recordRequiredInclude → prepend в конце). Правило формы - см. attr::Include.
    void collectInclude(const AstNodeAttr& node);
    void recordRequiredInclude(std::string_view include) const;
    void recordUsedType(TypeId type_id) const;
    void collectTypeIncludes() const;
    /// Собирает POD-проверки (`static_assert`) для КОНКРЕТНЫХ инстанциаций Struct-шаблонов,
    /// реально использованных в коде (m_usedTypes). Сам шаблон-определение assert НЕ получает
    /// (static_assert над шаблоном невыразим/зависит от T). Добавляет <type_traits>.
    void collectStructPodAsserts() const;
    /// Эмитит собранные POD-проверки инстанциаций Struct-шаблонов (после инклудов).
    void emitStructPodAsserts(MapperFile output_idx) const;
    std::optional<std::string> emitTypeName(TypeId type_id, std::string_view displayName);
    std::string emitTypeNameForNode(const AstNodeBase* type_node);
    /// true, если узел-аннотация типа — пользовательский record-шаблон (`:Box<Int32>`): конструкция
    /// в таком виде (`:Box<Int32>(...)`) эмитится как `c_Box<int32_t>(...)`, а не как каст/словарь.
    bool isRecordTemplateAnnotation(const AstNodeBase* type_node) const;
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
