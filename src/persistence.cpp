// persistence.cpp — Implementation of Snapshot and AOF persistence.

#include "persistence.h"
#include "command.h"

#include <algorithm>
#include <cctype>
#include <cstring>    // std::memcmp
#include <iostream>

namespace {

const char SNAPSHOT_MAGIC[8] = {'R', 'E', 'D', 'I', 'S', 'M', 'I', 'N'};
const uint32_t SNAPSHOT_VERSION = 1;

void write_u32(std::ostream& os, uint32_t v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(v));
}

bool read_u32(std::istream& is, uint32_t& v) {
    return static_cast<bool>(is.read(reinterpret_cast<char*>(&v), sizeof(v)));
}

void write_i64(std::ostream& os, int64_t v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(v));
}

bool read_i64(std::istream& is, int64_t& v) {
    return static_cast<bool>(is.read(reinterpret_cast<char*>(&v), sizeof(v)));
}

void write_str(std::ostream& os, const std::string& s) {
    write_u32(os, static_cast<uint32_t>(s.size()));
    if (!s.empty()) {
        os.write(s.data(), s.size());
    }
}

bool read_str(std::istream& is, std::string& s) {
    uint32_t len = 0;
    if (!read_u32(is, len)) return false;
    s.resize(len);
    if (len > 0) {
        if (!is.read(&s[0], len)) return false;
    }
    return true;
}

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

} // anonymous namespace

// ─── SnapshotManager Implementation ─────────────────────────────────────────

bool SnapshotManager::save(const std::string& filepath, const Database& db) {
    std::ofstream os(filepath, std::ios::binary | std::ios::trunc);
    if (!os.is_open()) {
        return false;
    }

    auto entries = db.export_snapshot();

    // 1. Header: Magic (8 bytes) + Version (4 bytes)
    os.write(SNAPSHOT_MAGIC, sizeof(SNAPSHOT_MAGIC));
    write_u32(os, SNAPSHOT_VERSION);

    // 2. Total key count
    write_u32(os, static_cast<uint32_t>(entries.size()));

    // 3. Serialize each entry
    for (const auto& entry : entries) {
        write_str(os, entry.key);
        write_i64(os, entry.ttl_ms);

        if (std::holds_alternative<std::string>(entry.value)) {
            os.put(1); // Type 1: String
            const auto& str = std::get<std::string>(entry.value);
            write_str(os, str);
        } else if (std::holds_alternative<std::deque<std::string>>(entry.value)) {
            os.put(2); // Type 2: List
            const auto& list = std::get<std::deque<std::string>>(entry.value);
            write_u32(os, static_cast<uint32_t>(list.size()));
            for (const auto& item : list) {
                write_str(os, item);
            }
        } else if (std::holds_alternative<std::unordered_map<std::string, std::string>>(entry.value)) {
            os.put(3); // Type 3: Hash
            const auto& map = std::get<std::unordered_map<std::string, std::string>>(entry.value);
            write_u32(os, static_cast<uint32_t>(map.size()));
            for (const auto& [field, val] : map) {
                write_str(os, field);
                write_str(os, val);
            }
        } else if (std::holds_alternative<std::unordered_set<std::string>>(entry.value)) {
            os.put(4); // Type 4: Set
            const auto& set = std::get<std::unordered_set<std::string>>(entry.value);
            write_u32(os, static_cast<uint32_t>(set.size()));
            for (const auto& member : set) {
                write_str(os, member);
            }
        }
    }

    os.flush();
    return os.good();
}

