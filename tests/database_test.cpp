// database_test.cpp — Unit tests for Database class including TTL & Expiration (Milestone 6).

#include "database.h"

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

// ─── String Tests ────────────────────────────────────────────────────────────

void test_new_database_is_empty() {
    Database db;
    ASSERT_EQ(0u, db.size());
}

void test_set_and_get() {
    Database db;
    db.set("name", "redis");
    auto result = db.get("name");

    ASSERT_FALSE(result.wrong_type);
    ASSERT_TRUE(result.value.has_value());
    ASSERT_EQ(std::string("redis"), result.value.value());
    ASSERT_EQ(1u, db.size());
}

void test_set_overwrites_existing_key() {
    Database db;
    db.set("color", "red");
    db.set("color", "blue");

    auto result = db.get("color");
    ASSERT_TRUE(result.value.has_value());
    ASSERT_EQ(std::string("blue"), result.value.value());
    ASSERT_EQ(1u, db.size());
}

void test_get_nonexistent_key() {
    Database db;
    auto result = db.get("missing");

    ASSERT_FALSE(result.wrong_type);
    ASSERT_FALSE(result.value.has_value());
}

void test_set_multiple_keys() {
    Database db;
    db.set("a", "1");
    db.set("b", "2");
    db.set("c", "3");

    ASSERT_EQ(3u, db.size());
    ASSERT_EQ(std::string("1"), db.get("a").value.value());
    ASSERT_EQ(std::string("2"), db.get("b").value.value());
    ASSERT_EQ(std::string("3"), db.get("c").value.value());
}

void test_del_existing_key() {
    Database db;
    db.set("key", "value");

    std::size_t removed = db.del({"key"});
    ASSERT_EQ(1u, removed);
    ASSERT_FALSE(db.get("key").value.has_value());
    ASSERT_EQ(0u, db.size());
}

void test_del_nonexistent_key() {
    Database db;
    std::size_t removed = db.del({"ghost"});
    ASSERT_EQ(0u, removed);
}

void test_del_multiple_keys_mixed() {
    Database db;
    db.set("x", "1");
    db.set("y", "2");

    std::size_t removed = db.del({"x", "z", "y"});
    ASSERT_EQ(2u, removed);
    ASSERT_EQ(0u, db.size());
}

void test_del_duplicate_keys() {
    Database db;
    db.set("dup", "value");

    std::size_t removed = db.del({"dup", "dup"});
    ASSERT_EQ(1u, removed);
}

void test_del_empty_list() {
    Database db;
    db.set("key", "val");

    std::size_t removed = db.del({});
    ASSERT_EQ(0u, removed);
    ASSERT_EQ(1u, db.size());
}

void test_exists_key_present() {
    Database db;
    db.set("alive", "yes");

    std::size_t count = db.exists({"alive"});
    ASSERT_EQ(1u, count);
}

void test_exists_key_missing() {
    Database db;
    std::size_t count = db.exists({"nope"});
    ASSERT_EQ(0u, count);
}

void test_exists_multiple_keys() {
    Database db;
    db.set("a", "1");
    db.set("c", "3");

    std::size_t count = db.exists({"a", "b", "c"});
    ASSERT_EQ(2u, count);
}

void test_exists_duplicate_keys() {
    Database db;
    db.set("dup", "value");

    std::size_t count = db.exists({"dup", "dup", "dup"});
    ASSERT_EQ(3u, count);
}

void test_exists_empty_list() {
    Database db;
    db.set("key", "val");

    std::size_t count = db.exists({});
    ASSERT_EQ(0u, count);
}

void test_flushdb() {
    Database db;
    db.set("a", "1");
    db.set("b", "2");
    db.set("c", "3");
    ASSERT_EQ(3u, db.size());

    db.flushdb();
    ASSERT_EQ(0u, db.size());
    ASSERT_FALSE(db.get("a").value.has_value());
    ASSERT_FALSE(db.get("b").value.has_value());
    ASSERT_FALSE(db.get("c").value.has_value());
}

