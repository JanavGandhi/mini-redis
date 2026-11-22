// command.cpp — Implementation of command dispatching and handlers.

#include "command.h"
#include "persistence.h"

#include <algorithm>  // std::transform
#include <atomic>     // std::atomic
#include <cctype>     // std::toupper
#include <charconv>   // std::from_chars
#include <ctime>      // std::time
#include <thread>     // std::thread

namespace {

static std::atomic<int64_t> s_last_save_time{0};
static std::atomic<uint64_t> s_total_commands{0};
static const auto s_start_time = std::chrono::steady_clock::now();

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

const char* WRONG_TYPE_MSG = "WRONGTYPE Operation against a key holding the wrong kind of value";

bool parse_int64_arg(const std::string& str, int64_t& out) {
    if (str.empty()) return false;
    std::string_view sv = str;
    if (sv[0] == '+') {
        sv.remove_prefix(1);
        if (sv.empty()) return false;
    }
    const char* first = sv.data();
    const char* last = sv.data() + sv.size();
    auto [ptr, ec] = std::from_chars(first, last, out);
    return (ec == std::errc{} && ptr == last);
}

} // anonymous namespace

RespValue CommandDispatcher::execute(const RespValue& command_value, Database& db) {
    std::vector<std::string> args;
    if (!command_value.to_string_vector(args) || args.empty()) {
        return RespValue::make_error("ERR Protocol error: expected array of strings");
    }

    return execute_args(args, db);
}

RespValue CommandDispatcher::execute_args(const std::vector<std::string>& args, Database& db) {
    if (args.empty()) {
        return RespValue::make_error("ERR empty command");
    }

    s_total_commands.fetch_add(1, std::memory_order_relaxed);

    std::string cmd = to_upper(args[0]);

    // Core / Generic
    if (cmd == "PING")      return handle_ping(args);
    if (cmd == "ECHO")      return handle_echo(args);
    if (cmd == "INFO")      return handle_info(args, db);
    if (cmd == "FLUSHDB")   return handle_flushdb(args, db);
    if (cmd == "COMMAND")   return handle_command(args);
    if (cmd == "DEL")       return handle_del(args, db);
    if (cmd == "EXISTS")    return handle_exists(args, db);

    // TTL / Expiration (Milestone 6)
    if (cmd == "EXPIRE")    return handle_expire(args, db);
    if (cmd == "PEXPIRE")   return handle_pexpire(args, db);
    if (cmd == "TTL")       return handle_ttl(args, db);
    if (cmd == "PTTL")      return handle_pttl(args, db);

    // Persistence (Milestone 7)
    if (cmd == "SAVE")      return handle_save(args, db);
    if (cmd == "BGSAVE")    return handle_bgsave(args, db);
    if (cmd == "LASTSAVE")  return handle_lastsave(args);

    // String (with EX/PX)
    if (cmd == "SET")       return handle_set(args, db);
    if (cmd == "GET")       return handle_get(args, db);

    // List
    if (cmd == "LPUSH")     return handle_lpush(args, db);
    if (cmd == "RPUSH")     return handle_rpush(args, db);
    if (cmd == "LPOP")      return handle_lpop(args, db);
    if (cmd == "RPOP")      return handle_rpop(args, db);
    if (cmd == "LLEN")      return handle_llen(args, db);
    if (cmd == "LRANGE")    return handle_lrange(args, db);

    // Hash
    if (cmd == "HSET")      return handle_hset(args, db);
    if (cmd == "HGET")      return handle_hget(args, db);
    if (cmd == "HDEL")      return handle_hdel(args, db);
    if (cmd == "HEXISTS")   return handle_hexists(args, db);
    if (cmd == "HGETALL")   return handle_hgetall(args, db);

    // Set
    if (cmd == "SADD")      return handle_sadd(args, db);
    if (cmd == "SREM")      return handle_srem(args, db);
    if (cmd == "SISMEMBER") return handle_sismember(args, db);
    if (cmd == "SMEMBERS")  return handle_smembers(args, db);
    if (cmd == "SCARD")     return handle_scard(args, db);

    return RespValue::make_error("ERR unknown command '" + args[0] + "'");
}

