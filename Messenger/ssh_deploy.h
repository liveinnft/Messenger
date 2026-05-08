#pragma once

#include <string>
#include <functional>

// SSH deployment via libssh2
// Connects to Linux server, uploads server binary, compiles and starts it

class SshDeploy {
public:
    struct Config {
        std::string host;
        int port = 22;
        std::string username;
        std::string password;
    };

    using LogCallback = std::function<void(const std::string&)>;

    static bool deploy(const Config& cfg, LogCallback log);

private:
    static bool runCommand(void* session, const std::string& cmd, LogCallback log);
    static bool uploadFile(void* session, const std::string& localPath,
                           const std::string& remotePath, LogCallback log);
};
