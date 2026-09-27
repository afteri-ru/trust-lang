#pragma once

// include/pm/protocol.hpp
// Machine-readable contract of the package manager: a single JSON envelope,
// the protocol version and structured errors. Every command answers with this
// envelope when --json is requested; the human-readable output is a rendering
// of the same data.

#include <nlohmann/json.hpp>

#include <exception>
#include <string>
#include <vector>

namespace trust::pm {

/// Version of the CLI+JSON protocol spoken by trust-pm.
inline constexpr int kProtocolVersion = 1;

/// Program name reported in the protocol envelope and by --version.
inline constexpr const char* kProgramName = "trust-pm";

/// A structured, machine-readable error.
struct PmError {
    std::string code;                                  ///< stable identifier, e.g. "pm.manifest.invalid"
    std::string message;                               ///< human-readable description
    nlohmann::json context = nlohmann::json::object(); ///< optional structured details
};

/// Exception carrying a PmError across the code base.
class PmException : public std::exception {
  public:
    explicit PmException(PmError error);

    [[nodiscard]] const char* what() const noexcept override;
    [[nodiscard]] const PmError& error() const noexcept;

  private:
    PmError m_error;
    std::string m_what;
};

/// Envelope for a successful result: {"protocol", "ok":true, "data", "errors":[]}.
[[nodiscard]] nlohmann::json okEnvelope(nlohmann::json data);

/// Envelope for a single error.
[[nodiscard]] nlohmann::json errorEnvelope(const PmError& error);

/// Envelope for several errors.
[[nodiscard]] nlohmann::json errorEnvelope(const std::vector<PmError>& errors);

} // namespace trust::pm
