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

# ─── Library Target ──────────────────────────────────────────────────────────
# Build the core database logic as a static library so both the server
# executable and the test executable can link against it without
# compiling the same source files twice.

add_library(mini-redis-lib STATIC
    src/database.cpp
)

# Let any target that links mini-redis-lib use #include "database.h"
# by adding the src/ directory to its include search path.
# PUBLIC means this include path is inherited by targets that link us.
target_include_directories(mini-redis-lib PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

# Enable strict compiler warnings on the library as well.
target_compile_options(mini-redis-lib PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# ─── Server Executable ───────────────────────────────────────────────────────
# Create the main server executable.

add_executable(mini-redis
    src/main.cpp
)

# Link the core library so main.cpp can use the Database class.
target_link_libraries(mini-redis PRIVATE mini-redis-lib)

# Enable strict compiler warnings to catch potential bugs early.
#   -Wall      : Enable most common warnings.
#   -Wextra    : Enable additional warnings beyond -Wall.
#   -Wpedantic : Warn about non-standard C++ usage.
# PRIVATE means these flags only apply to this target, not to anything linking against it.
target_compile_options(mini-redis PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# ─── Test Executable ─────────────────────────────────────────────────────────
# Build the test runner as a separate executable.
# Run it after building to verify everything works: ./build/mini-redis-tests.exe

add_executable(mini-redis-tests
    tests/database_test.cpp
)

# Link the core library so tests can use the Database class.
target_link_libraries(mini-redis-tests PRIVATE mini-redis-lib)

target_compile_options(mini-redis-tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