bool SnapshotManager::load(const std::string& filepath, Database& db) {
    std::ifstream is(filepath, std::ios::binary);
    if (!is.is_open()) {
        return false;
    }

    char magic[8];
    if (!is.read(magic, sizeof(magic))) return false;
    if (std::memcmp(magic, SNAPSHOT_MAGIC, sizeof(SNAPSHOT_MAGIC)) != 0) {
        return false; // Invalid file format
    }

    uint32_t version = 0;
    if (!read_u32(is, version) || version != SNAPSHOT_VERSION) {
        return false;
    }

    uint32_t key_count = 0;
    if (!read_u32(is, key_count)) return false;

    std::vector<Database::SnapshotEntry> entries;
    entries.reserve(key_count);

    for (uint32_t i = 0; i < key_count; ++i) {
        Database::SnapshotEntry entry;
        if (!read_str(is, entry.key)) return false;
        if (!read_i64(is, entry.ttl_ms)) return false;

        int type_byte = is.get();
        if (type_byte == std::char_traits<char>::eof()) return false;

        if (type_byte == 1) { // String
            std::string s;
            if (!read_str(is, s)) return false;
            entry.value = std::move(s);
        } else if (type_byte == 2) { // List
            uint32_t count = 0;
            if (!read_u32(is, count)) return false;
            std::deque<std::string> list;
            for (uint32_t j = 0; j < count; ++j) {
                std::string item;
                if (!read_str(is, item)) return false;
                list.push_back(std::move(item));
            }
            entry.value = std::move(list);
        } else if (type_byte == 3) { // Hash
            uint32_t count = 0;
            if (!read_u32(is, count)) return false;
            std::unordered_map<std::string, std::string> map;
            for (uint32_t j = 0; j < count; ++j) {
                std::string f, v;
                if (!read_str(is, f) || !read_str(is, v)) return false;
                map.emplace(std::move(f), std::move(v));
            }
            entry.value = std::move(map);
        } else if (type_byte == 4) { // Set
            uint32_t count = 0;
            if (!read_u32(is, count)) return false;
            std::unordered_set<std::string> set;
            for (uint32_t j = 0; j < count; ++j) {
                std::string m;
                if (!read_str(is, m)) return false;
                set.emplace(std::move(m));
            }
            entry.value = std::move(set);
        } else {
            return false; // Unknown type byte
        }

        entries.push_back(std::move(entry));
    }

    db.import_snapshot(entries);
    return true;
}

// ─── AofManager Implementation ──────────────────────────────────────────────

AofManager::AofManager() = default;

AofManager::~AofManager() {
    close();
}

bool AofManager::open(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mutex_);
    filepath_ = filepath;
    out_.open(filepath, std::ios::binary | std::ios::app);
    return out_.is_open();
}

void AofManager::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (out_.is_open()) {
        out_.flush();
        out_.close();
    }
}

bool AofManager::is_open() const {
    return out_.is_open();
}

void AofManager::append(const RespValue& command_value) {
    std::vector<std::string> args;
    if (!command_value.to_string_vector(args) || args.empty()) {
        return;
    }

    if (!is_mutating(args[0])) {
        return; // Don't log read-only commands
    }

    std::string wire = command_value.serialize();

    std::lock_guard<std::mutex> lock(mutex_);
    if (out_.is_open()) {
        out_.write(wire.data(), wire.size());
        out_.flush();
    }
}

void AofManager::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (out_.is_open()) {
        out_.flush();
    }
}

bool AofManager::is_mutating(const std::string& cmd_name) {
    std::string u = to_upper(cmd_name);
    return (u == "SET" || u == "DEL" || u == "FLUSHDB" ||
            u == "EXPIRE" || u == "PEXPIRE" ||
            u == "LPUSH" || u == "RPUSH" || u == "LPOP" || u == "RPOP" ||
            u == "HSET" || u == "HDEL" ||
            u == "SADD" || u == "SREM");
}

bool AofManager::replay(const std::string& filepath, Database& db) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    RespParser parser;
    char buffer[4096];

    while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
        parser.append(buffer, static_cast<std::size_t>(in.gcount()));

        RespValue cmd;
        while (true) {
            ParseStatus status = parser.parse_next(cmd);
            if (status == ParseStatus::Ok) {
                CommandDispatcher::execute(cmd, db);
            } else if (status == ParseStatus::Incomplete) {
                break;
            } else {
                return false; // Corruption in AOF
            }
        }
    }

    return true;
}

