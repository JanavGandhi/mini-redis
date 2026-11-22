// reliability_test.cpp — Comprehensive tests for Milestone 9: Reliability & Performance.
//
// Covers:
//   1. Malformed input handling & protocol edge cases
//   2. Protocol fuzzing with pseudo-random byte streams
//   3. Resource limit enforcement: max database keys (OOM prevention)
//   4. Resource limit enforcement: max clients
//   5. Resource limit enforcement: max client buffer size (DoS prevention)
//   6. Server observability & stats (INFO command)

#ifndef FD_SETSIZE
#define FD_SETSIZE 256
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include "command.h"
#include "database.h"
#include "resp.h"
#include "server.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

// ─── Test Framework Helpers ─────────────────────────────────────────────────

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "FAILED: " << #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            ++g_tests_failed; \
            return; \
        } \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(actual, expected) \
    do { \
        if (!((actual) == (expected))) { \
            std::cerr << "FAILED: " << #actual << " == " << #expected << " (actual: " \
                      << (actual) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            ++g_tests_failed; \
            return; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        std::cout << "  " << #fn << "... " << std::flush; \
        int before = g_tests_failed; \
        fn(); \
        if (g_tests_failed == before) { \
            std::cout << "PASSED" << std::endl; \
            ++g_tests_passed; \
        } \
    } while (0)

// ─── Helper for TCP Client in Tests ─────────────────────────────────────────

SOCKET test_connect(uint16_t port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

std::string test_recv_line(SOCKET sock, int timeout_ms = 1000) {
    u_long mode = 0;
    ioctlsocket(sock, FIONBIO, &mode); // blocking mode

    // Set receive timeout
    DWORD tv = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    std::string result;
    char c = 0;
    while (recv(sock, &c, 1, 0) == 1) {
        result.push_back(c);
        if (result.size() >= 2 && result[result.size() - 2] == '\r' && result[result.size() - 1] == '\n') {
            break;
        }
    }
    return result;
}

// ─── 1. Malformed Input Tests ───────────────────────────────────────────────

void test_malformed_prefixes() {
    // Non-RESP prefix characters must return ParseStatus::Error
    const std::vector<std::string> invalid_inputs = {
        "?hello\r\n",
        "!error\r\n",
        "~tilde\r\n",
        "abc\r\n",
        "\x01\x02\r\n",
        "\xFF\xFE\r\n",
        "\r\n"
    };

    for (const auto& input : invalid_inputs) {
        RespParser parser;
        parser.append(input);
        RespValue val;
        ParseStatus status = parser.parse_next(val);
        ASSERT_EQ(status, ParseStatus::Error);
    }
}

void test_malformed_integers() {
    const std::vector<std::string> bad_ints = {
        ":\r\n",                     // Empty integer
        ":abc\r\n",                  // Non-numeric
        ":--123\r\n",                // Double negative
        ":++123\r\n",                // Double positive
        ":12a34\r\n",                // Mixed alphanumeric
        ":9999999999999999999999999999999999999999\r\n" // 64-bit overflow
    };

    for (const auto& input : bad_ints) {
        RespParser parser;
        parser.append(input);
        RespValue val;
        ParseStatus status = parser.parse_next(val);
        ASSERT_EQ(status, ParseStatus::Error);
    }
}

void test_malformed_bulk_strings() {
    // Negative length < -1
    {
        RespParser p;
        p.append("$-2\r\n");
        RespValue val;
        ASSERT_EQ(p.parse_next(val), ParseStatus::Error);
    }

    // Non-numeric length
    {
        RespParser p;
        p.append("$xyz\r\n");
        RespValue val;
        ASSERT_EQ(p.parse_next(val), ParseStatus::Error);
    }

    // Exceeds max bulk string limit (64MB)
    {
        RespParser p;
        p.append("$70000000\r\n");
        RespValue val;
        ASSERT_EQ(p.parse_next(val), ParseStatus::Error);
    }

    // Mismatched trailing CRLF (data length says 4, but 5th byte is not \r)
    {
        RespParser p;
        p.append("$4\r\nhello\r\n");
        RespValue val;
        ASSERT_EQ(p.parse_next(val), ParseStatus::Error);
    }
}

void test_malformed_arrays() {
    // Negative count < -1
    {
        RespParser p;
        p.append("*-2\r\n");
        RespValue val;
        ASSERT_EQ(p.parse_next(val), ParseStatus::Error);
    }

    // Count exceeds limit (1 million)
    {
        RespParser p;
        p.append("*2000000\r\n");
        RespValue val;
        ASSERT_EQ(p.parse_next(val), ParseStatus::Error);
    }

    // Array containing malformed child element
    {
        RespParser p;
        p.append("*2\r\n$3\r\nfoo\r\n?badchild\r\n");
        RespValue val;
        ASSERT_EQ(p.parse_next(val), ParseStatus::Error);
    }
}

// ─── 2. Protocol Fuzzing ────────────────────────────────────────────────────

void test_fuzz_parser_random_bytes() {
    // Feed 5,000 randomized sequences through RespParser.
    // Must never crash, segfault, or hang in an infinite loop.
    std::mt19937 rng(1337); // Deterministic seed
    std::uniform_int_distribution<int> len_dist(1, 256);
    std::uniform_int_distribution<int> byte_dist(0, 255);

    for (int iter = 0; iter < 5000; ++iter) {
        int len = len_dist(rng);
        std::string garbage(len, '\0');
        for (int i = 0; i < len; ++i) {
            garbage[i] = static_cast<char>(byte_dist(rng));
        }

        RespParser parser;
        parser.append(garbage);

        RespValue val;
        // Parse attempts must return either Ok, Incomplete, or Error safely
        ParseStatus status = parser.parse_next(val);
        (void)status; // Handled safely without exception or crash
    }
}

// ─── 3. Database Resource Limits (max_keys & OOM) ───────────────────────────

void test_resource_limit_max_keys() {
    Database db;
    db.set_max_keys(2);
    ASSERT_EQ(db.max_keys(), 2u);

    // 1. First 2 keys should succeed
    auto r1 = CommandDispatcher::execute_args({"SET", "k1", "v1"}, db);
    ASSERT_EQ(r1.string_value(), "OK");

    auto r2 = CommandDispatcher::execute_args({"SET", "k2", "v2"}, db);
    ASSERT_EQ(r2.string_value(), "OK");
    ASSERT_EQ(db.size(), 2u);

    // 2. 3rd key must fail with OOM error
    auto r3 = CommandDispatcher::execute_args({"SET", "k3", "v3"}, db);
    ASSERT_TRUE(r3.is_error());
    ASSERT_TRUE(r3.string_value().find("OOM") != std::string::npos);

    // 3. Overwriting existing key should still be allowed
    auto r1_update = CommandDispatcher::execute_args({"SET", "k1", "v1_new"}, db);
    ASSERT_EQ(r1_update.string_value(), "OK");
    ASSERT_EQ(db.get("k1").value.value(), "v1_new");

    // 4. Other data structures (LPUSH, HSET, SADD) must also respect max_keys
    auto r_lpush = CommandDispatcher::execute_args({"LPUSH", "k_list", "val"}, db);
    ASSERT_TRUE(r_lpush.is_error());
    ASSERT_TRUE(r_lpush.string_value().find("OOM") != std::string::npos);

    auto r_hset = CommandDispatcher::execute_args({"HSET", "k_hash", "f", "v"}, db);
    ASSERT_TRUE(r_hset.is_error());
    ASSERT_TRUE(r_hset.string_value().find("OOM") != std::string::npos);

    auto r_sadd = CommandDispatcher::execute_args({"SADD", "k_set", "m"}, db);
    ASSERT_TRUE(r_sadd.is_error());
    ASSERT_TRUE(r_sadd.string_value().find("OOM") != std::string::npos);

    // 5. Deleting a key frees a slot
    db.del({"k2"});
    ASSERT_EQ(db.size(), 1u);

    // Now adding k3 should succeed!
    auto r3_retry = CommandDispatcher::execute_args({"SET", "k3", "v3"}, db);
    ASSERT_EQ(r3_retry.string_value(), "OK");
    ASSERT_EQ(db.size(), 2u);
}

// ─── 4. Server Resource Limits (max_clients) ────────────────────────────────

void test_resource_limit_max_clients() {
    uint16_t port = 16383;
    Server server(port);
    server.set_max_clients(2);
    ASSERT_EQ(server.max_clients(), 2u);

    ASSERT_TRUE(server.start());
    std::thread server_thread([&]() { server.run(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Connect Client 1 -> Allowed
    SOCKET c1 = test_connect(port);
    ASSERT_TRUE(c1 != INVALID_SOCKET);

    // Connect Client 2 -> Allowed
    SOCKET c2 = test_connect(port);
    ASSERT_TRUE(c2 != INVALID_SOCKET);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(server.connected_clients_count(), 2u);

    // Connect Client 3 -> Exceeds max_clients, should be rejected with error
    SOCKET c3 = test_connect(port);
    ASSERT_TRUE(c3 != INVALID_SOCKET);

    std::string response = test_recv_line(c3);
    ASSERT_TRUE(response.find("-ERR max number of clients reached") != std::string::npos);

    closesocket(c1);
    closesocket(c2);
    closesocket(c3);

    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

// ─── 5. Server Resource Limits (buffer overflow / DoS protection) ───────────

void test_resource_limit_client_buffer_overflow() {
    uint16_t port = 16384;
    Server server(port);
    // Set a tiny buffer limit of 512 bytes for testing
    server.set_max_client_buffer_size(512);
    ASSERT_EQ(server.max_client_buffer_size(), 512u);

    ASSERT_TRUE(server.start());
    std::thread server_thread([&]() { server.run(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    SOCKET bad_client = test_connect(port);
    ASSERT_TRUE(bad_client != INVALID_SOCKET);

    // Send 1024 bytes of streaming junk without any CRLF
    std::string junk(1024, 'X');
    send(bad_client, junk.data(), static_cast<int>(junk.size()), 0);

    // Server must respond with buffer exceeded error and disconnect
    std::string response = test_recv_line(bad_client);
    ASSERT_TRUE(response.find("-ERR Protocol error: max client buffer size exceeded") != std::string::npos);

    closesocket(bad_client);

    // Verify server survived and continues servicing subsequent requests normally
    SOCKET good_client = test_connect(port);
    ASSERT_TRUE(good_client != INVALID_SOCKET);
    std::string ping_cmd = "*1\r\n$4\r\nPING\r\n";
    send(good_client, ping_cmd.data(), static_cast<int>(ping_cmd.size()), 0);
    std::string ping_resp = test_recv_line(good_client);
    ASSERT_EQ(ping_resp, "+PONG\r\n");

    closesocket(good_client);

    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

// ─── 6. Server Stats & INFO Command ─────────────────────────────────────────

void test_info_command() {
    Database db;
    db.set("test_key_1", "v1");
    db.set("test_key_2", "v2");

    CommandDispatcher::reset_stats();

    auto info_resp = CommandDispatcher::execute_args({"INFO"}, db);
    ASSERT_TRUE(info_resp.is_bulk_string());
    const std::string& info_str = info_resp.string_value();

    ASSERT_TRUE(info_str.find("redis_version:0.1.0") != std::string::npos);
    ASSERT_TRUE(info_str.find("os:Windows") != std::string::npos);
    ASSERT_TRUE(info_str.find("uptime_in_seconds:") != std::string::npos);
    ASSERT_TRUE(info_str.find("total_commands_processed:") != std::string::npos);
    ASSERT_TRUE(info_str.find("db0:keys=2") != std::string::npos);
}

// ─── Main Runner ────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n=== mini-redis Reliability & Performance Tests (Milestone 9) ===\n" << std::endl;

    std::cout << "[Malformed Input & Protocol Protection]" << std::endl;
    RUN_TEST(test_malformed_prefixes);
    RUN_TEST(test_malformed_integers);
    RUN_TEST(test_malformed_bulk_strings);
    RUN_TEST(test_malformed_arrays);

    std::cout << "\n[Protocol Fuzzing]" << std::endl;
    RUN_TEST(test_fuzz_parser_random_bytes);

    std::cout << "\n[Resource Limits & Protection]" << std::endl;
    RUN_TEST(test_resource_limit_max_keys);
    RUN_TEST(test_resource_limit_max_clients);
    RUN_TEST(test_resource_limit_client_buffer_overflow);

    std::cout << "\n[Observability & Stats]" << std::endl;
    RUN_TEST(test_info_command);

    std::cout << "\n===========================================" << std::endl;
    std::cout << "Total: " << (g_tests_passed + g_tests_failed)
              << "  Passed: " << g_tests_passed
              << "  Failed: " << g_tests_failed << std::endl;

    if (g_tests_failed == 0) {
        std::cout << "All tests PASSED!" << std::endl;
        std::cout << "===========================================\n" << std::endl;
        return 0;
    } else {
        std::cout << "SOME TESTS FAILED!" << std::endl;
        std::cout << "===========================================\n" << std::endl;
        return 1;
    }
}
