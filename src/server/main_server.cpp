#include "../include/network/server.h"
#include <iostream>
#include <csignal>
#include <cstdlib>

static flexql::Server* g_server = nullptr;

static volatile bool shutdown_requested = false;

void signal_handler(int sig) {
    (void)sig;
    std::cout << "\nShutting down FlexQL server...\n";
    shutdown_requested = true;
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    int port = 9000;
    std::string data_dir = "data";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = std::atoi(argv[++i]);
            }
        } else if (arg == "-d" || arg == "--data") {
            if (i + 1 < argc) {
                data_dir = argv[++i];
            }
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: flexql-server [options]\n"
                      << "  -p, --port PORT    Server port (default: 9000)\n"
                      << "  -d, --data DIR     Data directory (default: data)\n"
                      << "  -h, --help         Show this help\n";
            return 0;
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    flexql::Server server(port, data_dir);
    g_server = &server;
    server.start();
    
    // Ensure proper cleanup - destructor will be called
    std::cout << "Server stopped, flushing data...\n";
    g_server = nullptr;
    
    return 0;
}
