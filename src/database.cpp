// database.cpp — Implementation of database engine with TTL & expiration.

#include "database.h"

#include <algorithm>

// ─── Lazy Expiration Helper ─────────────────────────────────────────────────

bool Database::check_and_expire(const std::string& key) const {
    auto exp_it = expires_.find(key);
    if (exp_it == expires_.end()) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    if (now >= exp_it->second) {
        // Expired! Delete key from store and expires table.
        store_.erase(key);
        expires_.erase(exp_it);
        return true;
    }

    return false;
}

// ─── Generic Key Operations ──────────────────────────────────────────────────

std::size_t Database::del(const std::vector<std::string>& keys) {
    std::size_t count = 0;
    for (const auto& key : keys) {
        check_and_expire(key);
        expires_.erase(key);
        count += store_.erase(key);
    }
    return count;
}

std::size_t Database::exists(const std::vector<std::string>& keys) const {
    std::size_t count = 0;
    for (const auto& key : keys) {
        if (!check_and_expire(key)) {
            count += store_.count(key);
        }
    }
    return count;
}

void Database::flushdb() {
    store_.clear();
    expires_.clear();
}

std::size_t Database::size() const {
    // Purge expired keys first to return accurate active count
    const_cast<Database*>(this)->purge_expired();
    return store_.size();
}

DataType Database::type(const std::string& key) const {
    if (check_and_expire(key)) {
        return DataType::None;
    }

    auto it = store_.find(key);
    if (it == store_.end()) {
        return DataType::None;
    }
    if (std::holds_alternative<std::string>(it->second)) return DataType::String;
    if (std::holds_alternative<std::deque<std::string>>(it->second)) return DataType::List;
    if (std::holds_alternative<std::unordered_map<std::string, std::string>>(it->second)) return DataType::Hash;
    if (std::holds_alternative<std::unordered_set<std::string>>(it->second)) return DataType::Set;
    return DataType::None;
}

// ─── Expiration & TTL Operations (Milestone 6) ───────────────────────────────

bool Database::expire(const std::string& key, int64_t seconds) {
    return pexpire(key, seconds * 1000);
}

