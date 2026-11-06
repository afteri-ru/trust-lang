// Досборка макросов в SymbolIndex: единая точка для Pipeline::run и LSP-сервера.
#pragma once

#include "symbols/symbol_index.hpp"
#include "session/context.hpp"

#include <utility>

namespace trust {

// Дособирает макросы из Context::macroDefs() в SymbolIndex (isMacro=true). Имена макросов,
// записанные во время парсинга, не должны теряться даже если семантический шаг не выполнился
// (Fatal/исключение). Видимость макроса задаётся полями: `module` — модуль определения
// (`nullopt` → stdlib DSL), `visibility` — Exported для stdlib DSL (виден везде) и FilePrivate
// для определений в модуле (см. analysis::isMacroVisible).
inline void appendMacroSymbols(const Context& ctx, SymbolIndex& out) {
    for (const auto& md : ctx.macroDefs()) {
        SymbolInfo si;
        si.name = md.name;
        si.type = INVALID_TYPE_ID;
        si.nameRange = md.range;
        si.scopeRange = md.range;
        si.isMacro = true;
        si.documentation = md.documentation;
        si.module = md.module; // nullopt = stdlib DSL (виден везде)
        out.push_back(std::move(si));
    }
}

} // namespace trust
