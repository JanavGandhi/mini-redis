// main.cpp — Entry point for the mini-redis server.
//
// Milestone 4: The server is now fully operational!
// It listens on port 6379, accepts Redis clients (such as redis-cli),
// parses RESP commands, executes them against the in-memory Database,
// and transmits valid RESP responses.

#include "server.h"

#include <csignal>
#include <iostream>

static Server* g_server = nullptr;

void handle_shutdown(int /* signal */) {
    std::cout << "\nShutting down mini-redis..." << std::endl;
    if (g_server != nullptr) {
        g_server->stop();
    }
}

int main() {
    std::cout << "==========================================" << std::endl;
    std::cout << "        mini-redis server v0.5.0          " << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << std::endl;

    Server server(6379);
    g_server = &server;

    std::signal(SIGINT, handle_shutdown);

    if (!server.start()) {
        std::cerr << "Failed to start server. Exiting." << std::endl;
        return 1;
    }

    std::cout << "Server ready to accept connections on port 6379." << std::endl;
    std::cout << "Connect with redis-cli:" << std::endl;
    std::cout << "    redis-cli -p 6379" << std::endl;
    std::cout << std::endl;
    std::cout << "Supported commands:" << std::endl;
    std::cout << "    PING [message]" << std::endl;
    std::cout << "    ECHO message" << std::endl;
    std::cout << "    SET key value" << std::endl;
    std::cout << "    GET key" << std::endl;
    std::cout << "    DEL key [key ...]" << std::endl;
    std::cout << "    EXISTS key [key ...]" << std::endl;
    std::cout << "    FLUSHDB" << std::endl;
    std::cout << std::endl;
    std::cout << "Press Ctrl+C to shut down." << std::endl;
    std::cout << std::endl;

    // Start accept loop (blocks until stopped)
    server.run();

    std::cout << "Server shut down cleanly." << std::endl;
    g_server = nullptr;
    return 0;
}
