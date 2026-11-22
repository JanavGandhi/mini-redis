// server.cpp — TCP server implementation using Winsock2.

#include "server.h"
#include "command.h"
#include "resp.h"

#include <iostream>

Server::Server(uint16_t port)
    : port_(port)
    , internal_db_()
    , db_(internal_db_)
    , listen_socket_(INVALID_SOCKET)
    , client_socket_(INVALID_SOCKET)
    , running_(false)
    , winsock_initialized_(false) {
}

Server::Server(uint16_t port, Database& db)
    : port_(port)
    , internal_db_()
    , db_(db)
    , listen_socket_(INVALID_SOCKET)
    , client_socket_(INVALID_SOCKET)
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

bool Server::start() {
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

    std::cout << "Server listening on port " << port_ << std::endl;
    return true;
}

void Server::run() {
    running_ = true;

    while (running_) {
        sockaddr_in client_addr{};
        int addr_len = sizeof(client_addr);

        SOCKET new_client = accept(
            listen_socket_,
            reinterpret_cast<sockaddr*>(&client_addr),
            &addr_len
        );

        if (new_client == INVALID_SOCKET) {
            if (running_) {
                log_error("accept");
            }
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        uint16_t client_port = ntohs(client_addr.sin_port);

        std::cout << "Client connected: " << client_ip << ":" << client_port << std::endl;

        handle_client(new_client);

        closesocket(new_client);
        std::cout << "Client disconnected." << std::endl;
    }

    running_ = false;
}

void Server::stop() {
    running_ = false;

    SOCKET cs = client_socket_.load();
    if (cs != INVALID_SOCKET) {
        shutdown(cs, SD_BOTH);
    }

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

Database& Server::database() {
    return db_;
}

void Server::handle_client(SOCKET client_socket) {
    client_socket_.store(client_socket);

    const int BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];
    RespParser parser;

    while (running_) {
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);

        if (bytes_received > 0) {
            parser.append(buffer, bytes_received);

            // Process all complete RESP messages in the buffer (supports pipelining)
            while (running_) {
                RespValue cmd;
                ParseStatus status = parser.parse_next(cmd);

                if (status == ParseStatus::Ok) {
                    // Dispatch command against Database
                    RespValue response = CommandDispatcher::execute(cmd, db_);
                    std::string response_bytes = response.serialize();

                    if (!send_all(client_socket, response_bytes.data(),
                                 static_cast<int>(response_bytes.size()))) {
                        if (running_) {
                            log_error("send");
                        }
                        return;
                    }
                } else if (status == ParseStatus::Incomplete) {
                    // Incomplete command, wait for next recv chunk
                    break;
                } else {
                    // Syntax / protocol error from client
                    std::string err = RespEncoder::error("ERR Protocol error: invalid request");
                    send_all(client_socket, err.data(), static_cast<int>(err.size()));
                    parser.reset();
                    return;
                }
            }
        } else if (bytes_received == 0) {
            // Graceful client disconnect
            break;
        } else {
            if (running_) {
                log_error("recv");
            }
            break;
        }
    }

    client_socket_.store(INVALID_SOCKET);
}

bool Server::send_all(SOCKET sock, const char* data, int length) {
    int total_sent = 0;
    while (total_sent < length) {
        int bytes_sent = send(sock, data + total_sent, length - total_sent, 0);
        if (bytes_sent == SOCKET_ERROR) {
            return false;
        }
        total_sent += bytes_sent;
    }
    return true;
}

void Server::log_error(const std::string& context) const {
    int error_code = WSAGetLastError();
    std::cerr << "[Error] " << context << " failed (Winsock error "
              << error_code << ")" << std::endl;
}