bool Database::pexpire(const std::string& key, int64_t milliseconds) {
    if (check_and_expire(key)) {
        return false;
    }

    auto it = store_.find(key);
    if (it == store_.end()) {
        return false;
    }

    if (milliseconds <= 0) {
        // Immediate expiration
        store_.erase(it);
        expires_.erase(key);
        return true;
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    expires_[key] = deadline;
    return true;
}

int64_t Database::ttl(const std::string& key) const {
    int64_t ms = pttl(key);
    if (ms < 0) {
        return ms; // -2 (missing/expired) or -1 (no expire)
    }
    // Round up so that 500ms remaining reports 1 second
    return (ms + 999) / 1000;
}

int64_t Database::pttl(const std::string& key) const {
    if (check_and_expire(key)) {
        return -2;
    }

    if (store_.find(key) == store_.end()) {
        return -2;
    }

    auto exp_it = expires_.find(key);
    if (exp_it == expires_.end()) {
        return -1;
    }

    auto now = std::chrono::steady_clock::now();
    if (now >= exp_it->second) {
        // Expired right now
        store_.erase(key);
        expires_.erase(exp_it);
        return -2;
    }

    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(exp_it->second - now).count();
    return diff > 0 ? diff : 0;
}

std::size_t Database::purge_expired() {
    auto now = std::chrono::steady_clock::now();
    std::size_t purged = 0;

    for (auto it = expires_.begin(); it != expires_.end();) {
        if (now >= it->second) {
            store_.erase(it->first);
            it = expires_.erase(it);
            ++purged;
        } else {
            ++it;
        }
    }

    return purged;
}

// ─── String Operations ───────────────────────────────────────────────────────

void Database::set(const std::string& key, const std::string& value, int64_t ttl_ms) {
    store_[key] = value;
    if (ttl_ms > 0) {
        expires_[key] = std::chrono::steady_clock::now() + std::chrono::milliseconds(ttl_ms);
    } else {
        // Plain SET clears any previous TTL
        expires_.erase(key);
    }
}

DbResult<std::optional<std::string>> Database::get(const std::string& key) const {
    if (check_and_expire(key)) {
        return DbResult<std::optional<std::string>>::success(std::nullopt);
    }

    auto it = store_.find(key);
    if (it == store_.end()) {
        return DbResult<std::optional<std::string>>::success(std::nullopt);
    }
    const auto* str = std::get_if<std::string>(&it->second);
    if (!str) {
        return DbResult<std::optional<std::string>>::wrong_type_err();
    }
    return DbResult<std::optional<std::string>>::success(*str);
}

// ─── List Operations ─────────────────────────────────────────────────────────

DbResult<std::size_t> Database::lpush(const std::string& key, const std::vector<std::string>& values) {
    check_and_expire(key);

    auto it = store_.find(key);
    if (it == store_.end()) {
        it = store_.emplace(key, std::deque<std::string>{}).first;
    }

    auto* list = std::get_if<std::deque<std::string>>(&it->second);
    if (!list) {
        return DbResult<std::size_t>::wrong_type_err();
    }

    for (const auto& val : values) {
        list->push_front(val);
    }

    return DbResult<std::size_t>::success(list->size());
}

DbResult<std::size_t> Database::rpush(const std::string& key, const std::vector<std::string>& values) {
    check_and_expire(key);

    auto it = store_.find(key);
    if (it == store_.end()) {
        it = store_.emplace(key, std::deque<std::string>{}).first;
    }

    auto* list = std::get_if<std::deque<std::string>>(&it->second);
    if (!list) {
        return DbResult<std::size_t>::wrong_type_err();
    }

    for (const auto& val : values) {
        list->push_back(val);
    }

    return DbResult<std::size_t>::success(list->size());
}

DbResult<std::optional<std::string>> Database::lpop(const std::string& key) {
    if (check_and_expire(key)) {
        return DbResult<std::optional<std::string>>::success(std::nullopt);
    }

    auto it = store_.find(key);
    if (it == store_.end()) {
        return DbResult<std::optional<std::string>>::success(std::nullopt);
    }

    auto* list = std::get_if<std::deque<std::string>>(&it->second);
    if (!list) {
        return DbResult<std::optional<std::string>>::wrong_type_err();
    }

    if (list->empty()) {
        store_.erase(it);
        expires_.erase(key);
        return DbResult<std::optional<std::string>>::success(std::nullopt);
    }

    std::string val = std::move(list->front());
    list->pop_front();

    if (list->empty()) {
        store_.erase(it);
        expires_.erase(key);
    }

    return DbResult<std::optional<std::string>>::success(std::move(val));
}

DbResult<std::optional<std::string>> Database::rpop(const std::string& key) {
    if (check_and_expire(key)) {
        return DbResult<std::optional<std::string>>::success(std::nullopt);
    }

    auto it = store_.find(key);
    if (it == store_.end()) {
        return DbResult<std::optional<std::string>>::success(std::nullopt);
    }

    auto* list = std::get_if<std::deque<std::string>>(&it->second);
    if (!list) {
        return DbResult<std::optional<std::string>>::wrong_type_err();
    }

    if (list->empty()) {
        store_.erase(it);
        expires_.erase(key);
        return DbResult<std::optional<std::string>>::success(std::nullopt);
    }

    std::string val = std::move(list->back());
    list->pop_back();

    if (list->empty()) {
        store_.erase(it);
        expires_.erase(key);
    }

    return DbResult<std::optional<std::string>>::success(std::move(val));
}

DbResult<std::size_t> Database::llen(const std::string& key) const {
    if (check_and_expire(key)) {
        return DbResult<std::size_t>::success(0);
    }

    auto it = store_.find(key);
    if (it == store_.end()) {
        return DbResult<std::size_t>::success(0);
    }

    const auto* list = std::get_if<std::deque<std::string>>(&it->second);
    if (!list) {
        return DbResult<std::size_t>::wrong_type_err();
    }

    return DbResult<std::size_t>::success(list->size());
}

DbResult<std::vector<std::string>> Database::lrange(const std::string& key, int64_t start, int64_t stop) const {
    if (check_and_expire(key)) {
        return DbResult<std::vector<std::string>>::success({});
    }

    auto it = store_.find(key);
    if (it == store_.end()) {
        return DbResult<std::vector<std::string>>::success({});
    }

    const auto* list = std::get_if<std::deque<std::string>>(&it->second);
    if (!list) {
        return DbResult<std::vector<std::string>>::wrong_type_err();
    }

    int64_t n = static_cast<int64_t>(list->size());
    if (n == 0) {
        return DbResult<std::vector<std::string>>::success({});
    }

    if (start < 0) start = n + start;
    if (stop < 0)  stop  = n + stop;

    if (start < 0) start = 0;
    if (stop >= n) stop = n - 1;

    if (start > stop || start >= n) {
        return DbResult<std::vector<std::string>>::success({});
    }

    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(stop - start + 1));
    for (int64_t i = start; i <= stop; ++i) {
        result.push_back((*list)[static_cast<std::size_t>(i)]);
    }

    return DbResult<std::vector<std::string>>::success(std::move(result));
}