// ─── Core / Generic Handlers ─────────────────────────────────────────────────

RespValue CommandDispatcher::handle_ping(const std::vector<std::string>& args) {
    if (args.size() == 1) {
        return RespValue::make_simple_string("PONG");
    } else if (args.size() == 2) {
        return RespValue::make_bulk_string(args[1]);
    }
    return RespValue::make_error("ERR wrong number of arguments for 'ping' command");
}

RespValue CommandDispatcher::handle_echo(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return RespValue::make_error("ERR wrong number of arguments for 'echo' command");
    }
    return RespValue::make_bulk_string(args[1]);
}

RespValue CommandDispatcher::handle_flushdb(const std::vector<std::string>& args, Database& db) {
    if (args.size() != 1) {
        return RespValue::make_error("ERR wrong number of arguments for 'flushdb' command");
    }
    db.flushdb();
    return RespValue::make_simple_string("OK");
}

RespValue CommandDispatcher::handle_command(const std::vector<std::string>& /* args */) {
    return RespValue::make_array({});
}

RespValue CommandDispatcher::handle_info(const std::vector<std::string>& /*args*/, const Database& db) {
    auto now = std::chrono::steady_clock::now();
    auto uptime_sec = std::chrono::duration_cast<std::chrono::seconds>(now - s_start_time).count();

    std::string info;
    info += "# Server\r\n";
    info += "redis_version:0.1.0\r\n";
    info += "mini_redis_version:0.1.0\r\n";
    info += "os:Windows\r\n";
    info += "arch_bits:64\r\n";
    info += "uptime_in_seconds:" + std::to_string(uptime_sec) + "\r\n";
    info += "\r\n";

    info += "# Stats\r\n";
    info += "total_commands_processed:" + std::to_string(s_total_commands.load()) + "\r\n";
    info += "\r\n";

    info += "# Memory\r\n";
    info += "max_keys:" + std::to_string(db.max_keys()) + "\r\n";
    info += "used_keys:" + std::to_string(db.size()) + "\r\n";
    info += "\r\n";

    info += "# Keyspace\r\n";
    info += "db0:keys=" + std::to_string(db.size()) + "\r\n";

    return RespValue::make_bulk_string(info);
}

uint64_t CommandDispatcher::total_commands_processed() {
    return s_total_commands.load();
}

void CommandDispatcher::reset_stats() {
    s_total_commands.store(0);
}

RespValue CommandDispatcher::handle_del(const std::vector<std::string>& args, Database& db) {
    if (args.size() < 2) {
        return RespValue::make_error("ERR wrong number of arguments for 'del' command");
    }
    std::vector<std::string> keys(args.begin() + 1, args.end());
    std::size_t removed = db.del(keys);
    return RespValue::make_integer(static_cast<int64_t>(removed));
}

RespValue CommandDispatcher::handle_exists(const std::vector<std::string>& args, const Database& db) {
    if (args.size() < 2) {
        return RespValue::make_error("ERR wrong number of arguments for 'exists' command");
    }
    std::vector<std::string> keys(args.begin() + 1, args.end());
    std::size_t count = db.exists(keys);
    return RespValue::make_integer(static_cast<int64_t>(count));
}

// ─── Expiration & TTL Handlers (Milestone 6) ─────────────────────────────────

RespValue CommandDispatcher::handle_expire(const std::vector<std::string>& args, Database& db) {
    if (args.size() != 3) {
        return RespValue::make_error("ERR wrong number of arguments for 'expire' command");
    }
    int64_t seconds = 0;
    if (!parse_int64_arg(args[2], seconds)) {
        return RespValue::make_error("ERR value is not an integer or out of range");
    }
    bool ok = db.expire(args[1], seconds);
    return RespValue::make_integer(ok ? 1 : 0);
}

