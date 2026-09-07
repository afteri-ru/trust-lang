#pragma once

#include "diag/options.hpp"

#include <functional>

// include/diag/registry.hpp
// Пер-компонентная регистрация диагностик/флагов.
//
// Каждый компонент регистрирует СВОИ severity-диагностики и feature-флаги (Options::add(DiagId)
// / Options::add(FlagKind)) на static-init рядом со своим кодом, через registerDiagnostics().
// Метаданные (имя/подсказка/severity/группы) берутся из пер-компонентного набора
// TRUST_DIAG_SET/TRUST_FLAG_SET (diag/diag_set.hpp) через ADL при Options::add<T>/add_flag<T>.
// Context при создании Options вызывает
// applyRegisteredDiagnostics(), который применяет базовые (diag-владение) опции и все
// зарегистрированные компонентные колбэки. Это освобождает Context от знания о компонентах
// (diag остаётся листом, без зависимостей на semantic/transpiler/syntax).

namespace trust {

/// Регистрирует колбэк, который применяется к Options при их создании (static-init).
void registerDiagnostics(const std::function<void(Options&)>& fn);

/// Применяет базовые и все зарегистрированные компонентные регистрации к opts.
/// Вызывается из Context::Context().
void applyRegisteredDiagnostics(Options& opts);

} // namespace trust
