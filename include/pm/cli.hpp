#pragma once

// include/pm/cli.hpp
// Command-line entry point of trust-pm: argv parsing, command dispatch and the
// rendering of results (human-readable or the JSON protocol envelope).

#include <ostream>
#include <string>
#include <vector>

namespace trust::pm {

/// Runs the package manager. `args` includes argv[0] (program name).
/// Returns the process exit code. Output goes to `out`; diagnostics to `err`.
int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

} // namespace trust::pm
