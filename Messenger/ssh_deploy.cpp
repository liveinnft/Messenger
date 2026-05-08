#include "ssh_deploy.h"
#include <libssh2.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <sstream>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

bool SshDeploy::runCommand(void* session_ptr, const std::string& cmd, LogCallback log) {
    LIBSSH2_SESSION* session = (LIBSSH2_SESSION*)session_ptr;
    LIBSSH2_CHANNEL* channel = libssh2_channel_open_session(session);
    if (!channel) {
        log("ERROR: Cannot open channel for command: " + cmd);
        return false;
    }

    if (libssh2_channel_exec(channel, cmd.c_str()) != 0) {
        libssh2_channel_free(channel);
        log("ERROR: Cannot execute command: " + cmd);
        return false;
    }

    char buf[1024];
    std::string output;
    int rc;
    while ((rc = libssh2_channel_read(channel, buf, sizeof(buf))) > 0) {
        output += std::string(buf, rc);
    }

    if (!output.empty()) {
        log("  > " + output);
    }

    int exitCode = libssh2_channel_get_exit_status(channel);
    libssh2_channel_free(channel);

    return exitCode == 0;
}

bool SshDeploy::uploadFile(void* session_ptr, const std::string& localPath,
    const std::string& remotePath, LogCallback log) {
    LIBSSH2_SESSION* session = (LIBSSH2_SESSION*)session_ptr;

    std::ifstream file(localPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        log("ERROR: Cannot open local file: " + localPath);
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> data((size_t)size);
    if (!file.read(data.data(), size)) {
        log("ERROR: Cannot read local file: " + localPath);
        return false;
    }

    LIBSSH2_CHANNEL* channel = libssh2_scp_send(session, remotePath.c_str(), 0755, size);
    if (!channel) {
        log("ERROR: Cannot open SCP channel for: " + remotePath);
        return false;
    }

    size_t sent = 0;
    while (sent < (size_t)size) {
        int rc = libssh2_channel_write(channel, data.data() + sent, size - sent);
        if (rc < 0) {
            log("ERROR: SCP write failed");
            libssh2_channel_free(channel);
            return false;
        }
        sent += rc;
    }

    libssh2_channel_send_eof(channel);
    libssh2_channel_wait_eof(channel);
    libssh2_channel_wait_closed(channel);
    libssh2_channel_free(channel);
    return true;
}

bool SshDeploy::deploy(const Config& cfg, LogCallback log) {
    libssh2_init(0);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        log("ERROR: Cannot create socket");
        libssh2_exit();
        return false;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg.port);
    inet_pton(AF_INET, cfg.host.c_str(), &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        log("ERROR: Cannot connect to " + cfg.host + ":" + std::to_string(cfg.port));
        closesocket(sock);
        libssh2_exit();
        return false;
    }

    LIBSSH2_SESSION* session = libssh2_session_init();
    if (!session) {
        log("ERROR: Cannot create SSH session");
        closesocket(sock);
        libssh2_exit();
        return false;
    }

    if (libssh2_session_handshake(session, (libssh2_socket_t)sock) != 0) {
        log("ERROR: SSH handshake failed");
        libssh2_session_free(session);
        closesocket(sock);
        libssh2_exit();
        return false;
    }

    log("Connected to " + cfg.host);

    if (libssh2_userauth_password(session, cfg.username.c_str(), cfg.password.c_str()) != 0) {
        log("ERROR: Authentication failed");
        libssh2_session_disconnect(session, "bye");
        libssh2_session_free(session);
        closesocket(sock);
        libssh2_exit();
        return false;
    }

    log("Authenticated as " + cfg.username);

    // Create remote directory
    log("Creating remote directory ~/messenger_server ...");
    runCommand(session, "mkdir -p ~/messenger_server", log);

    // Upload source files
    const std::vector<std::string> files = {
        "server_files/main.cpp",
        "server_files/server.cpp",
        "server_files/server.h",
        "server_files/database.cpp",
        "server_files/database.h",
        "server_files/protocol.h",
        "server_files/CMakeLists.txt"
    };
    const std::vector<std::string> remoteNames = {
        "main.cpp", "server.cpp", "server.h",
        "database.cpp", "database.h", "protocol.h", "CMakeLists.txt"
    };

    for (size_t i = 0; i < files.size(); i++) {
        log("Uploading " + remoteNames[i] + " ...");
        if (!uploadFile(session, files[i], "~/messenger_server/" + remoteNames[i], log)) {
            log("WARNING: Could not upload " + files[i] + " (file may not exist locally)");
        }
    }

    // Compile
    log("Compiling server...");
    std::string buildCmd =
        "cd ~/messenger_server && "
        "mkdir -p build && cd build && "
        "cmake .. -DCMAKE_BUILD_TYPE=Release 2>&1 && "
        "make -j2 2>&1";
    if (!runCommand(session, buildCmd, log)) {
        log("ERROR: Compilation failed");
        libssh2_session_disconnect(session, "bye");
        libssh2_session_free(session);
        closesocket(sock);
        libssh2_exit();
        return false;
    }

    log("Compilation successful!");

    // Kill old instance
    runCommand(session, "pkill -f messenger_server/build/Server 2>/dev/null || true", log);

    // Start server
    log("Starting server on port 8888...");
    std::string startCmd =
        "nohup ~/messenger_server/build/Server > ~/messenger_server/server.log 2>&1 &";
    runCommand(session, startCmd, log);

    log("Server started successfully!");
    log("You can now connect to " + cfg.host + ":8888");

    libssh2_session_disconnect(session, "Deployment done");
    libssh2_session_free(session);
    closesocket(sock);
    libssh2_exit();
    return true;
}