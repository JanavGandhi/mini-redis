// database.h — Interface for the in-memory key-value store.
//
// This is the core storage engine of mini-redis. It wraps a standard
// hash map (std::unordered_map) to provide Redis-like operations:
// SET, GET, DEL, EXISTS, and FLUSHDB.
//
// Design notes:
// - Keys and values are both std::string (Redis treats everything as bytes).
// - GET returns std::optional — a C++17 type that is either "has a value"
//   or "empty" (std::nullopt). This avoids returning magic values like ""
//   to signal "key not found."
// - DEL and EXISTS accept a vector of keys, matching Redis behavior where
//   you can pass multiple keys in one command (e.g., DEL key1 key2 key3).

#ifndef MINI_REDIS_DATABASE_H
#define MINI_REDIS_DATABASE_H

#include <cstddef>       // std::size_t — an unsigned type for sizes/counts.
#include <optional>      // std::optional — holds a value OR nothing.
#include <string>        // std::string — the type for keys and values.
#include <unordered_map> // std::unordered_map — hash table with O(1) avg lookup.
#include <vector>        // std::vector — dynamic array for multi-key operations.

class Database {
public:
    // SET key value
    // Stores the key-value pair. If the key already exists, its value
    // is overwritten. This always succeeds — there is no failure case.
    void set(const std::string& key, const std::string& value);

    // GET key
    // Returns the value associated with the key, wrapped in std::optional.
    // If the key does not exist, returns std::nullopt.
    // The method is 'const' because reading data does not modify the store.
    std::optional<std::string> get(const std::string& key) const;

    // DEL key [key ...]
    // Removes the specified keys from the store. Keys that do not exist
    // are silently ignored. Returns the number of keys that were actually
    // removed (not the number of keys requested).
    // Example: if only 2 of 3 requested keys exist, returns 2.
    std::size_t del(const std::vector<std::string>& keys);

    // EXISTS key [key ...]
    // Counts how many of the specified keys exist in the store.
    // Note: if the same key appears multiple times in the input,
    // it is counted multiple times (matching real Redis behavior).
    // Example: EXISTS mykey mykey → returns 2 if mykey exists.
    std::size_t exists(const std::vector<std::string>& keys) const;

    // FLUSHDB
    // Removes ALL keys from the store. After this call, size() returns 0.
    void flushdb();

    // SIZE (helper, not a Redis command)
    // Returns the number of key-value pairs currently stored.
    // Useful for testing and debugging.
    std::size_t size() const;

private:
    // The actual data store: a hash map from string keys to string values.
    // std::unordered_map gives us:
    //   - O(1) average time for insert, lookup, and delete.
    //   - Automatic handling of hash collisions.
    //   - Simple API: operator[], find(), erase(), clear(), size().
    std::unordered_map<std::string, std::string> store_;
};

#endif // MINI_REDIS_DATABASE_H