// ─── Hash Operations ─────────────────────────────────────────────────────────

DbResult<std::size_t> Database::hset(const std::string& key, const std::vector<std::pair<std::string, std::string>>& field_values) {
    check_and_expire(key);

    auto it = store_.find(key);
    if (it == store_.end()) {
        it = store_.emplace(key, std::unordered_map<std::string, std::string>{}).first;
    }

    auto* hash = std::get_if<std::unordered_map<std::string, std::string>>(&it->second);
    if (!hash) {
        return DbResult<std::size_t>::wrong_type_err();
    }

    std::size_t added = 0;
    for (const auto& [field, value] : field_values) {
        auto [inserted_it, inserted] = hash->insert_or_assign(field, value);
        if (inserted) {
            ++added;
        }
    }

    return DbResult<std::size_t>::success(added);
}

DbResult<std::optional<std::string>> Database::hget(const std::string& key, const std::string& field) const {
    if (check_and_expire(key)) {
        return DbResult<std::optional<std::string>>::success(std::nullopt);
    }

    auto it = store_.find(key);
    if (it == store_.end()) {
        return DbResult<std::optional<std::string>>::success(std::nullopt);
    }

    const auto* hash = std::get_if<std::unordered_map<std::string, std::string>>(&it->second);
    if (!hash) {
        return DbResult<std::optional<std::string>>::wrong_type_err();
    }

    auto fit = hash->find(field);
    if (fit == hash->end()) {
        return DbResult<std::optional<std::string>>::success(std::nullopt);
    }

    return DbResult<std::optional<std::string>>::success(fit->second);
}

DbResult<std::size_t> Database::hdel(const std::string& key, const std::vector<std::string>& fields) {
    if (check_and_expire(key)) {
        return DbResult<std::size_t>::success(0);
    }

    auto it = store_.find(key);
    if (it == store_.end()) {
        return DbResult<std::size_t>::success(0);
    }

    auto* hash = std::get_if<std::unordered_map<std::string, std::string>>(&it->second);
    if (!hash) {
        return DbResult<std::size_t>::wrong_type_err();
    }

    std::size_t count = 0;
    for (const auto& field : fields) {
        count += hash->erase(field);
    }

    if (hash->empty()) {
        store_.erase(it);
        expires_.erase(key);
    }

    return DbResult<std::size_t>::success(count);
}

DbResult<bool> Database::hexists(const std::string& key, const std::string& field) const {
    if (check_and_expire(key)) {
        return DbResult<bool>::success(false);
    }

    auto it = store_.find(key);
    if (it == store_.end()) {
        return DbResult<bool>::success(false);
    }

    const auto* hash = std::get_if<std::unordered_map<std::string, std::string>>(&it->second);
    if (!hash) {
        return DbResult<bool>::wrong_type_err();
    }

    return DbResult<bool>::success(hash->find(field) != hash->end());
}

