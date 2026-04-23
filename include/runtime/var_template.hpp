#pragma once

#include <array>
#include <concepts>
#include <format>
#include <string>
#include <vector>

namespace trust {

// ============================================================================
// Шаблонная спецификация — единый источник для шаблонных типов в Var
// ============================================================================

// --- Концепт: допустимые элементы для Vector<Element> ---
template <typename T>
concept VectorElement = std::integral<T> || std::floating_point<T>;

// ============================================================================
// Реестр поддерживаемых инстанцирований Vector
// Формат: V(Kind, Type, Category, MinFormat)
// ============================================================================
#define SUPPORTED_VECTORS_EXPAND(V)                       \
    V(VectorInt64, std::vector<int64_t>, VectorCat, Trad) \
    V(VectorDouble, std::vector<double>, VectorCat, Trad)

// ============================================================================
// Автоматическая генерация конверсий из SUPPORTED_VECTORS_EXPAND
// Каждый Vector<T> получает: Vector -> String, Vector -> WideString, Vector -> Self
// ============================================================================
// TRUST_VAR_VECTOR_CONVERSIONS(M) is defined in var.hpp AFTER VarKind is declared.
// This file only provides SUPPORTED_VECTORS_EXPAND and the case-generation macros.
#define TRUST_VAR_VECTOR_CONVERSIONS(M) /* defined later in var.hpp */

// ============================================================================
// Генерация switch-case для векторов в var.cpp
// Использование:
//   SUPPORTED_VECTORS_EXPAND(TRUST__VEC_CASE_STRING)
//   SUPPORTED_VECTORS_EXPAND(TRUST__VEC_CASE_WSTRING)
// После использования — #undef эти макросы.
// ============================================================================
// Функция для получения имени типа вектора как строки
// (используется для генерации std::get<type>(m_value))
#define TRUST__VEC_CASE_STRING(name, type, cat, fmt) \
    case VarKind::name:                              \
        return format_vector_as_string(std::get<type>(m_value));
#define TRUST__VEC_CASE_WSTRING(name, type, cat, fmt) \
    case VarKind::name:                               \
        return widen_string(format_vector_as_string(std::get<type>(m_value)));

// ============================================================================
// Шаблонное форматирование для всех Vector<ElementType>
// ============================================================================
template <VectorElement T>
[[nodiscard]] inline std::string format_vector_as_string(const std::vector<T> &v) {
    std::string result = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0)
            result += ", ";
        result += std::format("{}", v[i]);
    }
    result += "]";
    return result;
}

// ============================================================================
// Расширяемость: будущие шаблонные типы
// ============================================================================

// Шаблонная спецификация для std::array<T, N>
// template <typename T, size_t N>
// concept ArrayElement = VectorElement<T>;
//
// #define SUPPORTED_ARRAYS_EXPAND(V) \
//     V(Array4Int64, std::array<int64_t, 4>, ArrayCat, Trad) \
//     V(Array3Double, std::array<double, 3>,  ArrayCat, Trad)

} // namespace trust