// main.cpp — Entry point for the mini-redis server.

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

int main(int argc, char* argv[]) {
    std::cout << "==========================================" << std::endl;
    std::cout << "        mini-redis server v0.9.0          " << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << std::endl;

    uint16_t port = 6379;
    std::string aof_file;
    std::size_t max_clients = 128;
    std::size_t max_keys = 0;

    // Check command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--aof" && i + 1 < argc) {
            aof_file = argv[++i];
        } else if (arg == "--maxclients" && i + 1 < argc) {
            max_clients = static_cast<std::size_t>(std::atoi(argv[++i]));
        } else if (arg == "--maxkeys" && i + 1 < argc) {
            max_keys = static_cast<std::size_t>(std::atoi(argv[++i]));
        } else if (arg[0] != '-' && i == 1) {
            // Positional port argument (e.g. mini-redis 6380)
            port = static_cast<uint16_t>(std::atoi(argv[i]));
        }
    }

    Server server(port);
    g_server = &server;

    server.set_max_clients(max_clients);
    if (max_keys > 0) {
        server.database().set_max_keys(max_keys);
    }
    if (!aof_file.empty()) {
        std::cout << "Enabling AOF persistence: " << aof_file << std::endl;
        server.enable_aof(aof_file);
    }

    std::signal(SIGINT, handle_shutdown);

    if (!server.start()) {
        std::cerr << "Failed to start server. Exiting." << std::endl;
        return 1;
    }

    std::cout << "Server ready to accept concurrent connections on port " << port << "." << std::endl;
    std::cout << "Engine: Non-blocking I/O multiplexing event loop (select)." << std::endl;
    std::cout << "Max clients: " << max_clients << " | Max keys: " << (max_keys == 0 ? "unlimited" : std::to_string(max_keys)) << std::endl;
    std::cout << "Connect with redis-cli:" << std::endl;
    std::cout << "    redis-cli -p " << port << std::endl;
    std::cout << std::endl;
    std::cout << "Supported commands:" << std::endl;
    std::cout << "  Core:        PING, ECHO, DEL, EXISTS, FLUSHDB, INFO" << std::endl;
    std::cout << "  TTL:         EXPIRE, PEXPIRE, TTL, PTTL" << std::endl;
    std::cout << "  Strings:     SET (with EX / PX), GET" << std::endl;
    std::cout << "  Lists:       LPUSH, RPUSH, LPOP, RPOP, LRANGE, LLEN" << std::endl;
    std::cout << "  Hashes:      HSET, HGET, HDEL, HEXISTS, HGETALL" << std::endl;
    std::cout << "  Sets:        SADD, SREM, SISMEMBER, SMEMBERS, SCARD" << std::endl;
    std::cout << "  Persistence: SAVE, BGSAVE, LASTSAVE" << std::endl;
    std::cout << std::endl;
    std::cout << "Press Ctrl+C to shut down." << std::endl;
    std::cout << std::endl;

    server.run();

    std::cout << "Server shut down cleanly." << std::endl;
    g_server = nullptr;
    return 0;
}
