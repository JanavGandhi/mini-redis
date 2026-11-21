// database_test.cpp — Unit tests for the Database class.
//
// This is a simple, hand-written test runner with no external dependencies.
// Each test function exercises one aspect of the Database. If a check fails,
// we report the failure and continue running remaining tests.
//
// The program exits with code 0 if all tests pass, or 1 if any test fails.
// This makes it easy to integrate with build systems and CI pipelines.

#include "database.h"

#include <iostream>  // std::cout, std::cerr
#include <string>    // std::string
#include <vector>    // std::vector

// ─── Minimal Test Framework ──────────────────────────────────────────────────
//
// We track pass/fail counts globally. The ASSERT_* macros check a condition
// and print a clear message on failure, including the file and line number.

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Track whether the current test has failed (used by ASSERT macros).
static bool current_test_failed = false;

// ASSERT_TRUE(condition):
//   If the condition is false, print the failing expression and location.
#define ASSERT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::cerr << "    FAIL: " << #cond                                \
                      << "  (" << __FILE__ << ":" << __LINE__ << ")"          \
                      << std::endl;                                           \
            current_test_failed = true;                                       \
        }                                                                     \
    } while (false)

// ASSERT_FALSE(condition): Opposite of ASSERT_TRUE.
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

// ASSERT_EQ(expected, actual):
//   If the two values are not equal, print both values for easy debugging.
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

// RUN_TEST(function_name):
//   Runs a test function, tracks pass/fail, and prints the result.
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

// ─── Test Cases ──────────────────────────────────────────────────────────────

// Test: A freshly created database should be empty.
void test_new_database_is_empty() {
    Database db;
    ASSERT_EQ(0u, db.size());
}

// Test: SET stores a value that GET can retrieve.
void test_set_and_get() {
    Database db;
    db.set("name", "redis");
    auto result = db.get("name");

    ASSERT_TRUE(result.has_value());   // Key should exist.
    ASSERT_EQ(std::string("redis"), result.value());  // Value should match.
    ASSERT_EQ(1u, db.size());          // Should have exactly one key.
}

// Test: SET on an existing key overwrites the previous value.
void test_set_overwrites_existing_key() {
    Database db;
    db.set("color", "red");
    db.set("color", "blue");  // Overwrite.

    auto result = db.get("color");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(std::string("blue"), result.value());  // Should be the new value.
    ASSERT_EQ(1u, db.size());          // Still only one key.
}

// Test: GET on a non-existent key returns nullopt.
void test_get_nonexistent_key() {
    Database db;
    auto result = db.get("missing");

    ASSERT_FALSE(result.has_value());  // Should be empty (nullopt).
}

// Test: Multiple key-value pairs can coexist.
void test_set_multiple_keys() {
    Database db;
    db.set("a", "1");
    db.set("b", "2");
    db.set("c", "3");

    ASSERT_EQ(3u, db.size());
    ASSERT_EQ(std::string("1"), db.get("a").value());
    ASSERT_EQ(std::string("2"), db.get("b").value());
    ASSERT_EQ(std::string("3"), db.get("c").value());
}

// Test: DEL removes an existing key and returns 1.
void test_del_existing_key() {
    Database db;
    db.set("key", "value");

    std::size_t removed = db.del({"key"});
    ASSERT_EQ(1u, removed);
    ASSERT_FALSE(db.get("key").has_value());  // Key should be gone.
    ASSERT_EQ(0u, db.size());
}

// Test: DEL on a non-existent key returns 0.
void test_del_nonexistent_key() {
    Database db;
    std::size_t removed = db.del({"ghost"});
    ASSERT_EQ(0u, removed);
}

// Test: DEL with multiple keys, some existing and some not.
void test_del_multiple_keys_mixed() {
    Database db;
    db.set("x", "1");
    db.set("y", "2");
    // "z" is not set.

    std::size_t removed = db.del({"x", "z", "y"});
    ASSERT_EQ(2u, removed);  // Only x and y existed.
    ASSERT_EQ(0u, db.size());
}

// Test: DEL with duplicate keys in the input only counts once.
void test_del_duplicate_keys() {
    Database db;
    db.set("dup", "value");

    // "dup" appears twice, but should only count as 1 deletion.
    std::size_t removed = db.del({"dup", "dup"});
    ASSERT_EQ(1u, removed);
}

// Test: EXISTS returns 1 for an existing key.
void test_exists_key_present() {
    Database db;
    db.set("alive", "yes");

    std::size_t count = db.exists({"alive"});
    ASSERT_EQ(1u, count);
}

// Test: EXISTS returns 0 for a missing key.
void test_exists_key_missing() {
    Database db;
    std::size_t count = db.exists({"nope"});
    ASSERT_EQ(0u, count);
}

// Test: EXISTS with multiple keys counts each one independently.
void test_exists_multiple_keys() {
    Database db;
    db.set("a", "1");
    db.set("c", "3");
    // "b" is not set.

    std::size_t count = db.exists({"a", "b", "c"});
    ASSERT_EQ(2u, count);  // Only a and c exist.
}

// Test: EXISTS counts duplicate keys multiple times (Redis behavior).
// In real Redis: EXISTS mykey mykey → 2 (if mykey exists).
void test_exists_duplicate_keys() {
    Database db;
    db.set("dup", "value");

    std::size_t count = db.exists({"dup", "dup", "dup"});
    ASSERT_EQ(3u, count);  // Counted once per occurrence.
}

