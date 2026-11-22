// command_test.cpp — Unit tests for CommandDispatcher.

#include "command.h"
#include "database.h"
#include "resp.h"

#include <iostream>
#include <string>
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

// ─── Helper to build RESP command array ─────────────────────────────────────

RespValue make_cmd(const std::vector<std::string>& args) {
    std::vector<RespValue> elements;
    for (const auto& a : args) {
        elements.push_back(RespValue::make_bulk_string(a));
    }
    return RespValue::make_array(elements);
}

// ─── Test Cases ──────────────────────────────────────────────────────────────

void test_ping_no_args() {
    Database db;
    RespValue res = CommandDispatcher::execute(make_cmd({"PING"}), db);
    ASSERT_TRUE(res.is_simple_string());
    ASSERT_EQ(std::string("PONG"), res.string_value());
}

void test_ping_with_message() {
    Database db;
    RespValue res = CommandDispatcher::execute(make_cmd({"PING", "hello"}), db);
    ASSERT_TRUE(res.is_bulk_string());
    ASSERT_EQ(std::string("hello"), res.string_value());
}

void test_ping_wrong_args() {
    Database db;
    RespValue res = CommandDispatcher::execute(make_cmd({"PING", "a", "b"}), db);
    ASSERT_TRUE(res.is_error());
}

void test_echo_valid() {
    Database db;
    RespValue res = CommandDispatcher::execute(make_cmd({"ECHO", "hello world"}), db);
    ASSERT_TRUE(res.is_bulk_string());
    ASSERT_EQ(std::string("hello world"), res.string_value());
}

void test_echo_wrong_args() {
    Database db;
    RespValue res1 = CommandDispatcher::execute(make_cmd({"ECHO"}), db);
    ASSERT_TRUE(res1.is_error());

    RespValue res2 = CommandDispatcher::execute(make_cmd({"ECHO", "a", "b"}), db);
    ASSERT_TRUE(res2.is_error());
}

void test_set_and_get() {
    Database db;
    RespValue set_res = CommandDispatcher::execute(make_cmd({"SET", "mykey", "myval"}), db);
    ASSERT_TRUE(set_res.is_simple_string());
    ASSERT_EQ(std::string("OK"), set_res.string_value());

    RespValue get_res = CommandDispatcher::execute(make_cmd({"GET", "mykey"}), db);
    ASSERT_TRUE(get_res.is_bulk_string());
    ASSERT_EQ(std::string("myval"), get_res.string_value());
}

void test_set_wrong_args() {
    Database db;
    RespValue res1 = CommandDispatcher::execute(make_cmd({"SET", "k"}), db);
    ASSERT_TRUE(res1.is_error());

    RespValue res2 = CommandDispatcher::execute(make_cmd({"SET", "k", "v", "extra"}), db);
    ASSERT_TRUE(res2.is_error());
}

void test_get_nonexistent() {
    Database db;
    RespValue res = CommandDispatcher::execute(make_cmd({"GET", "missing"}), db);
    ASSERT_TRUE(res.is_null());
    ASSERT_EQ(std::string("$-1\r\n"), res.serialize());
}

void test_get_wrong_args() {
    Database db;
    RespValue res1 = CommandDispatcher::execute(make_cmd({"GET"}), db);
    ASSERT_TRUE(res1.is_error());

    RespValue res2 = CommandDispatcher::execute(make_cmd({"GET", "a", "b"}), db);
    ASSERT_TRUE(res2.is_error());
}

void test_del_single_and_multiple() {
    Database db;
    CommandDispatcher::execute(make_cmd({"SET", "k1", "v1"}), db);
    CommandDispatcher::execute(make_cmd({"SET", "k2", "v2"}), db);

    // Delete k1
    RespValue del1 = CommandDispatcher::execute(make_cmd({"DEL", "k1"}), db);
    ASSERT_TRUE(del1.is_integer());
    ASSERT_EQ(int64_t(1), del1.integer_value());

    // Delete missing
    RespValue del_missing = CommandDispatcher::execute(make_cmd({"DEL", "k1"}), db);
    ASSERT_EQ(int64_t(0), del_missing.integer_value());

    // Delete multiple (k2 exists, k3 does not)
    RespValue del_multi = CommandDispatcher::execute(make_cmd({"DEL", "k2", "k3"}), db);
    ASSERT_EQ(int64_t(1), del_multi.integer_value());
}

void test_del_wrong_args() {
    Database db;
    RespValue res = CommandDispatcher::execute(make_cmd({"DEL"}), db);
    ASSERT_TRUE(res.is_error());
}

