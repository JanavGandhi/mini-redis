// server.h — Non-blocking multiplexed TCP server using Winsock2.
//
// Milestone 8:
// Converted from a blocking single-client loop to a non-blocking I/O event loop:
//   - Non-blocking sockets (ioctlsocket FIONBIO)
//   - Event multiplexing via select() with FD_SETSIZE 256
//   - Per-client context: dedicated RespParser & send_buffer
//   - Concurrent handling of multiple clients simultaneously
//   - Resilient against partial / fragmented input and abrupt client disconnects

#ifndef MINI_REDIS_SERVER_H
#define MINI_REDIS_SERVER_H

#ifndef FD_SETSIZE
#define FD_SETSIZE 256
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include "database.h"
#include "persistence.h"
#include "resp.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// ─── Per-Client State ───────────────────────────────────────────────────────

struct ClientContext {
    SOCKET socket = INVALID_SOCKET;
    RespParser parser;        // Each client has an independent streaming parser
    std::string send_buffer;  // Queued outgoing response bytes
    std::string peer_ip;
    uint16_t peer_port = 0;
};

// ─── Server Class ───────────────────────────────────────────────────────────

class Server {
public:
    explicit Server(uint16_t port = 6379);
    Server(uint16_t port, Database& db);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Enable AOF persistence
    void enable_aof(const std::string& aof_path);

    // Initialize Winsock, create socket, set non-blocking, bind & listen
    bool start();

    // Main non-blocking event loop using select()
    void run();

    // Signal server to stop and unblock sockets
    void stop();

    bool is_running() const;
    uint16_t port() const;
    Database& database();
    std::size_t connected_clients_count() const;

private:
    uint16_t port_;
    Database internal_db_;
    Database& db_;
    SOCKET listen_socket_;
    std::atomic<bool> running_;
    bool winsock_initialized_;
    AofManager aof_;
    std::string aof_path_;

    std::unordered_map<SOCKET, ClientContext> clients_;

    // Set socket to non-blocking mode
    bool set_non_blocking(SOCKET sock);

    // Event loop handlers
    void process_new_connections();
    void process_client_read(SOCKET sock, ClientContext& client, std::vector<SOCKET>& to_remove);
    void process_client_write(SOCKET sock, ClientContext& client, std::vector<SOCKET>& to_remove);
    void remove_client(SOCKET sock);

    void log_error(const std::string& context) const;
};

#endif // MINI_REDIS_SERVER_H
