#pragma once

// Объявленные (пользовательские) операторы: единая точка разрешения member/free форм.
// Операнды передаются в ИСХОДНОМ порядке (получатель первый); member-форма не получает
// неявный `this`, free-форма получает все операнды. Для нативных шаблонов-инстансов
// (`Iterator<T>`) сигнатуры инстанцируются по каноническому типу получателя.

#include "types/type_read.hpp"
#include "location/location.hpp"
#include "semantic/core/pass.hpp"
#include "types/type_id.hpp"
#include <optional>
#include <span>
#include <string_view>

namespace trust {

class TypeSystem;

} // namespace trust

namespace trust::semantic {

/// Тип может объявлять оператор (record/native class/native template, канонически).
[[nodiscard]] bool isUserOperatorType(const TypeReadModel& reg, TypeId type);

/// Выбранная перегрузка объявленного оператора: возврат + сигнатура + форма.
/// `overloaded` - набор выбранной формы содержал >1 сигнатуру (кодогену нужен
/// explicit-dispatch с кастами).
/// Для СВОБОДНОЙ формы всегда заполняются `namespacePath`/`module` объявления: у оператора нет
/// идентификатора, поэтому C++-имя — trust-namespace (`<ns>::operator…`; имя модуля в C++-символ
/// не входит), а вызов всегда эмитится квалифицированно (member-форма — invalid/пусто).
struct ResolvedUserOperator {
    TypeId returnType{INVALID_TYPE_ID};
    TypeId signature{INVALID_TYPE_ID};
    bool member{false};
    bool overloaded{false};
    std::string_view namespacePath; ///< trust-namespace объявления (интернированный view; "" = корень)
    ModuleIndex module = ModuleIndex::invalid();
};

/// Разрешает объявленный оператор symbol для операндов operands (получатель первый).
/// `postfix` - только для `++`/`--`: обе формы получают dummy-параметр `Int32`.
/// При отсутствии/неоднозначности выдаёт диагностику и возвращает nullopt; для не-user
/// типов (встроенные) - nullopt без диагностики (решает вызывающий путь).
[[nodiscard]] std::optional<ResolvedUserOperator> resolveUserOperator(AnalysisContext& actx, std::string_view symbol, std::span<const TypeId> operands,
                                                                      bool postfix, MapperRange range);

} // namespace trust::semantic
