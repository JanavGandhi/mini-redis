# Changelog

All notable changes to the mini-redis project will be documented in this file.

## [0.9.0] - 2026-11-22

### Added
- Non-blocking I/O multiplexed event loop architecture (`Milestone 8`):
  - Converted sockets to non-blocking mode with `ioctlsocket(FIONBIO)`
  - Single-threaded event multiplexing loop via `select()` with `FD_SETSIZE 256`
  - Per-client state context with isolated `RespParser` streaming buffer and send queue
  - Concurrent client support: handles dozens of simultaneous clients without thread contention or blocking
  - Resilient partial input handling: streaming parser maintains state across fragmented packets per client
  - Graceful and abrupt disconnection handling without affecting other active clients
  - Low-latency TCP responses with `TCP_NODELAY`
- Expanded Server integration test suite (`tests/server_test.cpp`) to 10 tests:
  - Added concurrent multi-client stress test with 8 simultaneous worker threads
  - Added interleaved partial input test verifying non-blocking isolation between clients
  - Added abrupt client disconnection test verifying stability of remaining connections

## [0.8.0] - 2026-11-22

### Added
- Snapshot (RDB) persistence engine (`SnapshotManager` in `src/persistence.h`, `src/persistence.cpp`):
  - Binary serialization format with magic header, versioning, and length prefixes
  - Complete support for Strings, Lists, Hashes, Sets, and remaining TTLs
  - Binary-safe storage handling embedded null bytes and CRLF
- Append-Only File (`AofManager` in `src/persistence.h`, `src/persistence.cpp`):
  - Streams mutating commands to disk in standard RESP wire format
  - Crash recovery replaying AOF logs on server startup
  - AOF rewrite / compaction compressing state into minimal equivalent commands
- Persistence commands:
  - `SAVE`: Synchronously writes snapshot to `dump.rdb`
  - `BGSAVE`: Asynchronously saves snapshot in a detached background thread
  - `LASTSAVE`: Returns UNIX timestamp of the last successful save
- Persistence unit test suite (`tests/persistence_test.cpp`) — 6 tests covering
  RDB save/load, binary safety, corrupt file rejection, AOF replay recovery, AOF compaction, and commands
- Added `mini-redis-persistence-tests` target in `CMakeLists.txt`

## [0.7.0] - 2026-11-22

### Added
- Expiration & TTL tracking system (`Milestone 6`):
  - `EXPIRE key seconds` and `PEXPIRE key milliseconds`
  - `TTL key` (returns seconds remaining, -1 if no TTL, -2 if missing/expired)
  - `PTTL key` (returns milliseconds remaining)
  - `SET key value [EX seconds | PX milliseconds]`
- Dual expiration engine:
  - **Lazy expiration**: automatic check & purge on any key read or write access
  - **Active expiration**: `purge_expired()` sweeps and frees expired keys
- Unit tests:
  - `tests/database_test.cpp`: expanded to 33 unit tests (added tests for TTL calculation, lazy expiration, non-positive TTL immediate cleanup, and active purging)
  - `tests/command_test.cpp`: expanded to 13 unit test cases (added tests for `EXPIRE`, `PEXPIRE`, `TTL`, `PTTL`, and `SET` with `EX`/`PX`)
  - `tests/server_test.cpp`: expanded to 7 integration test cases including TCP live TTL & expiration testing

## [0.6.0] - 2026-11-22

### Added
- Extended `Database` engine with C++17 `std::variant` supporting multiple data structures:
  - **Lists** (`std::deque<std::string>`): `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LRANGE`, `LLEN`
  - **Hashes** (`std::unordered_map<std::string, std::string>`): `HSET`, `HGET`, `HDEL`, `HEXISTS`, `HGETALL`
  - **Sets** (`std::unordered_set<std::string>`): `SADD`, `SREM`, `SISMEMBER`, `SMEMBERS`, `SCARD`
- Redis `WRONGTYPE` error handling when operations target mismatched data types
- Negative index and bounds clamping for `LRANGE` (matching official Redis behavior)
- Automatic key cleanup when Lists, Hashes, or Sets become empty
- Multi-argument support for `LPUSH`, `RPUSH`, `HSET`, `HDEL`, `SADD`, `SREM`, `DEL`, `EXISTS`
- `DEL`, `EXISTS`, and `FLUSHDB` support across all data types
- Database test suite expanded to 28 unit tests
- Command Dispatcher test suite expanded to 12 unit test cases covering all new data structure commands and WRONGTYPE errors
- Server integration test suite updated with multi-type TCP test cases

