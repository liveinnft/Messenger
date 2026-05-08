#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "database.h"

class Server {
private:
    int serverSocket;
    int port;
    Database* db;
    std::map<int, std::string> clients;
    std::mutex clientsMutex;
    bool running;

    void handleClient(int clientSocket);
    void processMessage(int clientSocket, const std::string& message);
    void broadcastToUser(const std::string& username, const std::string& message);

public:
    Server(int port, Database* database);
    ~Server();

    bool start();
    void stop();
    void run();
};

#endif