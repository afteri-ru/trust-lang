#ifndef TRUST_PCH_HPP
#define TRUST_PCH_HPP

// include/precompiled/trust_pch.hpp
// Shared precompiled header for the project's C++ translation units (opt-in via TRUST_USE_PCH).
//
// Contents - ONLY stable, heavy, widely used headers:
//   - C++ standard library headers used across the codebase;
//   - core project headers (AST / types / analysis / utils).
//
// INVARIANTS (do not break - see include/precompiled/MEMORY.md):
//   - The PCH is a pure front-end optimization. It must never be load-bearing: every TU must
//     compile with TRUST_USE_PCH=OFF, i.e. all TU includes stay explicit. A PCH that provides a
//     header a TU forgot to include masks a missing #include and violates the "no implicit"
//     policy (AGENTS.md).
//   - Deliberately NOT included: "syntax/term.h" / "session/context.hpp" - they pull
//     "trust/version.h", which carries the git hash in dev builds. Including it here would
//     invalidate the PCH on every commit and rebuild the whole project.
//   - Release builds must use TRUST_USE_PCH=OFF (enforced in the root CMakeLists.txt), so the
//     distributed artifact is always compiled from fully explicit includes.

#include <algorithm>
#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "analysis/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "types/registry.hpp"
#include "utils/strings.hpp"

#endif // TRUST_PCH_HPP
