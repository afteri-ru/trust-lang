// src/transpiler/type/type_emit.cpp
// Фасад эмиссии типов: делегирование TypeNameResolver (имена) и TypeIncludeCollector (include).
#include "transpiler/type/type_emit.hpp"

namespace trust {

void TypeEmitter::collectLinkLib(const AstNodeAttr& node) {
    m_includes.collectLinkLib(node);
}
void TypeEmitter::collectInclude(const AstNodeAttr& node) {
    m_includes.collectInclude(node);
}
void TypeEmitter::recordRequiredInclude(std::string_view include) const {
    m_includes.recordRequiredInclude(include);
}
void TypeEmitter::recordUsedType(TypeId type_id) const {
    m_includes.recordUsedType(type_id);
}
void TypeEmitter::collectTypeIncludes() const {
    m_includes.collectTypeIncludes();
}
void TypeEmitter::collectStructPodAsserts() const {
    m_includes.collectStructPodAsserts();
}
void TypeEmitter::emitStructPodAsserts(MapperFile output_idx) const {
    m_includes.emitStructPodAsserts(output_idx);
}
std::optional<std::string> TypeEmitter::emitTypeName(TypeId type_id) {
    return m_names.emitTypeName(type_id);
}
std::optional<std::string> TypeEmitter::emitTypeName(QualType state) {
    return m_names.emitTypeName(state);
}
QualType TypeEmitter::toRuntimeType(QualType state) const {
    return m_names.toRuntimeType(state);
}
std::string TypeEmitter::emitTypeNameForNode(const AstNodeBase* type_node) {
    return m_names.emitTypeNameForNode(type_node);
}
bool TypeEmitter::isRecordTemplateAnnotation(const AstNodeBase* type_node) const {
    return m_names.isRecordTemplateAnnotation(type_node);
}
void TypeEmitter::recordRuntimeSymbolHeaders(RuntimeSymbolId id) const {
    m_includes.recordRuntimeSymbolHeaders(id);
}
void TypeEmitter::recordRuntimeSymbolsInText(std::string_view text) const {
    m_includes.recordRuntimeSymbolsInText(text);
}
void TypeEmitter::emitCollectedIncludes(MapperFile output_idx) {
    m_includes.emitCollectedIncludes(output_idx);
}
std::optional<TypeId> TypeEmitter::findRegisteredType(std::string_view trustName) const {
    return m_includes.findRegisteredType(trustName);
}
std::optional<std::pair<std::string, std::string_view>> TypeEmitter::resolveCppTypeId(TypeId type_id) const {
    return m_names.resolveCppTypeId(type_id);
}

} // namespace trust