## [0.5.0] - 2026-11-22

### Added
- `CommandDispatcher` class (`src/command.h`, `src/command.cpp`):
  - Routes and executes Redis commands against the `Database`
  - Case-insensitive command parsing (e.g. `ping`, `PING`, `pInG`)
  - Argument count validation and standard Redis error messages
  - Core commands: `PING`, `ECHO`, `SET`, `GET`, `DEL`, `EXISTS`, `FLUSHDB`
  - `COMMAND` handler returning empty array for `redis-cli` compatibility
- Integrated full end-to-end pipeline: TCP Server ──→ RESP Parser ──→ Command Dispatcher ──→ Database Engine
- Full pipelining support over TCP connections
- Command Dispatcher unit tests (`tests/command_test.cpp`) — 18 unit tests
- TCP Server integration tests (`tests/server_test.cpp`) updated with live command execution over TCP sockets
- Added `mini-redis-command-tests` target in `CMakeLists.txt`

### Changed
- `main.cpp` updated with server banner, supported command list, and connection instructions

## [0.4.0] - 2026-11-22

### Added
- `RespValue`, `RespEncoder`, and `RespParser` classes (`src/resp.h`, `src/resp.cpp`)
- Full RESP2 protocol parsing and encoding:
  - Simple Strings (`+`)
  - Simple Errors (`-`)
  - Integers (`:`)
  - Bulk Strings (`$`) including empty, null (`$-1\r\n`), and binary payloads with embedded CRLF
  - Arrays (`*`) including empty (`*0\r\n`), null (`*-1\r\n`), and arbitrarily nested arrays
- Streaming parser with `append()` and `parse_next()` handling fragmented TCP input
- Helper `to_string_vector()` to convert command arrays to string arguments for dispatching
- Validation rejecting malformed lengths, counts, characters, and prefixes
- Comprehensive RESP unit test suite (`tests/resp_test.cpp`) — 28 unit tests covering
  all 5 data types, byte-by-byte streaming fragmentation, pipelined batches, encoding, and roundtrip serialization
- Added `mini-redis-resp-tests` executable target in `CMakeLists.txt`

## [0.3.0] - 2026-11-21

### Added
- `Server` class (`src/server.h`, `src/server.cpp`) — TCP echo server using Winsock2
- Winsock2 initialization, socket creation, binding, and listening
- Accept loop handling one client at a time
- Echo behavior: receives data from client and sends it back
- `send_all()` helper for reliable multi-part TCP sends
- Graceful shutdown via Ctrl+C signal handler in `main.cpp`
- Thread-safe `stop()` method using `std::atomic` for safe cross-thread shutdown
- Server test suite (`tests/server_test.cpp`) — 6 tests covering lifecycle,
  client connections, echo behavior, reconnection, and large messages
- Winsock2 library linking (`ws2_32`) in CMake

### Changed
- `main.cpp` updated to initialize and run the TCP server
- `CMakeLists.txt` restructured: project-wide warnings, Winsock2 linking,
  added `server.cpp` to library and `mini-redis-server-tests` executable

## [0.2.0] - 2026-11-21

### Added
- `Database` class (`src/database.h`, `src/database.cpp`) — in-memory key-value store
- SET, GET, DEL, EXISTS, FLUSHDB operations with full Redis-compatible behavior
- Comprehensive unit test suite (`tests/database_test.cpp`) — 21 tests covering
  basic operations, edge cases, empty inputs, special characters, and mixed workflows
- Static library target (`mini-redis-lib`) in CMake for shared compilation
- Test executable target (`mini-redis-tests`) in CMake

### Changed
- `main.cpp` updated to demonstrate Database engine usage
- `CMakeLists.txt` restructured with library, server, and test targets

## [0.1.0] - 2026-10-10

### Added
- Initial project structure with CMake build system
- `.gitignore` for build artifacts, IDE files, and OS-generated files
- `CMakeLists.txt` configured for C++17 with strict compiler warnings
- Minimal `src/main.cpp` to verify the build toolchain
- `README.md` with project overview, build instructions, architecture, and roadmap
- `PROJECT_GUIDE.md` with development plan, coding standards, and design decisions
