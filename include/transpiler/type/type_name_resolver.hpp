#pragma once

// Рендеринг C++-имени типа: разбор AST-аннотации, активные template-параметры, выбор C++-формы
// по TypeId. Побочные эффекты включений делегируются TypeIncludeCollector.

#include "transpiler/emit/emit_ctx.hpp"
#include "ast/ast_nodes.hpp"
#include "location/location.hpp"
#include "types/types.hpp"
#include "types/type_id.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace trust {

class TypeIncludeCollector;

class TypeNameResolver {
  public:
    TypeNameResolver(CppEmitContext& ectx, TypeIncludeCollector& includes)
    : m_ectx(ectx)
    , m_includes(includes) {}

    bool isRecordTemplateAnnotation(const AstNodeBase* type_node) const;
    std::optional<std::string> emitTypeName(TypeId type_id);
    std::optional<std::string> emitTypeName(QualType state);
    std::string emitTypeNameForNode(const AstNodeBase* type_node);
    std::optional<std::pair<std::string, std::string_view>> resolveCppTypeId(TypeId type_id) const;
    std::optional<std::pair<std::string, std::string_view>> resolveCppTypeId(QualType state) const;

    /// Приводит identity к рантайм-форме для ABI-границы `trust::TypedValue`: у встроенных типов
    /// (и их прозрачных алиасов) identity — только TypeKind (registry_index = 0), т.к. слот
    /// реестра компилятора в рантайме не воспроизводим; у пользовательских (номинальных) типов
    /// index входит в identity и сохраняется. Квалификаторы (const/inferred) сохраняются всегда.
    QualType toRuntimeType(QualType state) const;

    /// C++-имя материализуемого `Any` — ЕДИНЫЙ источник (реализация из `builtin/any.src` через
    /// declaredCppName); без реализации — FAULT (провайдер-гейт обязан отработать до кодогена).
    std::string anyTypeName();

  private:
    using CppTypeName = std::pair<std::string, std::string_view>;
    using CppTypeNameResult = std::optional<CppTypeName>;

    CppTypeNameResult resolveViaPrinter(QualType state, TypeId type_id, TypeId canonical, RefType refKind) const;
    CppTypeNameResult renderSimple(QualType state, TypeId type_id, TypeId canonical, RefType refKind) const;
    CppTypeNameResult renderRange(TypeId canonical, RefType refKind) const;
    CppTypeNameResult renderArray(QualType state, TypeId canonical, RefType refKind) const;
    CppTypeNameResult renderTensor(TypeId canonical, RefType refKind) const;
    CppTypeNameResult renderTuple(TypeId canonical, RefType refKind) const;
    CppTypeNameResult renderNominal(QualType state, TypeId canonical) const;
    CppTypeNameResult renderRecord(QualType state, TypeId canonical) const;
    CppTypeNameResult renderNativeTemplate(TypeId canonical, RefType refKind) const;
    CppTypeNameResult renderNativeClass(TypeId canonical, RefType refKind) const;
    static std::string wrapRefKind(RefType refKind, std::string base);

    CppEmitContext& m_ectx;
    TypeIncludeCollector& m_includes;
};

} // namespace trust