void test_flushdb_empty() {
    Database db;
    db.flushdb();
    ASSERT_EQ(0u, db.size());
}

void test_empty_string_key() {
    Database db;
    db.set("", "empty-key-value");

    auto result = db.get("");
    ASSERT_TRUE(result.value.has_value());
    ASSERT_EQ(std::string("empty-key-value"), result.value.value());
}

void test_empty_string_value() {
    Database db;
    db.set("key", "");

    auto result = db.get("key");
    ASSERT_TRUE(result.value.has_value());
    ASSERT_EQ(std::string(""), result.value.value());
}

void test_special_characters() {
    Database db;
    db.set("hello world", "value with spaces");
    db.set("key\nnewline", "tab\there");

    ASSERT_EQ(std::string("value with spaces"), db.get("hello world").value.value());
    ASSERT_EQ(std::string("tab\there"), db.get("key\nnewline").value.value());
}

void test_mixed_operations() {
    Database db;
    ASSERT_EQ(0u, db.size());

    db.set("x", "10");
    db.set("y", "20");
    db.set("z", "30");
    ASSERT_EQ(3u, db.size());

    db.set("y", "99");
    ASSERT_EQ(std::string("99"), db.get("y").value.value());
    ASSERT_EQ(3u, db.size());

    db.del({"z"});
    ASSERT_EQ(2u, db.size());
    ASSERT_FALSE(db.get("z").value.has_value());

    ASSERT_EQ(1u, db.exists({"x"}));
    ASSERT_EQ(0u, db.exists({"z"}));

    db.flushdb();
    ASSERT_EQ(0u, db.size());
    ASSERT_FALSE(db.get("x").value.has_value());
}

// ─── Lists ───────────────────────────────────────────────────────────────────

void test_list_lpush_rpush_llen() {
    Database db;
    auto res1 = db.lpush("mylist", {"a", "b"});
    ASSERT_FALSE(res1.wrong_type);
    ASSERT_EQ(2u, res1.value);

    auto res2 = db.rpush("mylist", {"c", "d"});
    ASSERT_FALSE(res2.wrong_type);
    ASSERT_EQ(4u, res2.value);

    auto len = db.llen("mylist");
    ASSERT_EQ(4u, len.value);
}

void test_list_lpop_rpop_and_cleanup() {
    Database db;
    db.rpush("mylist", {"1", "2", "3"});

    auto p1 = db.lpop("mylist");
    ASSERT_TRUE(p1.value.has_value());
    ASSERT_EQ(std::string("1"), p1.value.value());

    auto p2 = db.rpop("mylist");
    ASSERT_TRUE(p2.value.has_value());
    ASSERT_EQ(std::string("3"), p2.value.value());

    auto p3 = db.lpop("mylist");
    ASSERT_TRUE(p3.value.has_value());
    ASSERT_EQ(std::string("2"), p3.value.value());

    ASSERT_EQ(0u, db.size());
    ASSERT_EQ(0u, db.exists({"mylist"}));
}

void test_list_lrange() {
    Database db;
    db.rpush("mylist", {"zero", "one", "two", "three", "four"});

    auto r1 = db.lrange("mylist", 0, -1);
    ASSERT_EQ(5u, r1.value.size());
    ASSERT_EQ(std::string("zero"), r1.value[0]);
    ASSERT_EQ(std::string("four"), r1.value[4]);

    auto r2 = db.lrange("mylist", 1, 3);
    ASSERT_EQ(3u, r2.value.size());
    ASSERT_EQ(std::string("one"), r2.value[0]);
    ASSERT_EQ(std::string("three"), r2.value[2]);

    auto r3 = db.lrange("mylist", -3, -1);
    ASSERT_EQ(3u, r3.value.size());
    ASSERT_EQ(std::string("two"), r3.value[0]);
    ASSERT_EQ(std::string("four"), r3.value[2]);
}

// ─── Hashes ──────────────────────────────────────────────────────────────────

