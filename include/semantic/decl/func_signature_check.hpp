#pragma once

// include/semantic/decl/func_signature_check.hpp
// Проверки СИГНАТУРЫ объявления функции (FuncDecl), не требующие скоупа, таблицы символов и
// хуков драйвера. Оркестратор (`FuncDeclAnalyzer::analyzeFuncDecl`) задаёт порядок вызовов;
// сами проверки не знают ни порядка, ни соседей и тестируются изолированно.
//
// Контракт по семействам имён:
//   validate* - fatal: false = объявление невалидно, дальнейшие проверки и регистрация не выполняются;
//   check*    - report-only: диагностика выдана, объявление продолжает проверяться.
//
// Драйверное состояние (внутри ли тела типа, внутри ли функции) передаётся ЗНАЧЕНИЕМ: ни одна
// функция не обращается к NameResolutionPass и не открывает скоупы.

#include "ast/ast_nodes.hpp"
#include "types/type_id.hpp"

#include <cstddef>
#include <string>
#include <unordered_set>

namespace trust {

class AnalysisContext;
class Context;

namespace semantic {

/// Z1: системные имена (`__...__`) зарезервированы за компилятором - объявлять можно только
/// зарегистрированные операторы. `__main__` - точка входа, генерируемая макросом `@main`.
[[nodiscard]] bool validateReservedFuncName(Context& ctx, const FuncDecl& func_node);

/// Z2: generic-функции поддержаны только как свободные функции и шаблонный native-import;
/// generic-метод требует подстановки типовых параметров в методы типа (не реализовано).
[[nodiscard]] bool validateGenericMethodDecl(Context& ctx, const FuncDecl& func_node, bool in_type_body);

/// Z3/Z4: наборы допустимых типов в сигнатуре (`f(x:(:A + :B))`). Комбинация проверяется, разворот
/// объявления в N определений отложен. Одна зона на возврат и параметры: будущий владелец -
/// разворот сигнатуры (общий для обеих позиций).
[[nodiscard]] bool validateTypeSetSignature(AnalysisContext& actx, const FuncDecl& func_node);

/// Z6: нативная функция (`%...`) транслируется в C++ напрямую, поэтому тип возврата обязателен:
/// иначе получилась бы голая декларация без типа.
[[nodiscard]] bool validateNativeForwardDecl(Context& ctx, const FuncDecl& func_node);

/// Z7: `@[stack_check@]` (без аргумента, режим limit) либо `@[stack_check(N)@]` с одним целым N >= 0.
void checkStackCheckAttr(Context& ctx, const FuncDecl& func_node);

/// Z8: deleter/lifetime/pin применяются только к объявлениям переменных/параметров. На типе
/// возврата функции они не применяются (reftype поддержан отдельно через resolveTypeRef) -
/// явная диагностика вместо тихого игнора. Порядок диагностик = порядок объявления атрибутов.
void checkReturnTypeQualifiers(Context& ctx, const FuncDecl& func_node);

/// Z9: короткие (символьные) маркеры вида ссылки на типе возврата запрещены - сигнатура является
/// контрактом API. Правило относится к УМНЫМ маркерам (`&&`/`&*`/`&?`); нативные `%&`/`%*` -
/// отдельная переходная ось.
void checkReturnRefMarkers(Context& ctx, const FuncDecl& func_node);

/// Z5: свободный оператор объявляется только на уровне модуля (не внутри функции); member-форма
/// валидируется анализатором ТИПА. `in_type_body` различает формы, `inside_function` запрещает
/// локальные свободные операторы.
[[nodiscard]] bool validateOperatorForm(Context& ctx, const FuncDecl& func_node, bool in_type_body, bool inside_function);

/// Z13 (часть): каждый объявленный типовой параметр обязан встречаться в сигнатуре - иначе C++ не
/// выведет его при вызове, а эмиссия явных template-аргументов пропустит такой параметр.
[[nodiscard]] bool validateTemplateParamsUsed(Context& ctx, const FuncDecl& func_node, TypeId func_type);

/// Z14: свободный оператор обязан иметь хотя бы один параметр пользовательского типа - иначе он
/// никогда не участвует в разрешении (built-in операторы - интринсики) и был бы мёртвым.
/// Member-форма сюда не доходит (оркестратор выходит из объявления раньше).
[[nodiscard]] bool validateFreeOperatorOperand(Context& ctx, const FuncDecl& func_node, TypeId func_type);

/// Pack-типовые параметры объявления (`<T...>`): имена и количество.
struct PackParamInfo {
    std::unordered_set<std::string> names;
    std::size_t count = 0;
};

/// Собрать pack-типовые параметры объявления (`<T...>`): имена и число объявлений.
[[nodiscard]] PackParamInfo collectPackParams(const FuncDecl& func_node);

/// Z11: pack-объявлений не больше одного (zip не реализован); C-style `...` с pack-параметром
/// несовместим (обе формы задают «хвост» аргументов).
[[nodiscard]] bool validatePackDeclCount(Context& ctx, const FuncDecl& func_node, const PackParamInfo& pack_info);

/// Z12: форма pack-слота - ровно один, trailing, объявлен как `<T...>`. Выполняется ДО
/// buildFuncType: тот ожидает уже валидную форму (EXPECT в pass.cpp).
[[nodiscard]] bool validatePackSlotShape(Context& ctx, const FuncDecl& func_node, const PackParamInfo& pack_info);

/// Z15: pack-параметр допустим только в списковой позиции типа (аргументы шаблона), не одиночным
/// типом. Обход графа типа - ЕДИНЫЙ исчерпывающий (types/type_traversal.hpp).
[[nodiscard]] bool validatePackUsage(Context& ctx, const FuncDecl& func_node, TypeId func_type, const PackParamInfo& pack_info);

} // namespace semantic
} // namespace trust
