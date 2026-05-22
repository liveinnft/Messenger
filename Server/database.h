#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <vector>
#include <mutex>
#include <sqlite3.h>

struct Message {
    int id;
    std::string sender;
    std::string receiver;
    std::string message;
    std::string timestamp;
};

class Database {
private:
    sqlite3* db;
    std::mutex dbMutex;

    bool executeSQL(const std::string& sql);
    static int callbackSelectUsers(void* data, int argc, char** argv, char** azColName);
    static int callbackSelectMessages(void* data, int argc, char** argv, char** azColName);

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