
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <fstream>
#include <atomic>
#include <cctype>
#include "network.h"
#include "protocol.h"
#include <shellscalingapi.h>
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "comctl32.lib")

#define DT_WORD_ELL 0x00040000L
#define ODS_HOT_CUSTOM 0x00000040L

// цвета
#define CLR_BG RGB(26, 26, 30)
#define CLR_SIDEBAR RGB(30, 30, 34)
#define CLR_CHAT_BG RGB(30, 30, 34)
#define CLR_HEADER RGB(30, 30, 34)
#define CLR_BUBBLE_SELF RGB(0, 120, 255)
#define CLR_BUBBLE_OTHER RGB(46, 46, 50)
#define CLR_TEXT_SELF RGB(255, 255, 255)
#define CLR_TEXT_OTHER RGB(220, 220, 220)
#define CLR_DIVIDER RGB(42, 42, 46)
#define CLR_ONLINE RGB(0, 200, 100)
#define CLR_OFFLINE RGB(100, 100, 110)
#define CLR_INPUT_BG RGB(46, 46, 50)
#define CLR_STATUSBAR RGB(22, 22, 25)
#define CLR_SEARCH_BG RGB(36, 36, 42)
#define CLR_BORDER RGB(60, 60, 65)
#define CLR_TEXT RGB(255, 255, 255)
#define CLR_TEXT_SEC RGB(142, 142, 147)

#define SIDEBAR_W 280
#define CHAT_HEADER_H 54
#define INPUT_AREA_H 56
#define MSG_BUBBLE_PAD_X 12
#define MSG_BUBBLE_PAD_Y 6
#define MSG_BUBBLE_RAD 10
#define MSG_MAX_W 380
#define FONT_MAIN_SZ 14
#define FONT_SEC_SZ 12
#define FONT_TINY_SZ 10
#define CHAT_ITEM_H 64
#define SEND_BTN_W 80

#define ID_SIDEBAR_SEARCH 101
#define ID_CHAT_LIST 102
#define ID_MSG_VIEW 103
#define ID_MSG_INPUT 104
#define ID_SEND_BTN 105
#define ID_CONNECT_BTN 106
#define ID_DISCONNECT_BTN 107
#define ID_REFRESH_BTN 108

#define IDC_EDIT_HOST      201
#define IDC_EDIT_PORT      202
#define IDC_EDIT_USER      203
#define IDC_EDIT_PASS      204
#define IDC_BTN_LOGIN      205
#define IDC_BTN_REGISTER   206
#define IDC_BTN_CANCEL     207

HINSTANCE g_hInst = NULL;
HWND g_hMain = NULL, g_hSidebar = NULL, g_hChatHeader = NULL, g_hMsgView = NULL;
HWND g_hMsgInput = NULL, g_hSendBtn = NULL, g_hChatList = NULL;
HWND g_hSearchBox = NULL, g_hStatusTxt = NULL, g_hConnectBtn = NULL, g_hDisconnectBtn = NULL, g_hRefreshBtn = NULL;

HFONT g_fontMain = NULL, g_fontSec = NULL, g_fontTiny = NULL, g_fontBold = NULL, g_fontBtn = NULL;
HBRUSH g_hBrBg = NULL, g_hBrSidebar = NULL, g_hBrInput = NULL;

int g_scrollPos = 0;
int g_totalHeight = 0;

bool g_loggedIn = false;
std::string g_currentUser, g_currentChat, g_serverHost;
int g_serverPort = 8888;
const UINT WM_SERVER_MSG = WM_USER + 1;

struct ChatUser {
    std::string name;
    std::string lastMsg;
    std::string lastTime;
    bool online;
    int unreadCount;
};

struct ChatMsg {
    std::string sender;
    std::string text;
    std::string time;
    bool isSelf;
};

std::vector<ChatUser> g_allUsers, g_filteredUsers;
std::vector<ChatMsg> g_messages;
NetworkClient g_net;

std::atomic<bool> g_authWaiting{ false };
std::atomic<bool> g_authSuccess{ false };
std::string g_authError;

// логи
void LogToFile(const std::string& msg) {
    std::ofstream log("net.log", std::ios::app);
    if (log) {
        time_t now = time(NULL);
        struct tm t;
        localtime_s(&t, &now);
        char buf[20];
        strftime(buf, sizeof(buf), "%H:%M:%S", &t);
        log << buf << " " << msg << std::endl;
        log.close();
    }
}

struct AppConfig {
    std::string lastHost;
    int lastPort;
    std::string lastUsername;
    std::string lastPassword;
    bool autoLogin;
    AppConfig() : lastHost("127.0.0.1"), lastPort(8888), lastUsername(""), lastPassword(""), autoLogin(false) {}
};

void SaveConfig(const AppConfig& cfg) {
    std::ofstream f("messenger.ini");
    if (f) {
        f << "host=" << cfg.lastHost << "\n";
        f << "port=" << cfg.lastPort << "\n";
        f << "username=" << cfg.lastUsername << "\n";
        f << "password=" << cfg.lastPassword << "\n";
        f << "auto_login=" << (cfg.autoLogin ? "1" : "0") << "\n";
        f.close();
    }
}

AppConfig LoadConfig() {
    AppConfig cfg;
    std::ifstream f("messenger.ini");
    if (f) {
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            if (key == "host") cfg.lastHost = val;
            else if (key == "port") cfg.lastPort = std::stoi(val);
            else if (key == "username") cfg.lastUsername = val;
            else if (key == "password") cfg.lastPassword = val;
            else if (key == "auto_login") cfg.autoLogin = (val == "1");
        }
        f.close();
    }
    return cfg;
}


std::string curTime() {
    time_t n = time(NULL);
    struct tm t;
    localtime_s(&t, &n);
    char buf[8];
    strftime(buf, sizeof(buf), "%H:%M", &t);
    return buf;
}

std::wstring s2w(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    if (n == 0) return L"";
    std::wstring r(n - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &r[0], n);
    return r;
}

std::string w2s(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, NULL, 0, NULL, NULL);
    if (n == 0) return "";
    std::string r(n - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &r[0], n, NULL, NULL);
    return r;
}

void trimStr(std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) { s.clear(); return; }
    size_t b = s.find_last_not_of(" \t\r\n");
    s = s.substr(a, b - a + 1);
}

