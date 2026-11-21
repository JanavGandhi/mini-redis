# Changelog

All notable changes to the mini-redis project will be documented in this file.

## [0.2.0] - 2026-11-21

### Added
- `Database` class (`src/database.h`, `src/database.cpp`) — in-memory key-value store
- SET, GET, DEL, EXISTS, FLUSHDB operations with full Redis-compatible behavior
- Comprehensive unit test suite (`tests/database_test.cpp`) — 22 tests covering
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
