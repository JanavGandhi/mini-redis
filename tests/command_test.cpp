// command_test.cpp — Unit tests for CommandDispatcher with all data structures.

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

// ─── Core / String Command Tests ─────────────────────────────────────────────

void test_ping() {
    Database db;
    RespValue r1 = CommandDispatcher::execute(make_cmd({"PING"}), db);
    ASSERT_TRUE(r1.is_simple_string());
    ASSERT_EQ(std::string("PONG"), r1.string_value());

    RespValue r2 = CommandDispatcher::execute(make_cmd({"PING", "hi"}), db);
    ASSERT_TRUE(r2.is_bulk_string());
    ASSERT_EQ(std::string("hi"), r2.string_value());

    RespValue r3 = CommandDispatcher::execute(make_cmd({"PING", "a", "b"}), db);
    ASSERT_TRUE(r3.is_error());
}

void test_echo() {
    Database db;
    RespValue r = CommandDispatcher::execute(make_cmd({"ECHO", "hello"}), db);
    ASSERT_TRUE(r.is_bulk_string());
    ASSERT_EQ(std::string("hello"), r.string_value());

    RespValue err = CommandDispatcher::execute(make_cmd({"ECHO"}), db);
    ASSERT_TRUE(err.is_error());
}

void test_set_and_get() {
    Database db;
    RespValue set_res = CommandDispatcher::execute(make_cmd({"SET", "k", "v"}), db);
    ASSERT_TRUE(set_res.is_simple_string());
    ASSERT_EQ(std::string("OK"), set_res.string_value());

    RespValue get_res = CommandDispatcher::execute(make_cmd({"GET", "k"}), db);
    ASSERT_TRUE(get_res.is_bulk_string());
    ASSERT_EQ(std::string("v"), get_res.string_value());

    RespValue miss = CommandDispatcher::execute(make_cmd({"GET", "missing"}), db);
    ASSERT_TRUE(miss.is_null());
}

void test_del_and_exists() {
    Database db;
    CommandDispatcher::execute(make_cmd({"SET", "a", "1"}), db);
    CommandDispatcher::execute(make_cmd({"SET", "b", "2"}), db);

    RespValue ex = CommandDispatcher::execute(make_cmd({"EXISTS", "a", "b", "c"}), db);
    ASSERT_EQ(int64_t(2), ex.integer_value());

    RespValue del = CommandDispatcher::execute(make_cmd({"DEL", "a", "c"}), db);
    ASSERT_EQ(int64_t(1), del.integer_value());

    RespValue ex2 = CommandDispatcher::execute(make_cmd({"EXISTS", "a"}), db);
    ASSERT_EQ(int64_t(0), ex2.integer_value());
}

void test_flushdb() {
    Database db;
    CommandDispatcher::execute(make_cmd({"SET", "k", "v"}), db);
    RespValue f = CommandDispatcher::execute(make_cmd({"FLUSHDB"}), db);
    ASSERT_TRUE(f.is_simple_string());
    ASSERT_EQ(std::string("OK"), f.string_value());
    ASSERT_EQ(0u, db.size());
}

// ─── List Command Tests ──────────────────────────────────────────────────────

void test_list_commands() {
    Database db;
    // LPUSH
    RespValue l1 = CommandDispatcher::execute(make_cmd({"LPUSH", "letters", "b", "c"}), db);
    ASSERT_TRUE(l1.is_integer());
    ASSERT_EQ(int64_t(2), l1.integer_value());

    // RPUSH
    RespValue l2 = CommandDispatcher::execute(make_cmd({"RPUSH", "letters", "d"}), db);
    ASSERT_EQ(int64_t(3), l2.integer_value());

    // LLEN
    RespValue len = CommandDispatcher::execute(make_cmd({"LLEN", "letters"}), db);
    ASSERT_EQ(int64_t(3), len.integer_value());

    // LRANGE 0 -1 -> [c, b, d]
    RespValue range = CommandDispatcher::execute(make_cmd({"LRANGE", "letters", "0", "-1"}), db);
    ASSERT_TRUE(range.is_array());
    ASSERT_EQ(3u, range.array_value().size());
    ASSERT_EQ(std::string("c"), range.array_value()[0].string_value());
    ASSERT_EQ(std::string("b"), range.array_value()[1].string_value());
    ASSERT_EQ(std::string("d"), range.array_value()[2].string_value());

    // LPOP -> c
    RespValue pop1 = CommandDispatcher::execute(make_cmd({"LPOP", "letters"}), db);
    ASSERT_TRUE(pop1.is_bulk_string());
    ASSERT_EQ(std::string("c"), pop1.string_value());

    // RPOP -> d
    RespValue pop2 = CommandDispatcher::execute(make_cmd({"RPOP", "letters"}), db);
    ASSERT_TRUE(pop2.is_bulk_string());
    ASSERT_EQ(std::string("d"), pop2.string_value());

    // Pop remaining -> b
    CommandDispatcher::execute(make_cmd({"LPOP", "letters"}), db);

    // Now empty -> returns null
    RespValue pop_empty = CommandDispatcher::execute(make_cmd({"LPOP", "letters"}), db);
    ASSERT_TRUE(pop_empty.is_null());
}

void test_list_wrong_args() {
    Database db;
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"LPUSH", "k"}), db).is_error());
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"RPUSH", "k"}), db).is_error());
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"LPOP"}), db).is_error());
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"RPOP"}), db).is_error());
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"LLEN"}), db).is_error());
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"LRANGE", "k", "0"}), db).is_error());
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"LRANGE", "k", "abc", "1"}), db).is_error());
}

// ─── Hash Command Tests ──────────────────────────────────────────────────────

