// Единый источник описания рантайм-символов.
// Рантайм-символ - функция trust-runtime: имя в trust-коде (`runtimeSymbolSourceName`,
// напр. `trust__print__`) и C++-символ (`runtimeSymbolName`, напр. `trust::trust__print__`).
// Использование в сгенерированном коде требует линковки trust-runtime и инклуда публичных
// заголовков (см. TypeSystem и CppTranspiler::recordRuntimeSymbolHeaders). Заголовки берутся как директивы с
// ведущим '@' (путь заголовка внутри пакета стандартной библиотеки stdlib.trust,
// доступный через -Istdlib/include).
// Организован как X-macro: определения (id, C++-символ, заголовки) и функции доступа
// находятся в одном списке TRUST_RUNTIME_SYMBOLS; enum, массивы заголовков,
// runtimeSymbolName/runtimeSymbolHeaders и компайлтайм-проверки генерируются
// автоматически (паттерн, как в syntax/term/term_types.h). Опечатка в имени символа -
// ошибка компиляции, а не «молчаливый» пропуск инклуда.
// ВАЖНО (инвариант): рантайм-символы - ТОЛЬКО не-типовые функции. Типы, чья
// реализация живёт в trust-runtime (Dict, Rational, BigInteger), регистрируются через
// registerBuiltinType и НЕ должны дублироваться здесь: их заголовки подключаются
// по типу (механизм №1 в транспиляторе). Дублирование запрещено и отлавливается
// EXPECT-проверкой в TypeSystem::registerRuntimeSymbol. Сырая C++-вставка
// `{% trust::Dict d; %}` без типизированной ссылки на Dict НЕ должна тянуть
// заголовок - это ожидаемое поведение.
// Размещение в `target` (а не `types`): C++-символ и заголовки — целевые факты бэкенда (R37);
// языковая модель (`types`) их не хранит.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

// ЕДИНСТВЕННОЕ место описания каждого символа: id, C++-символ и набор заголовков
// (директивы с '@', полное транзитивное замыкание). Enum, массивы заголовков,
// функции доступа и компайлтайм-проверки генерируются из этого списка автоматически
// (паттерн X-macro, как в syntax/term/term_types.h). Формат записи:
//   _(Id, "источник-имя", "C++-символ", "@заголовок", ...)
// Источник-имя — имя, которое встречается в trust-коде (например `trust__print__` после
// раскрытия `@print`); C++-символ — имя в сгенерированном C++ (`trust::trust__print__`).
#define TRUST_RUNTIME_SYMBOLS(_)                                                                                  \
    _(kTrustAbort, "trust__abort__", "trust::trust__abort__", "@trust/assert.hpp")                                \
    _(kFormatMessage, "trust::formatMessage", "trust::formatMessage", "@trust/assert.hpp")                        \
    _(kTrustPrint, "trust__print__", "trust::trust__print__", "@trust/io.hpp")                                    \
    _(kCheckedCast, "trust::checked_cast", "trust::checked_cast", "@trust/checked_cast.hpp", "@trust/assert.hpp") \
    _(kAnyTo, "trust::any_to", "trust::any_to", "@trust/any_convert.hpp", "@stdlib/builtin/dict.hpp", "@trust/checked_cast.hpp", "@trust/assert.hpp")

/// Типизированные идентификаторы рантайм-символов (генерируются из списка).
/// Использование enum вместо строкового литерала даёт компайлтайм-проверку имени.
enum class RuntimeSymbolId : uint8_t {
#define RS_ENUM(name, src, sym, ...) name,
    TRUST_RUNTIME_SYMBOLS(RS_ENUM)
#undef RS_ENUM
        kCount,
};

// Массивы заголовков рантайм-символов (по одному на символ; генерируются из списка).
// Строковые литералы преобразуются в std::string_view при инициализации массива.
#define RS_HEADERS(name, src, sym, ...) inline constexpr const std::string_view kRuntimeSymHeaders##name[] = {__VA_ARGS__};
TRUST_RUNTIME_SYMBOLS(RS_HEADERS)
#undef RS_HEADERS

