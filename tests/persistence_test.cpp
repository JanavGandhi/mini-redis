// persistence_test.cpp — Unit tests for Snapshot (RDB) and AOF persistence (Milestone 7).

#include "command.h"
#include "database.h"
#include "persistence.h"
#include "resp.h"

#include <chrono>
#include <cstdio>
#include <fstream>
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

// ─── Snapshot Tests ──────────────────────────────────────────────────────────

void test_snapshot_save_and_load_all_types() {
    const std::string rdb_file = "test_dump.rdb";
    std::remove(rdb_file.c_str());

    Database db;
    // 1. String with TTL
    db.set("str_key", "hello world", 100000); // 100s TTL

    // 2. List
    db.rpush("list_key", {"alpha", "beta", "gamma"});

    // 3. Hash
    db.hset("hash_key", {{"field1", "val1"}, {"field2", "val2"}});

    // 4. Set
    db.sadd("set_key", {"m1", "m2", "m3"});

    // Save snapshot
    ASSERT_TRUE(SnapshotManager::save(rdb_file, db));

    // Load into a fresh database
    Database loaded_db;
    ASSERT_TRUE(SnapshotManager::load(rdb_file, loaded_db));

    // Verify String & TTL
    auto str_res = loaded_db.get("str_key");
    ASSERT_TRUE(str_res.value.has_value());
    ASSERT_EQ(std::string("hello world"), str_res.value.value());
    ASSERT_TRUE(loaded_db.ttl("str_key") > 50);

    // Verify List
    auto list_res = loaded_db.lrange("list_key", 0, -1);
    ASSERT_EQ(3u, list_res.value.size());
    ASSERT_EQ(std::string("alpha"), list_res.value[0]);
    ASSERT_EQ(std::string("gamma"), list_res.value[2]);

    // Verify Hash
    auto h1 = loaded_db.hget("hash_key", "field1");
    ASSERT_TRUE(h1.value.has_value());
    ASSERT_EQ(std::string("val1"), h1.value.value());
    ASSERT_TRUE(loaded_db.hexists("hash_key", "field2").value);

    // Verify Set
    ASSERT_EQ(3u, loaded_db.scard("set_key").value);
    ASSERT_TRUE(loaded_db.sismember("set_key", "m2").value);

    std::remove(rdb_file.c_str());
}

void test_snapshot_binary_safety() {
    const std::string rdb_file = "test_binary.rdb";
    std::remove(rdb_file.c_str());

    Database db;
    // String with embedded null bytes and CRLF
    std::string bin_key = "key\0null";
    std::string bin_val = "val\r\n\0embedded";
    db.set(bin_key, bin_val);

    ASSERT_TRUE(SnapshotManager::save(rdb_file, db));

    Database loaded_db;
    ASSERT_TRUE(SnapshotManager::load(rdb_file, loaded_db));

    auto res = loaded_db.get(bin_key);
    ASSERT_TRUE(res.value.has_value());
    ASSERT_EQ(bin_val, res.value.value());

    std::remove(rdb_file.c_str());
}

void test_snapshot_invalid_file() {
    const std::string corrupt_file = "corrupt.rdb";
    {
        std::ofstream out(corrupt_file, std::ios::binary);
        out << "INVALID_DATA_NOT_REDIS";
    }

    Database db;
    ASSERT_FALSE(SnapshotManager::load(corrupt_file, db));

    std::remove(corrupt_file.c_str());
}

// ─── AOF & Crash Recovery Tests ──────────────────────────────────────────────

