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

    std::string resp = send_and_receive(client, "*1\r\n$4\r\nPING\r\n");
    ASSERT_EQ(std::string("+PONG\r\n"), resp);

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

    std::string set_resp = send_and_receive(client, "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$1\r\nv\r\n");
    ASSERT_EQ(std::string("+OK\r\n"), set_resp);

    std::string get_resp = send_and_receive(client, "*2\r\n$3\r\nGET\r\n$1\r\nk\r\n");
    ASSERT_EQ(std::string("$1\r\nv\r\n"), get_resp);

    closesocket(client);
}

void test_command_list_hash_set_over_tcp() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());

    SOCKET client = connect_to_server(TEST_PORT);
    if (client == INVALID_SOCKET) {
        ASSERT_TRUE(false);
        return;
    }

    // 1. List: RPUSH mylist x y -> :2\r\n
    std::string lpush_resp = send_and_receive(client, "*4\r\n$5\r\nRPUSH\r\n$6\r\nmylist\r\n$1\r\nx\r\n$1\r\ny\r\n");
    ASSERT_EQ(std::string(":2\r\n"), lpush_resp);

    // LRANGE mylist 0 -1 -> *2\r\n$1\r\nx\r\n$1\r\ny\r\n
    std::string lrange_resp = send_and_receive(client, "*4\r\n$6\r\nLRANGE\r\n$6\r\nmylist\r\n$1\r\n0\r\n$2\r\n-1\r\n");
    ASSERT_EQ(std::string("*2\r\n$1\r\nx\r\n$1\r\ny\r\n"), lrange_resp);

    // 2. Hash: HSET user:1 name Bob -> :1\r\n
    std::string hset_resp = send_and_receive(client, "*4\r\n$4\r\nHSET\r\n$6\r\nuser:1\r\n$4\r\nname\r\n$3\r\nBob\r\n");
    ASSERT_EQ(std::string(":1\r\n"), hset_resp);

    // HGET user:1 name -> $3\r\nBob\r\n
    std::string hget_resp = send_and_receive(client, "*3\r\n$4\r\nHGET\r\n$6\r\nuser:1\r\n$4\r\nname\r\n");
    ASSERT_EQ(std::string("$3\r\nBob\r\n"), hget_resp);

    // 3. Set: SADD numbers 10 20 -> :2\r\n
    std::string sadd_resp = send_and_receive(client, "*4\r\n$4\r\nSADD\r\n$7\r\nnumbers\r\n$2\r\n10\r\n$2\r\n20\r\n");
    ASSERT_EQ(std::string(":2\r\n"), sadd_resp);

    // SCARD numbers -> :2\r\n
    std::string scard_resp = send_and_receive(client, "*2\r\n$5\r\nSCARD\r\n$7\r\nnumbers\r\n");
    ASSERT_EQ(std::string(":2\r\n"), scard_resp);

    // 4. WRONGTYPE: GET mylist -> -WRONGTYPE...
    std::string wt_resp = send_and_receive(client, "*2\r\n$3\r\nGET\r\n$6\r\nmylist\r\n");
    ASSERT_EQ(std::string("-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"), wt_resp);

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

    std::string pipeline =
        "*1\r\n$4\r\nPING\r\n"
        "*3\r\n$3\r\nSET\r\n$1\r\nx\r\n$2\r\n99\r\n"
        "*2\r\n$3\r\nGET\r\n$1\r\nx\r\n";

    send(client, pipeline.data(), static_cast<int>(pipeline.size()), 0);

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
    RUN_TEST(test_command_list_hash_set_over_tcp);
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
