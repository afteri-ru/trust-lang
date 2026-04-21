#pragma once

#include <string>
#include <string_view>

namespace trust {
namespace lsp {

enum class ErrorCategory {
    Lex,       // Lexer errors
    Parse,     // Parser errors
    Type,      // Type system errors
    Semantic,  // Semantic analysis errors
    CodeGen,   // Code generation errors
    Internal,  // Internal compiler errors
};

// Error codes for LSP Diagnostic.code field
enum class ErrorCode {
    // Lexer (E001-E099)
    UnknownCharacter = 1,
    UnterminatedString,
    UnterminatedComment,
    ReservedKeyword,

    // Parser (E100-E199)
    UnexpectedToken = 100,
    MissingIdentifier,
    ExpectedExpression,
    UnclosedBrace,
    DuplicateParameter,

    // Type (E200-E299)
    UnknownType = 200,
    TypeMismatch,
    ImplicitConversionError,
    UndeclaredType,

    // Semantic (E300-E399)
    UndeclaredVariable = 300,
    UndeclaredFunction,
    InvalidAssignment,
    MissingReturn,
    DuplicateDeclaration,
    ScopeViolation,

    // CodeGen (E400-E499)
    UnsupportedFeature = 400,
    TargetMismatch,

    // Internal (E900-E999)
    InternalError = 900,
    Unreachable,
};

struct ErrorInfo {
    ErrorCode code;
    ErrorCategory category;
    std::string_view message;
};

// Get error info by code
constexpr ErrorInfo get_error_info(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::UnknownCharacter:    return {code, ErrorCategory::Lex, "Unknown character"};
    case ErrorCode::UnterminatedString:  return {code, ErrorCategory::Lex, "Unterminated string literal"};
    case ErrorCode::UnterminatedComment: return {code, ErrorCategory::Lex, "Unterminated comment"};
    case ErrorCode::ReservedKeyword:     return {code, ErrorCategory::Lex, "Reserved keyword used as identifier"};

    case ErrorCode::UnexpectedToken:     return {code, ErrorCategory::Parse, "Unexpected token"};
    case ErrorCode::MissingIdentifier:   return {code, ErrorCategory::Parse, "Missing identifier"};
    case ErrorCode::ExpectedExpression:  return {code, ErrorCategory::Parse, "Expected expression"};
    case ErrorCode::UnclosedBrace:       return {code, ErrorCategory::Parse, "Unclosed brace"};
    case ErrorCode::DuplicateParameter:  return {code, ErrorCategory::Parse, "Duplicate parameter name"};

    case ErrorCode::UnknownType:         return {code, ErrorCategory::Type, "Unknown type"};
    case ErrorCode::TypeMismatch:        return {code, ErrorCategory::Type, "Type mismatch"};
    case ErrorCode::ImplicitConversionError: return {code, ErrorCategory::Type, "Invalid implicit conversion"};
    case ErrorCode::UndeclaredType:      return {code, ErrorCategory::Type, "Undeclared type"};

    case ErrorCode::UndeclaredVariable:  return {code, ErrorCategory::Semantic, "Undeclared variable"};
    case ErrorCode::UndeclaredFunction:  return {code, ErrorCategory::Semantic, "Undeclared function"};
    case ErrorCode::InvalidAssignment:   return {code, ErrorCategory::Semantic, "Invalid assignment target"};
    case ErrorCode::MissingReturn:       return {code, ErrorCategory::Semantic, "Missing return value"};
    case ErrorCode::DuplicateDeclaration: return {code, ErrorCategory::Semantic, "Duplicate declaration"};
    case ErrorCode::ScopeViolation:      return {code, ErrorCategory::Semantic, "Scope violation"};

    case ErrorCode::UnsupportedFeature:  return {code, ErrorCategory::CodeGen, "Unsupported language feature"};
    case ErrorCode::TargetMismatch:      return {code, ErrorCategory::CodeGen, "Target mismatch"};

    case ErrorCode::InternalError:       return {code, ErrorCategory::Internal, "Internal compiler error"};
    case ErrorCode::Unreachable:         return {code, ErrorCategory::Internal, "Reached unreachable code"};

    default: return {code, ErrorCategory::Internal, "Unknown error"};
    }
}

inline std::string error_code_string(ErrorCode code) {
    auto c = static_cast<int>(code);
    std::string s;
    if (c >= 900) { s = "E9"; c -= 900; }
    else if (c >= 400) { s = "E4"; c -= 400; }
    else if (c >= 300) { s = "E3"; c -= 300; }
    else if (c >= 200) { s = "E2"; c -= 200; }
    else if (c >= 100) { s = "E1"; c -= 100; }
    else { s = "E0"; }
    s += (c < 10 ? "0" : "") + std::to_string(c);
    return s;
}

inline std::string_view category_name(ErrorCategory cat) {
    switch (cat) {
    case ErrorCategory::Lex:      return "Lex";
    case ErrorCategory::Parse:    return "Parse";
    case ErrorCategory::Type:     return "Type";
    case ErrorCategory::Semantic: return "Semantic";
    case ErrorCategory::CodeGen:  return "CodeGen";
    case ErrorCategory::Internal: return "Internal";
    default: return "Unknown";
    }
}

} // namespace lsp
} // namespace trust