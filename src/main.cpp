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
    std::cout << "        mini-redis server v0.8.0          " << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << std::endl;

    Server server(6379);
    g_server = &server;

    // Check command line arguments for optional AOF path
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--aof" && i + 1 < argc) {
            std::string aof_file = argv[++i];
            std::cout << "Enabling AOF persistence: " << aof_file << std::endl;
            server.enable_aof(aof_file);
        }
    }

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
    std::cout << "  Core:        PING, ECHO, DEL, EXISTS, FLUSHDB" << std::endl;
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