void setStatus(const std::string& t) {
    if (g_hStatusTxt) SetWindowText(g_hStatusTxt, s2w(t).c_str());
}

COLORREF GetAvatarColor(const std::string& name) {
    if (name.empty()) return RGB(55, 60, 75);
    unsigned int hash = 0;
    for (char c : name) hash = hash * 131 + c;
    int r = (hash & 0xFF) % 180 + 40;
    int g = ((hash >> 8) & 0xFF) % 180 + 40;
    int b = ((hash >> 16) & 0xFF) % 180 + 40;
    return RGB(r, g, b);
}


void drawEllipse2(HDC dc, int cx, int cy, int rx, int ry, COLORREF fill) {
    HRGN rgn = CreateEllipticRgn(cx - rx, cy - ry, cx + rx, cy + ry);
    HBRUSH br = CreateSolidBrush(fill);
    FillRgn(dc, rgn, br);
    DeleteObject(rgn);
    DeleteObject(br);
}

void drawChatItem(DRAWITEMSTRUCT* dis) {
    if (dis->itemID == (UINT)-1) return;
    int idx = (int)dis->itemID;
    if (idx < 0 || idx >= (int)g_filteredUsers.size()) return;
    ChatUser& u = g_filteredUsers[idx];
    bool sel = ((dis->itemState & ODS_SELECTED) != 0);
    bool hot = ((dis->itemState & ODS_HOT_CUSTOM) != 0);
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    HBRUSH br = CreateSolidBrush(sel ? RGB(50, 50, 60) : (hot ? RGB(40, 40, 48) : CLR_SIDEBAR));
    FillRect(dc, &rc, br);
    DeleteObject(br);
    HPEN pen = CreatePen(PS_SOLID, 1, CLR_DIVIDER);
    HPEN oldPen = (HPEN)SelectObject(dc, pen);
    MoveToEx(dc, rc.left, rc.bottom - 1, NULL);
    LineTo(dc, rc.right, rc.bottom - 1);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    int L = 14;
    int avY = rc.top + (rc.bottom - rc.top) / 2 - 18;
    COLORREF avatarColor = GetAvatarColor(u.name);
    drawEllipse2(dc, L + 18, avY + 18, 18, 18, avatarColor);
    wchar_t init[2] = { 0 };
    if (!u.name.empty()) {
        init[0] = u.name[0];
        if (init[0] >= L'a' && init[0] <= L'z') init[0] = (wchar_t)(init[0] - 32);
    }
    HFONT oldF = (HFONT)SelectObject(dc, g_fontBold);
    SetTextColor(dc, CLR_TEXT_SELF);
    SetBkMode(dc, TRANSPARENT);
    RECT ir = { L, avY, L + 36, avY + 36 };
    DrawText(dc, init, 1, &ir, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    drawEllipse2(dc, L + 30, avY + 32, 5, 5, u.online ? CLR_ONLINE : CLR_OFFLINE);
    RECT nr = rc;
    nr.left = L + 48; nr.top = rc.top + 10; nr.right = rc.right - 12; nr.bottom = rc.top + 26;
    SetTextColor(dc, CLR_TEXT_SELF);
    SelectObject(dc, g_fontBold);
    DrawText(dc, s2w(u.name).c_str(), -1, &nr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_WORD_ELL);
    RECT mr = rc;
    mr.left = L + 48; mr.top = rc.top + 28; mr.right = rc.right - 40; mr.bottom = rc.bottom - 8;
    SetTextColor(dc, RGB(142, 142, 147));
    SelectObject(dc, g_fontSec);
    std::wstring lm = s2w(u.lastMsg);
    if (lm.size() > 35) lm = lm.substr(0, 35) + L"...";
    DrawText(dc, lm.c_str(), -1, &mr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_WORD_ELL);

    int rightEdge = rc.right - 12;
    if (!u.lastTime.empty()) {
        RECT tr = rc;
        tr.left = rc.right - 46; tr.top = rc.top + 10; tr.right = rc.right - 12; tr.bottom = rc.top + 26;
        SelectObject(dc, g_fontTiny);
        SetTextColor(dc, RGB(142, 142, 147));
        DrawText(dc, s2w(u.lastTime).c_str(), -1, &tr, DT_RIGHT | DT_TOP | DT_SINGLELINE);
        rightEdge = tr.left - 8;
    }
    if (u.unreadCount > 0) {
        std::wstring unreadStr = std::to_wstring(u.unreadCount);
        SIZE sz;
        GetTextExtentPoint32(dc, unreadStr.c_str(), (int)unreadStr.length(), &sz);
        int cx = rightEdge;
        int cy = rc.top + (rc.bottom - rc.top) / 2 - sz.cy / 2;
        RECT circleRect = { cx - sz.cx - 8, cy - 2, cx - 2, cy + sz.cy + 2 };
        HRGN rgnCirc = CreateRoundRectRgn(circleRect.left, circleRect.top, circleRect.right, circleRect.bottom, 12, 12);
        HBRUSH brRed = CreateSolidBrush(RGB(220, 50, 50));
        FillRgn(dc, rgnCirc, brRed);
        DeleteObject(rgnCirc);
        DeleteObject(brRed);
        SetTextColor(dc, RGB(255, 255, 255));
        SelectObject(dc, g_fontTiny);
        DrawText(dc, unreadStr.c_str(), -1, &circleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(dc, oldF);
}

//прокрутка
void RecalcScrollRange() {
    if (g_messages.empty()) {
        g_totalHeight = 0;
        SCROLLINFO si = { sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE, 0, 0, 0 };
        SetScrollInfo(g_hMsgView, SB_VERT, &si, TRUE);
        return;
    }
    RECT rc;
    GetClientRect(g_hMsgView, &rc);
    int clientHeight = rc.bottom - rc.top;
    const int GAP = 4;
    HDC dc = GetDC(g_hMsgView);
    int y = 8;
    for (size_t idx = 0; idx < g_messages.size(); idx++) {
        ChatMsg& msg = g_messages[idx];
        std::wstring wtext = s2w(msg.text);
        std::wstring wtime = s2w(msg.time);
        if (wtext.length() > 500) wtext = wtext.substr(0, 500);
        if (wtime.length() > 15) wtime = wtime.substr(0, 15);
        RECT trc0 = { 0, 0, MSG_MAX_W - MSG_BUBBLE_PAD_X * 2, 2000 };
        DrawText(dc, wtext.c_str(), -1, &trc0, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
        int textH = trc0.bottom - trc0.top;
        int textW = trc0.right - trc0.left;
        SIZE ts;
        GetTextExtentPoint32(dc, wtime.c_str(), (int)wtime.length(), &ts);
        int timeW = ts.cx + 6;
        int bubbleH = max(textH + MSG_BUBBLE_PAD_Y * 2 + 4, 40);
        y += bubbleH + GAP;
    }
    g_totalHeight = y - GAP + 8;
    ReleaseDC(g_hMsgView, dc);
    SCROLLINFO si = { sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE, 0, g_totalHeight, clientHeight };
    SetScrollInfo(g_hMsgView, SB_VERT, &si, TRUE);
    int maxScroll = g_totalHeight - clientHeight;
    if (maxScroll < 0) maxScroll = 0;
    if (g_scrollPos > maxScroll) g_scrollPos = maxScroll;
    SetScrollPos(g_hMsgView, SB_VERT, g_scrollPos, TRUE);
}

void ScrollToBottom() {
    if (g_messages.empty()) return;
    RECT rc;
    GetClientRect(g_hMsgView, &rc);
    int clientHeight = rc.bottom - rc.top;
    int maxScroll = g_totalHeight - clientHeight;
    if (maxScroll < 0) maxScroll = 0;
    if (g_scrollPos != maxScroll) {
        g_scrollPos = maxScroll;
        SetScrollPos(g_hMsgView, SB_VERT, g_scrollPos, TRUE);
        InvalidateRect(g_hMsgView, NULL, FALSE);
    }
}

//соо
void drawMsgView(HDC dc, RECT rc) {
    HBRUSH brBg = CreateSolidBrush(CLR_CHAT_BG);
    FillRect(dc, &rc, brBg);
    DeleteObject(brBg);
    if (g_messages.empty()) {
        SetBkMode(dc, TRANSPARENT);
        HFONT oldF = (HFONT)SelectObject(dc, g_fontMain);
        SetTextColor(dc, RGB(142, 142, 147));
        RECT trc = rc;
        DrawText(dc, L"Нет сообщений. Начните диалог!", -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldF);
        g_totalHeight = 0;
        g_scrollPos = 0;
        return;
    }
    if (g_totalHeight == 0) {
        RecalcScrollRange();
        ScrollToBottom();
    }
    HFONT oldF = (HFONT)SelectObject(dc, g_fontMain);
    const int GAP = 4;
    int y = 8 - g_scrollPos;
    for (size_t idx = 0; idx < g_messages.size(); idx++) {
        ChatMsg& msg = g_messages[idx];
        std::wstring wtext = s2w(msg.text);
        std::wstring wtime = s2w(msg.time);
        if (wtext.length() > 500) wtext = wtext.substr(0, 500);
        if (wtime.length() > 15) wtime = wtime.substr(0, 15);
        RECT trc0 = { 0, 0, MSG_MAX_W - MSG_BUBBLE_PAD_X * 2, 2000 };
        DrawText(dc, wtext.c_str(), -1, &trc0, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
        int textH = trc0.bottom - trc0.top;
        int textW = trc0.right - trc0.left;
        SIZE ts;
        GetTextExtentPoint32(dc, wtime.c_str(), (int)wtime.length(), &ts);
        int timeW = ts.cx + 6;
        int bubbleW = min(textW + MSG_BUBBLE_PAD_X * 2 + timeW + 8, MSG_MAX_W);
        int bubbleH = max(textH + MSG_BUBBLE_PAD_Y * 2 + 4, 40);
        RECT brc = rc;
        brc.top = y;
        brc.bottom = y + bubbleH;
        if (brc.bottom > rc.top && brc.top < rc.bottom) {
            if (msg.isSelf) {
                brc.left = rc.right - bubbleW - 12;
                brc.right = rc.right - 12;
            }
            else {
                brc.left = rc.left + 12;
                brc.right = rc.left + bubbleW + 12;
            }
            COLORREF bubbleFill = msg.isSelf ? CLR_BUBBLE_SELF : CLR_BUBBLE_OTHER;
            HRGN rgn = CreateRoundRectRgn(brc.left, brc.top, brc.right + 1, brc.bottom + 1, MSG_BUBBLE_RAD, MSG_BUBBLE_RAD);
            HBRUSH br = CreateSolidBrush(bubbleFill);
            FillRgn(dc, rgn, br);
            DeleteObject(rgn);
            DeleteObject(br);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            RECT txtR = brc;
            txtR.left += MSG_BUBBLE_PAD_X;
            txtR.top += MSG_BUBBLE_PAD_Y;
            txtR.right -= MSG_BUBBLE_PAD_X + timeW + 4;
            txtR.bottom -= MSG_BUBBLE_PAD_Y;
            DrawText(dc, wtext.c_str(), -1, &txtR, DT_LEFT | DT_TOP | DT_WORDBREAK);
            RECT tmR = brc;
            tmR.left = tmR.right - timeW - MSG_BUBBLE_PAD_X;
            tmR.top = tmR.bottom - MSG_BUBBLE_PAD_Y - ts.cy - 2;
            SetTextColor(dc, RGB(180, 180, 190));
            SelectObject(dc, g_fontSec);
            DrawText(dc, wtime.c_str(), -1, &tmR, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE);
            SelectObject(dc, g_fontMain);
        }
        y += bubbleH + GAP;
        if (y > rc.bottom + 500) break;
    }
    SelectObject(dc, oldF);
}

void drawChatHeader(HWND hWnd, HDC dc) {
    RECT rc;
    GetClientRect(hWnd, &rc);
    HBRUSH br = CreateSolidBrush(CLR_HEADER);
    FillRect(dc, &rc, br);
    DeleteObject(br);
    HPEN pen = CreatePen(PS_SOLID, 1, CLR_DIVIDER);
    HPEN oldPen = (HPEN)SelectObject(dc, pen);
    MoveToEx(dc, rc.left, rc.bottom - 1, NULL);
    LineTo(dc, rc.right, rc.bottom - 1);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    if (g_currentChat.empty()) {
        HFONT oldF = (HFONT)SelectObject(dc, g_fontSec);
        SetTextColor(dc, RGB(142, 142, 147));
        SetBkMode(dc, TRANSPARENT);
        RECT trc = rc;
        DrawText(dc, L"Выберите контакт", -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldF);
        return;
    }
    int L = 14;
    int avY = (rc.bottom + rc.top) / 2 - 18;
    COLORREF avatarColor = GetAvatarColor(g_currentChat);
    drawEllipse2(dc, L + 18, avY + 18, 18, 18, avatarColor);
    wchar_t init[2] = { 0 };
    if (!g_currentChat.empty()) {
        init[0] = g_currentChat[0];
        if (init[0] >= L'a' && init[0] <= L'z') init[0] = (wchar_t)(init[0] - 32);
    }
    HFONT oldF = (HFONT)SelectObject(dc, g_fontBold);
    SetTextColor(dc, CLR_TEXT_SELF);
    SetBkMode(dc, TRANSPARENT);
    RECT ir = { L, avY, L + 36, avY + 36 };
    DrawText(dc, init, 1, &ir, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    bool isOn = false;
    for (size_t i = 0; i < g_allUsers.size(); i++) {
        if (g_allUsers[i].name == g_currentChat) { isOn = g_allUsers[i].online; break; }
    }
    drawEllipse2(dc, L + 30, avY + 32, 5, 5, isOn ? CLR_ONLINE : CLR_OFFLINE);
    RECT nr = rc;
    nr.left = L + 46; nr.top = rc.top + 8; nr.right = rc.right - 12; nr.bottom = rc.top + 26;
    SetTextColor(dc, CLR_TEXT_SELF);
    SelectObject(dc, g_fontBold);
    DrawText(dc, s2w(g_currentChat).c_str(), -1, &nr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_WORD_ELL);
    RECT sr = rc;
    sr.left = L + 46; sr.top = rc.top + 28; sr.right = rc.right - 12; sr.bottom = rc.bottom - 6;
    SelectObject(dc, g_fontSec);
    SetTextColor(dc, isOn ? CLR_ONLINE : RGB(142, 142, 147));
    DrawText(dc, isOn ? L"онлайн" : L"был(а) недавно", -1, &sr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_WORD_ELL);
    SelectObject(dc, oldF);
}

//обнов
struct SrvMsg { std::string raw; SrvMsg(const std::string& s) : raw(s) {} };

void onSrvMsg(const std::string& raw) {
    LogToFile("IN: " + raw);
    SrvMsg* m = new SrvMsg(raw);
    PostMessage(g_hMain, WM_SERVER_MSG, 0, (LPARAM)m);
}

std::string extractTime(const std::string& datetime) {
    size_t pos = datetime.rfind(' ');
    if (pos != std::string::npos && pos + 1 < datetime.length())
        return datetime.substr(pos + 1);
    return datetime;
}

void handleSrvMsg(const std::string& raw) {
    LogToFile("handleSrvMsg: " + raw);
    std::string clean = raw;
    while (!clean.empty() && (clean.back() == '\r' || clean.back() == '\n' || clean.back() == ' '))
        clean.pop_back();
    auto parts = Protocol::split(clean, Protocol::DELIMITER);
    if (parts.empty()) return;
    Protocol::MessageType t = Protocol::stringToType(parts[0]);

    if (g_authWaiting) {
        if (t == Protocol::RESPONSE_OK) {
            g_authSuccess = true;
            g_authWaiting = false;
            LogToFile("Auth OK");
        }
        else if (t == Protocol::RESPONSE_ERROR) {
            g_authSuccess = false;
            g_authError = (parts.size() >= 2) ? parts[1] : "Unknown error";
            g_authWaiting = false;
            LogToFile("Auth ERROR: " + g_authError);
        }
        return;
    }

    if (t == Protocol::USER_LIST) {
        LogToFile("USER_LIST received");
        g_allUsers.clear();
        for (size_t i = 1; i < parts.size(); i++) {
            if (!parts[i].empty() && parts[i] != g_currentUser) {
                ChatUser u;
                u.name = parts[i];
                u.online = true;
                u.lastMsg = "";
                u.lastTime = "";
                u.unreadCount = 0;
                g_allUsers.push_back(u);
            }
        }
        g_filteredUsers = g_allUsers;
        SendMessage(g_hChatList, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < g_filteredUsers.size(); i++)
            SendMessage(g_hChatList, LB_ADDSTRING, 0, (LPARAM)s2w(g_filteredUsers[i].name).c_str());
        InvalidateRect(g_hChatList, NULL, FALSE);
    }
    else if (t == Protocol::MESSAGE_LIST) {
        LogToFile("MESSAGE_LIST received, count=" + std::to_string((parts.size() - 1) / 4));
        g_messages.clear();
        for (size_t i = 1; i + 3 < parts.size(); i += 4) {
            ChatMsg m;
            m.sender = parts[i];
            m.text = parts[i + 2];
            m.time = (i + 3 < parts.size()) ? extractTime(parts[i + 3]) : curTime();
            m.isSelf = (m.sender == g_currentUser);
            g_messages.push_back(m);
        }
        g_totalHeight = 0;
        RecalcScrollRange();
        ScrollToBottom();
        InvalidateRect(g_hMsgView, NULL, FALSE);
    }
    else if (t == Protocol::NEW_MESSAGE) {
        LogToFile("NEW_MESSAGE received");
        if (parts.size() >= 4) {
            std::string sender = parts[1];
            std::string text = parts[3];
            std::string time = curTime();
            bool isSelf = (sender == g_currentUser);
            ChatMsg m;
            m.sender = sender;
            m.text = text;
            m.time = time;
            m.isSelf = isSelf;
            g_messages.push_back(m);
            for (auto& u : g_allUsers) {
                if (u.name == sender) {
                    u.lastMsg = text;
                    u.lastTime = time;
                    if (!isSelf && g_currentChat != sender) {
                        u.unreadCount++;
                    }
                    break;
                }
            }
            if (!isSelf && g_currentChat == sender) {
                for (auto& u : g_allUsers) {
                    if (u.name == sender) {
                        u.unreadCount = 0;
                        break;
                    }
                }
            }
            g_filteredUsers = g_allUsers;
            RecalcScrollRange();
            ScrollToBottom();
            InvalidateRect(g_hMsgView, NULL, FALSE);
            InvalidateRect(g_hChatList, NULL, FALSE);
        }
    }
}

void reqUsers() {
    if (g_net.isConnected())
        g_net.sendMessage(Protocol::createMessage(Protocol::GET_USERS, {}));
}

void reqHist(const std::string& who) {
    if (g_net.isConnected()) {
        std::vector<std::string> args = { g_currentUser, who };
        g_net.sendMessage(Protocol::createMessage(Protocol::GET_MESSAGES, args));
    }
}

void refreshData() {
    if (!g_loggedIn || !g_net.isConnected()) return;
    reqUsers();
    if (!g_currentChat.empty()) {
        reqHist(g_currentChat);
    }
}

void sendMsg(const std::string& text) {
    if (!g_loggedIn || g_currentChat.empty() || text.empty()) return;
    std::vector<std::string> args = { g_currentUser, g_currentChat, text };
    if (g_net.sendMessage(Protocol::createMessage(Protocol::SEND_MSG, args))) {
        ChatMsg m;
        m.sender = g_currentUser;
        m.text = text;
        m.time = curTime();
        m.isSelf = true;
        g_messages.push_back(m);
        for (auto& u : g_allUsers) {
            if (u.name == g_currentChat) {
                u.lastMsg = text;
                u.lastTime = m.time;
                u.unreadCount = 0;
                break;
            }
        }
        g_filteredUsers = g_allUsers;
        RecalcScrollRange();
        ScrollToBottom();
        InvalidateRect(g_hMsgView, NULL, FALSE);
        InvalidateRect(g_hChatList, NULL, FALSE);
    }
}

void disconnect() {
    if (g_net.isConnected()) {
        g_net.disconnect();
    }
    g_loggedIn = false;
    g_currentUser.clear();
    g_currentChat.clear();
    g_allUsers.clear();
    g_filteredUsers.clear();
    g_messages.clear();
    SendMessage(g_hChatList, LB_RESETCONTENT, 0, 0);
    InvalidateRect(g_hChatList, NULL, FALSE);
    InvalidateRect(g_hMsgView, NULL, FALSE);
    InvalidateRect(g_hChatHeader, NULL, FALSE);
    SetWindowText(g_hMain, L"Messenger");
    setStatus("Отключено");
    EnableWindow(g_hMsgInput, FALSE);
    EnableWindow(g_hSendBtn, FALSE);
    KillTimer(g_hMain, 1);
}


struct LoginData { std::string host; int port; std::string user, pass; bool doReg; };
LoginData g_ld;
bool g_loginDialogResult = false;

LRESULT CALLBACK LoginDialogProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        AppConfig cfg = LoadConfig();
        SetDlgItemText(hDlg, IDC_EDIT_HOST, s2w(cfg.lastHost).c_str());
        SetDlgItemText(hDlg, IDC_EDIT_PORT, s2w(std::to_string(cfg.lastPort)).c_str());
        SetDlgItemText(hDlg, IDC_EDIT_USER, s2w(cfg.lastUsername).c_str());
        SetDlgItemText(hDlg, IDC_EDIT_PASS, s2w(cfg.lastPassword).c_str());
        SetFocus(GetDlgItem(hDlg, IDC_EDIT_USER));
        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDC_BTN_CANCEL) {
            DestroyWindow(hDlg);
            g_loginDialogResult = false;
            return TRUE;
        }
        if (id == IDC_BTN_LOGIN || id == IDC_BTN_REGISTER) {
            wchar_t buf[256];
            GetDlgItemText(hDlg, IDC_EDIT_HOST, buf, 256); g_ld.host = w2s(buf);
            GetDlgItemText(hDlg, IDC_EDIT_PORT, buf, 256); g_ld.port = _wtoi(buf);
            if (g_ld.port <= 0) g_ld.port = 8888;
            GetDlgItemText(hDlg, IDC_EDIT_USER, buf, 256); g_ld.user = w2s(buf);
            GetDlgItemText(hDlg, IDC_EDIT_PASS, buf, 256); g_ld.pass = w2s(buf);
            trimStr(g_ld.user); trimStr(g_ld.pass);
            if (g_ld.user.empty() || g_ld.pass.empty()) {
                MessageBox(hDlg, L"Заполните логин и пароль", L"Ошибка", MB_OK);
                return TRUE;
            }
            g_ld.doReg = (id == IDC_BTN_REGISTER);
            DestroyWindow(hDlg);
            g_loginDialogResult = true;
            return TRUE;
        }
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hDlg, msg, wp, lp);
}

bool ShowLoginDialog(HWND hParent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = LoginDialogProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"LoginDialogClass";
        RegisterClassEx(&wc);
        registered = true;
    }

    HWND hDlg = CreateWindowEx(WS_EX_DLGMODALFRAME, L"LoginDialogClass", L"Подключение",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, 400, 280, hParent, NULL, g_hInst, NULL);
    if (!hDlg) return false;

    HFONT hFont = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    auto createStatic = [&](const wchar_t* txt, int x, int y) {
        HWND h = CreateWindow(L"STATIC", txt, WS_VISIBLE | WS_CHILD, x, y, 80, 22, hDlg, NULL, g_hInst, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        return h;
        };
    auto createEdit = [&](int id, int x, int y, bool pass) {
        DWORD style = WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL;
        if (pass) style |= ES_PASSWORD;
        HWND h = CreateWindow(L"EDIT", L"", style, x, y, 250, 24, hDlg, (HMENU)(INT_PTR)id, g_hInst, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        return h;
        };
    auto createBtn = [&](const wchar_t* txt, int id, int x, int y) {
        HWND h = CreateWindow(L"BUTTON", txt, WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x, y, 100, 32, hDlg, (HMENU)(INT_PTR)id, g_hInst, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        return h;
        };

    createStatic(L"Сервер:", 20, 20);
    createEdit(IDC_EDIT_HOST, 110, 18, false);
    createStatic(L"Порт:", 20, 55);
    createEdit(IDC_EDIT_PORT, 110, 53, false);
    createStatic(L"Логин:", 20, 90);
    createEdit(IDC_EDIT_USER, 110, 88, false);
    createStatic(L"Пароль:", 20, 125);
    createEdit(IDC_EDIT_PASS, 110, 123, true);
    createBtn(L"Войти", IDC_BTN_LOGIN, 20, 180);
    createBtn(L"Регистрация", IDC_BTN_REGISTER, 140, 180);
    createBtn(L"Отмена", IDC_BTN_CANCEL, 280, 180);

    RECT prc, drc;
    GetWindowRect(hParent, &prc);
    GetWindowRect(hDlg, &drc);
    SetWindowPos(hDlg, NULL,
        prc.left + (prc.right - prc.left) / 2 - (drc.right - drc.left) / 2,
        prc.top + (prc.bottom - prc.top) / 2 - (drc.bottom - drc.top) / 2,
        0, 0, SWP_NOSIZE);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsWindow(hDlg)) break;
        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    DeleteObject(hFont);
    return g_loginDialogResult;
}

//вход
bool SendAndWait(const std::string& cmd, int timeoutSec = 30) {
    if (!g_net.sendMessage(cmd)) return false;
    g_authWaiting = true;
    for (int i = 0; i < timeoutSec * 10; ++i) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!g_authWaiting) break;
        Sleep(100);
    }
    if (g_authWaiting) {
        g_authWaiting = false;
        return false;
    }
    return g_authSuccess;
}

bool PerformAuth(HWND hParent, const LoginData& ld) {
    g_authError.clear();
    g_serverHost = ld.host;
    g_serverPort = ld.port;
    setStatus("Подключение к " + g_serverHost + "...");
    if (g_net.isConnected()) g_net.disconnect();
    if (!g_net.connectToServer(ld.host, ld.port)) {
        MessageBox(hParent, L"Не удалось подключиться.\nПроверьте, запущен ли сервер, и правильность адреса/порта.", L"Ошибка", MB_OK | MB_ICONERROR);
        setStatus("Не подключено");
        return false;
    }
    g_net.setOnMessage(onSrvMsg);
    setStatus("Авторизация...");

    if (ld.doReg) {
        std::string regMsg = Protocol::createMessage(Protocol::REGISTER, { ld.user, ld.pass });
        if (!SendAndWait(regMsg)) {
            std::wstring err = L"Ошибка регистрации: " + (g_authError.empty() ? L"сервер не ответил (таймаут 30 сек)" : s2w(g_authError));
            MessageBox(hParent, err.c_str(), L"Ошибка", MB_OK | MB_ICONERROR);
            setStatus("Ошибка регистрации");
            g_net.disconnect();
            return false;
        }
    }
    std::string loginMsg = Protocol::createMessage(Protocol::LOGIN, { ld.user, ld.pass });
    if (!SendAndWait(loginMsg)) {
        std::wstring err = L"Ошибка входа: " + (g_authError.empty() ? L"сервер не ответил (таймаут 30 сек)" : s2w(g_authError));
        MessageBox(hParent, err.c_str(), L"Ошибка", MB_OK | MB_ICONERROR);
        setStatus("Ошибка входа");
        g_net.disconnect();
        return false;
    }

    g_currentUser = ld.user;
    g_loggedIn = true;
    SetWindowText(hParent, (L"Messenger - " + s2w(g_currentUser)).c_str());
    wchar_t sb[128];
    swprintf(sb, 128, L"Подключено: %S:%d", g_serverHost.c_str(), g_serverPort);
    setStatus(w2s(sb));
    EnableWindow(g_hMsgInput, TRUE);
    EnableWindow(g_hSendBtn, TRUE);
    reqUsers();

    AppConfig cfg;
    cfg.lastHost = ld.host;
    cfg.lastPort = ld.port;
    cfg.lastUsername = ld.user;
    cfg.lastPassword = ld.pass;
    cfg.autoLogin = true;
    SaveConfig(cfg);

    SetTimer(g_hMain, 1, 5000, NULL);
    return true;
}

void doConnect(HWND hParent) {
    if (!ShowLoginDialog(hParent)) return;
    PerformAuth(hParent, g_ld);
}

//окна обработчик
LRESULT CALLBACK SidebarProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_CTLCOLORLISTBOX) {
        HDC dc = (HDC)wp;
        SetTextColor(dc, CLR_TEXT_SELF);
        SetBkColor(dc, CLR_SIDEBAR);
        return (LRESULT)g_hBrSidebar;
    }
    if (m == WM_DRAWITEM) {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
        if (dis->CtlType == ODT_LISTBOX) { drawChatItem(dis); return 1; }
    }
    if (m == WM_MEASUREITEM) {
        MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lp;
        if (mis->CtlType == ODT_LISTBOX) { mis->itemHeight = CHAT_ITEM_H; return 1; }
    }
    if (m == WM_COMMAND) {
        int id = LOWORD(wp), ev = HIWORD(wp);
        if (id == ID_CONNECT_BTN) { doConnect(g_hMain); return 0; }
        if (id == ID_DISCONNECT_BTN) { disconnect(); return 0; }
        if (id == ID_REFRESH_BTN) { refreshData(); return 0; }
        if (id == ID_CHAT_LIST && ev == LBN_SELCHANGE) {
            int idx = (int)SendMessage(g_hChatList, LB_GETCURSEL, 0, 0);
            if (idx != LB_ERR && idx >= 0 && idx < (int)g_filteredUsers.size()) {
                g_currentChat = g_filteredUsers[idx].name;
                g_messages.clear();
                for (auto& u : g_allUsers) {
                    if (u.name == g_currentChat) {
                        u.unreadCount = 0;
                        break;
                    }
                }
                g_filteredUsers = g_allUsers;
                g_totalHeight = 0;
                if (g_loggedIn) reqHist(g_currentChat);
                InvalidateRect(g_hChatList, NULL, FALSE);
                InvalidateRect(g_hMsgView, NULL, FALSE);
                InvalidateRect(g_hChatHeader, NULL, FALSE);
            }
            return 0;
        }
        if (id == ID_SIDEBAR_SEARCH && ev == EN_CHANGE) {
            wchar_t b[256];
            GetWindowText(g_hSearchBox, b, 256);
            std::string flt = w2s(b);
            trimStr(flt);
            g_filteredUsers.clear();
            if (flt.empty()) g_filteredUsers = g_allUsers;
            else {
                for (auto& u : g_allUsers) {
                    std::string nm = u.name;
                    std::transform(nm.begin(), nm.end(), nm.begin(), ::tolower);
                    std::string f2 = flt;
                    std::transform(f2.begin(), f2.end(), f2.begin(), ::tolower);
                    if (nm.find(f2) != std::string::npos) g_filteredUsers.push_back(u);
                }
            }
            SendMessage(g_hChatList, LB_RESETCONTENT, 0, 0);
            for (auto& u : g_filteredUsers)
                SendMessage(g_hChatList, LB_ADDSTRING, 0, (LPARAM)s2w(u.name).c_str());
            return 0;
        }
    }
    return DefWindowProc(h, m, wp, lp);
}

