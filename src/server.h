// server.h — TCP server interface using Winsock2.
//
// Milestone 4 update:
// The Server now integrates networking, RESP parsing, and the Database engine.
// Incoming client requests are parsed as RESP commands, dispatched to the
// Database, and responses are formatted and transmitted back to the client.

#ifndef MINI_REDIS_SERVER_H
#define MINI_REDIS_SERVER_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include "database.h"
#include "persistence.h"

#include <atomic>
#include <cstdint>
#include <string>

class Server {
public:
    // Constructor: sets port (default 6379) and uses internal Database instance.
    explicit Server(uint16_t port = 6379);

    // Overload: allows passing an external Database (useful for tests or shared state).
    Server(uint16_t port, Database& db);

    // Destructor: ensures all sockets are closed and Winsock is cleaned up (RAII).
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Enable Append-Only File (AOF) persistence.
    // If the file already exists on start(), its commands will be replayed.
    void enable_aof(const std::string& aof_path);

    // Initialize Winsock, bind socket, and listen on port.
    bool start();

    // Enter accept loop (blocking).
    void run();

    // Signal server to stop and unblock sockets.
    void stop();

    bool is_running() const;
    uint16_t port() const;
    Database& database();

private:
    uint16_t port_;
    Database internal_db_;
    Database& db_;
    SOCKET listen_socket_;
    std::atomic<SOCKET> client_socket_;
    std::atomic<bool> running_;
    bool winsock_initialized_;
    AofManager aof_;
    std::string aof_path_;

    // Handle a client connection: parse RESP commands, execute, and send responses.
    void handle_client(SOCKET client_socket);

    // Reliable send helper for TCP socket.
    bool send_all(SOCKET sock, const char* data, int length);

    void log_error(const std::string& context) const;
};

#endif // MINI_REDIS_SERVER_H
