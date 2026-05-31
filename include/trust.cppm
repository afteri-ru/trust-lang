module;

#include "diag/location.hpp"
#include "ast/token.hpp"
#include "diag/context.hpp"
#include "types/types.hpp"
#include "parser/mmproc.hpp"

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
using ::trust::TokenCategory;
typedef ::trust::TokenCategory NodeCategory;
using ::trust::Context;
using ::trust::DiagnosticEngine;
using ::trust::MapperFile;
using ::trust::MMProcessor;
using ::trust::Severity;
using ::trust::TypeInfo;
using ::trust::TypeKind;
} // namespace trust