// Test: FLUSHDB clears all data.
void test_flushdb() {
    Database db;
    db.set("a", "1");
    db.set("b", "2");
    db.set("c", "3");
    ASSERT_EQ(3u, db.size());

    db.flushdb();
    ASSERT_EQ(0u, db.size());
    ASSERT_FALSE(db.get("a").has_value());
    ASSERT_FALSE(db.get("b").has_value());
    ASSERT_FALSE(db.get("c").has_value());
}

// Test: FLUSHDB on an already empty database is a no-op.
void test_flushdb_empty() {
    Database db;
    db.flushdb();  // Should not crash or misbehave.
    ASSERT_EQ(0u, db.size());
}

// Test: Empty string is a valid key.
void test_empty_string_key() {
    Database db;
    db.set("", "empty-key-value");

    auto result = db.get("");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(std::string("empty-key-value"), result.value());
}

// Test: Empty string is a valid value.
void test_empty_string_value() {
    Database db;
    db.set("key", "");

    auto result = db.get("key");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(std::string(""), result.value());  // Value is "", not nullopt.
}

// Test: Keys with spaces and special characters work correctly.
void test_special_characters() {
    Database db;
    db.set("hello world", "value with spaces");
    db.set("key\nnewline", "tab\there");

    ASSERT_EQ(std::string("value with spaces"), db.get("hello world").value());
    ASSERT_EQ(std::string("tab\there"), db.get("key\nnewline").value());
}

// Test: Operations work correctly in sequence (integration-style).
void test_mixed_operations() {
    Database db;

    // Start empty.
    ASSERT_EQ(0u, db.size());

    // Add some keys.
    db.set("x", "10");
    db.set("y", "20");
    db.set("z", "30");
    ASSERT_EQ(3u, db.size());

    // Overwrite one.
    db.set("y", "99");
    ASSERT_EQ(std::string("99"), db.get("y").value());
    ASSERT_EQ(3u, db.size());  // Size unchanged.

    // Delete one.
    db.del({"z"});
    ASSERT_EQ(2u, db.size());
    ASSERT_FALSE(db.get("z").has_value());

    // Check existence.
    ASSERT_EQ(1u, db.exists({"x"}));
    ASSERT_EQ(0u, db.exists({"z"}));

    // Flush and verify.
    db.flushdb();
    ASSERT_EQ(0u, db.size());
    ASSERT_FALSE(db.get("x").has_value());
}

// Test: DEL with an empty key list returns 0.
void test_del_empty_list() {
    Database db;
    db.set("key", "val");

    std::size_t removed = db.del({});
    ASSERT_EQ(0u, removed);
    ASSERT_EQ(1u, db.size());  // Nothing was removed.
}

// Test: EXISTS with an empty key list returns 0.
void test_exists_empty_list() {
    Database db;
    db.set("key", "val");

    std::size_t count = db.exists({});
    ASSERT_EQ(0u, count);
}

// ─── Test Runner ─────────────────────────────────────────────────────────────

int main() {
    std::cout << std::endl;
    std::cout << "=== mini-redis Database Tests ===" << std::endl;
    std::cout << std::endl;

    // --- Basic Operations ---
    std::cout << "[Basic Operations]" << std::endl;
    RUN_TEST(test_new_database_is_empty);
    RUN_TEST(test_set_and_get);
    RUN_TEST(test_set_overwrites_existing_key);
    RUN_TEST(test_get_nonexistent_key);
    RUN_TEST(test_set_multiple_keys);
    std::cout << std::endl;

    // --- DEL Command ---
    std::cout << "[DEL Command]" << std::endl;
    RUN_TEST(test_del_existing_key);
    RUN_TEST(test_del_nonexistent_key);
    RUN_TEST(test_del_multiple_keys_mixed);
    RUN_TEST(test_del_duplicate_keys);
    RUN_TEST(test_del_empty_list);
    std::cout << std::endl;

    // --- EXISTS Command ---
    std::cout << "[EXISTS Command]" << std::endl;
    RUN_TEST(test_exists_key_present);
    RUN_TEST(test_exists_key_missing);
    RUN_TEST(test_exists_multiple_keys);
    RUN_TEST(test_exists_duplicate_keys);
    RUN_TEST(test_exists_empty_list);
    std::cout << std::endl;

    // --- FLUSHDB Command ---
    std::cout << "[FLUSHDB Command]" << std::endl;
    RUN_TEST(test_flushdb);
    RUN_TEST(test_flushdb_empty);
    std::cout << std::endl;

    // --- Edge Cases ---
    std::cout << "[Edge Cases]" << std::endl;
    RUN_TEST(test_empty_string_key);
    RUN_TEST(test_empty_string_value);
    RUN_TEST(test_special_characters);
    RUN_TEST(test_mixed_operations);
    std::cout << std::endl;

    // --- Summary ---
    std::cout << "=================================" << std::endl;
    std::cout << "Total: " << tests_run
              << "  Passed: " << tests_passed
              << "  Failed: " << tests_failed << std::endl;

    if (tests_failed == 0) {
        std::cout << "All tests PASSED!" << std::endl;
    } else {
        std::cerr << "Some tests FAILED." << std::endl;
    }

    std::cout << "=================================" << std::endl;
    std::cout << std::endl;

    // Return 0 (success) if all tests passed, 1 (failure) otherwise.
    return (tests_failed == 0) ? 0 : 1;
}
