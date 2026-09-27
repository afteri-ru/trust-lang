// src/pm/main.cpp
// Entry point of trust-pm: a self-contained package manager with a CLI + JSON
// contract. Network/TLS lives here, never in the compiler.

#include "pm/cli.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    return trust::pm::runCli(args, std::cout, std::cerr);
}
