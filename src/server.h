// server.h — TCP server interface using Winsock2.
//
// This class manages the lifecycle of a TCP server:
//   1. Initialize the Windows Sockets library (Winsock).
//   2. Create a TCP socket and bind it to a port.
//   3. Listen for incoming client connections.
//   4. Accept connections and handle them one at a time.
//   5. Clean up all resources when done.
//
// IMPORTANT NETWORKING CONCEPTS:
//
// Socket: A socket is an endpoint for communication. Think of it like a
//   phone — you create one, bind it to a "number" (IP + port), and then
//   use it to send/receive data.
//
// TCP (Transmission Control Protocol): A reliable, ordered protocol.
//   Data sent via TCP arrives in the same order it was sent, without
//   duplicates or missing pieces. This is what Redis uses.
//
// Port: A number (0-65535) that identifies a specific service on a machine.
//   Redis uses port 6379 by convention.
//
// Winsock2: The Windows implementation of the Berkeley Sockets API.
//   It requires explicit initialization (WSAStartup) and cleanup (WSACleanup).
//
// Current limitations:
//   - Handles only ONE client at a time (Milestone 8 adds concurrency).
//   - Echoes data back without parsing (Milestone 3 adds RESP protocol).

#ifndef MINI_REDIS_SERVER_H
#define MINI_REDIS_SERVER_H

// WIN32_LEAN_AND_MEAN excludes rarely-used Windows headers to speed up
// compilation and avoid conflicts with Winsock2 definitions.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>   // Core Winsock2 functions: socket, bind, listen, accept, etc.
#include <ws2tcpip.h>   // TCP/IP-specific functions: inet_ntop, inet_pton, etc.

#include <atomic>       // std::atomic — thread-safe boolean for the running flag.
#include <cstdint>      // uint16_t — unsigned 16-bit integer for port numbers.
#include <string>       // std::string — for error messages.

class Server {
public:
    // Constructor: sets the port number but does NOT start the server.
    // The default Redis port is 6379.
    explicit Server(uint16_t port = 6379);

    // Destructor: ensures all sockets are closed and Winsock is cleaned up.
    // This follows the RAII principle — resources are released automatically.
    ~Server();

    // Servers should not be copied. Each server owns a unique socket.
    // If we allowed copying, two Server objects would share the same socket,
    // leading to double-close bugs.
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Initialize Winsock, create the listening socket, bind it to the port,
    // and begin listening for connections.
    // Returns true on success, false on failure (error details go to stderr).
    bool start();

    // Enter the main accept loop. This BLOCKS the calling thread:
    // it waits for clients to connect, handles them one at a time,
    // and loops until stop() is called.
    void run();

    // Signal the server to stop. Can safely be called from another thread
    // (e.g., from a Ctrl+C signal handler). Closes sockets to unblock
    // accept() and recv(), which breaks the loops in run().
    void stop();

    // Returns true if the server is currently in the run() loop.
    bool is_running() const;

    // Returns the port number the server is configured to listen on.
    uint16_t port() const;

private:
    uint16_t port_;                    // Port number to listen on.
    SOCKET listen_socket_;             // The socket that listens for new connections.
    std::atomic<SOCKET> client_socket_;// Currently connected client (INVALID_SOCKET if none).
    std::atomic<bool> running_;        // Thread-safe flag: true while run() is active.
    bool winsock_initialized_;         // Whether WSAStartup has been called.

    // Handle a single client connection: read data in a loop and echo it back.
    // Returns when the client disconnects or an error occurs.
    // In later milestones, this will parse RESP commands and interact with
    // the Database instead of echoing.
    void handle_client(SOCKET client_socket);

    // Send all bytes in the buffer to the socket.
    // TCP send() may not transmit everything in one call (especially for
    // large messages), so this loops until all bytes are sent.
    // Returns true if all bytes were sent, false on error.
    bool send_all(SOCKET sock, const char* data, int length);

    // Print a Winsock error message to stderr with context.
    // WSAGetLastError() returns the error code for the last failed operation.
    void log_error(const std::string& context) const;
};

#endif // MINI_REDIS_SERVER_H