void test_exists_command() {
    Database db;
    CommandDispatcher::execute(make_cmd({"SET", "a", "1"}), db);
    CommandDispatcher::execute(make_cmd({"SET", "b", "2"}), db);

    // Check single
    RespValue ex1 = CommandDispatcher::execute(make_cmd({"EXISTS", "a"}), db);
    ASSERT_TRUE(ex1.is_integer());
    ASSERT_EQ(int64_t(1), ex1.integer_value());

    // Check missing
    RespValue ex_miss = CommandDispatcher::execute(make_cmd({"EXISTS", "missing"}), db);
    ASSERT_EQ(int64_t(0), ex_miss.integer_value());

    // Check multiple with duplicates
    RespValue ex_multi = CommandDispatcher::execute(make_cmd({"EXISTS", "a", "b", "a", "c"}), db);
    ASSERT_EQ(int64_t(3), ex_multi.integer_value()); // a (1) + b (1) + a (1) = 3
}

void test_exists_wrong_args() {
    Database db;
    RespValue res = CommandDispatcher::execute(make_cmd({"EXISTS"}), db);
    ASSERT_TRUE(res.is_error());
}

void test_flushdb_command() {
    Database db;
    CommandDispatcher::execute(make_cmd({"SET", "k1", "v1"}), db);
    CommandDispatcher::execute(make_cmd({"SET", "k2", "v2"}), db);
    ASSERT_EQ(2u, db.size());

    RespValue flush_res = CommandDispatcher::execute(make_cmd({"FLUSHDB"}), db);
    ASSERT_TRUE(flush_res.is_simple_string());
    ASSERT_EQ(std::string("OK"), flush_res.string_value());
    ASSERT_EQ(0u, db.size());
}

void test_case_insensitivity() {
    Database db;
    // Lowercase
    RespValue r1 = CommandDispatcher::execute(make_cmd({"ping"}), db);
    ASSERT_TRUE(r1.is_simple_string());
    ASSERT_EQ(std::string("PONG"), r1.string_value());

    // Mixed case
    RespValue r2 = CommandDispatcher::execute(make_cmd({"sEt", "Key", "Val"}), db);
    ASSERT_TRUE(r2.is_simple_string());

    RespValue r3 = CommandDispatcher::execute(make_cmd({"gEt", "Key"}), db);
    ASSERT_TRUE(r3.is_bulk_string());
    ASSERT_EQ(std::string("Val"), r3.string_value());

    RespValue r4 = CommandDispatcher::execute(make_cmd({"eXiStS", "Key"}), db);
    ASSERT_EQ(int64_t(1), r4.integer_value());

    RespValue r5 = CommandDispatcher::execute(make_cmd({"dEl", "Key"}), db);
    ASSERT_EQ(int64_t(1), r5.integer_value());
}

void test_command_docs_compatibility() {
    Database db;
    // redis-cli sends "COMMAND" on startup
    RespValue res = CommandDispatcher::execute(make_cmd({"COMMAND"}), db);
    ASSERT_TRUE(res.is_array());
    ASSERT_EQ(0u, res.array_value().size());
}

void test_unknown_command() {
    Database db;
    RespValue res = CommandDispatcher::execute(make_cmd({"FOOBAR"}), db);
    ASSERT_TRUE(res.is_error());
    ASSERT_EQ(std::string("-ERR unknown command 'FOOBAR'\r\n"), res.serialize());
}

void test_protocol_error_non_array() {
    Database db;
    // Single string instead of array
    RespValue non_array = RespValue::make_simple_string("PING");
    RespValue res = CommandDispatcher::execute(non_array, db);
    ASSERT_TRUE(res.is_error());
}

// ─── Test Runner ─────────────────────────────────────────────────────────────

int main() {
    std::cout << std::endl;
    std::cout << "=== mini-redis Command Dispatcher Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "[PING / ECHO]" << std::endl;
    RUN_TEST(test_ping_no_args);
    RUN_TEST(test_ping_with_message);
    RUN_TEST(test_ping_wrong_args);
    RUN_TEST(test_echo_valid);
    RUN_TEST(test_echo_wrong_args);
    std::cout << std::endl;

    std::cout << "[SET / GET]" << std::endl;
    RUN_TEST(test_set_and_get);
    RUN_TEST(test_set_wrong_args);
    RUN_TEST(test_get_nonexistent);
    RUN_TEST(test_get_wrong_args);
    std::cout << std::endl;

    std::cout << "[DEL / EXISTS / FLUSHDB]" << std::endl;
    RUN_TEST(test_del_single_and_multiple);
    RUN_TEST(test_del_wrong_args);
    RUN_TEST(test_exists_command);
    RUN_TEST(test_exists_wrong_args);
    RUN_TEST(test_flushdb_command);
    std::cout << std::endl;

    std::cout << "[Protocol & Compatibility]" << std::endl;
    RUN_TEST(test_case_insensitivity);
    RUN_TEST(test_command_docs_compatibility);
    RUN_TEST(test_unknown_command);
    RUN_TEST(test_protocol_error_non_array);
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
