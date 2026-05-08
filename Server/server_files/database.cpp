#include "database.h"
#include <iostream>
#include <ctime>

Database::Database() : messageIdCounter(1) {}

Database::~Database() {}

bool Database::init() {
    std::cout << "Database initialized (in-memory)" << std::endl;
    return true;
}

bool Database::registerUser(const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (users.find(username) != users.end()) {
        return false;
    }
    users[username] = password;
    std::cout << "User registered: " << username << std::endl;
    return true;
}

bool Database::loginUser(const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> lock(dbMutex);
    auto it = users.find(username);
    if (it != users.end() && it->second == password) {
        std::cout << "User logged in: " << username << std::endl;
        return true;
    }
    return false;
}

bool Database::saveMessage(const std::string& sender, const std::string& receiver, const std::string& message) {
    std::lock_guard<std::mutex> lock(dbMutex);
    Message msg;
    msg.id = messageIdCounter++;
    msg.sender = sender;
    msg.receiver = receiver;
    msg.message = message;

    time_t now = time(0);
    char buf[80];
    struct tm* timeinfo = localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);
    msg.timestamp = buf;

    messages.push_back(msg);
    std::cout << "Message saved: " << sender << " -> " << receiver << std::endl;
    return true;
}

std::vector<Message> Database::getMessages(const std::string& user1, const std::string& user2) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<Message> result;
    for (const auto& msg : messages) {
        if ((msg.sender == user1 && msg.receiver == user2) ||
            (msg.sender == user2 && msg.receiver == user1)) {
            result.push_back(msg);
        }
    }
    return result;
}

std::vector<std::string> Database::getUsers() {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<std::string> userList;
    for (const auto& pair : users) {
        userList.push_back(pair.first);
    }
    return userList;
}