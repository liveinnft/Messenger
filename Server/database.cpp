#include "database.h"
#include <iostream>
#include <ctime>
#include <sstream>

Database::Database() : db(nullptr) {}

Database::~Database() {
    if (db) sqlite3_close(db);
}

bool Database::executeSQL(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool Database::init() {
    std::lock_guard<std::mutex> lock(dbMutex);
    int rc = sqlite3_open("messenger.db", &db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    std::string sqlUsers = 
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "username TEXT UNIQUE NOT NULL, "
        "password TEXT NOT NULL);";
    if (!executeSQL(sqlUsers)) return false;

    std::string sqlMessages = 
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "sender TEXT NOT NULL, "
        "receiver TEXT NOT NULL, "
        "message TEXT NOT NULL, "
        "timestamp TEXT NOT NULL);";
    if (!executeSQL(sqlMessages)) return false;

    std::cout << "Database initialized (SQLite) - messenger.db" << std::endl;
    return true;
}

bool Database::registerUser(const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string sql = "INSERT INTO users (username, password) VALUES ('" + username + "', '" + password + "');";
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Register error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    std::cout << "User registered: " << username << std::endl;
    return true;
}

bool Database::loginUser(const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string sql = "SELECT * FROM users WHERE username = '" + username + "' AND password = '" + password + "';";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Login prepare error" << std::endl;
        return false;
    }
    bool found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    if (found) {
        std::cout << "User logged in: " << username << std::endl;
        return true;
    }
    return false;
}

bool Database::saveMessage(const std::string& sender, const std::string& receiver, const std::string& message) {
    std::lock_guard<std::mutex> lock(dbMutex);
    time_t now = time(0);
    char buf[80];
    struct tm* timeinfo = localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);
    std::string timestamp(buf);
    std::string sql = "INSERT INTO messages (sender, receiver, message, timestamp) VALUES ('" 
                      + sender + "', '" + receiver + "', '" + message + "', '" + timestamp + "');";
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Save message error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    std::cout << "Message saved: " << sender << " -> " << receiver << std::endl;
    return true;
}

std::vector<Message> Database::getMessages(const std::string& user1, const std::string& user2) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<Message> result;
    std::string sql = "SELECT id, sender, receiver, message, timestamp FROM messages WHERE (sender = '" 
                      + user1 + "' AND receiver = '" + user2 + "') OR (sender = '" + user2 
                      + "' AND receiver = '" + user1 + "') ORDER BY id;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Message msg;
        msg.id = sqlite3_column_int(stmt, 0);
        msg.sender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        msg.receiver = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        msg.message = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        msg.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        result.push_back(msg);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<std::string> Database::getUsers() {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<std::string> userList;
    std::string sql = "SELECT username FROM users;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return userList;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        userList.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
    return userList;
}