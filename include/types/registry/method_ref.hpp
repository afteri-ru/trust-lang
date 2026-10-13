#pragma once

// include/types/registry/method_ref.hpp
// Результат поиска метода типа: полный ключ (нативность '%'/константность '^') + набор
// интернированных сигнатур (перегрузки, >= 1). Владелец данных — MethodTable; TypeSystem
// не вводит псевдонимов: используется как `trust::MethodRef`.

#include "types/call/callable_info.hpp"
#include "types/type_id.hpp"

#include <string>
#include <vector>

namespace trust {

struct MethodRef {
    std::string key;                        // полный ключ цели (напр. "%count^"); для алиаса - ключ цели
    TypeId owner{INVALID_TYPE_ID};          // тип, объявивший метод (для конверсии неявного object-параметра);
                                            // INVALID - объявление найдено в fallback-таблице (Range/Array/Tensor/
                                            // native-template): `this` имеет тип самого получателя (exact)
    std::vector<CallableSignature> entries; // набор сигнатур (identity + параметры + C++-суффикс), >= 1
};

} // namespace trust
