// server_test.cpp — Unit tests for the TCP Server class.
//
// These tests verify the networking layer by running the server in a
// background thread and connecting to it with client sockets.
//
// We use port 16379 (not the default 6379) to avoid conflicts with
// any real Redis server that might be running.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include "server.h"

#include <chrono>       // std::chrono — for sleep durations.
#include <cstring>      // std::strlen — C-style string length.
#include <iostream>     // std::cout, std::cerr — console output.
#include <string>       // std::string
#include <thread>       // std::thread — run server in background.
#include <vector>       // std::vector — for multi-message tests.

// ─── Minimal Test Framework ──────────────────────────────────────────────────
// (Same pattern as database_test.cpp)

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static bool current_test_failed = false;

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::cerr << "    FAIL: " << #cond                                \
                      << "  (" << __FILE__ << ":" << __LINE__ << ")"          \
                      << std::endl;                                           \
            current_test_failed = true;                                       \
        }                                                                     \
    } while (false)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        if ((expected) != (actual)) {                                         \
            std::cerr << "    FAIL: expected (" << #expected << " = "         \
                      << (expected) << ") == (" << #actual << " = "           \
                      << (actual) << ")"                                      \
                      << "  (" << __FILE__ << ":" << __LINE__ << ")"          \
                      << std::endl;                                           \
            current_test_failed = true;                                       \
        }                                                                     \
    } while (false)

#define RUN_TEST(test_func)                                                   \
    do {                                                                      \
        current_test_failed = false;                                          \
        std::cout << "  " << #test_func << "... ";                            \
        test_func();                                                          \
        tests_run++;                                                          \
        if (current_test_failed) {                                            \
            tests_failed++;                                                   \
            std::cout << "FAILED" << std::endl;                               \
        } else {                                                              \
            tests_passed++;                                                   \
            std::cout << "PASSED" << std::endl;                               \
        }                                                                     \
    } while (false)

// ─── Test Configuration ─────────────────────────────────────────────────────

// Port for testing — not 6379 to avoid conflicts.
static const uint16_t TEST_PORT = 16379;

// ─── Helper: RAII Test Server ────────────────────────────────────────────────
//
// Manages starting the server in a background thread and cleaning up
// automatically when the helper goes out of scope (even if a test fails).

struct TestServer {
    Server server;
    std::thread thread;

    explicit TestServer(uint16_t port) : server(port) {}

    // Start the server and launch its run() in a background thread.
    bool start() {
        if (!server.start()) return false;
        thread = std::thread([this]() { server.run(); });
        // Give the server thread time to reach accept().
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return true;
    }

    // Stop the server and wait for the background thread to finish.
    ~TestServer() {
        server.stop();
        if (thread.joinable()) {
            thread.join();
        }
    }

    // Non-copyable.
    TestServer(const TestServer&) = delete;
    TestServer& operator=(const TestServer&) = delete;
};

// ─── Helper: Connect a Client Socket to Localhost ────────────────────────────

SOCKET connect_to_server(uint16_t port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr),
                sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

// ─── Helper: Wait for Data with Timeout ──────────────────────────────────────
//
// Uses select() to wait up to timeout_ms for data to arrive on the socket.
// Returns true if data is available, false on timeout.
// This prevents tests from hanging forever if the server doesn't respond.

bool wait_for_data(SOCKET sock, int timeout_ms) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);

    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    // On Windows, the first argument to select() is ignored.
    return select(0, &read_fds, nullptr, nullptr, &tv) > 0;
}

// ─── Test Cases ──────────────────────────────────────────────────────────────

// Test: Server starts, reports running, and stops cleanly.
void test_server_starts_and_stops() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());
    ASSERT_TRUE(ts.server.is_running());
    // Destructor will stop and join — verifies clean shutdown.
}

// Test: A client can successfully connect to the server.
void test_client_connects() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());

    SOCKET client = connect_to_server(TEST_PORT);
    ASSERT_TRUE(client != INVALID_SOCKET);

    closesocket(client);
}

// Test: Server echoes a single message back to the client.
void test_echo_single_message() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());

    SOCKET client = connect_to_server(TEST_PORT);
    if (client == INVALID_SOCKET) {
        ASSERT_TRUE(false);  // Force failure — could not connect.
        return;
    }

    // Send a message.
    const char* msg = "Hello, mini-redis!";
    int msg_len = static_cast<int>(std::strlen(msg));
    int sent = send(client, msg, msg_len, 0);
    ASSERT_EQ(msg_len, sent);

    // Wait for the echo (with timeout to avoid hanging).
    ASSERT_TRUE(wait_for_data(client, 2000));

    // Receive the echo.
    char buf[1024];
    int received = recv(client, buf, sizeof(buf), 0);
    ASSERT_EQ(msg_len, received);

    if (received > 0) {
        std::string echo(buf, received);
        ASSERT_EQ(std::string(msg), echo);
    }

    closesocket(client);
}

