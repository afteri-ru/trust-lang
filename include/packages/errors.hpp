#pragma once

// include/packages/errors.hpp
// Component-local structured error of `packages`. It exists for the trust <-> trust-pm
// interaction: the JSON contract of trust-pm needs a stable machine-readable code
// distinct from the human-readable message (PmError does the same at the pm boundary).
// Deliberately self-contained: no JSON, no diag/Context, so the component stays a leaf.

#include <format>
#include <stdexcept>
#include <string>
#include <utility>

namespace trust::packages {

/// Machine-readable component error: a stable code plus a human-readable message.
struct PackageError {
    std::string code;
    std::string message;
};

/// Exception carrying a PackageError.
class PackageException : public std::runtime_error {
  public:
    explicit PackageException(PackageError error)
    : std::runtime_error(error.message)
    , m_error(std::move(error)) {}

    [[nodiscard]] const PackageError& error() const noexcept { return m_error; }

  private:
    PackageError m_error;
};

/// Throws a PackageException with a formatted message (reuses std::format).
template <typename... Args>
[[noreturn]] void fail(std::string code, std::format_string<Args...> fmt, Args&&... args) {
    throw PackageException(PackageError{std::move(code), std::format(fmt, std::forward<Args>(args)...)});
}

/// Overload for an already-formed message (e.g. a runtime libarchive error string).
[[noreturn]] inline void fail(std::string code, std::string message) {
    throw PackageException(PackageError{std::move(code), std::move(message)});
}

} // namespace trust::packages
