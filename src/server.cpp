// server.cpp — TCP server implementation using Winsock2.
//
// This file implements the networking lifecycle:
//   WSAStartup → socket → bind → listen → accept → recv/send → cleanup
//
// Each step is explained in detail since networking can be intimidating
// for beginners. The key insight is that it's just a sequence of system
// calls, each doing one specific thing.

#include "server.h"

#include <iostream>  // std::cout, std::cerr for logging.

// ─── Constructor & Destructor ────────────────────────────────────────────────

Server::Server(uint16_t port)
    : port_(port)
    , listen_socket_(INVALID_SOCKET)   // No socket created yet.
    , client_socket_(INVALID_SOCKET)   // No client connected yet.
    , running_(false)
    , winsock_initialized_(false) {
}

Server::~Server() {
    // RAII: ensure everything is cleaned up when the Server is destroyed,
    // even if the caller forgot to call stop().
    stop();

    // WSACleanup tells Windows we're done using Winsock.
    // This must be called once for each successful WSAStartup.
    if (winsock_initialized_) {
        WSACleanup();
        winsock_initialized_ = false;
    }
}

// ─── Public Methods ──────────────────────────────────────────────────────────

bool Server::start() {
    // ── Step 1: Initialize Winsock ──────────────────────────────────────
    //
    // On Windows, you must call WSAStartup() before using ANY socket
    // function. This loads the Winsock DLL and negotiates the version.
    //
    // MAKEWORD(2, 2) requests Winsock version 2.2, which is the latest
    // and most widely supported version.
    //
    // wsa_data receives information about the Winsock implementation,
    // but we don't need to inspect it for our purposes.

    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        std::cerr << "[Error] WSAStartup failed with code " << result
                  << std::endl;
        return false;
    }
    winsock_initialized_ = true;

    // ── Step 2: Create a TCP Socket ─────────────────────────────────────
    //
    // socket() creates a new communication endpoint.
    //   AF_INET      = IPv4 address family (as opposed to IPv6 = AF_INET6).
    //   SOCK_STREAM  = TCP (reliable, ordered byte stream).
    //   IPPROTO_TCP  = Explicitly specify TCP protocol.
    //
    // Returns INVALID_SOCKET (-1) on failure.

    listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket_ == INVALID_SOCKET) {
        log_error("socket creation");
        return false;
    }

    // ── Step 3: Set Socket Options ──────────────────────────────────────
    //
    // SO_REUSEADDR allows us to restart the server immediately after
    // stopping it. Without this, the OS might keep the port in a
    // TIME_WAIT state for a few minutes, preventing re-binding.

    int opt_val = 1;
    setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt_val), sizeof(opt_val));

    // ── Step 4: Bind the Socket to an Address ───────────────────────────
    //
    // bind() assigns a local address (IP + port) to our socket.
    //
    // sockaddr_in is a structure that holds an IPv4 address:
    //   sin_family = AF_INET (IPv4)
    //   sin_addr   = The IP address to bind to.
    //                INADDR_ANY means "listen on ALL network interfaces"
    //                (localhost, Wi-Fi, Ethernet, etc.).
    //   sin_port   = The port number, converted to network byte order
    //                using htons() (Host TO Network Short).
    //
    // Why htons()? Different CPUs store multi-byte numbers in different
    // orders (big-endian vs little-endian). Network protocols use
    // big-endian ("network byte order"), so we must convert our port
    // number before passing it to the socket API.

    sockaddr_in server_addr{};                       // Zero-initialize all fields.
    server_addr.sin_family = AF_INET;                // IPv4.
    server_addr.sin_addr.s_addr = INADDR_ANY;        // Listen on all interfaces.
    server_addr.sin_port = htons(port_);             // Convert port to network order.

    if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&server_addr),
             sizeof(server_addr)) == SOCKET_ERROR) {
        log_error("bind");
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
        return false;
    }

    // ── Step 5: Start Listening ─────────────────────────────────────────
    //
    // listen() marks the socket as a passive socket — one that will be
    // used to ACCEPT incoming connections, not to send data itself.
    //
    // SOMAXCONN tells the OS to use the maximum reasonable backlog size.
    // The backlog is the queue of clients waiting to be accepted.

    if (listen(listen_socket_, SOMAXCONN) == SOCKET_ERROR) {
        log_error("listen");
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
        return false;
    }

    std::cout << "Server listening on port " << port_ << std::endl;
    return true;
}

