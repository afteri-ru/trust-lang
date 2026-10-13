// src/semantic/decl/type_completeness.cpp
#include "types/type_read.hpp"
#include "semantic/decl/type_completeness.hpp"

#include "semantic/core/pass.hpp"
#include "symbols/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "diag/diag.hpp"
#include "types/type_system.hpp"
#include "types/typekind.hpp"

#include <string>

namespace trust::semantic {

namespace {

/// Имя типа под узлом аннотации: маркер вида ссылки (`&&`/`&*`) — обёртка без текста, имя типа
/// лежит в её операнде (`m_body[0]`).
const AstNodeBase* typeNameOperand(const AstNodeBase& typeNode) {
    const ParserToken::Kind kind = typeNode.kind();
    if (kind != ParserToken::Kind::RefMakeExpr && kind != ParserToken::Kind::RefTakeExpr) {
        return &typeNode;
    }
    const auto& marker = static_cast<const Sequence&>(typeNode);
    return marker.m_body.empty() ? nullptr : marker.m_body.front().get();
}

/// Объявление имени типа, видимое в этой точке (`decl` символа), либо nullptr.
/// Аннотация может быть узлом-маркером без текста (`&&`/`&*`) - имя берётся из его операнда.
const AstNodeBase* visibleTypeDecl(const AnalysisContext& actx, const AstNodeBase& typeNode) {
    const AstNodeBase* operand = typeNameOperand(typeNode);
    if (operand == nullptr) {
        return nullptr;
    }
    const HasText* text = operand->as_text();
    if (text == nullptr || text->text().empty()) {
        return nullptr;
    }
    const Symbol* symbol = actx.symbols().resolve(text->text());
    return (symbol != nullptr) ? symbol->decl : nullptr;
}

/// Объявление - forward-record (`:T ::= :Struct ...;` без тела)?
bool isForwardRecordDecl(const AstNodeBase& decl) {
    if (decl.kind() != ParserToken::Kind::TypeDecl) {
        return false;
    }
    const auto& binary = static_cast<const Binary&>(decl);
    if (!binary.m_right || binary.m_right->kind() != ParserToken::Kind::StructDecl) {
        return false;
    }
    return !static_cast<const RecordDecl&>(*binary.m_right).m_body.has_value();
}

} // namespace

void requireCompleteValueType(AnalysisContext& actx, const AstNodeBase* typeNode, QualType state, MapperRange range, std::string_view entity) {
    if (!state.isValid()) {
        return; // тип не разрешился - диагностику выдал resolveTypeRef
    }
    const TypeReadModel& reg = actx.ctx().types();
    const TypeId id = getUnqualifiedType(state.getTypeId());
    const RefType ref = getRefType(getTypeKind(id));
    // Неполнота для КЛИЕНТА: видимое объявление — forward-record из ДРУГОГО файла. Определение
    // из тела за forward'ом интерфейса клиенту не публикуется (isExportImplementation), поэтому
    // видимый символ клиента остаётся forward. В файле владельца forward до определения допустим
    // (реестр доопределит тип позже, в т.ч. self-ссылки), проверки нет.
    bool clientIncomplete = false;
    if (typeNode != nullptr) {
        if (const AstNodeBase* visible = visibleTypeDecl(actx, *typeNode); visible != nullptr && isForwardRecordDecl(*visible)) {
            // Точка использования — место диагностики (`range`): при опущенной аннотации узел типа
            // принадлежит объявлению callee (другой файл), поэтому cross-file считается по `range`.
            const MapperRange& useRange = range;
            const MapperRange& declRange = visible->range();
            const bool crossFile = !useRange.begin.isInvalid() && !declRange.begin.isInvalid() && useRange.begin.fileIdx() != declRange.begin.fileIdx();
            clientIncomplete = crossFile;
        }
    }
    if (ref != RefType::kValue) {
        // Владение с обязательной полнотой: unique_ptr разрушает объект через deleter.
        if (ref == RefType::kUnique && clientIncomplete) {
            const TypeId pointee = reg.store().desugaredTypeId(getUnqualifiedType(reg.store().getPointeeType(id)));
            actx.ctx().diag().report(Severity::Error, range, "{} holds an owning 'unique' reference to incomplete type '{}': the full definition is required",
                                     entity, reg.printer().getTypeName(pointee));
        }
        return; // прочие виды ссылок полноты не требуют
    }
    if (clientIncomplete) {
        actx.ctx().diag().report(Severity::Error, range, "{} uses incomplete type '{}' by value; use a pointer/reference or provide the definition", entity,
                                 reg.printer().getTypeName(reg.store().desugaredTypeId(id)));
    }
}

} // namespace trust::semantic
