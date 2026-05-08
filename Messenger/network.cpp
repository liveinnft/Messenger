
#include "network.h"
#include <iostream>

NetworkClient::NetworkClient() : sock(INVALID_SOCKET), connected(false) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

NetworkClient::~NetworkClient() {
    disconnect();
    WSACleanup();
}

bool NetworkClient::connectToServer(const std::string& host, int port) {
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return false;

    // Set connection timeout (5 seconds)
    DWORD timeout = 5000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    // Set TCP no-delay for faster response
    int nodelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

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
            tv.tv_sec = 5;
            tv.tv_usec = 0;
            result = select(0, nullptr, &write_fds, nullptr, &tv);
            if (result <= 0) {
                closesocket(sock);
                sock = INVALID_SOCKET;
                return false;
            }
        } else {
            closesocket(sock);
            sock = INVALID_SOCKET;
            return false;
        }
    }

    nonblocking = 0;
    ioctlsocket(sock, FIONBIO, &nonblocking);

    connected = true;
    recvThread = std::thread(&NetworkClient::receiveLoop, this);
    return true;
}

void NetworkClient::disconnect() {
    connected = false;

    if (sock != INVALID_SOCKET) {
        shutdown(sock, SD_BOTH);
        closesocket(sock);
        sock = INVALID_SOCKET;
    }

    if (recvThread.joinable()) {
        if (std::this_thread::get_id() != recvThread.get_id()) {
            recvThread.join();
        }
    }
}

bool NetworkClient::sendMessage(const std::string& message) {
    if (!connected || sock == INVALID_SOCKET) return false;

    int len = (int)message.length();
    int sent = 0;
    while (sent < len) {
        int r = send(sock, message.c_str() + sent, len - sent, 0);
        if (r == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                Sleep(10);
                continue;
            }
            return false;
        }
        sent += r;
    }
    return true;
}

void NetworkClient::receiveLoop() {
    char buffer[8192];
    std::string partial;

    while (connected) {
        memset(buffer, 0, sizeof(buffer));
        int bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0);

        if (bytesRead == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                Sleep(10);
                continue;
            }
            break;
        }

        if (bytesRead <= 0) {
            break;
        }

        partial.append(buffer, bytesRead);

        // Process complete messages (newline-delimited)
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
}