RespValue CommandDispatcher::handle_pexpire(const std::vector<std::string>& args, Database& db) {
    if (args.size() != 3) {
        return RespValue::make_error("ERR wrong number of arguments for 'pexpire' command");
    }
    int64_t ms = 0;
    if (!parse_int64_arg(args[2], ms)) {
        return RespValue::make_error("ERR value is not an integer or out of range");
    }
    bool ok = db.pexpire(args[1], ms);
    return RespValue::make_integer(ok ? 1 : 0);
}

RespValue CommandDispatcher::handle_ttl(const std::vector<std::string>& args, const Database& db) {
    if (args.size() != 2) {
        return RespValue::make_error("ERR wrong number of arguments for 'ttl' command");
    }
    int64_t val = db.ttl(args[1]);
    return RespValue::make_integer(val);
}

RespValue CommandDispatcher::handle_pttl(const std::vector<std::string>& args, const Database& db) {
    if (args.size() != 2) {
        return RespValue::make_error("ERR wrong number of arguments for 'pttl' command");
    }
    int64_t val = db.pttl(args[1]);
    return RespValue::make_integer(val);
}

// ─── Persistence Handlers (Milestone 7) ──────────────────────────────────────

RespValue CommandDispatcher::handle_save(const std::vector<std::string>& args, const Database& db) {
    if (args.size() != 1) {
        return RespValue::make_error("ERR wrong number of arguments for 'save' command");
    }
    if (SnapshotManager::save("dump.rdb", db)) {
        s_last_save_time.store(std::time(nullptr));
        return RespValue::make_simple_string("OK");
    }
    return RespValue::make_error("ERR failed to save snapshot");
}

RespValue CommandDispatcher::handle_bgsave(const std::vector<std::string>& args, const Database& db) {
    if (args.size() != 1) {
        return RespValue::make_error("ERR wrong number of arguments for 'bgsave' command");
    }

    // Export current snapshot state before spawning thread to avoid concurrency issues
    auto snapshot_data = db.export_snapshot();

    std::thread([data = std::move(snapshot_data)]() {
        Database temp_db;
        temp_db.import_snapshot(data);
        if (SnapshotManager::save("dump.rdb", temp_db)) {
            s_last_save_time.store(std::time(nullptr));
        }
    }).detach();

    return RespValue::make_simple_string("Background saving started");
}

RespValue CommandDispatcher::handle_lastsave(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return RespValue::make_error("ERR wrong number of arguments for 'lastsave' command");
    }
    int64_t t = s_last_save_time.load();
    if (t == 0) {
        t = std::time(nullptr);
    }
    return RespValue::make_integer(t);
}

// ─── String Handlers (with EX/PX) ───────────────────────────────────────────

RespValue CommandDispatcher::handle_set(const std::vector<std::string>& args, Database& db) {
    if (args.size() < 3) {
        return RespValue::make_error("ERR wrong number of arguments for 'set' command");
    }

    if (!db.can_add_key(args[1])) {
        return RespValue::make_error("OOM command not allowed when used memory > 'maxmemory'");
    }

    int64_t ttl_ms = 0;

    if (args.size() == 3) {
        // Plain SET key value
        db.set(args[1], args[2], 0);
        return RespValue::make_simple_string("OK");
    } else if (args.size() == 5) {
        std::string opt = to_upper(args[3]);
        if (opt == "EX") {
            int64_t sec = 0;
            if (!parse_int64_arg(args[4], sec) || sec <= 0) {
                return RespValue::make_error("ERR value is not an integer or out of range");
            }
            ttl_ms = sec * 1000;
        } else if (opt == "PX") {
            int64_t ms = 0;
            if (!parse_int64_arg(args[4], ms) || ms <= 0) {
                return RespValue::make_error("ERR value is not an integer or out of range");
            }
            ttl_ms = ms;
        } else {
            return RespValue::make_error("ERR syntax error");
        }

        db.set(args[1], args[2], ttl_ms);
        return RespValue::make_simple_string("OK");
    }

    return RespValue::make_error("ERR syntax error");
}

RespValue CommandDispatcher::handle_get(const std::vector<std::string>& args, const Database& db) {
    if (args.size() != 2) {
        return RespValue::make_error("ERR wrong number of arguments for 'get' command");
    }
    auto res = db.get(args[1]);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }
    if (!res.value.has_value()) {
        return RespValue::make_null_bulk_string();
    }
    return RespValue::make_bulk_string(res.value.value());
}

