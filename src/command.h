// command.h — Command dispatcher and execution engine for mini-redis.

#ifndef MINI_REDIS_COMMAND_H
#define MINI_REDIS_COMMAND_H

#include "database.h"
#include "resp.h"

#include <string>
#include <vector>

class CommandDispatcher {
public:
    static RespValue execute(const RespValue& command_value, Database& db);
    static RespValue execute_args(const std::vector<std::string>& args, Database& db);

private:
    // Core server commands
    static RespValue handle_ping(const std::vector<std::string>& args);
    static RespValue handle_echo(const std::vector<std::string>& args);
    static RespValue handle_flushdb(const std::vector<std::string>& args, Database& db);
    static RespValue handle_command(const std::vector<std::string>& args);

    // Generic key commands
    static RespValue handle_del(const std::vector<std::string>& args, Database& db);
    static RespValue handle_exists(const std::vector<std::string>& args, const Database& db);

    // Expiration & TTL commands (Milestone 6)
    static RespValue handle_expire(const std::vector<std::string>& args, Database& db);
    static RespValue handle_pexpire(const std::vector<std::string>& args, Database& db);
    static RespValue handle_ttl(const std::vector<std::string>& args, const Database& db);
    static RespValue handle_pttl(const std::vector<std::string>& args, const Database& db);

    // Persistence commands (Milestone 7)
    static RespValue handle_save(const std::vector<std::string>& args, const Database& db);
    static RespValue handle_bgsave(const std::vector<std::string>& args, const Database& db);
    static RespValue handle_lastsave(const std::vector<std::string>& args);

    // String commands (with EX / PX options)
    static RespValue handle_set(const std::vector<std::string>& args, Database& db);
    static RespValue handle_get(const std::vector<std::string>& args, const Database& db);

    // List commands
    static RespValue handle_lpush(const std::vector<std::string>& args, Database& db);
    static RespValue handle_rpush(const std::vector<std::string>& args, Database& db);
    static RespValue handle_lpop(const std::vector<std::string>& args, Database& db);
    static RespValue handle_rpop(const std::vector<std::string>& args, Database& db);
    static RespValue handle_llen(const std::vector<std::string>& args, const Database& db);
    static RespValue handle_lrange(const std::vector<std::string>& args, const Database& db);

    // Hash commands
    static RespValue handle_hset(const std::vector<std::string>& args, Database& db);
    static RespValue handle_hget(const std::vector<std::string>& args, const Database& db);
    static RespValue handle_hdel(const std::vector<std::string>& args, Database& db);
    static RespValue handle_hexists(const std::vector<std::string>& args, const Database& db);
    static RespValue handle_hgetall(const std::vector<std::string>& args, const Database& db);

    // Set commands
    static RespValue handle_sadd(const std::vector<std::string>& args, Database& db);
    static RespValue handle_srem(const std::vector<std::string>& args, Database& db);
    static RespValue handle_sismember(const std::vector<std::string>& args, const Database& db);
    static RespValue handle_smembers(const std::vector<std::string>& args, const Database& db);
    static RespValue handle_scard(const std::vector<std::string>& args, const Database& db);
};

#endif // MINI_REDIS_COMMAND_H
