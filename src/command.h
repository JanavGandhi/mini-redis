// command.h — Command dispatcher and execution engine for mini-redis.
//
// In Redis, incoming client requests arrive as RESP Arrays of Bulk Strings.
// For example:
//   SET mykey myvalue → *3\r\n$3\r\nSET\r\n$5\r\nmykey\r\n$7\r\nmyvalue\r\n
//
// The CommandDispatcher:
//   1. Validates the command structure (must be an array of strings).
//   2. Extracts the command name and arguments.
//   3. Normalizes command name to uppercase (Redis commands are case-insensitive).
//   4. Validates argument counts.
//   5. Executes the corresponding operation on the Database.
//   6. Returns a RespValue to be encoded and sent back to the client.

#ifndef MINI_REDIS_COMMAND_H
#define MINI_REDIS_COMMAND_H

#include "database.h"
#include "resp.h"

#include <string>
#include <vector>

class CommandDispatcher {
public:
    // Execute a parsed RESP command against the given Database.
    // Returns a RespValue representing the response.
    static RespValue execute(const RespValue& command_value, Database& db);

    // Convenience overload: execute from raw string arguments (e.g. {"SET", "k", "v"}).
    static RespValue execute_args(const std::vector<std::string>& args, Database& db);

private:
    // Individual command handlers:
    static RespValue handle_ping(const std::vector<std::string>& args);
    static RespValue handle_echo(const std::vector<std::string>& args);
    static RespValue handle_set(const std::vector<std::string>& args, Database& db);
    static RespValue handle_get(const std::vector<std::string>& args, const Database& db);
    static RespValue handle_del(const std::vector<std::string>& args, Database& db);
    static RespValue handle_exists(const std::vector<std::string>& args, const Database& db);
    static RespValue handle_flushdb(const std::vector<std::string>& args, Database& db);
    static RespValue handle_command(const std::vector<std::string>& args);
};

#endif // MINI_REDIS_COMMAND_H
