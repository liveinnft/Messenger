// Messenger Client - main.cpp
// Win32 GUI мессенджер: список чатов слева, окно сообщений в центре, поле ввода снизу

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <sstream>
#include "network.h"
#include "protocol.h"
#include "ssh_deploy.h"

#pragma comment(lib, "comctl32.lib")

// ===== ID элементов управления =====
#define ID_CHAT_LIST        101
#define ID_MSG_VIEW         102
#define ID_MSG_INPUT        103
#define ID_SEND_BTN         104
#define ID_CONNECT_BTN      105
#define ID_DEPLOY_BTN       106
#define ID_REFRESH_BTN      107

// ID диалогов
#define IDD_LOGIN           200
#define IDC_DLG_HOST        201
#define IDC_DLG_PORT        202
#define IDC_DLG_USER        203
#define IDC_DLG_PASS        204
#define IDC_DLG_REGISTER    205
#define IDC_DLG_LOGIN_BTN   206

#define IDD_DEPLOY          300
#define IDC_SSH_HOST        301
#define IDC_SSH_PORT        302
#define IDC_SSH_USER        303
#define IDC_SSH_PASS        304
#define IDC_SSH_LOG         305
#define IDC_SSH_DEPLOY_BTN  306

// ===== Глобальные переменные =====
HINSTANCE hInst;
HWND hMainWnd;

// Элементы главного окна
HWND hChatList;
HWND hMsgView;
HWND hMsgInput;
HWND hSendBtn;

// Состояние
NetworkClient netClient;
std::string currentUser;
std::string currentChat;
bool loggedIn = false;

// ===== Вспомогательные функции =====

