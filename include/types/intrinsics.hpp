// intrinsics.hpp - единый источник описания интринсиков языка.
//
// Интринсик - распознаваемое компилятором имя вызова (область `trust`, имя с префиксом
// `intrinsic_`, например `trust::intrinsic_assert`), которое НЕ является реальной C++-функцией:
// в теле раскрытия DSL-макроса вставляется ИМЯ интринсика (а не готовый код), а сам интринсик
// «разворачивается» на этапе генерации кода (CppTranspiler::emitIntrinsic) в конкретную
// реализацию (как __builtin_* в GCC/clang). Это даёт единую точку для рантайм-проверок и
// позволяет менять реализацию без правки макросов и без копипаста тела макроса в кодогенерации.
//
// Организован как X-macro (паттерн как в types/runtime_symbols.hpp): единый список
// TRUST_INTRINSICS порождает enum IntrinsicId, имя, заголовки и компайлтайм-проверки.
// Интринсики НЕ являются рантайм-символами (нет реального C++-символа/линковки) - только
// имя + требуемый заголовок рантайм-заголовков (если реализация использует рантайм-функции).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace trust {

// -- Таблица интринсиков (X-macro) --------------------------------------
// ЕДИНСТВЕННОЕ место описания каждого интринсика: id, имя вызова (область trust, префикс
// `intrinsic_` - отличимо от стандартных/рантайм-функций) и заголовки рантайм-заголовков.
//   _(Id, "trust::intrinsic_<name>", "@заголовок", ...)
#define TRUST_INTRINSICS(_) _(kTrustAssert, "trust::intrinsic_assert", "@trust/assert.hpp")

/// Типизированные идентификаторы интринсиков (генерируются из списка).
enum class IntrinsicId : uint8_t {
#define INTR_ENUM(name, sym, ...) name,
    TRUST_INTRINSICS(INTR_ENUM)
#undef INTR_ENUM
        kCount,
};

// Массивы заголовков интринсиков (по одному на интринсик; генерируются из списка).
#define INTR_HEADERS(name, sym, ...) inline constexpr const std::string_view kIntrinsicHeaders##name[] = {__VA_ARGS__};
TRUST_INTRINSICS(INTR_HEADERS)
#undef INTR_HEADERS

/// Единый источник строкового имени интринсика (генерируется из списка).
constexpr std::string_view intrinsicName(IntrinsicId id) {
    switch (id) {
#define INTR_NAME(name, sym, ...) \
    case IntrinsicId::name:       \
        return sym;
        TRUST_INTRINSICS(INTR_NAME)
#undef INTR_NAME
    case IntrinsicId::kCount:
        break;
    }
    return "";
}

/// Заголовки рантайм-заголовков, требуемые реализацией интринсика (генерируются из списка).
constexpr std::span<const std::string_view> intrinsicHeaders(IntrinsicId id) {
    switch (id) {
#define INTR_HEADERS_CASE(name, sym, ...) \
    case IntrinsicId::name:               \
        return kIntrinsicHeaders##name;
        TRUST_INTRINSICS(INTR_HEADERS_CASE)
#undef INTR_HEADERS_CASE
    case IntrinsicId::kCount:
        break;
    }
    return {};
}

/// Находит id интринсика по точному имени. Интринсик - НЕ нативная функция (без префикса '%'),
/// поэтому имя сопоставляется дословно. nullopt - имя не интринсик.
constexpr std::optional<IntrinsicId> findIntrinsicByName(std::string_view name) {
    for (size_t i = 0; i < static_cast<size_t>(IntrinsicId::kCount); ++i) {
        const auto id = static_cast<IntrinsicId>(i);
        if (intrinsicName(id) == name) {
            return id;
        }
    }
    return std::nullopt;
}

// Компайлтайм-инварианты таблицы: у каждого интринсика непустые имя и заголовки.
#define INTR_ASSERT(name, sym, ...)                        \
    static_assert(intrinsicName(IntrinsicId::name) != ""); \
    static_assert(!intrinsicHeaders(IntrinsicId::name).empty());
TRUST_INTRINSICS(INTR_ASSERT)
#undef INTR_ASSERT

#undef TRUST_INTRINSICS

} // namespace trust