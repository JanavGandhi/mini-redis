// main.cpp — Entry point for the mini-redis server.
//
// Initializes and starts the TCP server. Currently operates as an
// echo server — it sends back whatever the client sends.
// In later milestones, the RESP protocol parser and Database engine
// will be integrated to handle real Redis commands.

#include "server.h"

#include <csignal>   // std::signal — register a function to run on Ctrl+C.
#include <iostream>  // std::cout, std::cerr for console output.

// ─── Signal Handling ─────────────────────────────────────────────────────────
//
// When you press Ctrl+C in the terminal, the OS sends a SIGINT signal
// to the process. By default, this kills the program immediately.
// We register a handler so we can shut down the server gracefully
// (close sockets, free resources) instead of crashing.

// Global pointer to the server instance, used by the signal handler.
// This is a common pattern for handling Ctrl+C in C++ servers.
static Server* g_server = nullptr;

// Called when the user presses Ctrl+C.
void handle_shutdown(int /* signal */) {
    std::cout << "\nShutting down..." << std::endl;
    if (g_server != nullptr) {
        g_server->stop();
    }
}

// ─── Main ────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "mini-redis server v0.3.0" << std::endl;
    std::cout << std::endl;

    // Create the server on the default Redis port (6379).
    Server server(6379);
    g_server = &server;

    // Register the Ctrl+C handler for clean shutdown.
    std::signal(SIGINT, handle_shutdown);

    // Start the server (initialize Winsock, create socket, bind, listen).
    if (!server.start()) {
        std::cerr << "Failed to start server. Exiting." << std::endl;
        return 1;
    }

    std::cout << std::endl;
    std::cout << "Press Ctrl+C to shut down." << std::endl;
    std::cout << "Test with:  telnet localhost 6379" << std::endl;
    std::cout << std::endl;

    // Enter the main accept loop (blocks until stop() is called).
    server.run();

    std::cout << std::endl;
    std::cout << "Server shut down cleanly." << std::endl;
    g_server = nullptr;
    return 0;
}
