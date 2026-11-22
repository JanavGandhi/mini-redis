// command_test.cpp — Unit tests for CommandDispatcher with Expiration & TTL (Milestone 6).

#include "command.h"
#include "database.h"
#include "resp.h"

#include <chrono>
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

RespValue make_cmd(const std::vector<std::string>& args) {
    std::vector<RespValue> elements;
    for (const auto& a : args) {
        elements.push_back(RespValue::make_bulk_string(a));
    }
    return RespValue::make_array(elements);
}

// ─── Core / String Command Tests ─────────────────────────────────────────────

void test_ping() {
    Database db;
    RespValue r1 = CommandDispatcher::execute(make_cmd({"PING"}), db);
    ASSERT_TRUE(r1.is_simple_string());
    ASSERT_EQ(std::string("PONG"), r1.string_value());
}

void test_echo() {
    Database db;
    RespValue r = CommandDispatcher::execute(make_cmd({"ECHO", "hello"}), db);
    ASSERT_TRUE(r.is_bulk_string());
    ASSERT_EQ(std::string("hello"), r.string_value());
}

void test_set_and_get() {
    Database db;
    RespValue set_res = CommandDispatcher::execute(make_cmd({"SET", "k", "v"}), db);
    ASSERT_TRUE(set_res.is_simple_string());
    ASSERT_EQ(std::string("OK"), set_res.string_value());

    RespValue get_res = CommandDispatcher::execute(make_cmd({"GET", "k"}), db);
    ASSERT_TRUE(get_res.is_bulk_string());
    ASSERT_EQ(std::string("v"), get_res.string_value());
}

void test_del_and_exists() {
    Database db;
    CommandDispatcher::execute(make_cmd({"SET", "a", "1"}), db);
    CommandDispatcher::execute(make_cmd({"SET", "b", "2"}), db);

    RespValue ex = CommandDispatcher::execute(make_cmd({"EXISTS", "a", "b", "c"}), db);
    ASSERT_EQ(int64_t(2), ex.integer_value());

    RespValue del = CommandDispatcher::execute(make_cmd({"DEL", "a", "c"}), db);
    ASSERT_EQ(int64_t(1), del.integer_value());
}

void test_flushdb() {
    Database db;
    CommandDispatcher::execute(make_cmd({"SET", "k", "v"}), db);
    RespValue f = CommandDispatcher::execute(make_cmd({"FLUSHDB"}), db);
    ASSERT_TRUE(f.is_simple_string());
    ASSERT_EQ(0u, db.size());
}

// ─── List / Hash / Set Tests ─────────────────────────────────────────────────

void test_list_commands() {
    Database db;
    CommandDispatcher::execute(make_cmd({"RPUSH", "letters", "a", "b"}), db);
    RespValue len = CommandDispatcher::execute(make_cmd({"LLEN", "letters"}), db);
    ASSERT_EQ(int64_t(2), len.integer_value());

    RespValue range = CommandDispatcher::execute(make_cmd({"LRANGE", "letters", "0", "-1"}), db);
    ASSERT_TRUE(range.is_array());
    ASSERT_EQ(2u, range.array_value().size());
}

void test_hash_commands() {
    Database db;
    CommandDispatcher::execute(make_cmd({"HSET", "user", "name", "Alice"}), db);
    RespValue g = CommandDispatcher::execute(make_cmd({"HGET", "user", "name"}), db);
    ASSERT_TRUE(g.is_bulk_string());
    ASSERT_EQ(std::string("Alice"), g.string_value());
}

void test_set_commands() {
    Database db;
    CommandDispatcher::execute(make_cmd({"SADD", "tags", "c++", "go"}), db);
    RespValue card = CommandDispatcher::execute(make_cmd({"SCARD", "tags"}), db);
    ASSERT_EQ(int64_t(2), card.integer_value());
}

void test_wrongtype_errors() {
    Database db;
    CommandDispatcher::execute(make_cmd({"SET", "str", "val"}), db);
    RespValue err = CommandDispatcher::execute(make_cmd({"LPUSH", "str", "x"}), db);
    ASSERT_TRUE(err.is_error());
}

// ─── Milestone 6: Expiration & TTL Tests ─────────────────────────────────────

