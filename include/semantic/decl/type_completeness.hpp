// include/semantic/decl/type_completeness.hpp
#pragma once

// Проверка ПОЛНОТЫ типа в позициях «по значению» (переменная/поле/параметр/возврат).
// Неполный (forward) тип не имеет определения, ВИДИМОГО В ЭТОЙ ТОЧКЕ, поэтому годится
// только за невладеющей ссылкой: C++ не может создать/разрушить объект неизвестного размера.
// Единственная точка правила (не дублировать проверку по анализаторам).

#include "location/location.hpp"
#include "types/type_id.hpp"

#include <string_view>

namespace trust {

class AnalysisContext;
class AstNodeBase;

namespace semantic {

/// Требует полноты объявленного типа для позиции `entity` (текст для диагностики).
/// `state` - разрешённый тип, `typeNode` - узел аннотации (nullptr, если тип выведен).
/// Неполнота определяется по ВИДИМОМУ объявлению в этой точке: символ имени типа, чей `decl` -
/// forward-record (`:T ::= :Struct ...;` без тела) из ДРУГОГО файла. Клиент модуля видит
/// интерфейсный forward, даже если владелец определил тип в своём теле; в файле владельца forward
/// до определения допустим (реестр доопределит тип позже, в т.ч. self-ссылки).
/// Неполный тип допустим за невладеющей ссылкой (`ptr`/`ref`/`weak`/`shared`/`locker`);
/// владеющая `unique` отвергается (C++ `unique_ptr` разрушает объект - нужен полный тип).
void requireCompleteValueType(AnalysisContext& actx, const AstNodeBase* typeNode, QualType state, MapperRange range, std::string_view entity);

/// Маркер неполноты: клиент видит предварительное объявление из экспорт-набора модуля.
/// Полнота определяется видимой декларацией в точке использования: forward из ДРУГОГО файла —
/// неполный тип; определение из тела за forward'ом интерфейса клиенту не публикуется
/// (`AstNodeBase::isExportImplementation`), поэтому он видит forward.

} // namespace semantic
} // namespace trust
