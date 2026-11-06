#pragma once

// Метаданные вызова, НЕ входящие в идентичность типа: имена параметров и наличие значения
// по умолчанию. Нужны для привязки именованных/опциональных аргументов к слотам сигнатуры.
// Значения по умолчанию и имена НЕ входят в TypeKey/суффикс перегрузки (C++-идентичность
// сигнатуры определяется только типами параметров).

#include <cstddef>
#include <string>
#include <vector>

#include "location/decl_id.hpp"
#include "types/type_id.hpp"
#include "types/visibility.hpp"

namespace trust {

/// Один параметр вызываемого набора: имя и наличие значения по умолчанию.
struct ParamSlot {
    std::string name;
    bool hasDefault = false;
};

struct CallableParamInfo {
    std::vector<ParamSlot> slots; ///< порядок = порядок объявленных параметров

    /// Число обязательных параметров: первый параметр с default и все последующие считаются
    /// опциональными (C++-совместимое требование «defaults только в хвосте»).
    [[nodiscard]] std::size_t requiredCount() const noexcept {
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (slots[i].hasDefault) {
                return i;
            }
        }
        return slots.size();
    }

    [[nodiscard]] bool hasAnyDefault() const noexcept {
        for (const ParamSlot& slot : slots) {
            if (slot.hasDefault) {
                return true;
            }
        }
        return false;
    }
};

/// ОДНА сигнатура вызываемого набора: identity + метаданные параметров + C++-суффикс.
/// Единый элемент для `Symbol::overloads` и `TypeDescriptor::methods[key]`.
struct CallableSignature {
    TypeId signature{INVALID_TYPE_ID}; ///< интернированный FunctionTypeData (identity)
    CallableParamInfo params;          ///< имена/defaults (не входят в TypeKey)
    /// Идентичность объявления метода — КЛЮЧ целевого C++-суффикса в контракте `target`
    /// (`DeclCppFacts::cppSuffixOf`), а не сам факт (R1): второй копии суффикса в `types` нет.
    /// Языковая идентичность (не целевой факт). Пусто — у синтетического/builtin-метода нет
    /// объявления, целевой суффикс не назначается.
    DeclId decl{};
    /// C++-доступ МЕТОДА тела класса (Public по умолчанию; у свободных функций/встроенных
    /// методов всегда Public). Ставит семантика из `AstNodeBase::visibility`; читает проверка доступа.
    Visibility access{Visibility::Public};
};

} // namespace trust