void test_hash_hset_hget_hexists() {
    Database db;
    auto res = db.hset("myhash", {{"field1", "val1"}, {"field2", "val2"}});
    ASSERT_FALSE(res.wrong_type);
    ASSERT_EQ(2u, res.value);

    auto res2 = db.hset("myhash", {{"field1", "newval1"}, {"field3", "val3"}});
    ASSERT_EQ(1u, res2.value);

    auto g1 = db.hget("myhash", "field1");
    ASSERT_TRUE(g1.value.has_value());
    ASSERT_EQ(std::string("newval1"), g1.value.value());

    ASSERT_TRUE(db.hexists("myhash", "field2").value);
    ASSERT_FALSE(db.hexists("myhash", "ghost").value);
}

void test_hash_hdel_and_hgetall() {
    Database db;
    db.hset("myhash", {{"a", "1"}, {"b", "2"}});

    auto all = db.hgetall("myhash");
    ASSERT_EQ(2u, all.value.size());

    db.hdel("myhash", {"a", "b"});
    ASSERT_EQ(0u, db.size());
    ASSERT_EQ(0u, db.exists({"myhash"}));
}

// ─── Sets ────────────────────────────────────────────────────────────────────

void test_set_sadd_srem_sismember_scard() {
    Database db;
    auto res = db.sadd("myset", {"a", "b", "c", "a"});
    ASSERT_FALSE(res.wrong_type);
    ASSERT_EQ(3u, res.value);

    ASSERT_EQ(3u, db.scard("myset").value);
    ASSERT_TRUE(db.sismember("myset", "b").value);
    ASSERT_FALSE(db.sismember("myset", "z").value);

    auto smem = db.smembers("myset");
    ASSERT_EQ(3u, smem.value.size());

    db.srem("myset", {"a", "b", "c"});
    ASSERT_EQ(0u, db.size());
}

void test_cross_type_operations() {
    Database db;

    db.set("str_key", "hello");
    ASSERT_TRUE(db.lpush("str_key", {"a"}).wrong_type);
    ASSERT_TRUE(db.hset("str_key", {{"f", "v"}}).wrong_type);
    ASSERT_TRUE(db.sadd("str_key", {"m"}).wrong_type);

    db.rpush("list_key", {"item1"});
    ASSERT_TRUE(db.get("list_key").wrong_type);
    ASSERT_TRUE(db.hset("list_key", {{"f", "v"}}).wrong_type);
    ASSERT_TRUE(db.sadd("list_key", {"m"}).wrong_type);

    ASSERT_EQ(2u, db.del({"str_key", "list_key"}));
}

// ─── Milestone 6: Expiration & TTL Tests ─────────────────────────────────────

void test_expire_and_ttl() {
    Database db;
    db.set("mykey", "val");

    // Key exists without TTL -> ttl = -1
    ASSERT_EQ(int64_t(-1), db.ttl("mykey"));
    ASSERT_EQ(int64_t(-1), db.pttl("mykey"));

    // Missing key -> ttl = -2
    ASSERT_EQ(int64_t(-2), db.ttl("missing"));
    ASSERT_EQ(int64_t(-2), db.pttl("missing"));

    // Set TTL = 100 seconds
    ASSERT_TRUE(db.expire("mykey", 100));
    ASSERT_TRUE(db.ttl("mykey") > 90);
    ASSERT_TRUE(db.pttl("mykey") > 90000);

    // Expire on missing key returns false
    ASSERT_FALSE(db.expire("ghost", 10));
}

void test_lazy_expiration() {
    Database db;
    // Set with 50ms TTL
    db.set("short_lived", "hello", 50);

    // Immediately accessible
    ASSERT_TRUE(db.get("short_lived").value.has_value());
    ASSERT_EQ(1u, db.exists({"short_lived"}));

    // Sleep 70ms to allow expiration
    std::this_thread::sleep_for(std::chrono::milliseconds(70));

    // Key should now be expired and lazily deleted
    ASSERT_FALSE(db.get("short_lived").value.has_value());
    ASSERT_EQ(0u, db.exists({"short_lived"}));
    ASSERT_EQ(int64_t(-2), db.ttl("short_lived"));
}

