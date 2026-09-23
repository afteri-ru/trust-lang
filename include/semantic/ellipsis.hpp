#pragma once

// include/semantic/ellipsis.hpp
// ЕДИНЫЙ примитив семейства «замыкающего» многоточия в СПИСКАХ (аргументы вызова И элементы массива):
//   `... expr ...` (Fill)   - заполнить оставшиеся позиции перевычисляемым операндом;
//   `...`          (Repeat) - циклически повторить явный префикс до capacity;
//   `... src`      (Spread) - раскрыть конечный источник (рантайм-раскрытие).
//
// Fill/Repeat - capacity-driven («число позиций» известно из типа-цели/сигнатуры): семантика
// МАТЕРИАЛИЗУЕТ список (разворачивает в конкретные элементы), поэтому кодогенерация о многоточии не
// знает вообще (в AST у неё уже готовый список). Spread не материализуется (источник может быть
// неизвестного размера) - в неподдержанных позициях выдаётся диагностика (см. reportUnresolvedEllipsis).

#include "ast/ast_nodes.hpp"
#include "types/type_id.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace trust {

class AnalysisContext;
class TypeRegistry;

/// Форма «замыкающего» многоточия.
enum class EllipsisForm {
    None,   ///< многоточия нет
    Fill,   ///< `... expr ...`
    Repeat, ///< `...`
    Spread, ///< `... src`
};

/// Разбор списка: единственное многоточие (форма + операнд) и число явных элементов.
struct EllipsisInfo {
    EllipsisForm form = EllipsisForm::None;
    bool isLast = false;      ///< многоточие стоит последним элементом
    size_t count = 0;         ///< число элементов-многоточий
    size_t explicitCount = 0; ///< явных (не-многоточие) элементов
    AstNodePtr operand;       ///< Fill: `expr`; Spread: источник
};

/// Разбор аргументов вызова (`CallExpr::m_args`).
EllipsisInfo scanCallEllipsis(const CallExpr& call);
/// Разбор элементов литерала/конструкции массива (`DictLiteralNode::m_body`).
EllipsisInfo scanArrayEllipsis(const DictLiteralNode& node);

/// Структурные правила (единственное многоточие; стоит последним). `what` - «аргументе вызова»/
/// «элементе массива» для текста диагностики. false - диагностика выдана.
bool validateEllipsis(const EllipsisInfo& info, const AstNodeBase& node, AnalysisContext& actx, std::string_view what);

/// ЕДИНЫЙ разворот для ВЫЗОВА (функции и метода): при известной сигнатуре превращает
/// `... expr ...`/`...` в конкретный список аргументов. varidiac - число позиций неизвестно (ошибка).
bool expandCallEllipsis(CallExpr& call, AnalysisContext& actx, const EllipsisInfo& info, const std::vector<TypeId>& paramTypes, bool variadic);

/// ЕДИНЫЙ разворот для ЭЛЕМЕНТОВ МАССИВА: при известной capacity превращает `... expr ...`/`...`
/// в конкретный список элементов (ArgNode).
bool expandArrayEllipsis(DictLiteralNode& node, AnalysisContext& actx, const EllipsisInfo& info, size_t capacity, TypeId elementType);

/// Совместим ли тип операнда с типом целевой позиции: равенство канонических типов, числовое
/// расширение/равенство в одной группе (Int/Unsigned) либо цель - универсальный Any.
/// Неизвестный тип (INVALID) - совместим (проверит кодогенерация).
bool fillingTypeCompatible(const TypeRegistry& reg, TypeId operand, TypeId target);

/// Пост-проход семантики: нераскрытые многоточия в СПИСКОВЫХ позициях (аргументы вызова/элементы
/// коллекций) - явная диагностика. Покрывает позиции, где разворот невозможен: `... src`
/// (раскрытие источника), `...`/`... expr ...` без известного числа позиций, tuple/словарь.
void reportUnresolvedEllipsis(AnalysisContext& actx, const std::vector<AstNodePtr>& roots);

/// Удалить элементы-многоточия из списка ПОСЛЕ выданной диагностики (единый «diagnose-then-discard»,
/// как ErrorExpr): нераскрытое многоточие нейтрализуется, чтобы пост-проход не выдавал каскадную
/// диагностику по тому же узлу. `arrayElements` - список элементов массива (значение в
/// `ArgNode::m_value`), иначе - список аргументов вызова.
void discardEllipsisElements(std::vector<AstNodePtr>& elements, bool arrayElements);

} // namespace trust
