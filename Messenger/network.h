#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

class NetworkClient {
private:
    SOCKET sock;
    std::atomic<bool> connected;
    std::thread recvThread;
    std::function<void(const std::string&)> onMessageCallback;

    void receiveLoop();

public:
    NetworkClient();
    ~NetworkClient();

    bool connectToServer(const std::string& host, int port);
    void disconnect();
    bool sendMessage(const std::string& message);
    bool isConnected() const { return connected; }

    void setOnMessage(std::function<void(const std::string&)> callback) {
        onMessageCallback = callback;
    }
};
