# mini-redis

A Redis-compatible, in-memory key-value server built from scratch in C++17.

This is a learning project — every component (networking, protocol parsing, data storage, persistence) is implemented from the ground up without using any existing Redis libraries.

## Current Status

🚧 **Milestone 0: Environment & Project Setup** — Build toolchain verified, project structure created.

## Prerequisites

| Tool | Minimum Version | Purpose |
|------|----------------|---------|
| [MSYS2](https://www.msys2.org/) | — | Provides GCC and Unix tools on Windows |
| GCC (g++) | 14.x+ | C++17 compiler |
| [CMake](https://cmake.org/) | 3.15+ | Build system |
| Git | 2.x+ | Version control |
| GDB | 15.x+ | Debugger (optional) |

All tools are installed through the MSYS2 UCRT64 environment.

## Build & Run

```bash
# 1. Configure the build (run once, or after changing CMakeLists.txt)
cmake -S . -B build -G "MinGW Makefiles"

# 2. Compile
cmake --build build

# 3. Run
./build/mini-redis.exe
```

## Project Architecture

The server is designed as a set of modular components:

```
Client ──→ TCP Networking ──→ RESP Parser ──→ Command Dispatcher ──→ Database Engine
                                                                          │
Client ←── TCP Networking ←── Response Encoder ←──────────────────────────┘
```

| Component | Responsibility |
|-----------|---------------|
| TCP Networking | Accept connections, send/receive bytes over the network |
| RESP Parser | Decode Redis protocol messages into commands |
| Command Dispatcher | Route commands to the correct handler |
| Database Engine | Store and retrieve key-value data in memory |
| Response Encoder | Format results back into Redis protocol |
| Expiration System | Track and enforce key TTLs |
| Persistence | Save/restore data to/from disk |

## Roadmap

- [x] Milestone 0: Environment & project setup
- [ ] Milestone 1: In-memory database (SET, GET, DEL, EXISTS, FLUSHDB)
- [ ] Milestone 2: TCP networking with Winsock2
- [ ] Milestone 3: RESP protocol parser & encoder
- [ ] Milestone 4: Core Redis command compatibility
- [ ] Milestone 5: Additional data structures (lists, hashes, sets)
- [ ] Milestone 6: Key expiration & TTL
- [ ] Milestone 7: Persistence (snapshots, append-only log)
- [ ] Milestone 8: Multiple clients & concurrency
- [ ] Milestone 9: Reliability & performance

## Technology Stack

- **Language:** C++17
- **Compiler:** GCC (MSYS2 UCRT64)
- **Build System:** CMake
- **Networking:** Winsock2 (native Windows TCP)
- **OS:** Windows

## License

This project is for educational purposes.