// Test: Multiple send/receive rounds work correctly.
void test_echo_multiple_messages() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());

    SOCKET client = connect_to_server(TEST_PORT);
    if (client == INVALID_SOCKET) {
        ASSERT_TRUE(false);
        return;
    }

    // Simulate Redis-like commands (still just raw strings for now).
    std::vector<std::string> messages = {
        "PING",
        "SET mykey myvalue",
        "GET mykey",
        "DEL mykey"
    };

    for (const auto& msg : messages) {
        int msg_len = static_cast<int>(msg.size());
        send(client, msg.c_str(), msg_len, 0);

        ASSERT_TRUE(wait_for_data(client, 2000));

        char buf[1024];
        int received = recv(client, buf, sizeof(buf), 0);
        ASSERT_EQ(msg_len, received);

        if (received > 0) {
            std::string echo(buf, received);
            ASSERT_EQ(msg, echo);
        }
    }

    closesocket(client);
}

// Test: After one client disconnects, the server accepts the next one.
void test_client_disconnect_and_reconnect() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());

    // ── First client connects and exchanges data ──
    SOCKET client1 = connect_to_server(TEST_PORT);
    if (client1 == INVALID_SOCKET) {
        ASSERT_TRUE(false);
        return;
    }

    const char* msg1 = "Client 1";
    send(client1, msg1, static_cast<int>(std::strlen(msg1)), 0);
    ASSERT_TRUE(wait_for_data(client1, 2000));

    char buf[1024];
    int received = recv(client1, buf, sizeof(buf), 0);
    ASSERT_EQ(static_cast<int>(std::strlen(msg1)), received);

    // First client disconnects.
    closesocket(client1);

    // Give the server time to notice the disconnect and return to accept().
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // ── Second client connects and exchanges data ──
    SOCKET client2 = connect_to_server(TEST_PORT);
    if (client2 == INVALID_SOCKET) {
        ASSERT_TRUE(false);
        return;
    }

    const char* msg2 = "Client 2";
    send(client2, msg2, static_cast<int>(std::strlen(msg2)), 0);
    ASSERT_TRUE(wait_for_data(client2, 2000));

    received = recv(client2, buf, sizeof(buf), 0);
    ASSERT_EQ(static_cast<int>(std::strlen(msg2)), received);

    if (received > 0) {
        std::string echo(buf, received);
        ASSERT_EQ(std::string(msg2), echo);
    }

    closesocket(client2);
}

// Test: A larger message (500 bytes) is echoed back correctly.
void test_echo_large_message() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());

    SOCKET client = connect_to_server(TEST_PORT);
    if (client == INVALID_SOCKET) {
        ASSERT_TRUE(false);
        return;
    }

    // Create a 500-byte message.
    std::string large_msg(500, 'X');
    int msg_len = static_cast<int>(large_msg.size());
    send(client, large_msg.c_str(), msg_len, 0);

    // The echo may arrive in multiple recv() calls, so accumulate.
    std::string echo;
    while (static_cast<int>(echo.size()) < msg_len) {
        if (!wait_for_data(client, 2000)) break;  // Timeout — stop waiting.
        char buf[1024];
        int received = recv(client, buf, sizeof(buf), 0);
        if (received <= 0) break;
        echo.append(buf, received);
    }

    ASSERT_EQ(large_msg.size(), echo.size());
    ASSERT_EQ(large_msg, echo);

    closesocket(client);
}

// ─── Test Runner ─────────────────────────────────────────────────────────────

int main() {
    std::cout << std::endl;
    std::cout << "=== mini-redis Server Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "[Server Lifecycle]" << std::endl;
    RUN_TEST(test_server_starts_and_stops);
    std::cout << std::endl;

    std::cout << "[Client Connections]" << std::endl;
    RUN_TEST(test_client_connects);
    RUN_TEST(test_client_disconnect_and_reconnect);
    std::cout << std::endl;

    std::cout << "[Echo Behavior]" << std::endl;
    RUN_TEST(test_echo_single_message);
    RUN_TEST(test_echo_multiple_messages);
    RUN_TEST(test_echo_large_message);
    std::cout << std::endl;

    // --- Summary ---
    std::cout << "================================" << std::endl;
    std::cout << "Total: " << tests_run
              << "  Passed: " << tests_passed
              << "  Failed: " << tests_failed << std::endl;

    if (tests_failed == 0) {
        std::cout << "All tests PASSED!" << std::endl;
    } else {
        std::cerr << "Some tests FAILED." << std::endl;
    }

    std::cout << "================================" << std::endl;
    std::cout << std::endl;

    return (tests_failed == 0) ? 0 : 1;
}