// ─── List Handlers ───────────────────────────────────────────────────────────

RespValue CommandDispatcher::handle_lpush(const std::vector<std::string>& args, Database& db) {
    if (args.size() < 3) {
        return RespValue::make_error("ERR wrong number of arguments for 'lpush' command");
    }
    if (!db.can_add_key(args[1])) {
        return RespValue::make_error("OOM command not allowed when used memory > 'maxmemory'");
    }
    std::vector<std::string> values(args.begin() + 2, args.end());
    auto res = db.lpush(args[1], values);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }
    return RespValue::make_integer(static_cast<int64_t>(res.value));
}

RespValue CommandDispatcher::handle_rpush(const std::vector<std::string>& args, Database& db) {
    if (args.size() < 3) {
        return RespValue::make_error("ERR wrong number of arguments for 'rpush' command");
    }
    if (!db.can_add_key(args[1])) {
        return RespValue::make_error("OOM command not allowed when used memory > 'maxmemory'");
    }
    std::vector<std::string> values(args.begin() + 2, args.end());
    auto res = db.rpush(args[1], values);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }
    return RespValue::make_integer(static_cast<int64_t>(res.value));
}

RespValue CommandDispatcher::handle_lpop(const std::vector<std::string>& args, Database& db) {
    if (args.size() != 2) {
        return RespValue::make_error("ERR wrong number of arguments for 'lpop' command");
    }
    auto res = db.lpop(args[1]);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }
    if (!res.value.has_value()) {
        return RespValue::make_null_bulk_string();
    }
    return RespValue::make_bulk_string(res.value.value());
}

RespValue CommandDispatcher::handle_rpop(const std::vector<std::string>& args, Database& db) {
    if (args.size() != 2) {
        return RespValue::make_error("ERR wrong number of arguments for 'rpop' command");
    }
    auto res = db.rpop(args[1]);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }
    if (!res.value.has_value()) {
        return RespValue::make_null_bulk_string();
    }
    return RespValue::make_bulk_string(res.value.value());
}

RespValue CommandDispatcher::handle_llen(const std::vector<std::string>& args, const Database& db) {
    if (args.size() != 2) {
        return RespValue::make_error("ERR wrong number of arguments for 'llen' command");
    }
    auto res = db.llen(args[1]);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }
    return RespValue::make_integer(static_cast<int64_t>(res.value));
}

RespValue CommandDispatcher::handle_lrange(const std::vector<std::string>& args, const Database& db) {
    if (args.size() != 4) {
        return RespValue::make_error("ERR wrong number of arguments for 'lrange' command");
    }
    int64_t start = 0;
    int64_t stop = 0;
    if (!parse_int64_arg(args[2], start) || !parse_int64_arg(args[3], stop)) {
        return RespValue::make_error("ERR value is not an integer or out of range");
    }

    auto res = db.lrange(args[1], start, stop);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }

    std::vector<RespValue> items;
    items.reserve(res.value.size());
    for (const auto& elem : res.value) {
        items.push_back(RespValue::make_bulk_string(elem));
    }
    return RespValue::make_array(items);
}

// ─── Hash Handlers ───────────────────────────────────────────────────────────

RespValue CommandDispatcher::handle_hset(const std::vector<std::string>& args, Database& db) {
    if (args.size() < 4 || (args.size() % 2 != 0)) {
        return RespValue::make_error("ERR wrong number of arguments for 'hset' command");
    }
    if (!db.can_add_key(args[1])) {
        return RespValue::make_error("OOM command not allowed when used memory > 'maxmemory'");
    }

    std::vector<std::pair<std::string, std::string>> field_values;
    for (std::size_t i = 2; i < args.size(); i += 2) {
        field_values.emplace_back(args[i], args[i + 1]);
    }

    auto res = db.hset(args[1], field_values);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }
    return RespValue::make_integer(static_cast<int64_t>(res.value));
}