void test_hash_commands() {
    Database db;
    // HSET user:1 name Alice age 30
    RespValue hset_res = CommandDispatcher::execute(make_cmd({"HSET", "user:1", "name", "Alice", "age", "30"}), db);
    ASSERT_TRUE(hset_res.is_integer());
    ASSERT_EQ(int64_t(2), hset_res.integer_value());

    // HGET user:1 name
    RespValue hget_res = CommandDispatcher::execute(make_cmd({"HGET", "user:1", "name"}), db);
    ASSERT_TRUE(hget_res.is_bulk_string());
    ASSERT_EQ(std::string("Alice"), hget_res.string_value());

    // HEXISTS user:1 age
    RespValue hexists_res = CommandDispatcher::execute(make_cmd({"HEXISTS", "user:1", "age"}), db);
    ASSERT_EQ(int64_t(1), hexists_res.integer_value());

    // HGETALL user:1
    RespValue hgetall_res = CommandDispatcher::execute(make_cmd({"HGETALL", "user:1"}), db);
    ASSERT_TRUE(hgetall_res.is_array());
    ASSERT_EQ(4u, hgetall_res.array_value().size());

    // HDEL user:1 age
    RespValue hdel_res = CommandDispatcher::execute(make_cmd({"HDEL", "user:1", "age"}), db);
    ASSERT_EQ(int64_t(1), hdel_res.integer_value());
    ASSERT_EQ(int64_t(0), CommandDispatcher::execute(make_cmd({"HEXISTS", "user:1", "age"}), db).integer_value());
}

void test_hash_wrong_args() {
    Database db;
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"HSET", "k", "field"}), db).is_error());
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"HGET", "k"}), db).is_error());
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"HDEL", "k"}), db).is_error());
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"HEXISTS", "k"}), db).is_error());
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"HGETALL"}), db).is_error());
}

// ─── Set Command Tests ───────────────────────────────────────────────────────

void test_set_commands() {
    Database db;
    // SADD tags c++ redis networking c++
    RespValue sadd_res = CommandDispatcher::execute(make_cmd({"SADD", "tags", "c++", "redis", "networking", "c++"}), db);
    ASSERT_TRUE(sadd_res.is_integer());
    ASSERT_EQ(int64_t(3), sadd_res.integer_value());

    // SCARD tags
    RespValue scard_res = CommandDispatcher::execute(make_cmd({"SCARD", "tags"}), db);
    ASSERT_EQ(int64_t(3), scard_res.integer_value());

    // SISMEMBER tags redis
    RespValue sismember_res = CommandDispatcher::execute(make_cmd({"SISMEMBER", "tags", "redis"}), db);
    ASSERT_EQ(int64_t(1), sismember_res.integer_value());

    // SMEMBERS tags
    RespValue smembers_res = CommandDispatcher::execute(make_cmd({"SMEMBERS", "tags"}), db);
    ASSERT_TRUE(smembers_res.is_array());
    ASSERT_EQ(3u, smembers_res.array_value().size());

    // SREM tags c++
    RespValue srem_res = CommandDispatcher::execute(make_cmd({"SREM", "tags", "c++"}), db);
    ASSERT_EQ(int64_t(1), srem_res.integer_value());
    ASSERT_EQ(int64_t(0), CommandDispatcher::execute(make_cmd({"SISMEMBER", "tags", "c++"}), db).integer_value());
}

void test_set_wrong_args() {
    Database db;
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"SADD", "k"}), db).is_error());
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"SREM", "k"}), db).is_error());
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"SISMEMBER", "k"}), db).is_error());
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"SMEMBERS"}), db).is_error());
    ASSERT_TRUE(CommandDispatcher::execute(make_cmd({"SCARD"}), db).is_error());
}

// ─── WRONGTYPE Error Tests ───────────────────────────────────────────────────

void test_wrongtype_errors() {
    Database db;
    // Store a string
    CommandDispatcher::execute(make_cmd({"SET", "str", "val"}), db);

    // Call List command on String
    RespValue err1 = CommandDispatcher::execute(make_cmd({"LPUSH", "str", "x"}), db);
    ASSERT_TRUE(err1.is_error());
    ASSERT_EQ(std::string("-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"), err1.serialize());

    // Call Hash command on String
    RespValue err2 = CommandDispatcher::execute(make_cmd({"HSET", "str", "f", "v"}), db);
    ASSERT_TRUE(err2.is_error());
    ASSERT_EQ(std::string("-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"), err2.serialize());

    // Call Set command on String
    RespValue err3 = CommandDispatcher::execute(make_cmd({"SADD", "str", "m"}), db);
    ASSERT_TRUE(err3.is_error());

    // Store a List
    CommandDispatcher::execute(make_cmd({"RPUSH", "lst", "elem"}), db);

    // Call String command on List
    RespValue err4 = CommandDispatcher::execute(make_cmd({"GET", "lst"}), db);
    ASSERT_TRUE(err4.is_error());
    ASSERT_EQ(std::string("-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"), err4.serialize());
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

    std::cout << "[List Commands]" << std::endl;
    RUN_TEST(test_list_commands);
    RUN_TEST(test_list_wrong_args);
    std::cout << std::endl;

    std::cout << "[Hash Commands]" << std::endl;
    RUN_TEST(test_hash_commands);
    RUN_TEST(test_hash_wrong_args);
    std::cout << std::endl;

    std::cout << "[Set Commands]" << std::endl;
    RUN_TEST(test_set_commands);
    RUN_TEST(test_set_wrong_args);
    std::cout << std::endl;

    std::cout << "[WRONGTYPE Errors]" << std::endl;
    RUN_TEST(test_wrongtype_errors);
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