DbResult<std::vector<std::pair<std::string, std::string>>> Database::hgetall(const std::string& key) const {
    if (check_and_expire(key)) {
        return DbResult<std::vector<std::pair<std::string, std::string>>>::success({});
    }

    auto it = store_.find(key);
    if (it == store_.end()) {
        return DbResult<std::vector<std::pair<std::string, std::string>>>::success({});
    }

    const auto* hash = std::get_if<std::unordered_map<std::string, std::string>>(&it->second);
    if (!hash) {
        return DbResult<std::vector<std::pair<std::string, std::string>>>::wrong_type_err();
    }

    std::vector<std::pair<std::string, std::string>> result(hash->begin(), hash->end());
    return DbResult<std::vector<std::pair<std::string, std::string>>>::success(std::move(result));
}

// ─── Set Operations ──────────────────────────────────────────────────────────

DbResult<std::size_t> Database::sadd(const std::string& key, const std::vector<std::string>& members) {
    check_and_expire(key);

    auto it = store_.find(key);
    if (it == store_.end()) {
        it = store_.emplace(key, std::unordered_set<std::string>{}).first;
    }

    auto* set = std::get_if<std::unordered_set<std::string>>(&it->second);
    if (!set) {
        return DbResult<std::size_t>::wrong_type_err();
    }

    std::size_t added = 0;
    for (const auto& member : members) {
        auto [inserted_it, inserted] = set->insert(member);
        if (inserted) {
            ++added;
        }
    }

    return DbResult<std::size_t>::success(added);
}

DbResult<std::size_t> Database::srem(const std::string& key, const std::vector<std::string>& members) {
    if (check_and_expire(key)) {
        return DbResult<std::size_t>::success(0);
    }

    auto it = store_.find(key);
    if (it == store_.end()) {
        return DbResult<std::size_t>::success(0);
    }

    auto* set = std::get_if<std::unordered_set<std::string>>(&it->second);
    if (!set) {
        return DbResult<std::size_t>::wrong_type_err();
    }

    std::size_t count = 0;
    for (const auto& member : members) {
        count += set->erase(member);
    }

    if (set->empty()) {
        store_.erase(it);
        expires_.erase(key);
    }

    return DbResult<std::size_t>::success(count);
}

DbResult<bool> Database::sismember(const std::string& key, const std::string& member) const {
    if (check_and_expire(key)) {
        return DbResult<bool>::success(false);
    }

    auto it = store_.find(key);
    if (it == store_.end()) {
        return DbResult<bool>::success(false);
    }

    const auto* set = std::get_if<std::unordered_set<std::string>>(&it->second);
    if (!set) {
        return DbResult<bool>::wrong_type_err();
    }

    return DbResult<bool>::success(set->find(member) != set->end());
}

DbResult<std::vector<std::string>> Database::smembers(const std::string& key) const {
    if (check_and_expire(key)) {
        return DbResult<std::vector<std::string>>::success({});
    }

    auto it = store_.find(key);
    if (it == store_.end()) {
        return DbResult<std::vector<std::string>>::success({});
    }

    const auto* set = std::get_if<std::unordered_set<std::string>>(&it->second);
    if (!set) {
        return DbResult<std::vector<std::string>>::wrong_type_err();
    }

    std::vector<std::string> result(set->begin(), set->end());
    return DbResult<std::vector<std::string>>::success(std::move(result));
}

DbResult<std::size_t> Database::scard(const std::string& key) const {
    if (check_and_expire(key)) {
        return DbResult<std::size_t>::success(0);
    }

    auto it = store_.find(key);
    if (it == store_.end()) {
        return DbResult<std::size_t>::success(0);
    }

    const auto* set = std::get_if<std::unordered_set<std::string>>(&it->second);
    if (!set) {
        return DbResult<std::size_t>::wrong_type_err();
    }

    return DbResult<std::size_t>::success(set->size());
}