/// Единый источник C++-имён рантайм-символов (генерируется из списка).
constexpr std::string_view runtimeSymbolName(RuntimeSymbolId id) {
    switch (id) {
#define RS_NAME(name, src, sym, ...) \
    case RuntimeSymbolId::name:      \
        return sym;
        TRUST_RUNTIME_SYMBOLS(RS_NAME)
#undef RS_NAME
    case RuntimeSymbolId::kCount:
        break;
    }
    return "";
}

/// Единый источник имён рантайм-символов в trust-коде (генерируется из списка).
constexpr std::string_view runtimeSymbolSourceName(RuntimeSymbolId id) {
    switch (id) {
#define RS_SRC_NAME(name, src, sym, ...) \
    case RuntimeSymbolId::name:          \
        return src;
        TRUST_RUNTIME_SYMBOLS(RS_SRC_NAME)
#undef RS_SRC_NAME
    case RuntimeSymbolId::kCount:
        break;
    }
    return "";
}

/// Полное транзитивное замыкание заголовков рантайм-символа (генерируется из списка).
constexpr std::span<const std::string_view> runtimeSymbolHeaders(RuntimeSymbolId id) {
    switch (id) {
#define RS_HEADERS_CASE(name, src, sym, ...) \
    case RuntimeSymbolId::name:              \
        return kRuntimeSymHeaders##name;
        TRUST_RUNTIME_SYMBOLS(RS_HEADERS_CASE)
#undef RS_HEADERS_CASE
    case RuntimeSymbolId::kCount:
        break;
    }
    return {};
}

/// Находит id рантайм-символа по имени в trust-коде (точное совпадение). nullopt - не символ.
constexpr std::optional<RuntimeSymbolId> findRuntimeSymbolBySourceName(std::string_view name) {
    for (size_t i = 0; i < static_cast<size_t>(RuntimeSymbolId::kCount); ++i) {
        const auto id = static_cast<RuntimeSymbolId>(i);
        if (runtimeSymbolSourceName(id) == name) {
            return id;
        }
    }
    return std::nullopt;
}

/// Находит id рантайм-символа по C++-имени (точное совпадение). nullopt - не символ.
constexpr std::optional<RuntimeSymbolId> findRuntimeSymbolByCppName(std::string_view name) {
    for (size_t i = 0; i < static_cast<size_t>(RuntimeSymbolId::kCount); ++i) {
        const auto id = static_cast<RuntimeSymbolId>(i);
        if (runtimeSymbolName(id) == name) {
            return id;
        }
    }
    return std::nullopt;
}

/// Находит id по ЛЮБОМУ из двух имён (граница: callee в trust-коде или C++-текст).
constexpr std::optional<RuntimeSymbolId> findRuntimeSymbolByName(std::string_view name) {
    if (auto id = findRuntimeSymbolBySourceName(name)) {
        return id;
    }
    return findRuntimeSymbolByCppName(name);
}

// Компайлтайм-инварианты таблицы: каждый символ имеет непустые имя и заголовки (генерируются).
#define RS_ASSERT(name, src, sym, ...)                                   \
    static_assert(runtimeSymbolName(RuntimeSymbolId::name) != "");       \
    static_assert(runtimeSymbolSourceName(RuntimeSymbolId::name) != ""); \
    static_assert(!runtimeSymbolHeaders(RuntimeSymbolId::name).empty());
TRUST_RUNTIME_SYMBOLS(RS_ASSERT)
#undef RS_ASSERT

#undef TRUST_RUNTIME_SYMBOLS

// C++-символ, присутствие которого в сгенерированном коде требует линковки
// trust-runtime библиотеки (и, как правило, инклуда её публичного заголовка).
// Символ ищется как подстрока в тексте сгенерированного файла; runtimeHeaders -
// директивы препроцессора с ведущим '@' (путь заголовка внутри stdlib.trust).
struct RuntimeSymbol {
    std::string symbol;                      ///< подстрока C++-кода, напр. "trust__abort__"
    std::vector<std::string> runtimeHeaders; ///< директивы препроцессора с ведущим '@'
};

/// Список рантайм-символов (единственный источник — X-macro таблица выше).
[[nodiscard]] const std::vector<RuntimeSymbol>& runtimeSymbols();

} // namespace trust