LRESULT CALLBACK ChatHeaderProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        drawChatHeader(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}

LRESULT CALLBACK MsgViewProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        drawMsgView(dc, rc);
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_VSCROLL) {
        SCROLLINFO si = { sizeof(SCROLLINFO), SIF_ALL };
        GetScrollInfo(h, SB_VERT, &si);
        int newPos = g_scrollPos;
        switch (LOWORD(wp)) {
        case SB_LINEUP:      newPos -= 20; break;
        case SB_LINEDOWN:    newPos += 20; break;
        case SB_PAGEUP:      newPos -= si.nPage; break;
        case SB_PAGEDOWN:    newPos += si.nPage; break;
        case SB_THUMBTRACK:  newPos = si.nTrackPos; break;
        case SB_TOP:         newPos = 0; break;
        case SB_BOTTOM:      newPos = si.nMax; break;
        }
        int maxScroll = si.nMax - (int)si.nPage;
        if (maxScroll < 0) maxScroll = 0;
        if (newPos > maxScroll) newPos = maxScroll;
        if (newPos < 0) newPos = 0;
        if (newPos != g_scrollPos) {
            g_scrollPos = newPos;
            SetScrollPos(h, SB_VERT, g_scrollPos, TRUE);
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    if (m == WM_SIZE) {
        g_totalHeight = 0;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    if (m == WM_ERASEBKGND) return 1;
    return DefWindowProc(h, m, wp, lp);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_SERVER_MSG) {
        SrvMsg* msg = (SrvMsg*)lp;
        if (msg) { handleSrvMsg(msg->raw); delete msg; }
        return 0;
    }
    if (m == WM_TIMER && wp == 1) {
        if (g_loggedIn && g_net.isConnected()) {
            refreshData();
        }
        return 0;
    }
    if (m == WM_CTLCOLORSTATIC) {
        HDC dc = (HDC)wp;
        HWND ctrl = (HWND)lp;
        if (ctrl == g_hStatusTxt) {
            SetTextColor(dc, RGB(142, 142, 147));
            SetBkColor(dc, CLR_STATUSBAR);
            return (LRESULT)g_hBrBg;
        }
        SetTextColor(dc, CLR_TEXT_SELF);
        SetBkColor(dc, CLR_BG);
        return (LRESULT)g_hBrBg;
    }
    if (m == WM_CTLCOLOREDIT) {
        HDC dc = (HDC)wp;
        SetTextColor(dc, CLR_TEXT_SELF);
        SetBkColor(dc, (HWND)lp == g_hSearchBox ? CLR_SEARCH_BG : CLR_INPUT_BG);
        return (LRESULT)g_hBrInput;
    }
    if (m == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == ID_CONNECT_BTN) { doConnect(h); return 0; }
        if (id == ID_DISCONNECT_BTN) { disconnect(); return 0; }
        if (id == ID_REFRESH_BTN) { refreshData(); return 0; }
        if (id == ID_SEND_BTN) {
            wchar_t b[2048];
            GetWindowText(g_hMsgInput, b, 2048);
            std::string t = w2s(b);
            trimStr(t);
            if (!t.empty()) {
                sendMsg(t);
                SetWindowText(g_hMsgInput, L"");
            }
            return 0;
        }
    }
    if (m == WM_KEYDOWN && wp == VK_RETURN) {
        if (GetFocus() == g_hMsgInput) {
            SendMessage(h, WM_COMMAND, ID_SEND_BTN, 0);
            return 0;
        }
    }
    if (m == WM_SIZE) {
        RECT rc; GetClientRect(h, &rc);
        int W = rc.right, H = rc.bottom;
        int sbH = H - 28, iY = sbH - INPUT_AREA_H;
        MoveWindow(g_hSidebar, 0, 0, SIDEBAR_W, sbH, TRUE);
        MoveWindow(g_hChatHeader, SIDEBAR_W, 0, W - SIDEBAR_W, CHAT_HEADER_H, TRUE);
        MoveWindow(g_hMsgView, SIDEBAR_W, CHAT_HEADER_H, W - SIDEBAR_W, sbH - CHAT_HEADER_H - INPUT_AREA_H, TRUE);
        MoveWindow(g_hMsgInput, SIDEBAR_W, iY, W - SIDEBAR_W - SEND_BTN_W - 4, INPUT_AREA_H - 4, TRUE);
        MoveWindow(g_hSendBtn, SIDEBAR_W + W - SIDEBAR_W - SEND_BTN_W, iY, SEND_BTN_W, INPUT_AREA_H - 4, TRUE);
        MoveWindow(g_hStatusTxt, 0, H - 28, W, 28, TRUE);
        return 0;
    }
    if (m == WM_DESTROY) {
        KillTimer(h, 1);
        g_net.disconnect();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}
//автовход
void TryAutoLogin() {
    AppConfig cfg = LoadConfig();
    if (cfg.autoLogin && !cfg.lastUsername.empty() && !cfg.lastPassword.empty()) {
        LoginData ld;
        ld.host = cfg.lastHost;
        ld.port = cfg.lastPort;
        ld.user = cfg.lastUsername;
        ld.pass = cfg.lastPassword;
        ld.doReg = false;
        PerformAuth(g_hMain, ld);
    }
}


int APIENTRY wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int show) {
    g_hInst = hi;
    SetProcessDPIAware();
    g_hBrBg = CreateSolidBrush(CLR_BG);
    g_hBrSidebar = CreateSolidBrush(CLR_SIDEBAR);
    g_hBrInput = CreateSolidBrush(CLR_INPUT_BG);
    g_fontMain = CreateFont(FONT_MAIN_SZ, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontSec = CreateFont(FONT_SEC_SZ, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontTiny = CreateFont(FONT_TINY_SZ, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontBold = CreateFont(FONT_MAIN_SZ, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontBtn = CreateFont(FONT_SEC_SZ, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hi, LoadIcon(NULL, IDI_APPLICATION), LoadCursor(NULL, IDC_ARROW), g_hBrBg, NULL, L"MessengerApp", NULL };
    RegisterClassEx(&wc);
    WNDCLASSEX wcS = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, SidebarProc, 0, 0, hi, NULL, LoadCursor(NULL, IDC_ARROW), g_hBrSidebar, NULL, L"MessengerSidebar", NULL };
    RegisterClassEx(&wcS);
    WNDCLASSEX wcH = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, ChatHeaderProc, 0, 0, hi, NULL, LoadCursor(NULL, IDC_ARROW), CreateSolidBrush(CLR_HEADER), NULL, L"MessengerChatHeader", NULL };
    RegisterClassEx(&wcH);
    WNDCLASSEX wcM = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, MsgViewProc, 0, 0, hi, NULL, LoadCursor(NULL, IDC_ARROW), CreateSolidBrush(CLR_CHAT_BG), NULL, L"MessengerMsgView", NULL };
    RegisterClassEx(&wcM);

    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = min(1100, sw), wh = min(700, sh);
    g_hMain = CreateWindowEx(0, L"MessengerApp", L"Messenger", WS_OVERLAPPEDWINDOW,
        (sw - ww) / 2, (sh - wh) / 2, ww, wh, NULL, NULL, hi, NULL);

    int tY = 50;
    g_hSidebar = CreateWindowEx(0, L"MessengerSidebar", L"", WS_CHILD | WS_VISIBLE, 0, 0, SIDEBAR_W, wh, g_hMain, NULL, hi, NULL);
    g_hSearchBox = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 10, 10, SIDEBAR_W - 20, 30, g_hSidebar, (HMENU)ID_SIDEBAR_SEARCH, hi, NULL);
    SendMessage(g_hSearchBox, WM_SETFONT, (WPARAM)g_fontSec, TRUE);
    g_hConnectBtn = CreateWindow(L"BUTTON", L"Подключиться", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, tY, SIDEBAR_W - 20, 36, g_hSidebar, (HMENU)ID_CONNECT_BTN, hi, NULL);
    SendMessage(g_hConnectBtn, WM_SETFONT, (WPARAM)g_fontBtn, TRUE);
    g_hRefreshBtn = CreateWindow(L"BUTTON", L"Обновить", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, tY + 42, SIDEBAR_W - 20, 36, g_hSidebar, (HMENU)ID_REFRESH_BTN, hi, NULL);
    SendMessage(g_hRefreshBtn, WM_SETFONT, (WPARAM)g_fontBtn, TRUE);
    g_hDisconnectBtn = CreateWindow(L"BUTTON", L"Отключиться", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, tY + 84, SIDEBAR_W - 20, 36, g_hSidebar, (HMENU)ID_DISCONNECT_BTN, hi, NULL);
    SendMessage(g_hDisconnectBtn, WM_SETFONT, (WPARAM)g_fontBtn, TRUE);
    g_hChatList = CreateWindow(L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, 0, tY + 126, SIDEBAR_W, wh - 192, g_hSidebar, (HMENU)ID_CHAT_LIST, hi, NULL);
    SendMessage(g_hChatList, WM_SETFONT, (WPARAM)g_fontMain, TRUE);

    int cX = SIDEBAR_W, cW = ww - SIDEBAR_W, cH = wh - 28, mY = cH - INPUT_AREA_H;
    g_hChatHeader = CreateWindowEx(0, L"MessengerChatHeader", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, cX, 0, cW, CHAT_HEADER_H, g_hMain, NULL, hi, NULL);
    HWND hSeparator = CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDVERT, SIDEBAR_W, 0, 2, wh, g_hMain, NULL, hi, NULL);
    g_hMsgView = CreateWindowEx(0, L"MessengerMsgView", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPSIBLINGS, cX, CHAT_HEADER_H, cW, cH - CHAT_HEADER_H - INPUT_AREA_H, g_hMain, NULL, hi, NULL);
    g_hMsgInput = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_BORDER | ES_AUTOHSCROLL | ES_MULTILINE, cX, mY, cW - SEND_BTN_W - 4, INPUT_AREA_H - 4, g_hMain, (HMENU)ID_MSG_INPUT, hi, NULL);
    SendMessage(g_hMsgInput, WM_SETFONT, (WPARAM)g_fontMain, TRUE);
    HFONT hFontArrow = CreateFont(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
    g_hSendBtn = CreateWindow(L"BUTTON", L"➤", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_PUSHBUTTON, cX + cW - SEND_BTN_W, mY, SEND_BTN_W, INPUT_AREA_H - 4, g_hMain, (HMENU)ID_SEND_BTN, hi, NULL);
    SendMessage(g_hSendBtn, WM_SETFONT, (WPARAM)hFontArrow, TRUE);
    DeleteObject(hFontArrow);
    g_hStatusTxt = CreateWindow(L"STATIC", L"Не подключено", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, wh - 28, ww, 28, g_hMain, NULL, hi, NULL);
    SendMessage(g_hStatusTxt, WM_SETFONT, (WPARAM)g_fontTiny, TRUE);

    EnableWindow(g_hMsgInput, FALSE);
    EnableWindow(g_hSendBtn, FALSE);
    ShowWindow(g_hMain, show);
    UpdateWindow(g_hMain);

    TryAutoLogin();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.hwnd == g_hMsgInput && msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            SendMessage(g_hMain, WM_COMMAND, ID_SEND_BTN, 0);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DeleteObject(g_hBrBg); DeleteObject(g_hBrSidebar); DeleteObject(g_hBrInput);
    DeleteObject(g_fontMain); DeleteObject(g_fontSec); DeleteObject(g_fontTiny);
    DeleteObject(g_fontBold); DeleteObject(g_fontBtn);
    return (int)msg.wParam;
}
