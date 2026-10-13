// include/symbols/reference_index.hpp
// Проекция ССЫЛОК имя→объявление для LSP (go-to-definition/hover по использованию).
// Единственный источник решения «куда разрешилось имя» — материализованные семантикой факты:
// `IdentName::resolvedDecl` (выбранная перегрузка/объявление) и `IdentName::resolvedTypeId`
// (для имён типов — диапазон объявления из реестра). Source map не расширяется.
#pragma once

#include "types/type_read.hpp"
#include "ast/token.hpp"
#include "location/location.hpp"
#include "types/type_system.hpp"

#include <vector>

namespace trust {

class SourceMapWriter;

/// Одна ссылка: диапазон ИМЕНИ-ссылки и диапазон объявления-цели (оба — входные файлы этой же
/// компиляции, writer-домен offsets). Целевой файл определяется `decl.begin.fileIdx()`.
struct SymbolReference {
    MapperRange ref;
    MapperRange decl;
};

using ReferenceIndex = std::vector<SymbolReference>;

/// Собирает ссылки ТОЛЬКО текущего файла (`rootFile`): остальные файлы открываются как корень
/// собственной транспиляции, а память LSP ограничена размером редактируемого файла.
/// `source` расширяет диапазон ссылки до конца qualified-токена: AST-диапазон модульной ссылки
/// может покрывать только префикс, а текст нормализован семантикой до короткого имени.
void collectReferences(AstNodeBase& root, MapperFile rootFile, const TypeReadModel& types, const SourceMapWriter& source, ReferenceIndex& out);

} // namespace trust
