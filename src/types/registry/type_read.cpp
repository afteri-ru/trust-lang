// src/types/registry/type_read.cpp
// View над зонами существующего корня: только снятие возможностей записи (не новый путь).
#include "types/type_read.hpp"

#include "types/type_system.hpp"

namespace trust {

TypeReadModel::TypeReadModel(const TypeSystem& registry) noexcept
: m_store(registry.store())
, m_names(registry.names())
, m_printer(registry.printer())
, m_builtins(registry.builtins())
, m_methods(registry.methods())
, m_codes(registry.typeCodes()) {}

} // namespace trust
