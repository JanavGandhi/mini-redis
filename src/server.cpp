// server.cpp — Implementation of non-blocking multiplexed TCP server.

#include "server.h"
#include "command.h"

#include <iostream>

Server::Server(uint16_t port)
    : port_(port)
    , internal_db_()
    , db_(internal_db_)
    , listen_socket_(INVALID_SOCKET)
    , running_(false)
    , winsock_initialized_(false) {
}

Server::Server(uint16_t port, Database& db)
    : port_(port)
    , internal_db_()
    , db_(db)
    , listen_socket_(INVALID_SOCKET)
    , running_(false)
    , winsock_initialized_(false) {
}

Server::~Server() {
    stop();
    if (winsock_initialized_) {
        WSACleanup();
        winsock_initialized_ = false;
    }
}

void Server::enable_aof(const std::string& aof_path) {
    aof_path_ = aof_path;
}

bool Server::set_non_blocking(SOCKET sock) {
    u_long mode = 1;
    if (ioctlsocket(sock, FIONBIO, &mode) != 0) {
        log_error("ioctlsocket (FIONBIO)");
        return false;
    }
    return true;
}

bool Server::start() {
    if (!aof_path_.empty()) {
        std::cout << "Replaying AOF log from " << aof_path_ << "..." << std::endl;
        AofManager::replay(aof_path_, db_);
        aof_.open(aof_path_);
    }

    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        std::cerr << "[Error] WSAStartup failed with code " << result << std::endl;
        return false;
    }
    winsock_initialized_ = true;

    listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket_ == INVALID_SOCKET) {
        log_error("socket creation");
        return false;
    }

    int opt_val = 1;
    setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt_val), sizeof(opt_val));

    // Set listening socket to non-blocking
    if (!set_non_blocking(listen_socket_)) {
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
        return false;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_);

    if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&server_addr),
             sizeof(server_addr)) == SOCKET_ERROR) {
        log_error("bind");
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
        return false;
    }

    if (listen(listen_socket_, SOMAXCONN) == SOCKET_ERROR) {
        log_error("listen");
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
        return false;
    }

    std::cout << "Server listening on port " << port_ << " (non-blocking I/O multiplexing)" << std::endl;
    return true;
}

void Server::run() {
    running_ = true;

    while (running_) {
        fd_set read_fds;
        fd_set write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);

        // Monitor listening socket for incoming client connections
        FD_SET(listen_socket_, &read_fds);

        // Monitor all connected clients for reads and pending writes
        for (const auto& [sock, client] : clients_) {
            FD_SET(sock, &read_fds);
            if (!client.send_buffer.empty()) {
                FD_SET(sock, &write_fds);
            }
        }

        // 50ms timeout for responsive stop() and periodic expiration
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000;

        int activity = select(0, &read_fds, &write_fds, nullptr, &tv);

        if (!running_) {
            break;
        }

        if (activity == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (running_ && err != WSAEINTR) {
                log_error("select");
            }
            break;
        }

        std::vector<SOCKET> to_remove;

        // 1. Accept new connections
        if (FD_ISSET(listen_socket_, &read_fds)) {
            process_new_connections();
        }

        // 2. Process active clients
        for (auto& [sock, client] : clients_) {
            if (FD_ISSET(sock, &read_fds)) {
                process_client_read(sock, client, to_remove);
            }
            if (FD_ISSET(sock, &write_fds) && !client.send_buffer.empty()) {
                process_client_write(sock, client, to_remove);
            }
        }

        // 3. Remove disconnected clients
        for (SOCKET sock : to_remove) {
            remove_client(sock);
        }

        // 4. Active TTL expiration sweep
        db_.purge_expired();
    }

    // Clean up all active clients
    for (auto& [sock, client] : clients_) {
        closesocket(sock);
    }
    clients_.clear();

    running_ = false;
}

void Server::process_new_connections() {
    while (running_) {
        sockaddr_in client_addr{};
        int addr_len = sizeof(client_addr);
        SOCKET new_sock = accept(listen_socket_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);

        if (new_sock == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                break; // No more pending connections
            }
            if (running_) {
                log_error("accept");
            }
            break;
        }

        if (!set_non_blocking(new_sock)) {
            closesocket(new_sock);
            continue;
        }

        // Disable Nagle's algorithm for low-latency responses
        int flag = 1;
        setsockopt(new_sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        uint16_t port = ntohs(client_addr.sin_port);

        ClientContext ctx;
        ctx.socket = new_sock;
        ctx.peer_ip = ip_str;
        ctx.peer_port = port;

        clients_[new_sock] = std::move(ctx);
    }
}

void Server::process_client_read(SOCKET sock, ClientContext& client, std::vector<SOCKET>& to_remove) {
    char buffer[4096];
    int n = recv(sock, buffer, sizeof(buffer), 0);

    if (n > 0) {
        client.parser.append(buffer, n);

        RespValue cmd;
        while (running_) {
            ParseStatus status = client.parser.parse_next(cmd);
            if (status == ParseStatus::Ok) {
                RespValue response = CommandDispatcher::execute(cmd, db_);
                if (aof_.is_open() && !response.is_error()) {
                    aof_.append(cmd);
                }
                client.send_buffer.append(response.serialize());
            } else if (status == ParseStatus::Incomplete) {
                break; // Awaiting more TCP data
            } else {
                std::string err = RespEncoder::error("ERR Protocol error: invalid request");
                client.send_buffer.append(err);
                client.parser.reset();
                break;
            }
        }

        // Try to immediately transmit any generated responses
        if (!client.send_buffer.empty()) {
            int sent = send(sock, client.send_buffer.data(), static_cast<int>(client.send_buffer.size()), 0);
            if (sent > 0) {
                client.send_buffer.erase(0, sent);
            } else if (sent == SOCKET_ERROR) {
                int err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK) {
                    to_remove.push_back(sock);
                }
            }
        }
    } else if (n == 0) {
        // Client closed connection gracefully
        to_remove.push_back(sock);
    } else {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            to_remove.push_back(sock);
        }
    }
}

void Server::process_client_write(SOCKET sock, ClientContext& client, std::vector<SOCKET>& to_remove) {
    if (client.send_buffer.empty()) return;

    int sent = send(sock, client.send_buffer.data(), static_cast<int>(client.send_buffer.size()), 0);
    if (sent > 0) {
        client.send_buffer.erase(0, sent);
    } else if (sent == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            to_remove.push_back(sock);
        }
    }
}

void Server::remove_client(SOCKET sock) {
    auto it = clients_.find(sock);
    if (it != clients_.end()) {
        closesocket(it->second.socket);
        clients_.erase(it);
    }
}

void Server::stop() {
    running_ = false;

    if (listen_socket_ != INVALID_SOCKET) {
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
    }

    for (auto& [sock, client] : clients_) {
        closesocket(sock);
    }
}

bool Server::is_running() const {
    return running_;
}

uint16_t Server::port() const {
    return port_;
}

Database& Server::database() {
    return db_;
}

std::size_t Server::connected_clients_count() const {
    return clients_.size();
}

void Server::log_error(const std::string& context) const {
    int error_code = WSAGetLastError();
    std::cerr << "[Error] " << context << " failed (Winsock error "
              << error_code << ")" << std::endl;
}
