#pragma once

// include/pipeline/pm_client.hpp
// Compiler-side client of the package manager contract (CLI + JSON). The
// compiler never performs network I/O: it invokes trust-pm and consumes the
// machine-readable result.

#include "driver/options.hpp"

#include <string>
#include <vector>

namespace trust {

/// Resolves package dependencies for `opts` by invoking trust-pm and returns the
/// local archive paths of the resolved packages. Returns {} when no manifest is
/// present (auto mode) or pm_mode == "off".
/// Throws FatalError with an actionable message when trust-pm is missing, the
/// resolution fails, or the output is not valid protocol JSON.
[[nodiscard]] std::vector<std::string> resolvePackageModules(const PipelineOpts& opts);

} // namespace trust
