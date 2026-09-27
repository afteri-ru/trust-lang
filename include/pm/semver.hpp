#pragma once

// include/pm/semver.hpp
// Semantic version core (MAJOR.MINOR.PATCH) and version-range matching.
// Only the semantic core is used; the build variant {target, toolchain, ABI}
// is a separate dimension of an artifact, not of the version.

#include <string>
#include <string_view>

namespace trust::pm {

/// Semantic version core.
struct Semver {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

/// Parses a strict "MAJOR.MINOR.PATCH" version.
/// Throws PmException("pm.version.invalid") on malformed input.
[[nodiscard]] Semver parseSemver(std::string_view text);

/// Renders "MAJOR.MINOR.PATCH".
[[nodiscard]] std::string formatSemver(const Semver& version);

/// True if `version` satisfies the range. Supported operators: "" / "*"
/// (any), "=X[.Y[.Z]]", ">=", ">", "<=", "<", "^", "~", and comma-separated
/// conjunctions ("^1.2,<1.5"). Missing components default to zero.
/// Throws PmException("pm.version.invalid") on a malformed range.
[[nodiscard]] bool satisfiesRange(const Semver& version, std::string_view range);

} // namespace trust::pm
