#include "ssh_deploy.h"
#include <libssh2.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <thread>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

bool SshDeploy::runCommand(void* session_ptr, const std::string& cmd, LogCallback log) {
    LIBSSH2_SESSION* session = (LIBSSH2_SESSION*)session_ptr;

    log("Выполнение: " + cmd);

    LIBSSH2_CHANNEL* channel = libssh2_channel_open_session(session);
    if (!channel) {
        log("ERROR: Не удалось открыть канал для команды");
        return false;
    }

    if (libssh2_channel_exec(channel, cmd.c_str()) != 0) {
        libssh2_channel_free(channel);
        log("ERROR: Не удалось выполнить команду");
        return false;
    }

    // Читаем вывод команды
    char buf[1024];
    std::string output;
    int rc;

    // Читаем stdout
    while ((rc = libssh2_channel_read(channel, buf, sizeof(buf))) > 0) {
        output += std::string(buf, rc);
    }

    // Читаем stderr
    std::string errorOutput;
    while ((rc = libssh2_channel_read_stderr(channel, buf, sizeof(buf))) > 0) {
        errorOutput += std::string(buf, rc);
    }

    if (!output.empty()) {
        // Разбиваем на строки для лучшей читаемости
        std::istringstream iss(output);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty()) {
                log("  " + line);
            }
        }
    }

    if (!errorOutput.empty()) {
        std::istringstream iss(errorOutput);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty()) {
                log("  [stderr] " + line);
            }
        }
    }

    int exitCode = libssh2_channel_get_exit_status(channel);
    libssh2_channel_free(channel);

    if (exitCode != 0) {
        log("Команда завершилась с кодом: " + std::to_string(exitCode));
        return false;
    }

    return true;
}

