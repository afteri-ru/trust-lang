#pragma once

// include/symbols/table/symbol.hpp
// Значения таблицы символов: результат регистрации, символ и его перегрузки.
// Без поведения таблицы: подсистемы (скоупы/реестр/поверхность/сайты/алиасы) включают этот
// заголовок, не завися от фасада SymbolTable.

#include "types/call.hpp"
#include "types/type_id.hpp"
#include "utils/error.hpp"
#include "utils/string_interner.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {

class AstNodeBase;

/// Стабильное имя символа: view в StringInterner (временные строки безопасны).
[[nodiscard]] inline std::string_view stableSymbolName(std::string_view name) {
    return internText(name);
}

/// Результат регистрации символа с поддержкой завершения forward-объявления.
enum class DeclResult {
    Inserted,   ///< Новое имя вставлено в текущий скоуп.
    Completed,  ///< Существующее forward-объявление завершено определением (символ обновлён).
    Overloaded, ///< Имя уже занято функцией с ОТЛИЧАЮЩЕЙСЯ сигнатурой: набор перегрузок расширен.
    /// ТО ЖЕ объявление (тот же узел AST) уже зарегистрировано под этим ключом. Не дубль:
    /// повторный/ромбовидный импорт регистрирует ОДНУ AST (одна AST на модуль), поэтому
    /// совпадение узла и есть признак «тот же модуль». Хуки повторно НЕ уведомляются.
    AlreadyRegistered,
    Duplicate, ///< Имя уже занято: точный дубль / конфликт kinds (func vs var).
    /// C++-идентичная сигнатура, но структурно иная (напр. переименованы binder'ы) при
    /// завершении forward-объявления. Явная ошибка, а не ложный Duplicate/тихое завершение.
    SignatureMismatch,
};

/// Месторасположение переменной (физическая память) - как принято в компиляторах.
/// Фиксируется анализатором имён при создании переменной.
/// Единый источник: X-макрос генерирует и enum, и читаемые имена (storageName в debug_scope.hpp).
#define TRUST_STORAGE_NAMES(O)                                \
    /* глобальный сегмент данных (модуль/глобальный скоуп) */ \
    O(Global, "global")                                       \
    /* стек (функция/блок) */                                 \
    O(Local, "local")                                         \
    /* статические данные области имён (имя содержит '::') */ \
    O(Static, "static")                                       \
    /* TLS-сегмент (атрибут @[thread_local]) */               \
    O(ThreadLocal, "thread")

enum class Storage {
#define TRUST_STORAGE_ENUM(name, text) name,
    TRUST_STORAGE_NAMES(TRUST_STORAGE_ENUM)
#undef TRUST_STORAGE_ENUM
};

/// Одна перегрузка функции на привязке имени: сигнатура + метаданные вызова + объявление.
/// Элементы набора самодостаточны: сигнатура, метаданные вызова и объявление в одном элементе.
struct SymbolOverload {
    TypeId signature{INVALID_TYPE_ID}; ///< интернированный FunctionTypeData
    CallableParamInfo params;          ///< имена/defaults параметров объявления
    AstNodeBase* decl = nullptr;       ///< узел объявления (FuncDecl) перегрузки
    /// Namespace-путь объявления внутри его модуля (`ns::sub`; пусто — модульный уровень).
    /// Материализуется таблицей из ключа регистрации: кодоген обязан печатать C++-имя
    /// ВЫБРАННОЙ перегрузки, а не spelling ссылки (слияние `using` из разных областей).
    std::string_view namespacePath;
};

/// Symbol entry stored in the symbol table.
/// Узел объявления (decl) - источник истины: kind, range, атрибуты и определение (init/body).
/// QualType type - type-state места: identity + const/inferred.
/// Identity-часть — `type.getTypeId()`; состояние — методы `isConst()/isInferred()`.
/// Flow-факты (definite assignment) вычисляет CFG/dataflow-проход (semantic/definite_assignment).
struct Symbol {
    std::string_view name;                                        ///< Canonical name (view: AST/StringInterner)
    QualType type;                                                ///< Type-state (identity + состояние).
    AstNodeBase* decl = nullptr;                                  ///< Узел объявления (VarDecl/FuncDecl/TypeDecl/ArgNode); невладеющая ссылка.
                                                                  ///< Не-const: анализатор пишет финальный тип в узел (VarDecl::inferredType);
                                                                  ///< хук-анализаторы и диагностика читают узел как const.
    Storage storage = Storage::Global;                            ///< Месторасположение переменной (физическая память).
    int64_t dims = -1;                                            ///< Размерность (для словаря - число элементов) как компиляционное
                                                                  ///< свойство переменной. -1 = неизвестно (absent); >= 0 = static.
                                                                  ///< Копируется из типа/инициализатора; используется для статической
                                                                  ///< проверки индекса `d.1`.
    std::vector<std::pair<std::string, QualType>> dictFieldTypes; ///< Типы полей словаря из литерала-инициализатора.

    /// Набор перегрузок функционального имени: НЕПУСТ для функций (>= 1 элемент; при нескольких
    /// элементах имя перегружено) и ПУСТ для не-функций. `type`/`decl` — представитель (первый
    /// элемент) для запросов «символ как значение»; единственный корректный путь выбора
    /// сигнатуры — резолвер (types/overload_resolve.hpp).
    std::vector<SymbolOverload> overloads;
};

/// Проекция набора сигнатур (identity) символа в порядке набора. Требует вызываемый символ
/// (непустой `overloads`): для не-функции проекция не определена.
[[nodiscard]] inline std::vector<TypeId> overloadSignatures(const Symbol& sym) {
    EXPECT(!sym.overloads.empty() && "overloadSignatures requires a callable symbol");
    std::vector<TypeId> out;
    out.reserve(sym.overloads.size());
    for (const SymbolOverload& overload : sym.overloads) {
        out.push_back(overload.signature);
    }
    return out;
}

} // namespace trust
