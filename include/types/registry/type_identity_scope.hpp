#pragma once

// include/types/registry/type_identity_scope.hpp
// Отношение идентичности типа (диспетчер `TypeSystem::identityOf`). Явный выбор обязателен:
// разные потребители используют разные отношения (структурный ключ, семантика Trust,
// C++-представление).

#include <cstdint>

namespace trust {

enum class TypeIdentityScope : uint8_t {
    kStructural, ///< снять type-state флаги (getUnqualifiedType); ключ интернирования
    kSemantic,   ///< семантика Trust: пользовательский алиас номинален, встроенный раскрыт
    kCanonical,  ///< C++-представление: корень alias-цепочки верхнего уровня (shallow; глубокая — FunctionIdentity)
};

} // namespace trust
