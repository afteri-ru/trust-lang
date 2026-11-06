#pragma once

// Результат поиска метода типа: ключ-имя-форма MethodKey + запись (сигнатуры + isNative).
// Владелец данных — MethodTable; TypeSystem не вводит псевдонимов: используется как `trust::MethodRef`.

#include "types/call/callable_info.hpp"
#include "types/type/method_key.hpp"
#include "types/type_id.hpp"

#include <string>
#include <vector>

namespace trust {

struct MethodRef {
    MethodKey key;                          // ключ цели (имя-форма; для алиаса - ключ цели)
    TypeId owner{INVALID_TYPE_ID};          // тип, объявивший метод (для конверсии неявного object-параметра);
                                            // INVALID - объявление найдено в fallback-таблице (Range/Array/Tensor/
                                            // native-template): `this` имеет тип самого получателя (exact)
    std::vector<CallableSignature> entries; // набор сигнатур (identity + параметры + ключ суффикса DeclId), >= 1
    bool isNative = false;                  // C++-проекция записи (имя без манглинга)
};

} // namespace trust
