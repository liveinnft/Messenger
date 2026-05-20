#include "network.h"
#include <iostream>
#include <fstream>

void LogNet(const std::string& msg) {
    std::ofstream log("net.log", std::ios::app);
    if (log) {
        time_t now = time(NULL);
        struct tm t;
        localtime_s(&t, &now);
        char buf[20];
        strftime(buf, sizeof(buf), "%H:%M:%S", &t);
        log << buf << " " << msg << std::endl;
    }
}

NetworkClient::NetworkClient() : sock(INVALID_SOCKET), connected(false), stopRequested(false) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

NetworkClient::~NetworkClient() {
    disconnect();
    WSACleanup();
}

bool NetworkClient::connectToServer(const std::string& host, int port) {
    disconnect();

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return false;

    // Увеличиваем таймауты до 15 секунд
    DWORD timeout = 15000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    int nodelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

    // Увеличиваем буфер отправки (опционально)
    int sendBuf = 65536;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char*)&sendBuf, sizeof(sendBuf));

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons((u_short)port);

    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0) {
        closesocket(sock);
        sock = INVALID_SOCKET;
        return false;
    }

    unsigned long nonblocking = 1;
    ioctlsocket(sock, FIONBIO, &nonblocking);

    int result = connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr));
    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            fd_set write_fds;
            FD_ZERO(&write_fds);
            FD_SET(sock, &write_fds);
            timeval tv;
            tv.tv_sec = 15;
            tv.tv_usec = 0;
            result = select(0, nullptr, &write_fds, nullptr, &tv);
            if (result <= 0) {
                closesocket(sock);
                sock = INVALID_SOCKET;
                return false;
            }
        }
        else {
            closesocket(sock);
            sock = INVALID_SOCKET;
            return false;
        }
    }

    nonblocking = 0;
    ioctlsocket(sock, FIONBIO, &nonblocking);

    connected = true;
    stopRequested = false;
    recvThread = std::thread(&NetworkClient::receiveLoop, this);
    LogNet("Connected to " + host + ":" + std::to_string(port));
    return true;
}

void NetworkClient::disconnect() {
    stopRequested = true;
    connected = false;
    if (sock != INVALID_SOCKET) {
        shutdown(sock, SD_BOTH);
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    if (recvThread.joinable()) {
        recvThread.join();
    }
    LogNet("Disconnected");
}

bool NetworkClient::sendMessage(const std::string& message) {
    if (!connected || sock == INVALID_SOCKET) {
        LogNet("sendMessage: not connected");
        return false;
    }

    int len = (int)message.length();
    int sent = 0;
    LogNet("Sending " + std::to_string(len) + " bytes: " + message.substr(0, 50));
    while (sent < len) {
        int r = send(sock, message.c_str() + sent, len - sent, 0);
        if (r == SOCKET_ERROR) {
            int err = WSAGetLastError();
            LogNet("send error: " + std::to_string(err));
            if (err == WSAEWOULDBLOCK) {
                Sleep(50);
                continue;
            }
            else if (err == WSAECONNRESET || err == WSAECONNABORTED) {
                // Соединение разорвано
                connected = false;
                return false;
            }
            return false;
        }
        sent += r;
        LogNet("Sent " + std::to_string(sent) + " of " + std::to_string(len));
    }
    return true;
}

void NetworkClient::receiveLoop() {
    char buffer[8192];
    std::string partial;

    while (!stopRequested && connected) {
        memset(buffer, 0, sizeof(buffer));
        int bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytesRead == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                Sleep(10);
                continue;
            }
            LogNet("recv error: " + std::to_string(err));
            break;
        }
        if (bytesRead <= 0) {
            LogNet("recv returned 0, connection closed");
            break;
        }
        partial.append(buffer, bytesRead);
        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            std::string msg = partial.substr(0, pos);
            partial.erase(0, pos + 1);
            if (!msg.empty() && onMessageCallback) {
                onMessageCallback(msg);
            }
        }
    }
    connected = false;
    LogNet("Receive loop ended");
}