void test_cmd_expire_and_ttl() {
    Database db;
    CommandDispatcher::execute(make_cmd({"SET", "k", "v"}), db);

    // TTL before expire -> -1
    RespValue ttl_init = CommandDispatcher::execute(make_cmd({"TTL", "k"}), db);
    ASSERT_EQ(int64_t(-1), ttl_init.integer_value());

    // EXPIRE k 60 -> :1
    RespValue exp = CommandDispatcher::execute(make_cmd({"EXPIRE", "k", "60"}), db);
    ASSERT_EQ(int64_t(1), exp.integer_value());

    // TTL after expire -> > 50
    RespValue ttl_after = CommandDispatcher::execute(make_cmd({"TTL", "k"}), db);
    ASSERT_TRUE(ttl_after.integer_value() > 50);

    // PTTL -> > 50000
    RespValue pttl_res = CommandDispatcher::execute(make_cmd({"PTTL", "k"}), db);
    ASSERT_TRUE(pttl_res.integer_value() > 50000);

    // Missing key -> -2
    RespValue ttl_miss = CommandDispatcher::execute(make_cmd({"TTL", "ghost"}), db);
    ASSERT_EQ(int64_t(-2), ttl_miss.integer_value());
}

void test_cmd_pexpire() {
    Database db;
    CommandDispatcher::execute(make_cmd({"SET", "k", "v"}), db);

    // PEXPIRE k 5000 -> :1
    RespValue pexp = CommandDispatcher::execute(make_cmd({"PEXPIRE", "k", "5000"}), db);
    ASSERT_EQ(int64_t(1), pexp.integer_value());

    RespValue pttl_val = CommandDispatcher::execute(make_cmd({"PTTL", "k"}), db);
    ASSERT_TRUE(pttl_val.integer_value() > 4000);
}

void test_cmd_set_with_ex_and_px() {
    Database db;
    // SET k1 v1 EX 10
    RespValue r1 = CommandDispatcher::execute(make_cmd({"SET", "k1", "v1", "EX", "10"}), db);
    ASSERT_TRUE(r1.is_simple_string());
    ASSERT_EQ(std::string("OK"), r1.string_value());
    ASSERT_TRUE(db.ttl("k1") > 5);

    // SET k2 v2 PX 20000
    RespValue r2 = CommandDispatcher::execute(make_cmd({"SET", "k2", "v2", "PX", "20000"}), db);
    ASSERT_TRUE(r2.is_simple_string());
    ASSERT_TRUE(db.pttl("k2") > 15000);

    // Syntax error on bad options
    RespValue err1 = CommandDispatcher::execute(make_cmd({"SET", "k3", "v3", "INVALID", "10"}), db);
    ASSERT_TRUE(err1.is_error());

    // Bad integer for EX
    RespValue err2 = CommandDispatcher::execute(make_cmd({"SET", "k4", "v4", "EX", "abc"}), db);
    ASSERT_TRUE(err2.is_error());
}

void test_cmd_ttl_expiration_behavior() {
    Database db;
    // SET session token PX 50 (50ms)
    CommandDispatcher::execute(make_cmd({"SET", "session", "tok", "PX", "50"}), db);

    // Check GET works before expire
    RespValue before = CommandDispatcher::execute(make_cmd({"GET", "session"}), db);
    ASSERT_TRUE(before.is_bulk_string());
    ASSERT_EQ(std::string("tok"), before.string_value());

    // Wait 70ms
    std::this_thread::sleep_for(std::chrono::milliseconds(70));

    // GET should now return null bulk string
    RespValue after = CommandDispatcher::execute(make_cmd({"GET", "session"}), db);
    ASSERT_TRUE(after.is_null());

    // TTL should return -2
    RespValue ttl_exp = CommandDispatcher::execute(make_cmd({"TTL", "session"}), db);
    ASSERT_EQ(int64_t(-2), ttl_exp.integer_value());
}

// ─── Test Runner ─────────────────────────────────────────────────────────────

int main() {
    std::cout << std::endl;
    std::cout << "=== mini-redis Command Dispatcher Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "[Core / String Commands]" << std::endl;
    RUN_TEST(test_ping);
    RUN_TEST(test_echo);
    RUN_TEST(test_set_and_get);
    RUN_TEST(test_del_and_exists);
    RUN_TEST(test_flushdb);
    std::cout << std::endl;

    std::cout << "[List / Hash / Set Commands]" << std::endl;
    RUN_TEST(test_list_commands);
    RUN_TEST(test_hash_commands);
    RUN_TEST(test_set_commands);
    RUN_TEST(test_wrongtype_errors);
    std::cout << std::endl;

    std::cout << "[Expiration & TTL (Milestone 6)]" << std::endl;
    RUN_TEST(test_cmd_expire_and_ttl);
    RUN_TEST(test_cmd_pexpire);
    RUN_TEST(test_cmd_set_with_ex_and_px);
    RUN_TEST(test_cmd_ttl_expiration_behavior);
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
