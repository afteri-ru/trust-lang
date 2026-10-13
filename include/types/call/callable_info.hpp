#pragma once

// include/types/call/callable_info.hpp
// Метаданные вызова, НЕ входящие в идентичность типа: имена параметров и наличие значения
// по умолчанию. Нужны для привязки именованных/опциональных аргументов к слотам сигнатуры.
// Значения по умолчанию и имена НЕ входят в TypeKey/суффикс перегрузки (C++-идентичность
// сигнатуры определяется только типами параметров).

#include <cstddef>
#include <string>
#include <vector>

#include "types/type_id.hpp"

namespace trust {

struct CallableParamInfo {
    std::vector<std::string> names; // aligned with FunctionTypeData::paramTypes
    std::vector<bool> hasDefault;   // aligned; true - параметр опциональный (default в объявлении)

    [[nodiscard]] bool valid() const noexcept { return names.size() == hasDefault.size(); }

    /// Число обязательных параметров: первый параметр с default и все последующие считаются
    /// опциональными (C++-совместимое требование «defaults только в хвосте»).
    [[nodiscard]] std::size_t requiredCount() const noexcept {
        for (std::size_t i = 0; i < hasDefault.size(); ++i) {
            if (hasDefault[i]) {
                return i;
            }
        }
        return names.size();
    }

    [[nodiscard]] bool hasAnyDefault() const noexcept {
        for (const bool d : hasDefault) {
            if (d) {
                return true;
            }
        }
        return false;
    }
};

/// ОДНА сигнатура вызываемого набора: identity + метаданные параметров + C++-суффикс.
/// Единый элемент для Symbol::overloads и TypeDescriptor::methods[key] — вместо
/// параллельных массивов signatures/params/suffixes, выровненных по индексу.
struct CallableSignature {
    TypeId signature{INVALID_TYPE_ID}; ///< интернированный FunctionTypeData (identity)
    CallableParamInfo params;          ///< имена/defaults (не входят в TypeKey)
    std::string cppSuffix;             ///< C++-суффикс от identity объявления
};

} // namespace trust