void Server::run() {
    running_ = true;

    // ── Main Accept Loop ────────────────────────────────────────────────
    //
    // The server sits in this loop, waiting for clients to connect.
    // accept() BLOCKS (pauses execution) until a client connects.
    // When a client connects, accept() returns a NEW socket specifically
    // for communicating with that client. The listen_socket_ continues
    // to exist for accepting more clients later.

    while (running_) {
        // We pass a sockaddr_in to accept() so it can tell us the
        // client's IP address and port number.
        sockaddr_in client_addr{};
        int addr_len = sizeof(client_addr);

        SOCKET new_client = accept(
            listen_socket_,
            reinterpret_cast<sockaddr*>(&client_addr),
            &addr_len
        );

        if (new_client == INVALID_SOCKET) {
            // If we're shutting down, this error is expected (stop()
            // closes the listening socket to unblock accept()).
            if (running_) {
                log_error("accept");
            }
            break;
        }

        // Convert the client's IP address from binary to a readable string.
        // inet_ntop = "internet network to presentation" (binary → text).
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr,
                  client_ip, sizeof(client_ip));
        uint16_t client_port = ntohs(client_addr.sin_port);

        std::cout << "Client connected: " << client_ip
                  << ":" << client_port << std::endl;

        // Handle this client (blocks until client disconnects).
        handle_client(new_client);

        // Close the client socket and free its resources.
        closesocket(new_client);
        std::cout << "Client disconnected." << std::endl;
    }

    running_ = false;
}

void Server::stop() {
    // Set the flag BEFORE closing sockets. This way, when accept() or
    // recv() fail because we closed their socket, the code checks
    // running_ and knows it's an intentional shutdown, not an error.
    running_ = false;

    // Close the client socket (if any) to unblock recv() in handle_client().
    // shutdown(SD_BOTH) signals the connection is done in both directions,
    // causing recv() to return 0 (graceful disconnect) rather than an error.
    SOCKET cs = client_socket_.load();
    if (cs != INVALID_SOCKET) {
        shutdown(cs, SD_BOTH);
    }

    // Close the listening socket to unblock accept() in run().
    if (listen_socket_ != INVALID_SOCKET) {
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
    }
}

bool Server::is_running() const {
    return running_;
}

uint16_t Server::port() const {
    return port_;
}

// ─── Private Methods ─────────────────────────────────────────────────────────

void Server::handle_client(SOCKET client_socket) {
    // Track the client socket so stop() can close it from another thread.
    client_socket_.store(client_socket);

    // Buffer to hold incoming data. 1024 bytes is plenty for simple
    // Redis commands. In later milestones, we may need to handle
    // messages that span multiple recv() calls.
    const int BUFFER_SIZE = 1024;
    char buffer[BUFFER_SIZE];

    // ── Receive Loop ────────────────────────────────────────────────────
    //
    // We read data from the client in a loop. Each recv() call returns:
    //   > 0 : Number of bytes received (success — process the data).
    //     0 : Client disconnected gracefully (sent a FIN packet).
    //   < 0 : An error occurred (SOCKET_ERROR = -1 on Windows).

    while (running_) {
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);

        if (bytes_received > 0) {
            // Data received! For now, echo it back to the client.
            // In Milestone 3+, this is where we'll parse RESP commands
            // and send back real responses.
            if (!send_all(client_socket, buffer, bytes_received)) {
                if (running_) {
                    log_error("send");
                }
                break;
            }
        } else if (bytes_received == 0) {
            // The client closed the connection gracefully.
            // This is normal — not an error.
            break;
        } else {
            // recv() returned SOCKET_ERROR.
            // If we're shutting down, this may be expected (stop()
            // closed our socket to unblock us).
            if (running_) {
                log_error("recv");
            }
            break;
        }
    }

    // Clear the tracked client socket.
    client_socket_.store(INVALID_SOCKET);
}

bool Server::send_all(SOCKET sock, const char* data, int length) {
    // TCP send() might not transmit all bytes in a single call.
    // For example, if we try to send 1000 bytes, the OS might only
    // accept 700 into its buffer and return 700. We need to loop
    // and send the remaining 300 bytes.
    //
    // For small messages (like Redis responses), send() almost always
    // sends everything at once. But it's good practice to handle the
    // partial-send case — it WILL happen with larger payloads.

    int total_sent = 0;

    while (total_sent < length) {
        int bytes_sent = send(sock, data + total_sent,
                              length - total_sent, 0);

        if (bytes_sent == SOCKET_ERROR) {
            return false;  // Send failed — caller will log the error.
        }

        total_sent += bytes_sent;
    }

    return true;  // All bytes sent successfully.
}

void Server::log_error(const std::string& context) const {
    // WSAGetLastError() returns the error code for the most recent
    // failed Winsock operation. It's similar to errno but specific
    // to Winsock. Common codes:
    //   10048 (WSAEADDRINUSE)  — Port already in use.
    //   10054 (WSAECONNRESET)  — Client forcibly closed the connection.
    //   10038 (WSAENOTSOCK)    — Socket was already closed.
    int error_code = WSAGetLastError();
    std::cerr << "[Error] " << context << " failed (Winsock error "
              << error_code << ")" << std::endl;
}
