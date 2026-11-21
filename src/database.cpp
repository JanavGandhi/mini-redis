// database.cpp — Implementation of the in-memory key-value store.
//
// Each method maps directly to a Redis command. The implementation
// is straightforward because std::unordered_map already provides
// the core operations we need.

#include "database.h"

// SET key value
// insert_or_assign does exactly what we need:
// - If the key doesn't exist, it inserts a new key-value pair.
// - If the key already exists, it overwrites the existing value.
// We use insert_or_assign instead of operator[] because it works
// cleanly with const references and makes the intent explicit.
void Database::set(const std::string& key, const std::string& value) {
    store_.insert_or_assign(key, value);
}

// GET key
// We use find() instead of operator[] because:
// - operator[] would INSERT a default value if the key is missing,
//   which silently modifies the store — a dangerous side effect.
// - find() returns an iterator: if it equals end(), the key wasn't found.
//   Otherwise, it->second gives us the value.
std::optional<std::string> Database::get(const std::string& key) const {
    auto it = store_.find(key);
    if (it == store_.end()) {
        return std::nullopt;  // Key not found — return "nothing."
    }
    return it->second;  // Key found — return the value.
}

// DEL key [key ...]
// Loop through each requested key and try to erase it.
// erase() returns 1 if the key existed (and was removed), or 0 if not.
// We accumulate the count of actually-deleted keys.
// Note: if the same key appears twice in the input, the second erase()
// returns 0 because the key was already removed by the first one.
std::size_t Database::del(const std::vector<std::string>& keys) {
    std::size_t count = 0;
    for (const auto& key : keys) {
        count += store_.erase(key);  // erase returns 0 or 1.
    }
    return count;
}

// EXISTS key [key ...]
// Loop through each requested key and check if it's in the store.
// count() returns 1 if found, 0 if not (for unordered_map, never >1).
// Important: unlike DEL, we count duplicates! If the user sends
// EXISTS mykey mykey and mykey exists, we return 2.
// This matches real Redis behavior.
std::size_t Database::exists(const std::vector<std::string>& keys) const {
    std::size_t count = 0;
    for (const auto& key : keys) {
        count += store_.count(key);  // count returns 0 or 1.
    }
    return count;
}

// FLUSHDB
// clear() removes all elements from the map, freeing their memory.
// After this call, size() will return 0.
void Database::flushdb() {
    store_.clear();
}

// SIZE
// Returns the number of key-value pairs currently stored.
// size() is a constant-time operation on unordered_map.
std::size_t Database::size() const {
    return store_.size();
}
