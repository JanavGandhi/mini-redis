// database.h — In-memory key-value database engine supporting multiple data structures.
//
// Milestone 5:
// Redis is not just a string key-value store — it supports rich data structures!
// We extend the Database engine using C++17 std::variant to store:
//   1. Strings (std::string)
//   2. Lists   (std::deque<std::string>) — O(1) push/pop at head and tail
//   3. Hashes  (std::unordered_map<std::string, std::string>) — field-value pairs
//   4. Sets    (std::unordered_set<std::string>) — unique unordered members
//
// If an operation is attempted against a key holding a different data type,
// Redis returns a WRONGTYPE error.

#ifndef MINI_REDIS_DATABASE_H
#define MINI_REDIS_DATABASE_H

#include <cstddef>        // std::size_t
#include <cstdint>        // int64_t
#include <deque>          // std::deque (for Lists)
#include <optional>       // std::optional
#include <ostream>        // std::ostream (for operator<< printing)
#include <string>         // std::string
#include <unordered_map>  // std::unordered_map (for store & Hashes)
#include <unordered_set>  // std::unordered_set (for Sets)
#include <utility>        // std::pair
#include <variant>        // std::variant (C++17 type-safe union)
#include <vector>         // std::vector

// ─── Value Types in Database ────────────────────────────────────────────────

enum class DataType {
    None,    // Key does not exist
    String,
    List,
    Hash,
    Set
};

inline std::ostream& operator<<(std::ostream& os, DataType type) {
    switch (type) {
        case DataType::None:   return os << "DataType::None";
        case DataType::String: return os << "DataType::String";
        case DataType::List:   return os << "DataType::List";
        case DataType::Hash:   return os << "DataType::Hash";
        case DataType::Set:    return os << "DataType::Set";
    }
    return os << "Unknown";
}

// ─── Result wrapper for Database operations ─────────────────────────────────

template <typename T>
struct DbResult {
    T value{};
    bool wrong_type = false;

    static DbResult success(T val) {
        DbResult r;
        r.value = std::move(val);
        r.wrong_type = false;
        return r;
    }

    static DbResult wrong_type_err() {
        DbResult r;
        r.wrong_type = true;
        return r;
    }
};

// ─── Database Class ─────────────────────────────────────────────────────────

class Database {
public:
    Database() = default;

    // ── Generic Key Operations ──────────────────────────────────────────────
    // Remove one or more keys regardless of their data type.
    std::size_t del(const std::vector<std::string>& keys);

    // Count how many of the specified keys exist.
    std::size_t exists(const std::vector<std::string>& keys) const;

    // Clear all keys from the database.
    void flushdb();

    // Total number of keys currently stored.
    std::size_t size() const;

    // Query data type of a key.
    DataType type(const std::string& key) const;

    // ── String Operations ───────────────────────────────────────────────────
    // SET: stores or overwrites key with a String value (replaces other types).
    void set(const std::string& key, const std::string& value);

    // GET: returns string value, nullopt if missing, or wrong_type error.
    DbResult<std::optional<std::string>> get(const std::string& key) const;

    // ── List Operations (std::deque) ────────────────────────────────────────
    // LPUSH: inserts values at the head (left) of the list. Returns new length.
    DbResult<std::size_t> lpush(const std::string& key, const std::vector<std::string>& values);

    // RPUSH: inserts values at the tail (right) of the list. Returns new length.
    DbResult<std::size_t> rpush(const std::string& key, const std::vector<std::string>& values);

    // LPOP: removes and returns the first element of the list.
    DbResult<std::optional<std::string>> lpop(const std::string& key);

    // RPOP: removes and returns the last element of the list.
    DbResult<std::optional<std::string>> rpop(const std::string& key);

    // LLEN: returns the length of the list (0 if key does not exist).
    DbResult<std::size_t> llen(const std::string& key) const;

    // LRANGE: returns range of elements [start, stop] with negative index support.
    DbResult<std::vector<std::string>> lrange(const std::string& key, int64_t start, int64_t stop) const;

    // ── Hash Operations (std::unordered_map) ────────────────────────────────
    // HSET: sets field-value pairs in hash. Returns number of new fields added.
    DbResult<std::size_t> hset(const std::string& key, const std::vector<std::pair<std::string, std::string>>& field_values);

    // HGET: returns value for field, or nullopt if key or field does not exist.
    DbResult<std::optional<std::string>> hget(const std::string& key, const std::string& field) const;

    // HDEL: deletes fields from hash. Returns number of fields removed.
    DbResult<std::size_t> hdel(const std::string& key, const std::vector<std::string>& fields);

    // HEXISTS: checks if field exists in hash.
    DbResult<bool> hexists(const std::string& key, const std::string& field) const;

    // HGETALL: returns all field-value pairs in the hash.
    DbResult<std::vector<std::pair<std::string, std::string>>> hgetall(const std::string& key) const;

    // ── Set Operations (std::unordered_set) ─────────────────────────────────
    // SADD: adds unique members to set. Returns number of new members added.
    DbResult<std::size_t> sadd(const std::string& key, const std::vector<std::string>& members);

    // SREM: removes members from set. Returns number of members removed.
    DbResult<std::size_t> srem(const std::string& key, const std::vector<std::string>& members);

    // SISMEMBER: returns true if member exists in set.
    DbResult<bool> sismember(const std::string& key, const std::string& member) const;

    // SMEMBERS: returns all members of the set.
    DbResult<std::vector<std::string>> smembers(const std::string& key) const;

    // SCARD: returns cardinality (size) of the set.
    DbResult<std::size_t> scard(const std::string& key) const;

private:
    // C++17 std::variant holding one of the four Redis data types:
    using ValueVariant = std::variant<
        std::string,                                    // String
        std::deque<std::string>,                        // List
        std::unordered_map<std::string, std::string>,   // Hash
        std::unordered_set<std::string>                 // Set
    >;

    std::unordered_map<std::string, ValueVariant> store_;
};

#endif // MINI_REDIS_DATABASE_H
