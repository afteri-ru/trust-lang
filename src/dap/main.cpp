#include "dap_protocol.h"
#include <iostream>

int main(int argc, char* argv[]) {
    std::cerr << "Trust DAP Server starting..." << std::endl;
    
    std::string sourceMapPath;
    if (argc > 1) {
        sourceMapPath = argv[1];
    }
    
    DapServer server;
    
    // Override source map path if provided via stdin arguments
    // The actual source map path will be set during launch request
    
    try {
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}