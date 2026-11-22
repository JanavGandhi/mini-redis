// server_test.cpp — Integration tests for non-blocking multi-client Server (Milestone 8).

#ifndef FD_SETSIZE
#define FD_SETSIZE 256
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "server.h"

#include <winsock2.h>
#include <ws2tcpip.h>

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

// ─── Basic Lifecycle & Command Tests ─────────────────────────────────────────

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

    // List: RPUSH mylist x y
    std::string lpush_resp = send_and_receive(client, "*4\r\n$5\r\nRPUSH\r\n$6\r\nmylist\r\n$1\r\nx\r\n$1\r\ny\r\n");
    ASSERT_EQ(std::string(":2\r\n"), lpush_resp);

    // Hash: HSET user:1 name Bob
    std::string hset_resp = send_and_receive(client, "*4\r\n$4\r\nHSET\r\n$6\r\nuser:1\r\n$4\r\nname\r\n$3\r\nBob\r\n");
    ASSERT_EQ(std::string(":1\r\n"), hset_resp);

    // Set: SADD numbers 10 20
    std::string sadd_resp = send_and_receive(client, "*4\r\n$4\r\nSADD\r\n$7\r\nnumbers\r\n$2\r\n10\r\n$2\r\n20\r\n");
    ASSERT_EQ(std::string(":2\r\n"), sadd_resp);

    closesocket(client);
}

void test_command_expire_and_ttl_over_tcp() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());

    SOCKET client = connect_to_server(TEST_PORT);
    if (client == INVALID_SOCKET) {
        ASSERT_TRUE(false);
        return;
    }

    std::string set_resp = send_and_receive(client, "*5\r\n$3\r\nSET\r\n$3\r\nkey\r\n$3\r\nval\r\n$2\r\nEX\r\n$3\r\n100\r\n");
    ASSERT_EQ(std::string("+OK\r\n"), set_resp);

    std::string ttl_resp = send_and_receive(client, "*2\r\n$3\r\nTTL\r\n$3\r\nkey\r\n");
    ASSERT_TRUE(ttl_resp.size() >= 3 && ttl_resp[0] == ':');

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

// ─── Milestone 8: Multi-Client & Concurrency Tests ───────────────────────────

void test_multiple_concurrent_clients() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());

    const int CLIENT_COUNT = 8;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < CLIENT_COUNT; ++i) {
        threads.emplace_back([i, &success_count]() {
            SOCKET client = connect_to_server(TEST_PORT);
            if (client == INVALID_SOCKET) return;

            std::string key = "client_key_" + std::to_string(i);
            std::string val = "client_val_" + std::to_string(i);

            // SET key val
            std::string set_cmd = "*3\r\n$3\r\nSET\r\n$" + std::to_string(key.size()) + "\r\n" + key +
                                  "\r\n$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
            std::string set_resp = send_and_receive(client, set_cmd);
            if (set_resp != "+OK\r\n") {
                closesocket(client);
                return;
            }

            // GET key
            std::string get_cmd = "*2\r\n$3\r\nGET\r\n$" + std::to_string(key.size()) + "\r\n" + key + "\r\n";
            std::string get_resp = send_and_receive(client, get_cmd);
            std::string expected_get = "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";

            if (get_resp == expected_get) {
                ++success_count;
            }

            closesocket(client);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(CLIENT_COUNT, success_count.load());
}

void test_interleaved_partial_input() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());

    // Client 1 connects
    SOCKET client1 = connect_to_server(TEST_PORT);
    ASSERT_TRUE(client1 != INVALID_SOCKET);

    // Client 1 sends partial command (split across packets):
    // *2\r\n$4\r\nECHO\r\n$5\r\nhe
    std::string part1 = "*2\r\n$4\r\nECHO\r\n$5\r\nhe";
    send(client1, part1.data(), static_cast<int>(part1.size()), 0);

    // Client 2 connects concurrently while Client 1 is still half-way
    SOCKET client2 = connect_to_server(TEST_PORT);
    ASSERT_TRUE(client2 != INVALID_SOCKET);

    // Client 2 sends a complete PING and gets immediate response!
    std::string c2_resp = send_and_receive(client2, "*1\r\n$4\r\nPING\r\n");
    ASSERT_EQ(std::string("+PONG\r\n"), c2_resp);
    closesocket(client2);

    // Now Client 1 sends the remainder: llo\r\n
    std::string part2 = "llo\r\n";
    send(client1, part2.data(), static_cast<int>(part2.size()), 0);

    ASSERT_TRUE(wait_for_data(client1, 2000));
    char buf[1024];
    int n = recv(client1, buf, sizeof(buf), 0);
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(std::string("$5\r\nhello\r\n"), std::string(buf, n));

    closesocket(client1);
}

void test_client_disconnect_does_not_affect_others() {
    TestServer ts(TEST_PORT);
    ASSERT_TRUE(ts.start());

    SOCKET client1 = connect_to_server(TEST_PORT);
    SOCKET client2 = connect_to_server(TEST_PORT);
    ASSERT_TRUE(client1 != INVALID_SOCKET);
    ASSERT_TRUE(client2 != INVALID_SOCKET);

    // Both clients active
    ASSERT_EQ(std::string("+PONG\r\n"), send_and_receive(client1, "*1\r\n$4\r\nPING\r\n"));
    ASSERT_EQ(std::string("+PONG\r\n"), send_and_receive(client2, "*1\r\n$4\r\nPING\r\n"));

    // Client 1 abruptly disconnects
    closesocket(client1);

    // Give server event loop time to handle disconnect
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Client 2 should continue working without issue
    ASSERT_EQ(std::string("+OK\r\n"), send_and_receive(client2, "*3\r\n$3\r\nSET\r\n$4\r\nstay\r\n$5\r\nalive\r\n"));
    ASSERT_EQ(std::string("$5\r\nalive\r\n"), send_and_receive(client2, "*2\r\n$3\r\nGET\r\n$4\r\nstay\r\n"));

    closesocket(client2);
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
    RUN_TEST(test_command_expire_and_ttl_over_tcp);
    std::cout << std::endl;

    std::cout << "[TCP Pipelining]" << std::endl;
    RUN_TEST(test_pipelining);
    std::cout << std::endl;

    std::cout << "[Milestone 8: Multi-Client & Concurrency]" << std::endl;
    RUN_TEST(test_multiple_concurrent_clients);
    RUN_TEST(test_interleaved_partial_input);
    RUN_TEST(test_client_disconnect_does_not_affect_others);
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
