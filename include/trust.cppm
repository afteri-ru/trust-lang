module;

#include "diag/location.hpp"
#include "ast/token.hpp"
#include "diag/context.hpp"
#include "types/typekind.hpp"

#include <sstream>
#include <istream>
#include <ostream>
#include <string>
#include <vector>
#include <memory>

export module trust;

export namespace trust {
// Re-export types from headers into the module namespace
using ::trust::ParserToken;
using ::trust::Context;
using ::trust::DiagnosticEngine;
using ::trust::MapperFile;
using ::trust::Severity;
using ::trust::TypeKind;
} // namespace trust