#pragma once
// include/types/type/data_builtin.hpp
// BuiltinDefinition и RuntimeSymbol: реализация встроенного типа и рантайм-символ.
// Часть модели данных типов: самостоятельным контрактом НЕ является (umbrella —
// types/type_data.hpp). Набор include повторяет umbrella, чтобы часть компилировалась
// независимо от порядка включения.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "attrs/attr.hpp"
#include "location/location.hpp"
#include "location/module_index.hpp"
#include "types/call/callable_info.hpp"
#include "types/group.hpp"
#include "types/type/ref_type.hpp"
#include "types/type_id.hpp"

namespace trust {

// -- Builtin definition (реализация предварительно объявленного типа) --------
// Отдельного статуса нет: НАЛИЧИЕ определения = наличие реализации. Определение
// приходит из intrinsic-таблицы компилятора или из провайдер-модуля (`Name = %cpp { ... };`).
// Новый TypeId не создаётся: реализация привязывается к предварительному объявлению.
struct BuiltinDefinition {
    std::string cppName;
    std::vector<std::string> preprocIncludes;
    std::vector<std::string> libs;
};

// -- Runtime symbol -----------------------------------------
// C++-символ, присутствие которого в сгенерированном коде требует линковки
// trust-runtime библиотеки (и, как правило, инклуда её публичного заголовка).
// Символ ищется как подстрока в тексте сгенерированного файла; runtimeHeader -
// директива препроцессора с ведущим '@' (путь заголовка внутри stdlib.trust,
// доступный через -Istdlib/include).
struct RuntimeSymbol {
    // подстрока C++-кода, напр. "trust__abort__"
    std::string symbol;
    // список директив препроцессора с ведущим '@' (пути заголовков TrustLang)
    std::vector<std::string> runtimeHeaders;
};

} // namespace trust
