// runtime_symbols.hpp - единый источник описания рантайм-символов.
//
// Рантайм-символ - C++-символ, использование которого в сгенерированном коде
// требует линковки trust-runtime и инклуда публичных заголовков (см. TypeRegistry
// и CppTranspiler::recordRuntimeSymbolHeaders). Заголовки берутся как директивы с
// ведущим '@' (путь = имя ELF-секции внутри trust-runtime.so/.a).
//
// Организован как X-macro: определения (id, C++-символ, заголовки) и функции доступа
// находятся в одном списке TRUST_RUNTIME_SYMBOLS; enum, массивы заголовков,
// runtimeSymbolName/runtimeSymbolHeaders и компайлтайм-проверки генерируются
// автоматически (паттерн, как в syntax/term_types.h). Опечатка в имени символа -
// ошибка компиляции, а не «молчаливый» пропуск инклуда (раньше call-сайты передавали
// строковые литералы `recordRuntimeSymbolHeaders("trust::any_to")` - опечатка молча не
// давала заголовка).
//
// ВАЖНО (инвариант): рантайм-символы - ТОЛЬКО не-типовые функции. Типы, чья
// реализация живёт в trust-runtime (Dict, Rational), регистрируются через
// registerBuiltinType и НЕ должны дублироваться здесь: их заголовки подключаются
// по типу (механизм №1 в транспиляторе). Дублирование запрещено и отлавливается
// EXPECT-проверкой в TypeRegistry::registerRuntimeSymbol. Сырая C++-вставка
// `{% trust::Dict d; %}` без типизированной ссылки на Dict НЕ должна тянуть
// заголовок - это ожидаемое поведение.
//
// Размещение в types (а не runtime): таблицу потребляют TypeRegistry и
// CppTranspiler (сторона компилятора); types - нижний слой, runtime зависит от
// types (runtime_lib линкует types_lib), поэтому обратная зависимость types→runtime
// запрещена.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace trust {

// -- Таблица рантайм-символов (X-macro) -------------------------------------
// ЕДИНСТВЕННОЕ место описания каждого символа: id, C++-символ и набор заголовков
// (директивы с '@', полное транзитивное замыкание). Enum, массивы заголовков,
// функции доступа и компайлтайм-проверки генерируются из этого списка автоматически
// (паттерн X-macro, как в syntax/term_types.h). Формат записи:
//   _(Id, "C++-символ", "@заголовок", ...)  - заголовки переменным числом (через запятую).
#define TRUST_RUNTIME_SYMBOLS(_)                                                           \
    _(kTrustAbort, "trust::trust__abort__", "@trust/assert.hpp")                           \
    _(kFormatMessage, "trust::formatMessage", "@trust/assert.hpp")                         \
    _(kTrustPrint, "trust::trust__print__", "@trust/io.hpp")                               \
    _(kCheckedCast, "trust::checked_cast", "@trust/checked_cast.hpp", "@trust/assert.hpp") \
    _(kAnyTo, "trust::any_to", "@trust/any_convert.hpp", "@trust/dict.hpp", "@trust/rational.hpp", "@trust/checked_cast.hpp", "@trust/assert.hpp")

/// Типизированные идентификаторы рантайм-символов (генерируются из списка).
/// Использование enum вместо строкового литерала даёт компайлтайм-проверку имени.
enum class RuntimeSymbolId : uint8_t {
#define RS_ENUM(name, sym, ...) name,
    TRUST_RUNTIME_SYMBOLS(RS_ENUM)
#undef RS_ENUM
        kCount,
};

// Массивы заголовков рантайм-символов (по одному на символ; генерируются из списка).
// Строковые литералы преобразуются в std::string_view при инициализации массива.
#define RS_HEADERS(name, sym, ...) inline constexpr const std::string_view kRuntimeSymHeaders##name[] = {__VA_ARGS__};
TRUST_RUNTIME_SYMBOLS(RS_HEADERS)
#undef RS_HEADERS

/// Единый источник строковых имён рантайм-символов (генерируется из списка).
constexpr std::string_view runtimeSymbolName(RuntimeSymbolId id) {
    switch (id) {
#define RS_NAME(name, sym, ...) \
    case RuntimeSymbolId::name: \
        return sym;
        TRUST_RUNTIME_SYMBOLS(RS_NAME)
#undef RS_NAME
    case RuntimeSymbolId::kCount:
        break;
    }
    return "";
}

/// Полное транзитивное замыкание заголовков рантайм-символа (генерируется из списка).
constexpr std::span<const std::string_view> runtimeSymbolHeaders(RuntimeSymbolId id) {
    switch (id) {
#define RS_HEADERS_CASE(name, sym, ...) \
    case RuntimeSymbolId::name:         \
        return kRuntimeSymHeaders##name;
        TRUST_RUNTIME_SYMBOLS(RS_HEADERS_CASE)
#undef RS_HEADERS_CASE
    case RuntimeSymbolId::kCount:
        break;
    }
    return {};
}

/// Находит id рантайм-символа по точному имени. Ведущий '%' срезается (как в
/// семантическом распознавании нативных имён `%trust::...`). nullopt - имя не символ.
constexpr std::optional<RuntimeSymbolId> findRuntimeSymbolByName(std::string_view name) {
    if (name.size() > 1 && name.front() == '%') {
        name.remove_prefix(1);
    }
    for (size_t i = 0; i < static_cast<size_t>(RuntimeSymbolId::kCount); ++i) {
        const auto id = static_cast<RuntimeSymbolId>(i);
        if (runtimeSymbolName(id) == name) {
            return id;
        }
    }
    return std::nullopt;
}

// Компайлтайм-инварианты таблицы: каждый символ имеет непустые имя и заголовки (генерируются).
#define RS_ASSERT(name, sym, ...)                                  \
    static_assert(runtimeSymbolName(RuntimeSymbolId::name) != ""); \
    static_assert(!runtimeSymbolHeaders(RuntimeSymbolId::name).empty());
TRUST_RUNTIME_SYMBOLS(RS_ASSERT)
#undef RS_ASSERT

#undef TRUST_RUNTIME_SYMBOLS

} // namespace trust
