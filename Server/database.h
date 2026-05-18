#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <vector>
#include <map>
#include <mutex>

struct Message {
    int id;
    std::string sender;
    std::string receiver;
    std::string message;
    std::string timestamp;
};

class Database {
private:
    std::map<std::string, std::string> users;
    std::vector<Message> messages;
    int messageIdCounter;
    std::mutex dbMutex;

public:
    Database();
    ~Database();

    bool init();
    bool registerUser(const std::string& username, const std::string& password);
    bool loginUser(const std::string& username, const std::string& password);
    bool saveMessage(const std::string& sender, const std::string& receiver, const std::string& message);
    std::vector<Message> getMessages(const std::string& user1, const std::string& user2);
    std::vector<std::string> getUsers();
};

#endif 