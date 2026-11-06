#include "types/type_system.hpp"

#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "types/registry/builtin_core_builder.hpp"

namespace trust {

TypeSystem::TypeSystem(DiagnosticEngine& diag, const Options& opts)
: m_diag(diag)
, m_opts(opts)
, m_names(diag, opts, m_store)
, m_builtins(m_store)
, m_printer(m_store, m_builtins)
, m_identity(m_store, m_printer, m_codes)
, m_registrar(m_store, m_names, diag, opts)
, m_methods(m_store, m_identity)
, m_read(m_store, m_names, m_printer, m_builtins, m_methods, m_codes)
, m_interner(m_read, m_store, m_names, diag, opts) {
    m_store.resetToBuiltin(builtinCore());
    m_names.setBuiltinCore(m_store.builtinCore());
    // Продолжаем интернирование seed-структурных типов (единый TypeId на структуру).
    m_interner.reset(m_store.builtinCore()->structural);
    // On-demand материализация членов record-специализаций (C++-модель неявной инстанциации).
    m_methods.setMemberMaterializer(&m_interner);
}

// Seed-строитель ядра — зона BuiltinCoreBuilder: корень только разделяет готовое ядро.
const BuiltinTypeCore& TypeSystem::builtinCore() {
    // thread-safe (C++11 magic static): ядро строится один раз на процесс.
    static const BuiltinTypeCore core = BuiltinCoreBuilder::build();
    return core;
}

void TypeSystem::reset() {
    // Иммутабельное ядро встроенных типов разделяется всеми экземплярами (BuiltinTypeCore).
    m_store.resetToBuiltin(builtinCore());
    m_names.clearUser();
    m_names.setBuiltinCore(m_store.builtinCore());
    // Структурное интернирование продолжается таблицей ядра: seed-структурные типы сохраняют
    // тот же TypeId (одна структура - один TypeId).
    m_interner.reset(m_store.builtinCore()->structural);
    m_codes.clear(); // коды вычисляются из текущих дескрипторов/имён
    m_borrowedClasses.clear();
    m_borrowedMethods.clear();
    m_methods.clearBuiltinExtraMethods();
    // Единственная точка инвалидации производных снимков (SemanticModel): снимок, снятый до
    // reset(), при следующем обращении обнаружит расхождение поколения (R7).
    ++m_generation;
}

} // namespace trust
