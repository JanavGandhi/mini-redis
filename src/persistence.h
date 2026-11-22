// persistence.h — Snapshot (RDB) and Append-Only File (AOF) persistence engine.
//
// Milestone 7:
// Provides durability and crash recovery for mini-redis:
//   1. Snapshotting (RDB): Binary serialization of entire database state to disk.
//   2. Append-Only Log (AOF): Logs all state-mutating commands in RESP format.
//   3. Crash Recovery: Replays AOF or loads snapshot on startup to restore data.
//   4. AOF Compaction / Rewrite: Compresses state into minimal equivalent commands.

#ifndef MINI_REDIS_PERSISTENCE_H
#define MINI_REDIS_PERSISTENCE_H

#include "database.h"
#include "resp.h"

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

// ─── Snapshot Manager (RDB) ─────────────────────────────────────────────────

class SnapshotManager {
public:
    // Save current database state to a binary snapshot file.
    // Returns true on success, false on I/O error.
    static bool save(const std::string& filepath, const Database& db);

    // Load database state from binary snapshot file, restoring keys, values, and TTLs.
    // Returns true on success, false on error or invalid file.
    static bool load(const std::string& filepath, Database& db);
};

// ─── Append-Only File Manager (AOF) ─────────────────────────────────────────

class AofManager {
public:
    AofManager();
    ~AofManager();

    AofManager(const AofManager&) = delete;
    AofManager& operator=(const AofManager&) = delete;

    // Start logging to the specified AOF file.
    bool open(const std::string& filepath);

    // Append a mutating command to the log in RESP wire format.
    void append(const RespValue& command_value);

    // Flush buffered writes to disk.
    void flush();

    // Close the log file.
    void close();

    bool is_open() const;

    // Check if a command modifies state (e.g. SET, DEL) vs read-only (GET, PING).
    static bool is_mutating(const std::string& cmd_name);

    // Crash Recovery: Replay an existing AOF file against a Database.
    static bool replay(const std::string& filepath, Database& db);

    // AOF Rewrite: Compacts current Database state into a fresh, minimal AOF file.
    static bool rewrite(const std::string& filepath, const Database& db);

private:
    std::ofstream out_;
    std::string filepath_;
    std::mutex mutex_;
};

#endif // MINI_REDIS_PERSISTENCE_H
