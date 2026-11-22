// command.cpp — Implementation of command dispatching and handlers.

#include "command.h"

#include <algorithm>  // std::transform
#include <cctype>     // std::toupper

namespace {

// Helper: convert string to uppercase for case-insensitive matching.
std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
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

    std::string cmd_name = to_upper(args[0]);

    if (cmd_name == "PING") {
        return handle_ping(args);
    } else if (cmd_name == "ECHO") {
        return handle_echo(args);
    } else if (cmd_name == "SET") {
        return handle_set(args, db);
    } else if (cmd_name == "GET") {
        return handle_get(args, db);
    } else if (cmd_name == "DEL") {
        return handle_del(args, db);
    } else if (cmd_name == "EXISTS") {
        return handle_exists(args, db);
    } else if (cmd_name == "FLUSHDB") {
        return handle_flushdb(args, db);
    } else if (cmd_name == "COMMAND") {
        // Modern redis-cli sends "COMMAND" or "COMMAND DOCS" on connection.
        // Returning an empty array is the standard minimal response.
        return handle_command(args);
    }

    return RespValue::make_error("ERR unknown command '" + args[0] + "'");
}

// ─── Command Handlers ────────────────────────────────────────────────────────

// PING [message]
// Returns +PONG\r\n if no args, or bulk string message if provided.
RespValue CommandDispatcher::handle_ping(const std::vector<std::string>& args) {
    if (args.size() == 1) {
        return RespValue::make_simple_string("PONG");
    } else if (args.size() == 2) {
        return RespValue::make_bulk_string(args[1]);
    }
    return RespValue::make_error("ERR wrong number of arguments for 'ping' command");
}

// ECHO message
// Returns bulk string message.
RespValue CommandDispatcher::handle_echo(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return RespValue::make_error("ERR wrong number of arguments for 'echo' command");
    }
    return RespValue::make_bulk_string(args[1]);
}

// SET key value
// Stores value and returns +OK\r\n.
RespValue CommandDispatcher::handle_set(const std::vector<std::string>& args, Database& db) {
    if (args.size() != 3) {
        return RespValue::make_error("ERR wrong number of arguments for 'set' command");
    }
    db.set(args[1], args[2]);
    return RespValue::make_simple_string("OK");
}

// GET key
// Returns bulk string if found, or null bulk string ($-1\r\n) if not found.
RespValue CommandDispatcher::handle_get(const std::vector<std::string>& args, const Database& db) {
    if (args.size() != 2) {
        return RespValue::make_error("ERR wrong number of arguments for 'get' command");
    }
    auto val = db.get(args[1]);
    if (!val.has_value()) {
        return RespValue::make_null_bulk_string();
    }
    return RespValue::make_bulk_string(val.value());
}

// DEL key [key ...]
// Deletes keys and returns integer count of keys actually removed.
RespValue CommandDispatcher::handle_del(const std::vector<std::string>& args, Database& db) {
    if (args.size() < 2) {
        return RespValue::make_error("ERR wrong number of arguments for 'del' command");
    }
    std::vector<std::string> keys(args.begin() + 1, args.end());
    std::size_t removed = db.del(keys);
    return RespValue::make_integer(static_cast<int64_t>(removed));
}

// EXISTS key [key ...]
// Returns integer count of existing keys (duplicates in query counted multiple times).
RespValue CommandDispatcher::handle_exists(const std::vector<std::string>& args, const Database& db) {
    if (args.size() < 2) {
        return RespValue::make_error("ERR wrong number of arguments for 'exists' command");
    }
    std::vector<std::string> keys(args.begin() + 1, args.end());
    std::size_t count = db.exists(keys);
    return RespValue::make_integer(static_cast<int64_t>(count));
}

// FLUSHDB
// Clears database and returns +OK\r\n.
RespValue CommandDispatcher::handle_flushdb(const std::vector<std::string>& args, Database& db) {
    if (args.size() != 1) {
        return RespValue::make_error("ERR wrong number of arguments for 'flushdb' command");
    }
    db.flushdb();
    return RespValue::make_simple_string("OK");
}

// COMMAND [DOCS]
// Returns empty array for redis-cli compatibility.
RespValue CommandDispatcher::handle_command(const std::vector<std::string>& /* args */) {
    return RespValue::make_array({});
}