bool SshDeploy::uploadFile(void* session_ptr, const std::string& localPath,
    const std::string& remotePath, LogCallback log) {
    LIBSSH2_SESSION* session = (LIBSSH2_SESSION*)session_ptr;

    log("Загрузка: " + localPath + " -> " + remotePath);

    std::ifstream file(localPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        log("ERROR: Не удалось открыть локальный файл: " + localPath);
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> data((size_t)size);
    if (!file.read(data.data(), size)) {
        log("ERROR: Не удалось прочитать файл: " + localPath);
        return false;
    }

    LIBSSH2_CHANNEL* channel = libssh2_scp_send(session, remotePath.c_str(), 0755, size);
    if (!channel) {
        char* errmsg;
        int errlen;
        libssh2_session_last_error(session, &errmsg, &errlen, 0);
        log("ERROR: Не удалось открыть SCP канал: " + std::string(errmsg, errlen));
        return false;
    }

    size_t sent = 0;
    while (sent < (size_t)size) {
        int rc = libssh2_channel_write(channel, data.data() + sent, size - sent);
        if (rc < 0) {
            log("ERROR: Ошибка при передаче данных");
            libssh2_channel_free(channel);
            return false;
        }
        sent += rc;
    }

    libssh2_channel_send_eof(channel);
    libssh2_channel_wait_eof(channel);
    libssh2_channel_wait_closed(channel);
    libssh2_channel_free(channel);

    log("Файл успешно загружен (" + std::to_string(size) + " байт)");
    return true;
}

bool SshDeploy::deploy(const Config& cfg, LogCallback log) {
    log("=== Начало развертывания сервера ===");
    log("Подключение к " + cfg.host + ":" + std::to_string(cfg.port));

    // Инициализация libssh2
    if (libssh2_init(0) != 0) {
        log("ERROR: Не удалось инициализировать libssh2");
        return false;
    }

    // Инициализация сокета
    WSADATA wsadata;
    if (WSAStartup(MAKEWORD(2, 0), &wsadata) != 0) {
        log("ERROR: Не удалось инициализировать Winsock");
        libssh2_exit();
        return false;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        log("ERROR: Не удалось создать сокет");
        WSACleanup();
        libssh2_exit();
        return false;
    }

    // Настройка timeout для подключения
    DWORD timeout = 10000; // 10 секунд
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg.port);

    if (inet_pton(AF_INET, cfg.host.c_str(), &addr.sin_addr) != 1) {
        log("ERROR: Неверный формат IP адреса");
        closesocket(sock);
        WSACleanup();
        libssh2_exit();
        return false;
    }

    log("Попытка подключения...");
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        log("ERROR: Не удалось подключиться к " + cfg.host + ":" + std::to_string(cfg.port));
        log("Проверьте IP адрес и доступность сервера");
        closesocket(sock);
        WSACleanup();
        libssh2_exit();
        return false;
    }

    log("Соединение установлено");

    LIBSSH2_SESSION* session = libssh2_session_init();
    if (!session) {
        log("ERROR: Не удалось создать SSH сессию");
        closesocket(sock);
        WSACleanup();
        libssh2_exit();
        return false;
    }

    // Настройка timeout для SSH
    libssh2_session_set_timeout(session, 10000);

    log("SSH handshake...");
    if (libssh2_session_handshake(session, (libssh2_socket_t)sock) != 0) {
        char* errmsg;
        int errlen;
        libssh2_session_last_error(session, &errmsg, &errlen, 0);
        log("ERROR: SSH handshake провалился: " + std::string(errmsg, errlen));
        libssh2_session_free(session);
        closesocket(sock);
        WSACleanup();
        libssh2_exit();
        return false;
    }

    log("Аутентификация пользователя " + cfg.username + "...");
    if (libssh2_userauth_password(session, cfg.username.c_str(), cfg.password.c_str()) != 0) {
        char* errmsg;
        int errlen;
        libssh2_session_last_error(session, &errmsg, &errlen, 0);
        log("ERROR: Аутентификация не удалась: " + std::string(errmsg, errlen));
        log("Проверьте имя пользователя и пароль");
        libssh2_session_disconnect(session, "Authentication failed");
        libssh2_session_free(session);
        closesocket(sock);
        WSACleanup();
        libssh2_exit();
        return false;
    }

    log("Аутентификация успешна!");
    log("");

    // Проверка наличия необходимых инструментов
    log("Проверка наличия инструментов на сервере...");

    if (!runCommand(session, "which cmake", log)) {
        log("WARNING: cmake не найден, пытаемся установить...");
        runCommand(session, "apt-get update && apt-get install -y cmake", log);
    }

    if (!runCommand(session, "which g++", log)) {
        log("WARNING: g++ не найден, пытаемся установить...");
        runCommand(session, "apt-get update && apt-get install -y build-essential", log);
    }

    log("");
    log("Создание директории ~/messenger_server...");
    runCommand(session, "mkdir -p ~/messenger_server", log);

    // Загрузка файлов сервера
    log("");
    log("Загрузка файлов сервера...");

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

    int uploadedCount = 0;
    for (size_t i = 0; i < files.size(); i++) {
        if (uploadFile(session, files[i], "~/messenger_server/" + remoteNames[i], log)) {
            uploadedCount++;
        }
        else {
            log("WARNING: Не удалось загрузить " + files[i]);
        }
    }

    if (uploadedCount == 0) {
        log("ERROR: Ни один файл не был загружен");
        log("Убедитесь, что файлы находятся в папке server_files/");
        libssh2_session_disconnect(session, "Upload failed");
        libssh2_session_free(session);
        closesocket(sock);
        WSACleanup();
        libssh2_exit();
        return false;
    }

    log("");
    log("Компиляция сервера...");
    log("Это может занять некоторое время...");

    std::string buildCmd =
        "cd ~/messenger_server && "
        "mkdir -p build && cd build && "
        "cmake .. -DCMAKE_BUILD_TYPE=Release && "
        "make -j$(nproc)";

    if (!runCommand(session, buildCmd, log)) {
        log("");
        log("ERROR: Компиляция провалилась");
        log("Проверьте вывод выше для деталей");
        libssh2_session_disconnect(session, "Build failed");
        libssh2_session_free(session);
        closesocket(sock);
        WSACleanup();
        libssh2_exit();
        return false;
    }

    log("");
    log("Компиляция успешна!");

    // Остановка старого сервера
    log("");
    log("Остановка старого экземпляра сервера (если запущен)...");
    runCommand(session, "pkill -f 'messenger_server/build/Server' || true", log);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Запуск нового сервера
    log("");
    log("Запуск сервера на порту 8888...");
    std::string startCmd =
        "cd ~/messenger_server/build && "
        "nohup ./Server > ../server.log 2>&1 & "
        "echo $!";

    if (runCommand(session, startCmd, log)) {
        log("Сервер запущен!");

        // Даем серверу время запуститься
        std::this_thread::sleep_for(std::chrono::seconds(2));

        // Проверяем, что сервер действительно запущен
        log("");
        log("Проверка статуса сервера...");
        if (runCommand(session, "ps aux | grep '[S]erver' | grep messenger_server", log)) {
            log("Сервер работает корректно!");
        }
        else {
            log("WARNING: Не удалось подтвердить запуск сервера");
            log("Проверьте лог: ~/messenger_server/server.log");
        }
    }
    else {
        log("WARNING: Проблемы при запуске сервера");
    }

    log("");
    log("=== Развертывание завершено ===");
    log("Сервер доступен по адресу: " + cfg.host + ":8888");
    log("Лог сервера: ~/messenger_server/server.log");

    libssh2_session_disconnect(session, "Deployment completed");
    libssh2_session_free(session);
    closesocket(sock);
    WSACleanup();
    libssh2_exit();
    return true;
}
