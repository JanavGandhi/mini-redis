// main.cpp — Entry point for the mini-redis server.
//
// For now, this simply prints a message to verify that the
// build toolchain (GCC, CMake) is set up correctly.
// Later, this file will initialize and start the server.

#include <iostream>  // Provides std::cout and std::endl for console output.

int main() {
    // Print a success message to confirm the project compiles and runs.
    std::cout << "mini-redis server - build successful!" << std::endl;

    // Return 0 to the operating system, meaning "program finished successfully."
    return 0;
}
