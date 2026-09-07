// trust/interrupt.hpp - прерывания потока выполнения для TrustLang.
//
// Public runtime header: self-contained (standard headers only) so that generated
// C++ programs can include it without depending on the compiler's include tree.
// At build time it is embedded into trust-runtime.so/.a (via #embed, in an ELF
// section named "trust/interrupt.hpp"); the pipeline extracts it into a temporary
// `trust/` directory when a generated program uses try/catch-блоки (TRY_*).
//
// Классификация «ошибка или нет» идёт по классу прерывания (см.
// docs/architecture/errors.md):
//   IntAny   - базовый класс прерываний (ловится универсальным блоком {* ... *});
//   IntPlus  - «положительное» прерывание (++ value ++ / сквозной возврат через
//              вызовы функций) = штатная логика (блок {+ ... +});
//   IntMinus - «отрицательное» прерывание (-- err -- / ошибка) (блок {- ... -}).
//
// IntPlus (control flow) НЕ является std::exception - это обычный путь возврата, а не
// ошибка. IntMinus (категория «ошибка»), наоборот, НАСЛЕДУЕТ std::runtime_error: такие
// прерывания ловятся и trust-блоком {- ... -}, и C++-кодом через catch(std::runtime_error&).
// Встроенная в язык ошибка переполнения стека (stack_overflow) - подкласс IntMinus,
// поэтому перехватывается блоком {- ... -} (см. trust/stack_check.hpp).
// Прочие C++-исключения (не IntMinus) свободно проходят сквозь блоки прерываний.

#pragma once

#include <any>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace trust {

/// Типовая проверка значения для оператора сопоставления `~>` (и `~~>`/`~~~>`).
/// - Для полиморфных значений (классы прерываний IntAny/IntPlus/IntMinus и их наследники) -
///   `dynamic_cast`: совпадает тип T И его подтипы (как catch по базовому классу).
/// - Для не-полиморфных (скаляры и т.п.) - точное сравнение `typeid` (иначе `dynamic_cast` не
///   скомпилировался бы). Полиморфность определяется по СТАТИЧЕСКОМУ типу V выражения.
template <class T, class V>
inline bool typeMatches(const V& v) noexcept {
    if constexpr (std::is_polymorphic_v<V>) {
        return dynamic_cast<const T*>(&v) != nullptr;
    } else {
        return typeid(v) == typeid(T);
    }
}

/// Базовый класс прерываний потока выполнения (НЕ std::exception - см. errors.md).
/// Нужен универсальному блоку {* ... *} и динамической диспетчеризации (match по классу).
class IntAny {
  public:
    virtual ~IntAny() = default;
    IntAny() = default;
    IntAny(const IntAny&) = default;
    IntAny& operator=(const IntAny&) = default;
};

/// Положительное прерывание: сквозной возврат значения (штатная логика).
/// Переносимое значение (для будущей обработки через match) - в value().
/// НЕ std::exception: это control flow (обычный путь возврата), а не ошибка.
class IntPlus : public IntAny {
  public:
    IntPlus() = default;
    template <typename V>
    explicit IntPlus(V&& v)
    : m_value(std::forward<V>(v)) {}

    std::any& value() noexcept { return m_value; }
    const std::any& value() const noexcept { return m_value; }

  private:
    std::any m_value;
};

/// Отрицательное прерывание: ошибка. Наследует std::runtime_error, поэтому ловится и
/// trust-блоком {- ... -}, и C++-кодом (catch(std::runtime_error&)/catch(std::exception&)).
/// Переносимое значение (для будущей обработки через match) - в value().
class IntMinus : public IntAny, public std::runtime_error {
  public:
    IntMinus()
    : std::runtime_error("IntMinus") {}

    /// Явное сообщение (напр. для встроенных ошибок языка: stack_overflow).
    explicit IntMinus(std::string msg)
    : std::runtime_error(std::move(msg)) {}

    template <typename V>
    explicit IntMinus(V&& v)
    : std::runtime_error("IntMinus")
    , m_value(std::forward<V>(v)) {}

    std::any& value() noexcept { return m_value; }
    const std::any& value() const noexcept { return m_value; }

  private:
    std::any m_value;
};

} // namespace trust