void test_expire_immediate_when_nonpositive() {
    Database db;
    db.set("temp", "val");
    ASSERT_EQ(1u, db.exists({"temp"}));

    // EXPIRE with seconds <= 0 immediately deletes key
    ASSERT_TRUE(db.expire("temp", 0));
    ASSERT_EQ(0u, db.exists({"temp"}));
}

void test_set_clears_previous_ttl() {
    Database db;
    db.set("k", "v", 50000); // 50s TTL
    ASSERT_TRUE(db.ttl("k") > 0);

    // Plain SET without TTL clears the expiration!
    db.set("k", "v2");
    ASSERT_EQ(int64_t(-1), db.ttl("k"));
}

void test_purge_expired() {
    Database db;
    db.set("k1", "v1", 30);  // 30ms TTL
    db.set("k2", "v2", 30);  // 30ms TTL
    db.set("k3", "v3");      // persistent

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Active purge
    std::size_t purged = db.purge_expired();
    ASSERT_EQ(2u, purged);
    ASSERT_EQ(1u, db.size());
    ASSERT_TRUE(db.get("k3").value.has_value());
}

// ─── Test Runner ─────────────────────────────────────────────────────────────

int main() {
    std::cout << std::endl;
    std::cout << "=== mini-redis Database Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "[Basic String Operations]" << std::endl;
    RUN_TEST(test_new_database_is_empty);
    RUN_TEST(test_set_and_get);
    RUN_TEST(test_set_overwrites_existing_key);
    RUN_TEST(test_get_nonexistent_key);
    RUN_TEST(test_set_multiple_keys);
    std::cout << std::endl;

    std::cout << "[Generic DEL / EXISTS / FLUSHDB]" << std::endl;
    RUN_TEST(test_del_existing_key);
    RUN_TEST(test_del_nonexistent_key);
    RUN_TEST(test_del_multiple_keys_mixed);
    RUN_TEST(test_del_duplicate_keys);
    RUN_TEST(test_del_empty_list);
    RUN_TEST(test_exists_key_present);
    RUN_TEST(test_exists_key_missing);
    RUN_TEST(test_exists_multiple_keys);
    RUN_TEST(test_exists_duplicate_keys);
    RUN_TEST(test_exists_empty_list);
    RUN_TEST(test_flushdb);
    RUN_TEST(test_flushdb_empty);
    std::cout << std::endl;

    std::cout << "[Edge Cases]" << std::endl;
    RUN_TEST(test_empty_string_key);
    RUN_TEST(test_empty_string_value);
    RUN_TEST(test_special_characters);
    RUN_TEST(test_mixed_operations);
    std::cout << std::endl;

    std::cout << "[Lists]" << std::endl;
    RUN_TEST(test_list_lpush_rpush_llen);
    RUN_TEST(test_list_lpop_rpop_and_cleanup);
    RUN_TEST(test_list_lrange);
    std::cout << std::endl;

    std::cout << "[Hashes]" << std::endl;
    RUN_TEST(test_hash_hset_hget_hexists);
    RUN_TEST(test_hash_hdel_and_hgetall);
    std::cout << std::endl;

    std::cout << "[Sets]" << std::endl;
    RUN_TEST(test_set_sadd_srem_sismember_scard);
    std::cout << std::endl;

    std::cout << "[Cross-Type & WRONGTYPE]" << std::endl;
    RUN_TEST(test_cross_type_operations);
    std::cout << std::endl;

    std::cout << "[Expiration & TTL]" << std::endl;
    RUN_TEST(test_expire_and_ttl);
    RUN_TEST(test_lazy_expiration);
    RUN_TEST(test_expire_immediate_when_nonpositive);
    RUN_TEST(test_set_clears_previous_ttl);
    RUN_TEST(test_purge_expired);
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

    return (tests_failed == 0) ? 0 : 1;
}
