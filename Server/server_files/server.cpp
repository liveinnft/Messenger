#include "server.h"
#include "protocol.h"
#include <iostream>
#include <thread>
#include <cstring>
#include <arpa/inet.h>

Server::Server(int port, Database* database)
    : serverSocket(-1), port(port), db(database), running(false) {
}

Server::~Server() {
    stop();
}

bool Server::start() {
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }

    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        return false;
    }

    if (listen(serverSocket, 10) < 0) {
        std::cerr << "Listen failed" << std::endl;
        return false;
    }

    running = true;
    std::cout << "Server started on port " << port << std::endl;
    return true;
}

void Server::stop() {
    running = false;
    if (serverSocket >= 0) {
        close(serverSocket);
        serverSocket = -1;
    }
}

void Server::run() {
    while (running) {
        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientLen);

        if (clientSocket < 0) {
            if (running) {
                std::cerr << "Accept failed" << std::endl;
            }
            continue;
        }

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));
        std::cout << "New client connected from " << clientIP << std::endl;

        std::thread(&Server::handleClient, this, clientSocket).detach();
    }
}

void Server::handleClient(int clientSocket) {
    char buffer[4096];

    while (running) {
        memset(buffer, 0, sizeof(buffer));
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

        if (bytesRead <= 0) {
            break;
        }

        std::string message(buffer, bytesRead);
        processMessage(clientSocket, message);
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        auto it = clients.find(clientSocket);
        if (it != clients.end()) {
            std::cout << "Client disconnected: " << it->second << std::endl;
            clients.erase(it);
        }
    }

    close(clientSocket);
}

void Server::processMessage(int clientSocket, const std::string& message) {
    auto parts = Protocol::split(message, Protocol::DELIMITER);
    if (parts.empty()) return;

    Protocol::MessageType type = Protocol::stringToType(parts[0]);

    switch (type) {
    case Protocol::REGISTER: {
        if (parts.size() >= 3) {
            bool success = db->registerUser(parts[1], parts[2]);
            std::string response = success ?
                Protocol::createMessage(Protocol::RESPONSE_OK, {}) :
                Protocol::createMessage(Protocol::RESPONSE_ERROR, { "User already exists" });
            send(clientSocket, response.c_str(), (int)response.length(), 0);
        }
        break;
    }

    case Protocol::LOGIN: {
        if (parts.size() >= 3) {
            bool success = db->loginUser(parts[1], parts[2]);
            if (success) {
                std::lock_guard<std::mutex> lock(clientsMutex);
                clients[clientSocket] = parts[1];
                std::string response = Protocol::createMessage(Protocol::RESPONSE_OK, {});
                send(clientSocket, response.c_str(), (int)response.length(), 0);
            }
            else {
                std::string response = Protocol::createMessage(Protocol::RESPONSE_ERROR, { "Wrong username or password" });
                send(clientSocket, response.c_str(), (int)response.length(), 0);
            }
        }
        break;
    }

    case Protocol::GET_USERS: {
        auto users = db->getUsers();
        std::string response = Protocol::createMessage(Protocol::USER_LIST, users);
        send(clientSocket, response.c_str(), (int)response.length(), 0);
        break;
    }

    case Protocol::GET_MESSAGES: {
        if (parts.size() >= 3) {
            auto messages = db->getMessages(parts[1], parts[2]);
            std::vector<std::string> msgData;
            for (const auto& msg : messages) {
                msgData.push_back(msg.sender);
                msgData.push_back(msg.receiver);
                msgData.push_back(msg.message);
                msgData.push_back(msg.timestamp);
            }
            std::string response = Protocol::createMessage(Protocol::MESSAGE_LIST, msgData);
            send(clientSocket, response.c_str(), (int)response.length(), 0);
        }
        break;
    }

    case Protocol::MSG: {
        if (parts.size() >= 4) {
            db->saveMessage(parts[1], parts[2], parts[3]);
            std::string response = Protocol::createMessage(Protocol::RESPONSE_OK, {});
            send(clientSocket, response.c_str(), (int)response.length(), 0);

            std::string notification = Protocol::createMessage(Protocol::NEW_MESSAGE,
                { parts[1], parts[2], parts[3] });
            broadcastToUser(parts[2], notification);
        }
        break;
    }

    default:
        break;
    }
}

void Server::broadcastToUser(const std::string& username, const std::string& message) {
    std::lock_guard<std::mutex> lock(clientsMutex);
    for (const auto& client : clients) {
        if (client.second == username) {
            send(client.first, message.c_str(), (int)message.length(), 0);
            break;
        }
    }
}