bool AofManager::rewrite(const std::string& filepath, const Database& db) {
    std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    auto entries = db.export_snapshot();

    for (const auto& entry : entries) {
        if (std::holds_alternative<std::string>(entry.value)) {
            // SET key value [PX ttl_ms]
            std::vector<RespValue> cmd = {
                RespValue::make_bulk_string("SET"),
                RespValue::make_bulk_string(entry.key),
                RespValue::make_bulk_string(std::get<std::string>(entry.value))
            };
            if (entry.ttl_ms > 0) {
                cmd.push_back(RespValue::make_bulk_string("PX"));
                cmd.push_back(RespValue::make_bulk_string(std::to_string(entry.ttl_ms)));
            }
            std::string wire = RespValue::make_array(cmd).serialize();
            out.write(wire.data(), wire.size());
        } else if (std::holds_alternative<std::deque<std::string>>(entry.value)) {
            // RPUSH key elem1 elem2 ...
            const auto& list = std::get<std::deque<std::string>>(entry.value);
            if (!list.empty()) {
                std::vector<RespValue> cmd = {
                    RespValue::make_bulk_string("RPUSH"),
                    RespValue::make_bulk_string(entry.key)
                };
                for (const auto& item : list) {
                    cmd.push_back(RespValue::make_bulk_string(item));
                }
                std::string wire = RespValue::make_array(cmd).serialize();
                out.write(wire.data(), wire.size());
            }
            if (entry.ttl_ms > 0) {
                std::vector<RespValue> exp_cmd = {
                    RespValue::make_bulk_string("PEXPIRE"),
                    RespValue::make_bulk_string(entry.key),
                    RespValue::make_bulk_string(std::to_string(entry.ttl_ms))
                };
                std::string exp_wire = RespValue::make_array(exp_cmd).serialize();
                out.write(exp_wire.data(), exp_wire.size());
            }
        } else if (std::holds_alternative<std::unordered_map<std::string, std::string>>(entry.value)) {
            // HSET key f1 v1 f2 v2 ...
            const auto& map = std::get<std::unordered_map<std::string, std::string>>(entry.value);
            if (!map.empty()) {
                std::vector<RespValue> cmd = {
                    RespValue::make_bulk_string("HSET"),
                    RespValue::make_bulk_string(entry.key)
                };
                for (const auto& [f, v] : map) {
                    cmd.push_back(RespValue::make_bulk_string(f));
                    cmd.push_back(RespValue::make_bulk_string(v));
                }
                std::string wire = RespValue::make_array(cmd).serialize();
                out.write(wire.data(), wire.size());
            }
            if (entry.ttl_ms > 0) {
                std::vector<RespValue> exp_cmd = {
                    RespValue::make_bulk_string("PEXPIRE"),
                    RespValue::make_bulk_string(entry.key),
                    RespValue::make_bulk_string(std::to_string(entry.ttl_ms))
                };
                std::string exp_wire = RespValue::make_array(exp_cmd).serialize();
                out.write(exp_wire.data(), exp_wire.size());
            }
        } else if (std::holds_alternative<std::unordered_set<std::string>>(entry.value)) {
            // SADD key m1 m2 ...
            const auto& set = std::get<std::unordered_set<std::string>>(entry.value);
            if (!set.empty()) {
                std::vector<RespValue> cmd = {
                    RespValue::make_bulk_string("SADD"),
                    RespValue::make_bulk_string(entry.key)
                };
                for (const auto& m : set) {
                    cmd.push_back(RespValue::make_bulk_string(m));
                }
                std::string wire = RespValue::make_array(cmd).serialize();
                out.write(wire.data(), wire.size());
            }
            if (entry.ttl_ms > 0) {
                std::vector<RespValue> exp_cmd = {
                    RespValue::make_bulk_string("PEXPIRE"),
                    RespValue::make_bulk_string(entry.key),
                    RespValue::make_bulk_string(std::to_string(entry.ttl_ms))
                };
                std::string exp_wire = RespValue::make_array(exp_cmd).serialize();
                out.write(exp_wire.data(), exp_wire.size());
            }
        }
    }

    out.flush();
    return out.good();
}
