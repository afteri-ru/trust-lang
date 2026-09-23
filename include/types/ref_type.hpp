#pragma once

// include/types/ref_type.hpp
// Единый источник C++-представления ВИДА ССЫЛКИ (RefType) для кодогенерации.
// Оба потребителя (TypeRegistry::getCppTypeName и TypeEmitter::wrapRefKind) обязаны
// строить обёртку/суффикс только здесь, иначе при добавлении вида/backend'а они разойдутся.
// Имена классов-обёрток берутся из X-macro refTypeCppTemplateName (types/typekind.hpp).

#include "types/typekind.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace trust {

/// C++-имя типа с применённым видом ссылки - ЕДИНЫЙ источник для реестра и транспилятора
/// (дублировать switch по RefType нельзя).
/// @param kind        вид ссылки (значение kValue - без обёртки/суффикса).
/// @param pointeeCpp  уже готовое C++-имя базового (pointee) типа.
/// @param deleterCpp  C++-имя deleter'а внешнего ресурса - только для `unique` (D - часть типа);
///                    пусто - без deleter. Для прочих видов игнорируется.
/// Sync-backend (trust::AccessShared<T,Policy>) композируется в TypeRegistry::getCppTypeName:
/// там доступны РЕАЛЬНЫЕ типы обёртки/политики из реестра (строки имён сюда не передаются).
/// kMptr не комбинируется с ref-видами (указатель на член рендерится отдельно) - возвращает pointee.
[[nodiscard]] std::string refTypeCppName(RefType kind, std::string_view pointeeCpp, std::string_view deleterCpp = {});

/// Рантайм-заголовки (в форме `@trust/...`), требуемые видом ссылки с учётом backend'а - ЕДИНЫЙ
/// источник выбора инклюда для кодогенерации: shared/weak/unique -> `@trust/trusted-cpp.hpp`;
/// при sync дополнительно `@trust/trusted-cpp-sync.hpp`; прочие виды (value/ptr/ref/...) -> пусто.
[[nodiscard]] std::vector<std::string_view> refTypeRuntimeIncludes(RefType kind, bool sync = false);

/// Отображаемое имя вида ссылки (мнемоника `refTypeName`) для диагностик/дампов:
/// `shared<T>`, `unique<T, D>` и т.п. Парная к `refTypeCppName` (C++-представление).
/// Для kValue/kMptr возвращает pointee без обёртки.
[[nodiscard]] std::string refTypeDisplayName(RefType kind, std::string_view pointeeName, std::string_view deleterName = {});

} // namespace trust
