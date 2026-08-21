// trust/enum.hpp - типобезопасное перечисление для рантайма Trust.
//
// Публичный рантайм-заголовок: самодостаточный (только стандартные заголовки), чтобы
// сгенерированные C++-программы могли включать его без зависимости от include-дерева
// компилятора. На этапе сборки внедряется в trust-runtime.so/.a (через #embed, в ELF-секции
// с именем "trust/enum.hpp"); пайплайн извлекает его во временный каталог `trust/`, когда
// программа использует тип Enum.
//
// Дизайн:
//   - Enum - типобезопасное перечисление: ЕДИНЫЙ тип значений (Value) для всех членов.
//   - Шаблон `Enum<ValueT, N, Self>` предоставляет generic-логику: поле значения value, поле
//     порядкового номера (позиции) ordinal, constexpr-конструкторы, операторы сравнения
//     <,>,<=,>=,==,!=, count(), fromName()/fromValue() (поиск по таблице Self::kMembers).
//     Self - производная структура (CRTP), объявленная кодогенератором; её члены - `static const`
//     константы (`Color.RED`), определяемые out-of-class (`const c_Color c_Color::c_RED{...};`),
//     а таблица имя↔значение `static constexpr Self::kMembers[N]` - для fromName/fromValue.
//   - СРАВНЕНИЕ ИДЁТ ПО ЗНАЧЕНИЮ ЧЛЕНА (value), а НЕ по позиции (ordinal): позиция и значение -
//     разные понятия, порядок по позиции и по значению могут разойтись при произвольных
//     значениях (RED=10, GREEN=2 → по значению RED > GREEN). Это как IntEnum: алиасы с равными
//     значениями сравниваются `==`. Требование сравнимости типа значений (напр. StrChar имеет
//     `<`) - на типе Value; для несравнимых типов операторы `<` не скомпилируются.
//   - Производная структура наследует конструкторы (`using Enum::Enum`), поэтому
//     `c_Color{value, ordinal}` создаёт член из значения и позиции.
//   - Универсальное имя типа значений - вложенный алиас `Value` (аналог underlying_type);
//     тип значений может быть нечисловым (Bool/StrChar/Rational/...), для совместимых
//     неоднородных членов тип выводится по общим правилам, при несовместимости - Any
//     (с предупреждением анализатора WidenAny).

#pragma once

#include <cstddef>
#include <string_view>

namespace trust {

// Элемент таблицы имя↔значение члена enum (для fromName/fromValue). value - значение члена,
// name - имя члена (C-строка). Таблица `Self::kMembers[N]` производной структуры хранится
// в порядке объявления (индекс = ordinal).
template <typename ValueT>
struct EnumMember {
    ValueT value;
    const char* name;
};

// -- Enum<ValueT, N, Self> - generic-ядро типобезопасного перечисления ----------
// ValueT - тип значений членов (Color.Value); N - число членов; Self - производная структура
// (CRTP), предоставляющая `static constexpr EnumMember<ValueT> kMembers[N]` и `static const`
// константы-члены.
template <typename ValueT, std::size_t N, typename Self>
struct Enum {
    using Value = ValueT; // универсальное имя типа значений (Color.Value)

    Value value{};         // значение члена (тип един для всех членов)
    std::size_t ordinal{}; // позиция члена (порядок объявления) - НЕ используется в сравнении

    constexpr Enum() = default;
    constexpr Enum(Value v, std::size_t o)
    : value(v)
    , ordinal(o) {}

    // Сравнение ПО ЗНАЧЕНИЮ членов (value), а не по позиции (ordinal). Типобезопасность
    // (сравниваются только однотипные enum) проверяется анализатором. Операторы условно
    // доступны (C++20 requires): `==`/`!=` - если тип значений поддерживает `==`;
    // `<,>,<=,>=` - если поддерживает `<`. Для несравнимого типа значений соответствующие
    // операторы не участвуют в перегрузке → чистая ошибка «нет подходящего оператора».
    constexpr bool operator==(const Enum& o) const
        requires requires { value == o.value; }
    {
        return value == o.value;
    }
    constexpr bool operator!=(const Enum& o) const
        requires requires { value == o.value; }
    {
        return value != o.value;
    }
    constexpr bool operator<(const Enum& o) const
        requires requires { value < o.value; }
    {
        return value < o.value;
    }
    constexpr bool operator>(const Enum& o) const
        requires requires { value < o.value; }
    {
        return value > o.value;
    }
    constexpr bool operator<=(const Enum& o) const
        requires requires { value < o.value; }
    {
        return value <= o.value;
    }
    constexpr bool operator>=(const Enum& o) const
        requires requires { value < o.value; }
    {
        return value >= o.value;
    }

    static constexpr std::size_t count() { return N; } // число членов

    // Поиск члена ПО ИМЕНИ (из таблицы Self::kMembers) → член (Self). Не найден → default Self.
    // Не constexpr: Self::kMembers может содержать не-литеральные типы (Rational), инициализируемые
    // в рантайме; поэтому поиск выполняется во время выполнения.
    static Self fromName(std::string_view name) {
        for (std::size_t i = 0; i < N; ++i) {
            if (Self::kMembers[i].name == name) {
                return Self{Self::kMembers[i].value, i};
            }
        }
        return Self{};
    }

    // Поиск члена ПО ЗНАЧЕНИЮ (из таблицы Self::kMembers) → член (Self). Не найден → default Self.
    static Self fromValue(Value v) {
        for (std::size_t i = 0; i < N; ++i) {
            if (Self::kMembers[i].value == v) {
                return Self{Self::kMembers[i].value, i};
            }
        }
        return Self{};
    }
};

} // namespace trust
