// database.h — In-memory multi-type database engine with TTL and key expiration.
//
// Milestone 6:
// Added support for key expiration:
//   - EXPIRE (seconds), PEXPIRE (milliseconds)
//   - TTL (remaining seconds), PTTL (remaining milliseconds)
//   - SET with EX / PX options
//   - Lazy expiration on access + active purge_expired()

#ifndef MINI_REDIS_DATABASE_H
#define MINI_REDIS_DATABASE_H

#include <chrono>         // std::chrono (for time points & durations)
#include <cstddef>        // std::size_t
#include <cstdint>        // int64_t
#include <deque>          // std::deque (for Lists)
#include <optional>       // std::optional
#include <ostream>        // std::ostream (for operator<< printing)
#include <string>         // std::string
#include <unordered_map>  // std::unordered_map
#include <unordered_set>  // std::unordered_set
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
    using TimePoint = std::chrono::steady_clock::time_point;

    Database() = default;

    // ── Generic Key Operations ──────────────────────────────────────────────
    std::size_t del(const std::vector<std::string>& keys);
    std::size_t exists(const std::vector<std::string>& keys) const;
    void flushdb();
    std::size_t size() const;
    DataType type(const std::string& key) const;

    // ── Expiration & TTL Operations (Milestone 6) ───────────────────────────
    // Set a timeout on key in seconds. Returns true if timeout was set,
    // false if key does not exist. If seconds <= 0, key is immediately expired (deleted).
    bool expire(const std::string& key, int64_t seconds);

    // Set a timeout on key in milliseconds. Returns true if timeout was set, false otherwise.
    bool pexpire(const std::string& key, int64_t milliseconds);

    // Returns remaining time to live in seconds:
    //  -2 if key does not exist (or expired)
    //  -1 if key exists with no associated TTL
    //  >= 0 remaining TTL in seconds
    int64_t ttl(const std::string& key) const;

    // Returns remaining time to live in milliseconds:
    //  -2 if key does not exist (or expired)
    //  -1 if key exists with no associated TTL
    //  >= 0 remaining TTL in milliseconds
    int64_t pttl(const std::string& key) const;

    // Active cleanup routine: sweeps expired keys and removes them.
    // Returns count of expired keys purged.
    std::size_t purge_expired();

    // ── String Operations ───────────────────────────────────────────────────
    // SET: stores value. If ttl_ms > 0, sets expiration; if ttl_ms == 0, clears any prior expiration.
    void set(const std::string& key, const std::string& value, int64_t ttl_ms = 0);
    DbResult<std::optional<std::string>> get(const std::string& key) const;

    // ── List Operations ─────────────────────────────────────────────────────
    DbResult<std::size_t> lpush(const std::string& key, const std::vector<std::string>& values);
    DbResult<std::size_t> rpush(const std::string& key, const std::vector<std::string>& values);
    DbResult<std::optional<std::string>> lpop(const std::string& key);
    DbResult<std::optional<std::string>> rpop(const std::string& key);
    DbResult<std::size_t> llen(const std::string& key) const;
    DbResult<std::vector<std::string>> lrange(const std::string& key, int64_t start, int64_t stop) const;

    // ── Hash Operations ─────────────────────────────────────────────────────
    DbResult<std::size_t> hset(const std::string& key, const std::vector<std::pair<std::string, std::string>>& field_values);
    DbResult<std::optional<std::string>> hget(const std::string& key, const std::string& field) const;
    DbResult<std::size_t> hdel(const std::string& key, const std::vector<std::string>& fields);
    DbResult<bool> hexists(const std::string& key, const std::string& field) const;
    DbResult<std::vector<std::pair<std::string, std::string>>> hgetall(const std::string& key) const;

    // ── Set Operations ──────────────────────────────────────────────────────
    DbResult<std::size_t> sadd(const std::string& key, const std::vector<std::string>& members);
    DbResult<std::size_t> srem(const std::string& key, const std::vector<std::string>& members);
    DbResult<bool> sismember(const std::string& key, const std::string& member) const;
    DbResult<std::vector<std::string>> smembers(const std::string& key) const;
    DbResult<std::size_t> scard(const std::string& key) const;

private:
    using ValueVariant = std::variant<
        std::string,                                    // String
        std::deque<std::string>,                        // List
        std::unordered_map<std::string, std::string>,   // Hash
        std::unordered_set<std::string>                 // Set
    >;

    // Lazy expiration helper: checks if key is expired. If expired, deletes it
    // and returns true. If not expired or no expiration, returns false.
    // Mutable allows lazy deletion inside const methods (e.g. get, exists).
    bool check_and_expire(const std::string& key) const;

    mutable std::unordered_map<std::string, ValueVariant> store_;
    mutable std::unordered_map<std::string, TimePoint> expires_;
};

#endif // MINI_REDIS_DATABASE_H