RespValue CommandDispatcher::handle_hget(const std::vector<std::string>& args, const Database& db) {
    if (args.size() != 3) {
        return RespValue::make_error("ERR wrong number of arguments for 'hget' command");
    }
    auto res = db.hget(args[1], args[2]);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }
    if (!res.value.has_value()) {
        return RespValue::make_null_bulk_string();
    }
    return RespValue::make_bulk_string(res.value.value());
}

RespValue CommandDispatcher::handle_hdel(const std::vector<std::string>& args, Database& db) {
    if (args.size() < 3) {
        return RespValue::make_error("ERR wrong number of arguments for 'hdel' command");
    }
    std::vector<std::string> fields(args.begin() + 2, args.end());
    auto res = db.hdel(args[1], fields);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }
    return RespValue::make_integer(static_cast<int64_t>(res.value));
}

RespValue CommandDispatcher::handle_hexists(const std::vector<std::string>& args, const Database& db) {
    if (args.size() != 3) {
        return RespValue::make_error("ERR wrong number of arguments for 'hexists' command");
    }
    auto res = db.hexists(args[1], args[2]);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }
    return RespValue::make_integer(res.value ? 1 : 0);
}

RespValue CommandDispatcher::handle_hgetall(const std::vector<std::string>& args, const Database& db) {
    if (args.size() != 2) {
        return RespValue::make_error("ERR wrong number of arguments for 'hgetall' command");
    }
    auto res = db.hgetall(args[1]);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }

    std::vector<RespValue> items;
    items.reserve(res.value.size() * 2);
    for (const auto& [f, v] : res.value) {
        items.push_back(RespValue::make_bulk_string(f));
        items.push_back(RespValue::make_bulk_string(v));
    }
    return RespValue::make_array(items);
}

// ─── Set Handlers ────────────────────────────────────────────────────────────

RespValue CommandDispatcher::handle_sadd(const std::vector<std::string>& args, Database& db) {
    if (args.size() < 3) {
        return RespValue::make_error("ERR wrong number of arguments for 'sadd' command");
    }
    if (!db.can_add_key(args[1])) {
        return RespValue::make_error("OOM command not allowed when used memory > 'maxmemory'");
    }
    std::vector<std::string> members(args.begin() + 2, args.end());
    auto res = db.sadd(args[1], members);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }
    return RespValue::make_integer(static_cast<int64_t>(res.value));
}

RespValue CommandDispatcher::handle_srem(const std::vector<std::string>& args, Database& db) {
    if (args.size() < 3) {
        return RespValue::make_error("ERR wrong number of arguments for 'srem' command");
    }
    std::vector<std::string> members(args.begin() + 2, args.end());
    auto res = db.srem(args[1], members);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }
    return RespValue::make_integer(static_cast<int64_t>(res.value));
}

RespValue CommandDispatcher::handle_sismember(const std::vector<std::string>& args, const Database& db) {
    if (args.size() != 3) {
        return RespValue::make_error("ERR wrong number of arguments for 'sismember' command");
    }
    auto res = db.sismember(args[1], args[2]);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }
    return RespValue::make_integer(res.value ? 1 : 0);
}

RespValue CommandDispatcher::handle_smembers(const std::vector<std::string>& args, const Database& db) {
    if (args.size() != 2) {
        return RespValue::make_error("ERR wrong number of arguments for 'smembers' command");
    }
    auto res = db.smembers(args[1]);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }

    std::vector<RespValue> items;
    items.reserve(res.value.size());
    for (const auto& m : res.value) {
        items.push_back(RespValue::make_bulk_string(m));
    }
    return RespValue::make_array(items);
}

RespValue CommandDispatcher::handle_scard(const std::vector<std::string>& args, const Database& db) {
    if (args.size() != 2) {
        return RespValue::make_error("ERR wrong number of arguments for 'scard' command");
    }
    auto res = db.scard(args[1]);
    if (res.wrong_type) {
        return RespValue::make_error(WRONG_TYPE_MSG);
    }
    return RespValue::make_integer(static_cast<int64_t>(res.value));
}
