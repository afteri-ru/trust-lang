#pragma once

// include/mangling/mangling.hpp
// Единый источник ПРАВИЛ именования: Trust-имена → C++-идентификаторы/линковочные символы,
// имена операторов, суффиксы перегрузок и схемы временных имён. Потребители — анализатор
// (semantic) и кодогенерация (transpiler); вычисление имён НЕ должно дублироваться вне
// этого компонента.
//
// Низкоуровневый маппинг идентификатора Trust→C++ остаётся в utils::name_to_cpp (лист ниже);
// mangling поднимает правила уровня языка над ним.

#include "types/registry.hpp"
#include "types/type_id.hpp"

#include <string>
#include <string_view>

namespace trust::mangling {

/// C++-имя функции/метода/оператора — ЕДИНАЯ точка вычисления.
/// Оператор (имя-СИМВОЛ в обратных кавычках) → `operator<sym>`; entry-функция модуля
/// (`<модуль>__main__`) → сырое имя без манглинга; иначе — utils::name_to_cpp.
[[nodiscard]] std::string cppFuncIdent(std::string_view trustName, bool isOperator, bool isEntry);

/// C++-суффикс имени перегрузки, детерминированный по СИГНАТУРЕ (стабилен между единицами
/// трансляции — не зависит от registry_index). Совпадает у объявления и сайта вызова.
[[nodiscard]] std::string overloadSuffix(const TypeRegistry& reg, TypeId sig);

/// Детерминированное валидное имя линковочного символа (asm-метка): каждый
/// не-алфанумерический символ (включая `_`) кодируется `_<hex>`, что делает преобразование
/// ИНЪЕКТИВНЫМ (разные имена → разные символы).
[[nodiscard]] std::string sanitizeSymbolPart(std::string_view s);

/// Вид экспортируемой сущности для генерации линковочного символа.
enum class ExportKind {
    Function, ///< свободная/member-функция → `__trust_fn_`
    Variable, ///< переменная модуля → `__trust_var_`
    Operator, ///< свободный оператор → `__trust_op_`
};

/// Единое линковочное (нативное, asm) имя экспортируемой сущности по её квалифицированному
/// C++-имени: `__trust_<fn|var|op>_` + `sanitizeSymbolPart`. Одна точка для кодогена
/// (asm-метка) и будущего Trust-интерфейса (правая часть `:= %<symbol>;`).
[[nodiscard]] std::string exportSymbol(std::string_view qualifiedCppName, ExportKind kind);

/// Объявление Trust-интерфейса с привязкой к нативному символу: к `fwdDecl`
/// (`name(...):Ret := ...;`) подставляет правую часть `:= <symbolRef>;` (symbolRef = `%<symbol>`).
/// Пустой symbolRef или отсутствие ` := ...;` — нарушение инварианта (EXPECT).
[[nodiscard]] std::string interfaceDecl(std::string_view fwdDecl, std::string_view symbolRef);

/// Вид синтезируемой временной переменной (имя — часть единой схемы именования).
enum class TempKind {
    Match,   ///< scrutinee match → `_matchN`
    Result,  ///< hoist возврата для пост-условий → `__trust_res_N`
    Discard, ///< `x :=: _` (move-to-discard) → `__trust_discard_N`
};

/// Имя синтезируемой временной по её виду и уникальному id (id выдаёт стадия-владелец).
[[nodiscard]] std::string tmpName(TempKind kind, int id);

} // namespace trust::mangling
