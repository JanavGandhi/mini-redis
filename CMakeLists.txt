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

# Create an executable target named "mini-redis" from the listed source files.
# As the project grows, we will add more .cpp files here.
add_executable(mini-redis
    src/main.cpp
)

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
