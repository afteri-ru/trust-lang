// src/symbols/reference_index.cpp
// Проекция ссылок имя→объявление для LSP: один обход AST после семантики.
#include "types/type_read.hpp"
#include "symbols/reference_index.hpp"

#include "ast/ast_nodes.hpp"
#include "sourcemap/writer.hpp"

#include <cctype>
#include <string_view>

namespace trust {

namespace {

bool isQualifiedNameChar(char c) {
    const auto u = static_cast<unsigned char>(c);
    return u >= 0x80 || std::isalnum(u) != 0 || c == '_' || c == ':' || c == '\\' || c == '@' || c == '%' || c == '$';
}

// Диапазон ссылки расширяется до конца qualified-токена: AST-диапазон модульной ссылки может
// покрывать только префикс, а текст нормализован до короткого имени.
MapperRange referenceRange(const IdentName& ident, const SourceMapWriter& source) {
    MapperRange ref = ident.range();
    if (ref.isInvalid() || ref.begin.fileIdx().isOutput()) {
        return ref;
    }
    const std::string_view file = source.source(ref.begin.fileIdx());
    std::size_t next = ref.end.offset();
    while (next > 0 && next <= file.size() && isQualifiedNameChar(file[next - 1])) {
        ++next;
    }
    if (next > ref.end.offset()) {
        ref.end = source.makeLoc(ref.begin.fileIdx(), static_cast<uint32_t>(next - 1));
    }
    return ref;
}

void recordIdent(const IdentName& ident, MapperFile rootFile, const TypeReadModel& types, const SourceMapWriter& source, ReferenceIndex& out) {
    const MapperRange ref = referenceRange(ident, source);
    if (ref.isInvalid() || ref.begin.fileIdx() != rootFile) {
        return;
    }
    MapperRange target{};
    if (ident.resolvedTypeId != INVALID_TYPE_ID) {
        // Имя типа: цель — объявление типа в реестре (у встроенных/native диапазона нет).
        target = types.store().getTypeSourceRange(ident.resolvedTypeId);
    } else if (ident.resolvedDecl != nullptr) {
        target = ident.resolvedDecl->range();
    }
    if (target.isInvalid() || target.begin.fileIdx().isOutput()) {
        return;
    }
    if (target.begin.fileIdx().as_index() == ref.begin.fileIdx().as_index() && target.begin.offset() == ref.begin.offset()) {
        return; // ссылка сама на себя (не объявление)
    }
    out.push_back(SymbolReference{ref, target});
}

void walk(AstNodeBase& node, MapperFile rootFile, const TypeReadModel& types, const SourceMapWriter& source, ReferenceIndex& out) {
    const ParserToken::Kind kind = node.kind();
    if (kind == ParserToken::Kind::Ident || kind == ParserToken::Kind::TypeName) {
        recordIdent(static_cast<const IdentName&>(node), rootFile, types, source, out);
    }
    std::vector<AstNodePtr*> slots;
    node.collectChildren(slots);
    for (AstNodePtr* slot : slots) {
        if (slot != nullptr && *slot != nullptr) {
            walk(**slot, rootFile, types, source, out);
        }
    }
}

} // namespace

void collectReferences(AstNodeBase& root, MapperFile rootFile, const TypeReadModel& types, const SourceMapWriter& source, ReferenceIndex& out) {
    walk(root, rootFile, types, source, out);
}

} // namespace trust
