// server_test.cpp — Integration tests for Server executing Redis commands over TCP.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include "server.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// ─── Minimal Test Framework ──────────────────────────────────────────────────

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

static const uint16_t TEST_PORT = 16379;

struct TestServer {
    Server server;
    std::thread thread;

    explicit TestServer(uint16_t port) : server(port) {}

    bool start() {
        if (!server.start()) return false;
        thread = std::thread([this]() { server.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return true;
    }

    ~TestServer() {
        server.stop();
        if (thread.joinable()) {
            thread.join();
        }
    }

    TestServer(const TestServer&) = delete;
    TestServer& operator=(const TestServer&) = delete;
};

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

bool wait_for_data(SOCKET sock, int timeout_ms) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);

    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    return select(0, &read_fds, nullptr, nullptr, &tv) > 0;
}

std::string send_and_receive(SOCKET sock, const std::string& request) {
    send(sock, request.data(), static_cast<int>(request.size()), 0);

    if (!wait_for_data(sock, 2000)) {
        return "";
    }

    char buf[4096];
    int n = recv(sock, buf, sizeof(buf), 0);
    if (n > 0) {
        return std::string(buf, n);
    }
    return "";
}

// ─── Test Cases ──────────────────────────────────────────────────────────────

void test_server_starts_and_stops() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());
    ASSERT_TRUE(ts.server.is_running());
}

void test_client_connects() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());

    SOCKET client = connect_to_server(TEST_PORT);
    ASSERT_TRUE(client != INVALID_SOCKET);

    closesocket(client);
}

void test_command_ping() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());

    SOCKET client = connect_to_server(TEST_PORT);
    if (client == INVALID_SOCKET) {
        ASSERT_TRUE(false);
        return;
    }

    // PING command: *1\r\n$4\r\nPING\r\n
    std::string resp = send_and_receive(client, "*1\r\n$4\r\nPING\r\n");
    ASSERT_EQ(std::string("+PONG\r\n"), resp);

    // PING with message: *2\r\n$4\r\nPING\r\n$5\r\nhello\r\n
    std::string resp2 = send_and_receive(client, "*2\r\n$4\r\nPING\r\n$5\r\nhello\r\n");
    ASSERT_EQ(std::string("$5\r\nhello\r\n"), resp2);

    closesocket(client);
}

void test_command_set_and_get() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());

    SOCKET client = connect_to_server(TEST_PORT);
    if (client == INVALID_SOCKET) {
        ASSERT_TRUE(false);
        return;
    }

    // SET greeting "hello world"
    std::string set_cmd = "*3\r\n$3\r\nSET\r\n$8\r\ngreeting\r\n$11\r\nhello world\r\n";
    std::string set_resp = send_and_receive(client, set_cmd);
    ASSERT_EQ(std::string("+OK\r\n"), set_resp);

    // GET greeting
    std::string get_cmd = "*2\r\n$3\r\nGET\r\n$8\r\ngreeting\r\n";
    std::string get_resp = send_and_receive(client, get_cmd);
    ASSERT_EQ(std::string("$11\r\nhello world\r\n"), get_resp);

    // GET nonexistent key
    std::string miss_cmd = "*2\r\n$3\r\nGET\r\n$7\r\nunknown\r\n";
    std::string miss_resp = send_and_receive(client, miss_cmd);
    ASSERT_EQ(std::string("$-1\r\n"), miss_resp);

    closesocket(client);
}

void test_command_del_exists_flushdb() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());

    SOCKET client = connect_to_server(TEST_PORT);
    if (client == INVALID_SOCKET) {
        ASSERT_TRUE(false);
        return;
    }

    // SET a 1, SET b 2
    send_and_receive(client, "*3\r\n$3\r\nSET\r\n$1\r\na\r\n$1\r\n1\r\n");
    send_and_receive(client, "*3\r\n$3\r\nSET\r\n$1\r\nb\r\n$1\r\n2\r\n");

    // EXISTS a b c -> :2\r\n
    std::string exists_resp = send_and_receive(client, "*4\r\n$6\r\nEXISTS\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n");
    ASSERT_EQ(std::string(":2\r\n"), exists_resp);

    // DEL a -> :1\r\n
    std::string del_resp = send_and_receive(client, "*2\r\n$3\r\nDEL\r\n$1\r\na\r\n");
    ASSERT_EQ(std::string(":1\r\n"), del_resp);

    // FLUSHDB -> +OK\r\n
    std::string flush_resp = send_and_receive(client, "*1\r\n$7\r\nFLUSHDB\r\n");
    ASSERT_EQ(std::string("+OK\r\n"), flush_resp);

    // EXISTS b -> :0\r\n
    std::string after_flush = send_and_receive(client, "*2\r\n$6\r\nEXISTS\r\n$1\r\nb\r\n");
    ASSERT_EQ(std::string(":0\r\n"), after_flush);

    closesocket(client);
}

void test_pipelining() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());

    SOCKET client = connect_to_server(TEST_PORT);
    if (client == INVALID_SOCKET) {
        ASSERT_TRUE(false);
        return;
    }

    // Send 3 commands in one single TCP packet (pipelining):
    // PING, SET x 99, GET x
    std::string pipeline =
        "*1\r\n$4\r\nPING\r\n"
        "*3\r\n$3\r\nSET\r\n$1\r\nx\r\n$2\r\n99\r\n"
        "*2\r\n$3\r\nGET\r\n$1\r\nx\r\n";

    send(client, pipeline.data(), static_cast<int>(pipeline.size()), 0);

    // Accumulate responses
    std::string expected = "+PONG\r\n+OK\r\n$2\r\n99\r\n";
    std::string actual;

    while (actual.size() < expected.size()) {
        if (!wait_for_data(client, 2000)) break;
        char buf[1024];
        int n = recv(client, buf, sizeof(buf), 0);
        if (n <= 0) break;
        actual.append(buf, n);
    }

    ASSERT_EQ(expected, actual);

    closesocket(client);
}

// ─── Test Runner ─────────────────────────────────────────────────────────────

int main() {
    std::cout << std::endl;
    std::cout << "=== mini-redis Server Integration Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "[Lifecycle & Connectivity]" << std::endl;
    RUN_TEST(test_server_starts_and_stops);
    RUN_TEST(test_client_connects);
    std::cout << std::endl;

    std::cout << "[Core Redis Commands over TCP]" << std::endl;
    RUN_TEST(test_command_ping);
    RUN_TEST(test_command_set_and_get);
    RUN_TEST(test_command_del_exists_flushdb);
    std::cout << std::endl;

    std::cout << "[TCP Pipelining]" << std::endl;
    RUN_TEST(test_pipelining);
    std::cout << std::endl;

    // --- Summary ---
    std::cout << "===========================================" << std::endl;
    std::cout << "Total: " << tests_run
              << "  Passed: " << tests_passed
              << "  Failed: " << tests_failed << std::endl;

    if (tests_failed == 0) {
        std::cout << "All tests PASSED!" << std::endl;
    } else {
        std::cerr << "Some tests FAILED." << std::endl;
    }

    std::cout << "===========================================" << std::endl;
    std::cout << std::endl;

    return (tests_failed == 0) ? 0 : 1;
}
