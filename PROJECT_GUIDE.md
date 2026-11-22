# PROJECT GUIDE — mini-redis

This is the living project document for mini-redis. It tracks the current state,
design decisions, roadmap progress, and development workflow.

---

## Developer Profile

- **Experience level:** C++ beginner. Knows basic syntax and OOP.
- **Learning goals:** Systems programming, networking, protocol design, memory management.
- **Teaching preference:** Explain everything before writing code. One small step at a time.

## Project Overview

A Redis-compatible, in-memory key-value server built from scratch in C++17.
No external Redis libraries are used — every component is implemented manually.

## Development Environment

| Tool | Version | Notes |
|------|---------|-------|
| OS | Windows | — |
| IDE | Google Antigravity IDE | — |
| Compiler | GCC 14.2.0 (MSYS2 UCRT64) | C++17 standard |
| Build System | CMake 4.4.4 | MinGW Makefiles generator |
| Debugger | GDB 15.2 | — |
| Version Control | Git 2.49.0 | — |
| Remote Repo | GitHub | https://github.com/JanavGandhi/mini-redis |

## Architecture

```
Client ──→ TCP Networking ──→ RESP Parser ──→ Command Dispatcher ──→ Database Engine
                                                                          │
Client ←── TCP Networking ←── Response Encoder ←──────────────────────────┘
```

### Components (to be built in order)

1. **Database Engine** — In-memory key-value store using STL containers.
2. **TCP Networking** — Winsock2-based server accepting client connections.
3. **RESP Parser** — Decodes Redis protocol messages into commands.
4. **Command Dispatcher** — Routes parsed commands to handler functions.
5. **Response Encoder** — Formats results into valid RESP responses.
6. **Expiration System** — TTL tracking and key expiration.
7. **Persistence** — Snapshot saving/loading and append-only log.
8. **Concurrency** — Non-blocking I/O for multiple clients.

## Development Roadmap

### Milestone 0: Environment & Project Setup ✅
- [x] Verify GCC, CMake, Git, GDB installation
- [x] Create project directory structure
- [x] Create .gitignore
- [x] Create CMakeLists.txt with C++17 and warnings enabled
- [x] Create minimal main.cpp
- [x] Compile and run successfully
- [x] Connect local repo to GitHub remote
- [x] Update README.md
- [x] Create PROJECT_GUIDE.md
- [x] Create CHANGELOG.md
- [x] Commit and push all initial files to GitHub

### Milestone 1: In-memory Database ✅
- [x] Design key-value store using std::unordered_map
- [x] Implement SET command
- [x] Implement GET command
- [x] Implement DEL command
- [x] Implement EXISTS command
- [x] Implement FLUSHDB command
- [x] Write unit tests for all operations

### Milestone 2: TCP Networking (Winsock2) ✅
- [x] Initialize Winsock
- [x] Create listening socket on port 6379
- [x] Accept single client connection
- [x] Send and receive raw bytes
- [x] Proper socket cleanup and error handling

### Milestone 3: RESP Protocol ✅
- [x] Parse RESP arrays and bulk strings
- [x] Handle fragmented TCP input
- [x] Encode responses (simple strings, errors, integers, bulk strings, arrays)
- [x] Validate and reject malformed input

### Milestone 4: Core Command Compatibility ✅
- [x] Integrate networking + parsing + database
- [x] PING, ECHO, SET, GET, DEL, EXISTS, FLUSHDB
- [x] Test with redis-cli or custom client

### Milestone 5: Additional Data Structures ✅
- [x] Lists (LPUSH, RPUSH, LPOP, RPOP, LRANGE, LLEN)
- [x] Hashes (HSET, HGET, HDEL, HGETALL, HEXISTS)
- [x] Sets (SADD, SREM, SISMEMBER, SMEMBERS, SCARD)

### Milestone 6: Expiration & TTL ✅
- [x] EXPIRE, TTL, PTTL
- [x] SET with EX/PX options
- [x] Lazy and periodic expiration

### Milestone 7: Persistence ✅
- [x] Snapshot save/load
- [x] Append-only log
- [x] Crash recovery

### Milestone 8: Multiple Clients ✅
- [x] Non-blocking I/O
- [x] Event loop (select/WSAPoll)
- [x] Handle disconnections and partial input

### Milestone 9: Reliability & Performance ✅
- [x] Automated test suite (99 tests across 6 comprehensive test suites)
- [x] Malformed input handling (bounds enforcement on lengths, prefix validation, random byte fuzzing)
- [x] Benchmarking (built-in mini-redis-benchmark measuring ops/sec and latency distributions)
- [x] Resource limits (max clients, max buffer size, max keys OOM enforcement, INFO command)

## Coding Standards

- Use clear, descriptive names for all identifiers.
- Prefer small functions with single responsibilities.
- Add comments explaining what each section does.
- Use C++17 features where appropriate.
- Enable and fix all compiler warnings.
- Use RAII for resource management.
- Validate all external input.
- Handle errors explicitly — no silent failures.
- Test edge cases, not just the happy path.

## Git Workflow

- One logical change per commit.
- Descriptive commit messages using conventional format (feat:, fix:, docs:, test:, chore:).
- Never commit build artifacts, credentials, or IDE-specific files.
- Always verify compilation before committing.
- User runs all git commit/push commands manually.

## Known Bugs & Limitations

None. All 9 milestones have been implemented and verified with 100% test pass rate.

## Design Decisions Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-10-10 | Use std::unordered_map for key-value store | O(1) average lookup, simple API, good starting point |
| 2026-10-10 | Use Winsock2 for networking | Native Windows TCP — no external dependencies |
| 2026-10-10 | CMake with MinGW Makefiles | Compatible with MSYS2 UCRT64 GCC toolchain |
| 2026-11-22 | Non-blocking I/O with select() | Scalable single-threaded event loop multiplexing without threads |
| 2026-11-22 | Resource limits & bounded buffers | Protect server against DoS and memory exhaustion |

## Latest Verified State

- **Last verified commit:** Pending — Milestone 9 files ready for commit.
- **Build status:** ✅ Compiles with zero warnings. All 99 tests pass (33 database + 28 RESP + 13 command + 10 server + 6 persistence + 9 reliability).
- **Benchmark:** ~70,000 ops/sec on PING, ~60,000 ops/sec on SET/GET/LPUSH/LPOP with sub-millisecond latencies.
- **Next task:** Commit Milestone 9 to GitHub.
