#include "cli/cli.hpp"
#include "utils/backtrace.hpp"

int main(int argc, char* argv[]) {
    trust::utils::install_fault_handler();
    auto result = trust::parse_cli_args(argc, argv);

    // Help/version/errors — выходим сразу
    if (trust::is_special_exit(result) || result.opts.help_requested || result.opts.version_requested) {
        return result.exit_code;
    }

    trust::Cli cli(std::move(result));
    return cli.run();
}