void test_aof_append_and_replay_recovery() {
    const std::string aof_file = "test_appendonly.aof";
    std::remove(aof_file.c_str());

    // 1. Log commands using AofManager
    {
        AofManager aof;
        ASSERT_TRUE(aof.open(aof_file));

        Database db;
        // Run commands and log them
        auto c1 = make_cmd({"SET", "k1", "v1"});
        CommandDispatcher::execute(c1, db);
        aof.append(c1);

        auto c2 = make_cmd({"RPUSH", "mylist", "10", "20"});
        CommandDispatcher::execute(c2, db);
        aof.append(c2);

        auto c3 = make_cmd({"HSET", "myhash", "f1", "val1"});
        CommandDispatcher::execute(c3, db);
        aof.append(c3);

        auto c4 = make_cmd({"SADD", "myset", "apple", "banana"});
        CommandDispatcher::execute(c4, db);
        aof.append(c4);

        // DEL k1
        auto c5 = make_cmd({"DEL", "k1"});
        CommandDispatcher::execute(c5, db);
        aof.append(c5);

        aof.flush();
    }

    // 2. Crash recovery: replay onto a brand new Database
    Database recovered_db;
    ASSERT_TRUE(AofManager::replay(aof_file, recovered_db));

    // Verify state
    ASSERT_FALSE(recovered_db.get("k1").value.has_value()); // Was deleted
    ASSERT_EQ(2u, recovered_db.llen("mylist").value);
    ASSERT_EQ(std::string("val1"), recovered_db.hget("myhash", "f1").value.value());
    ASSERT_EQ(2u, recovered_db.scard("myset").value);

    std::remove(aof_file.c_str());
}

void test_aof_rewrite_compaction() {
    const std::string aof_file = "test_compact.aof";
    std::remove(aof_file.c_str());

    Database db;
    // Mutate state with repetitive operations
    db.set("counter", "1");
    db.set("counter", "2");
    db.set("counter", "99"); // Final value 99

    db.rpush("items", {"a", "b", "c"});
    db.lpop("items"); // Leaves b, c

    db.hset("settings", {{"theme", "dark"}, {"font", "mono"}});

    // Rewrite compacted state
    ASSERT_TRUE(AofManager::rewrite(aof_file, db));

    // Replay on fresh DB
    Database restored_db;
    ASSERT_TRUE(AofManager::replay(aof_file, restored_db));

    ASSERT_EQ(std::string("99"), restored_db.get("counter").value.value());

    auto list_res = restored_db.lrange("items", 0, -1);
    ASSERT_EQ(2u, list_res.value.size());
    ASSERT_EQ(std::string("b"), list_res.value[0]);
    ASSERT_EQ(std::string("c"), list_res.value[1]);

    ASSERT_EQ(std::string("dark"), restored_db.hget("settings", "theme").value.value());

    std::remove(aof_file.c_str());
}

void test_save_and_lastsave_commands() {
    Database db;
    db.set("foo", "bar");

    RespValue save_res = CommandDispatcher::execute(make_cmd({"SAVE"}), db);
    ASSERT_TRUE(save_res.is_simple_string());
    ASSERT_EQ(std::string("OK"), save_res.string_value());

    RespValue lastsave_res = CommandDispatcher::execute(make_cmd({"LASTSAVE"}), db);
    ASSERT_TRUE(lastsave_res.is_integer());
    ASSERT_TRUE(lastsave_res.integer_value() > 0);

    RespValue bgsave_res = CommandDispatcher::execute(make_cmd({"BGSAVE"}), db);
    ASSERT_TRUE(bgsave_res.is_simple_string());
    ASSERT_EQ(std::string("Background saving started"), bgsave_res.string_value());

    // Give background save thread a moment to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::remove("dump.rdb");
}

// ─── Test Runner ─────────────────────────────────────────────────────────────

int main() {
    std::cout << std::endl;
    std::cout << "=== mini-redis Persistence Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "[Snapshot (RDB)]" << std::endl;
    RUN_TEST(test_snapshot_save_and_load_all_types);
    RUN_TEST(test_snapshot_binary_safety);
    RUN_TEST(test_snapshot_invalid_file);
    std::cout << std::endl;

    std::cout << "[Append-Only Log (AOF) & Recovery]" << std::endl;
    RUN_TEST(test_aof_append_and_replay_recovery);
    RUN_TEST(test_aof_rewrite_compaction);
    std::cout << std::endl;

    std::cout << "[Commands (SAVE, BGSAVE, LASTSAVE)]" << std::endl;
    RUN_TEST(test_save_and_lastsave_commands);
    std::cout << std::endl;

    // --- Summary ---
    std::cout << "====================================" << std::endl;
    std::cout << "Total: " << tests_run
              << "  Passed: " << tests_passed
              << "  Failed: " << tests_failed << std::endl;

    if (tests_failed == 0) {
        std::cout << "All tests PASSED!" << std::endl;
    } else {
        std::cerr << "Some tests FAILED." << std::endl;
    }

    std::cout << "====================================" << std::endl;
    std::cout << std::endl;

    return (tests_failed == 0) ? 0 : 1;
}
