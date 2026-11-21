// main.cpp — Entry point for the mini-redis server.
//
// Currently, this file demonstrates that the Database class works
// by running a quick smoke test. Later, this will be replaced with
// actual server initialization (Winsock, event loop, etc.).

#include "database.h"

#include <iostream>  // std::cout, std::endl

int main() {
    std::cout << "mini-redis server starting..." << std::endl;
    std::cout << std::endl;

    // Quick demonstration of the Database engine.
    Database db;

    // SET and GET.
    db.set("hello", "world");
    db.set("version", "0.1.0");

    auto val = db.get("hello");
    if (val.has_value()) {
        std::cout << "GET hello → " << val.value() << std::endl;
    }

    // EXISTS check.
    std::cout << "EXISTS hello → " << db.exists({"hello"}) << std::endl;
    std::cout << "EXISTS missing → " << db.exists({"missing"}) << std::endl;

    // Database size.
    std::cout << "Database size: " << db.size() << std::endl;

    std::cout << std::endl;
    std::cout << "Database engine operational. Server not yet implemented." << std::endl;

    return 0;
}
