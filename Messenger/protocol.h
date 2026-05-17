#pragma once

#include <string>
#include <vector>

namespace Protocol {
    const std::string DELIMITER = "|";

    enum MessageType {
        REGISTER,
        LOGIN,
        MESSAGE,
        SEND_MSG = MESSAGE, // Добавлено для совместимости с Messenger.cpp
        GET_USERS,
        GET_MESSAGES,
        RESPONSE_OK,
        RESPONSE_ERROR,
        USER_LIST,
        MESSAGE_LIST,
        NEW_MESSAGE
    };

    inline std::string typeToString(MessageType type) {
        switch (type) {
        case REGISTER: return "REGISTER";
        case LOGIN: return "LOGIN";
        case MESSAGE: return "MSG";
        case GET_USERS: return "GET_USERS";
        case GET_MESSAGES: return "GET_MESSAGES";
        case RESPONSE_OK: return "OK";
        case RESPONSE_ERROR: return "ERROR";
        case USER_LIST: return "USER_LIST";
        case MESSAGE_LIST: return "MESSAGE_LIST";
        case NEW_MESSAGE: return "NEW_MESSAGE";
        default: return "UNKNOWN";
        }
    }

    inline MessageType stringToType(const std::string& str) {
        if (str == "REGISTER") return REGISTER;
        if (str == "LOGIN") return LOGIN;
        if (str == "MSG") return MESSAGE;
        if (str == "GET_USERS") return GET_USERS;
        if (str == "GET_MESSAGES") return GET_MESSAGES;
        if (str == "OK") return RESPONSE_OK;
        if (str == "ERROR") return RESPONSE_ERROR;
        if (str == "USER_LIST") return USER_LIST;
        if (str == "MESSAGE_LIST") return MESSAGE_LIST;
        if (str == "NEW_MESSAGE") return NEW_MESSAGE;
        return RESPONSE_ERROR;
    }

    inline std::vector<std::string> split(const std::string& str, const std::string& delim) {
        std::vector<std::string> tokens;
        size_t start = 0;
        size_t end = str.find(delim);
        while (end != std::string::npos) {
            tokens.push_back(str.substr(start, end - start));
            start = end + delim.length();
            end = str.find(delim, start);
        }
        tokens.push_back(str.substr(start));
        return tokens;
    }

    inline std::string createMessage(MessageType type, const std::vector<std::string>& data) {
        std::string msg = typeToString(type);
        for (const auto& item : data) {
            msg += DELIMITER + item;
        }
        msg += "\n";
        return msg;
    }
}