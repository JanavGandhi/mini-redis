# Changelog

All notable changes to the mini-redis project will be documented in this file.

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