// Конвертация std::string <-> std::wstring (UTF-8 не используем, только ASCII)
std::wstring toWide(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

std::string toNarrow(const std::wstring& s) {
    return std::string(s.begin(), s.end());
}

// Добавить текст в окно сообщений
void appendMessage(const std::string& text) {
    std::wstring wtext = toWide(text + "\r\n");
    int len = GetWindowTextLength(hMsgView);
    SendMessage(hMsgView, EM_SETSEL, len, len);
    SendMessage(hMsgView, EM_REPLACESEL, 0, (LPARAM)wtext.c_str());
    SendMessage(hMsgView, WM_VSCROLL, SB_BOTTOM, 0);
}

// Запрос пользователей у сервера
void requestUsers() {
    std::string req = Protocol::createMessage(Protocol::GET_USERS, {});
    netClient.sendMessage(req);
}

// Запрос истории сообщений
void requestHistory(const std::string& other) {
    std::string req = Protocol::createMessage(Protocol::GET_MESSAGES, { currentUser, other });
    netClient.sendMessage(req);
}

// Обработка входящих сообщений от сервера
// Вызывается из рабочего потока, поэтому используем PostMessage
struct ServerMsg {
    std::string raw;
};

UINT WM_SERVER_MESSAGE = WM_USER + 1;

void onServerMessage(const std::string& raw) {
    ServerMsg* msg = new ServerMsg{ raw };
    PostMessage(hMainWnd, WM_SERVER_MESSAGE, 0, (LPARAM)msg);
}

void handleServerMessage(const std::string& raw) {
    auto parts = Protocol::split(raw, Protocol::DELIMITER);
    if (parts.empty()) return;

    Protocol::MessageType type = Protocol::stringToType(parts[0]);

    switch (type) {
    case Protocol::USER_LIST: {
        SendMessage(hChatList, LB_RESETCONTENT, 0, 0);
        for (size_t i = 1; i < parts.size(); i++) {
            if (!parts[i].empty() && parts[i] != currentUser) {
                std::wstring wname = toWide(parts[i]);
                SendMessage(hChatList, LB_ADDSTRING, 0, (LPARAM)wname.c_str());
            }
        }
        break;
    }

    case Protocol::MESSAGE_LIST: {
        SendMessage(hMsgView, WM_SETTEXT, 0, (LPARAM)L"");
        // parts: MESSAGE_LIST|sender|receiver|text|timestamp|sender|receiver|...
        for (size_t i = 1; i + 3 < parts.size(); i += 4) {
            std::string line = "[" + parts[i + 3] + "] " + parts[i] + ": " + parts[i + 2];
            appendMessage(line);
        }
        break;
    }

    case Protocol::NEW_MESSAGE: {
        if (parts.size() >= 4) {
            std::string from = parts[1];
            std::string text = parts[3];
            if (from == currentChat) {
                appendMessage(from + ": " + text);
            } else {
                // Уведомление в заголовке
                std::wstring title = L"Messenger [Новое сообщение от " + toWide(from) + L"]";
                SetWindowText(hMainWnd, title.c_str());
            }
        }
        break;
    }

    case Protocol::RESPONSE_OK:
        // просто успех, ничего не делаем
        break;

    case Protocol::RESPONSE_ERROR:
        if (parts.size() >= 2) {
            MessageBox(hMainWnd, toWide(parts[1]).c_str(), L"Ошибка", MB_OK | MB_ICONERROR);
        }
        break;

    default:
        break;
    }
}

// ===== Диалог подключения / авторизации =====

struct LoginInfo {
    std::string host;
    int port;
    std::string username;
    std::string password;
    bool doRegister;
};

LoginInfo g_loginInfo;

INT_PTR CALLBACK LoginDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetDlgItemText(hDlg, IDC_DLG_HOST, L"127.0.0.1");
        SetDlgItemText(hDlg, IDC_DLG_PORT, L"8888");
        return TRUE;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        if (id == IDC_DLG_LOGIN_BTN || id == IDC_DLG_REGISTER) {
            wchar_t buf[256];

            GetDlgItemText(hDlg, IDC_DLG_HOST, buf, 256);
            g_loginInfo.host = toNarrow(buf);

            GetDlgItemText(hDlg, IDC_DLG_PORT, buf, 256);
            g_loginInfo.port = _wtoi(buf);
            if (g_loginInfo.port <= 0) g_loginInfo.port = 8888;

            GetDlgItemText(hDlg, IDC_DLG_USER, buf, 256);
            g_loginInfo.username = toNarrow(buf);

            GetDlgItemText(hDlg, IDC_DLG_PASS, buf, 256);
            g_loginInfo.password = toNarrow(buf);

            if (g_loginInfo.username.empty() || g_loginInfo.password.empty()) {
                MessageBox(hDlg, L"Введите имя пользователя и пароль", L"Ошибка", MB_OK);
                return TRUE;
            }

            g_loginInfo.doRegister = (id == IDC_DLG_REGISTER);
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

// Создаёт диалог подключения вручную (без .rc файла)
HWND createLoginDialog(HWND parent) {
    // Размеры в диалоговых единицах
    // Используем DialogBoxIndirect
    return NULL; // см. showLoginDialog ниже
}

bool showLoginDialog(HWND parent) {
    // Создаём диалог программно через CreateDialog
    HWND hDlg = CreateWindowEx(
        WS_EX_DLGMODALFRAME, L"#32770", L"Подключение к серверу",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_CENTER,
        0, 0, 350, 260, parent, NULL, hInst, NULL);

    if (!hDlg) return false;

    auto addLabel = [&](const wchar_t* text, int x, int y, int w, int h) {
        CreateWindow(L"STATIC", text, WS_VISIBLE | WS_CHILD,
            x, y, w, h, hDlg, NULL, hInst, NULL);
    };
    auto addEdit = [&](int id, int x, int y, int w, int h, bool pass = false) -> HWND {
        DWORD style = WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL;
        if (pass) style |= ES_PASSWORD;
        return CreateWindow(L"EDIT", L"", style, x, y, w, h, hDlg, (HMENU)(INT_PTR)id, hInst, NULL);
    };
    auto addBtn = [&](const wchar_t* text, int id, int x, int y, int w, int h) {
        CreateWindow(L"BUTTON", text, WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x, y, w, h, hDlg, (HMENU)(INT_PTR)id, hInst, NULL);
    };

    addLabel(L"Адрес сервера:", 10, 15, 120, 20);
    HWND hHost = addEdit(IDC_DLG_HOST, 140, 12, 150, 22);
    SetWindowText(hHost, L"127.0.0.1");

    addLabel(L"Порт:", 10, 45, 120, 20);
    HWND hPort = addEdit(IDC_DLG_PORT, 140, 42, 80, 22);
    SetWindowText(hPort, L"8888");

    addLabel(L"Имя пользователя:", 10, 80, 120, 20);
    addEdit(IDC_DLG_USER, 140, 77, 150, 22);

    addLabel(L"Пароль:", 10, 115, 120, 20);
    addEdit(IDC_DLG_PASS, 140, 112, 150, 22, true);

    addLabel(L"Нет аккаунта? Нажмите", 10, 160, 200, 20);
    addLabel(L"\"Регистрация\" чтобы создать.", 10, 178, 220, 20);

    addBtn(L"Войти", IDC_DLG_LOGIN_BTN, 10, 215, 90, 28);
    addBtn(L"Регистрация", IDC_DLG_REGISTER, 115, 215, 110, 28);
    addBtn(L"Отмена", IDCANCEL, 240, 215, 90, 28);

    SetWindowLongPtr(hDlg, GWLP_WNDPROC, (LONG_PTR)LoginDlgProc);

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    MSG m;
    INT_PTR result = 0;
    while (IsWindow(hDlg)) {
        if (GetMessage(&m, NULL, 0, 0)) {
            if (!IsDialogMessage(hDlg, &m)) {
                TranslateMessage(&m);
                DispatchMessage(&m);
            }
        }
    }

    return result == IDOK;
}

// ===== Диалог SSH развёртывания =====

HWND hDeployLog;

INT_PTR CALLBACK DeployDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(hDlg, IDC_SSH_PORT, L"22");
        SetDlgItemText(hDlg, IDC_SSH_USER, L"root");
        hDeployLog = GetDlgItem(hDlg, IDC_SSH_LOG);
        return TRUE;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        if (id == IDC_SSH_DEPLOY_BTN) {
            wchar_t buf[256];
            SshDeploy::Config cfg;

            GetDlgItemText(hDlg, IDC_SSH_HOST, buf, 256);
            cfg.host = toNarrow(buf);

            GetDlgItemText(hDlg, IDC_SSH_PORT, buf, 256);
            cfg.port = _wtoi(buf);
            if (cfg.port <= 0) cfg.port = 22;

            GetDlgItemText(hDlg, IDC_SSH_USER, buf, 256);
            cfg.username = toNarrow(buf);

            GetDlgItemText(hDlg, IDC_SSH_PASS, buf, 256);
            cfg.password = toNarrow(buf);

            if (cfg.host.empty()) {
                MessageBox(hDlg, L"Введите IP адрес сервера", L"Ошибка", MB_OK);
                return TRUE;
            }

            EnableWindow(GetDlgItem(hDlg, IDC_SSH_DEPLOY_BTN), FALSE);
            SetWindowText(hDeployLog, L"");

            HWND hLog = hDeployLog;
            HWND hBtn = GetDlgItem(hDlg, IDC_SSH_DEPLOY_BTN);

            auto logFn = [hLog](const std::string& s) {
                std::wstring ws = toWide(s + "\r\n");
                int len = GetWindowTextLength(hLog);
                SendMessage(hLog, EM_SETSEL, len, len);
                SendMessage(hLog, EM_REPLACESEL, 0, (LPARAM)ws.c_str());
            };

            std::thread([cfg, logFn, hBtn]() {
                bool ok = SshDeploy::deploy(cfg, logFn);
                if (!ok) {
                    logFn("Развёртывание не выполнено.");
                } else {
                    logFn("Готово! Теперь подключитесь к серверу в главном окне.");
                }
                EnableWindow(hBtn, TRUE);
            }).detach();

            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

void showDeployDialog(HWND parent) {
    HWND hDlg = CreateWindowEx(
        WS_EX_DLGMODALFRAME, L"#32770", L"Развернуть сервер на Linux",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        100, 100, 500, 420, parent, NULL, hInst, NULL);

    if (!hDlg) return;

    auto addLabel = [&](const wchar_t* text, int x, int y, int w, int h) {
        CreateWindow(L"STATIC", text, WS_VISIBLE | WS_CHILD,
            x, y, w, h, hDlg, NULL, hInst, NULL);
    };
    auto addEdit = [&](int id, int x, int y, int w, int h, bool pass = false) -> HWND {
        DWORD style = WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL;
        if (pass) style |= ES_PASSWORD;
        return CreateWindow(L"EDIT", L"", style, x, y, w, h, hDlg, (HMENU)(INT_PTR)id, hInst, NULL);
    };
    auto addBtn = [&](const wchar_t* text, int id, int x, int y, int w, int h) {
        CreateWindow(L"BUTTON", text, WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x, y, w, h, hDlg, (HMENU)(INT_PTR)id, hInst, NULL);
    };

    addLabel(L"IP адрес сервера:", 10, 15, 130, 20);
    addEdit(IDC_SSH_HOST, 150, 12, 200, 22);

    addLabel(L"SSH порт:", 10, 45, 130, 20);
    HWND hPort = addEdit(IDC_SSH_PORT, 150, 42, 80, 22);
    SetWindowText(hPort, L"22");

    addLabel(L"Пользователь:", 10, 75, 130, 20);
    HWND hUser = addEdit(IDC_SSH_USER, 150, 72, 150, 22);
    SetWindowText(hUser, L"root");

    addLabel(L"Пароль:", 10, 105, 130, 20);
    addEdit(IDC_SSH_PASS, 150, 102, 150, 22, true);

    addLabel(L"Лог развёртывания:", 10, 140, 200, 20);
    HWND hLog = CreateWindow(L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        10, 162, 470, 190, hDlg, (HMENU)(INT_PTR)IDC_SSH_LOG, hInst, NULL);
    hDeployLog = hLog;

    addBtn(L"Развернуть!", IDC_SSH_DEPLOY_BTN, 10, 365, 130, 30);
    addBtn(L"Закрыть", IDCANCEL, 380, 365, 90, 30);

    SetWindowLongPtr(hDlg, GWLP_WNDPROC, (LONG_PTR)DeployDlgProc);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    MSG m;
    while (IsWindow(hDlg)) {
        if (GetMessage(&m, NULL, 0, 0)) {
            if (!IsDialogMessage(hDlg, &m)) {
                TranslateMessage(&m);
                DispatchMessage(&m);
            }
        }
    }
}

// ===== Создание элементов главного окна =====

void createControls(HWND hWnd) {
    RECT rc;
    GetClientRect(hWnd, &rc);
    int w = rc.right;
    int h = rc.bottom;

    int listW = 180;
    int btnH = 32;
    int inputH = 36;
    int toolbarH = btnH + 8;

    // Тулбар сверху
    CreateWindow(L"BUTTON", L"Подключиться",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        4, 4, 130, btnH, hWnd, (HMENU)ID_CONNECT_BTN, hInst, NULL);

    CreateWindow(L"BUTTON", L"Развернуть сервер",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        140, 4, 140, btnH, hWnd, (HMENU)ID_DEPLOY_BTN, hInst, NULL);

    CreateWindow(L"BUTTON", L"Обновить",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        286, 4, 90, btnH, hWnd, (HMENU)ID_REFRESH_BTN, hInst, NULL);

    int contentY = toolbarH + 4;
    int contentH = h - contentY;

    // Список чатов слева
    hChatList = CreateWindow(L"LISTBOX", NULL,
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        0, contentY, listW, contentH, hWnd, (HMENU)ID_CHAT_LIST, hInst, NULL);

    // Окно сообщений
    int msgX = listW + 2;
    int msgW = w - msgX;
    int msgViewH = contentH - inputH - 6;

    hMsgView = CreateWindow(L"EDIT", NULL,
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        msgX, contentY, msgW, msgViewH, hWnd, (HMENU)ID_MSG_VIEW, hInst, NULL);

    // Поле ввода и кнопка
    int inputY = contentY + msgViewH + 4;
    int sendBtnW = 80;

    hMsgInput = CreateWindow(L"EDIT", NULL,
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        msgX, inputY, msgW - sendBtnW - 4, inputH,
        hWnd, (HMENU)ID_MSG_INPUT, hInst, NULL);

    hSendBtn = CreateWindow(L"BUTTON", L"Отправить",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        msgX + msgW - sendBtnW, inputY, sendBtnW, inputH,
        hWnd, (HMENU)ID_SEND_BTN, hInst, NULL);

    // Шрифт
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessage(hChatList, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hMsgView, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hMsgInput, WM_SETFONT, (WPARAM)hFont, TRUE);

    EnableWindow(hSendBtn, FALSE);
    EnableWindow(hMsgInput, FALSE);
}

// Отправить сообщение
void sendCurrentMessage() {
    if (!loggedIn || currentChat.empty()) return;

    wchar_t buf[1024];
    GetWindowText(hMsgInput, buf, 1024);
    std::string text = toNarrow(buf);
    if (text.empty()) return;

    std::string req = Protocol::createMessage(Protocol::MSG, { currentUser, currentChat, text });
    if (netClient.sendMessage(req)) {
        appendMessage(currentUser + ": " + text);
        SetWindowText(hMsgInput, L"");
    }
}

// Подключение и авторизация
void doConnect(HWND hWnd) {
    // Показываем диалог подключения
    HWND hDlg = CreateWindowEx(
        WS_EX_DLGMODALFRAME, L"#32770", L"Подключение к серверу",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_CENTER,
        0, 0, 360, 270, hWnd, NULL, hInst, NULL);

    if (!hDlg) return;

    auto addLabel = [&](const wchar_t* text, int x, int y, int w, int h) {
        CreateWindow(L"STATIC", text, WS_VISIBLE | WS_CHILD,
            x, y, w, h, hDlg, NULL, hInst, NULL);
    };
    auto addEdit = [&](int id, int x, int y, int w, int h, bool pass = false) {
        DWORD style = WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL;
        if (pass) style |= ES_PASSWORD;
        CreateWindow(L"EDIT", L"", style, x, y, w, h, hDlg, (HMENU)(INT_PTR)id, hInst, NULL);
    };
    auto addBtn = [&](const wchar_t* text, int id, int x, int y, int w, int h) {
        CreateWindow(L"BUTTON", text, WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x, y, w, h, hDlg, (HMENU)(INT_PTR)id, hInst, NULL);
    };

    addLabel(L"Адрес сервера:", 10, 15, 120, 20);
    HWND hHost = CreateWindow(L"EDIT", L"127.0.0.1",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        140, 12, 150, 22, hDlg, (HMENU)(INT_PTR)IDC_DLG_HOST, hInst, NULL);
    (void)hHost;

    addLabel(L"Порт:", 10, 45, 120, 20);
    HWND hPort = CreateWindow(L"EDIT", L"8888",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        140, 42, 80, 22, hDlg, (HMENU)(INT_PTR)IDC_DLG_PORT, hInst, NULL);
    (void)hPort;

    addLabel(L"Имя пользователя:", 10, 80, 130, 20);
    addEdit(IDC_DLG_USER, 140, 77, 150, 22);

    addLabel(L"Пароль:", 10, 115, 120, 20);
    addEdit(IDC_DLG_PASS, 140, 112, 150, 22, true);

    addLabel(L"Нет аккаунта? Нажмите", 10, 155, 200, 20);
    addLabel(L"\"Регистрация\" для создания нового.", 10, 173, 250, 20);

    addBtn(L"Войти", IDC_DLG_LOGIN_BTN, 10, 220, 90, 30);
    addBtn(L"Регистрация", IDC_DLG_REGISTER, 110, 220, 110, 30);
    addBtn(L"Отмена", IDCANCEL, 240, 220, 90, 30);

    SetWindowLongPtr(hDlg, GWLP_WNDPROC, (LONG_PTR)LoginDlgProc);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    MSG m;
    while (IsWindow(hDlg)) {
        if (GetMessage(&m, NULL, 0, 0)) {
            if (!IsDialogMessage(hDlg, &m)) {
                TranslateMessage(&m);
                DispatchMessage(&m);
            }
        }
    }

    if (g_loginInfo.username.empty()) return;

    // Подключаемся
    if (netClient.isConnected()) {
        netClient.disconnect();
    }

    if (!netClient.connectToServer(g_loginInfo.host, g_loginInfo.port)) {
        MessageBox(hWnd, L"Не удалось подключиться к серверу.\nПроверьте адрес и порт.", L"Ошибка", MB_OK | MB_ICONERROR);
        return;
    }

    netClient.setOnMessage(onServerMessage);

    // Регистрация или вход
    std::string cmd;
    if (g_loginInfo.doRegister) {
        cmd = Protocol::createMessage(Protocol::REGISTER, { g_loginInfo.username, g_loginInfo.password });
    } else {
        cmd = Protocol::createMessage(Protocol::LOGIN, { g_loginInfo.username, g_loginInfo.password });
    }

    netClient.sendMessage(cmd);

    // Небольшая задержка, затем логин если регистрировались
    if (g_loginInfo.doRegister) {
        Sleep(300);
        cmd = Protocol::createMessage(Protocol::LOGIN, { g_loginInfo.username, g_loginInfo.password });
        netClient.sendMessage(cmd);
    }

    Sleep(300);

    currentUser = g_loginInfo.username;
    loggedIn = true;

    std::wstring title = L"Messenger — " + toWide(currentUser) + L" @ " + toWide(g_loginInfo.host);
    SetWindowText(hWnd, title.c_str());

    EnableWindow(hSendBtn, TRUE);
    EnableWindow(hMsgInput, TRUE);

    requestUsers();
}

// ===== Главная оконная процедура =====

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        createControls(hWnd);
        return 0;

    case WM_SIZE: {
        // Перемещаем элементы при изменении размера
        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right;
        int h = rc.bottom;

        int listW = 180;
        int btnH = 32;
        int inputH = 36;
        int toolbarH = btnH + 8;
        int contentY = toolbarH + 4;
        int contentH = h - contentY;
        int msgX = listW + 2;
        int msgW = w - msgX;
        int msgViewH = contentH - inputH - 6;
        int inputY = contentY + msgViewH + 4;
        int sendBtnW = 80;

        SetWindowPos(hChatList, NULL, 0, contentY, listW, contentH, SWP_NOZORDER);
        SetWindowPos(hMsgView, NULL, msgX, contentY, msgW, msgViewH, SWP_NOZORDER);
        SetWindowPos(hMsgInput, NULL, msgX, inputY, msgW - sendBtnW - 4, inputH, SWP_NOZORDER);
        SetWindowPos(hSendBtn, NULL, msgX + msgW - sendBtnW, inputY, sendBtnW, inputH, SWP_NOZORDER);
        return 0;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);

        if (wmId == ID_CONNECT_BTN) {
            doConnect(hWnd);
            return 0;
        }

        if (wmId == ID_DEPLOY_BTN) {
            showDeployDialog(hWnd);
            return 0;
        }

        if (wmId == ID_REFRESH_BTN) {
            if (loggedIn) requestUsers();
            return 0;
        }

        if (wmId == ID_SEND_BTN) {
            sendCurrentMessage();
            return 0;
        }

        if (wmId == ID_CHAT_LIST && HIWORD(wParam) == LBN_SELCHANGE) {
            int idx = (int)SendMessage(hChatList, LB_GETCURSEL, 0, 0);
            if (idx != LB_ERR) {
                wchar_t buf[256];
                SendMessage(hChatList, LB_GETTEXT, idx, (LPARAM)buf);
                currentChat = toNarrow(buf);
                SetWindowText(hMsgView, L"");
                if (loggedIn) {
                    requestHistory(currentChat);
                }
            }
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
        if (wParam == VK_RETURN && GetFocus() == hMsgInput) {
            sendCurrentMessage();
            return 0;
        }
        break;

    case WM_SERVER_MESSAGE: {
        ServerMsg* msg = (ServerMsg*)lParam;
        if (msg) {
            handleServerMessage(msg->raw);
            delete msg;
        }
        return 0;
    }

    case WM_DESTROY:
        netClient.disconnect();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

// ===== Точка входа =====

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    hInst = hInstance;

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"MessengerWnd";
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassEx(&wc);

    hMainWnd = CreateWindowEx(0, L"MessengerWnd", L"Messenger",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
        NULL, NULL, hInstance, NULL);

    if (!hMainWnd) return 0;

    ShowWindow(hMainWnd, nCmdShow);
    UpdateWindow(hMainWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.hwnd == hMsgInput && msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            sendCurrentMessage();
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
