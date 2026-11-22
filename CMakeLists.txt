# Minimum CMake version required to build this project.
# We use 3.15 as baseline — it supports all the features we need.
cmake_minimum_required(VERSION 3.15)

# Define the project name, version, language, and a short description.
# CXX tells CMake this is a C++ project.
project(mini-redis
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "A Redis-compatible in-memory key-value server built from scratch in C++"
)

# Use the C++17 standard for all targets in this project.
set(CMAKE_CXX_STANDARD 17)

# Make C++17 mandatory — don't silently fall back to an older standard.
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable compiler-specific extensions (e.g., GNU extensions).
# This ensures our code is portable, standard-compliant C++.
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable strict compiler warnings for ALL targets in this project.
#   -Wall      : Enable most common warnings.
#   -Wextra    : Enable additional warnings beyond -Wall.
#   -Wpedantic : Warn about non-standard C++ usage.
add_compile_options(-Wall -Wextra -Wpedantic)

# ─── Core Library ────────────────────────────────────────────────────────────
# Build the core logic (database + networking + RESP protocol + command dispatcher)
# as a static library so both the server executable and test executables can link
# against it without compiling the same source files twice.

add_library(mini-redis-lib STATIC
    src/database.cpp
    src/server.cpp
    src/resp.cpp
    src/command.cpp
)

# Let any target that links mini-redis-lib use #include "database.h" etc.
# by adding the src/ directory to its include search path.
# PUBLIC means this include path is inherited by targets that link us.
target_include_directories(mini-redis-lib PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

# Link the Winsock2 library (ws2_32) for TCP networking.
# PUBLIC means any target linking mini-redis-lib also gets ws2_32.
target_link_libraries(mini-redis-lib PUBLIC ws2_32)

# ─── Server Executable ───────────────────────────────────────────────────────
# Create the main server executable.

add_executable(mini-redis
    src/main.cpp
)

# Link the core library so main.cpp can use Database, Server, Resp, etc.
target_link_libraries(mini-redis PRIVATE mini-redis-lib)

# ─── Test Executables ────────────────────────────────────────────────────────
# Each test suite is built as a separate executable.
# Run them after building to verify everything works.

# Database unit tests.
add_executable(mini-redis-tests
    tests/database_test.cpp
)
target_link_libraries(mini-redis-tests PRIVATE mini-redis-lib)

# Server (networking & TCP command integration) tests.
add_executable(mini-redis-server-tests
    tests/server_test.cpp
)
target_link_libraries(mini-redis-server-tests PRIVATE mini-redis-lib)

# RESP protocol unit tests.
add_executable(mini-redis-resp-tests
    tests/resp_test.cpp
)
target_link_libraries(mini-redis-resp-tests PRIVATE mini-redis-lib)

# Command dispatcher unit tests.
add_executable(mini-redis-command-tests
    tests/command_test.cpp
)
target_link_libraries(mini-redis-command-tests PRIVATE mini-redis-lib)
