#include "server.h"
#include "database.h"
#include <iostream>
#include <csignal>

static Server* g_server = nullptr;

void signalHandler(int sig) {
    std::cout << "\nStopping server..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

int main() {
    int port = 8889;

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    Database db;
    if (!db.init()) {
        std::cerr << "Failed to initialize database" << std::endl;
        return 1;
    }

    Server server(port, &db);
    g_server = &server;

    if (!server.start()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }

    std::cout << "Press Ctrl+C to stop server..." << std::endl;
    server.run();

    return 0;
}