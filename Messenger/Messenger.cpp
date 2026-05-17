// Messenger Client - Telegram-Style Redesign
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <sstream>
#include <ctime>
#include <algorithm>
#include "network.h"
#include "protocol.h"
// #include "ssh_deploy.h" // Отключено - не нужен libssh2
#pragma comment(lib, "comctl32.lib")

#define DT_WORD_ELL 0x00040000L
#define ODS_HOT_CUSTOM 0x00000040L

#define CLR_BG RGB(26, 26, 30)
#define CLR_SIDEBAR RGB(30, 30, 34)
#define CLR_CHAT_BG RGB(30, 30, 34)
#define CLR_HEADER RGB(30, 30, 34)
#define CLR_BUBBLE_SELF RGB(0, 120, 255)
#define CLR_BUBBLE_OTHER RGB(46, 46, 50)
#define CLR_TEXT RGB(255, 255, 255)
#define CLR_TEXT_SEC RGB(142, 142, 147)
#define CLR_TEXT_BUBBLE RGB(255, 255, 255)
#define CLR_DIVIDER RGB(42, 42, 46)
#define CLR_ONLINE RGB(0, 200, 100)
#define CLR_OFFLINE RGB(100, 100, 110)
#define CLR_INPUT_BG RGB(46, 46, 50)
#define CLR_STATUSBAR RGB(22, 22, 25)
#define CLR_SEARCH_BG RGB(36, 36, 42)

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
#define ID_DEPLOY_BTN 107

#define IDC_DLG_HOST 201
#define IDC_DLG_PORT 202
#define IDC_DLG_USER 203
#define IDC_DLG_PASS 204
#define IDC_DLG_REGISTER 205
#define IDC_DLG_LOGIN_BTN 206

#define IDC_SSH_HOST 301
#define IDC_SSH_PORT 302
#define IDC_SSH_USER 303
#define IDC_SSH_PASS 304
#define IDC_SSH_LOG 305
#define IDC_SSH_DEPLOY_BTN 306

HINSTANCE g_hInst = NULL;
HWND g_hMain = NULL, g_hSidebar = NULL, g_hChatHeader = NULL, g_hMsgView = NULL;
HWND g_hMsgInput = NULL, g_hSendBtn = NULL, g_hChatList = NULL;
HWND g_hSearchBox = NULL, g_hStatusTxt = NULL, g_hConnectBtn = NULL, g_hDeployBtn = NULL;
HWND g_hLoginDlg = NULL, g_hDeployDlg = NULL, g_hSshLog = NULL;

HFONT g_fontMain = NULL, g_fontSec = NULL, g_fontTiny = NULL, g_fontBold = NULL, g_fontBtn = NULL;
HBRUSH g_hBrBg = NULL, g_hBrSidebar = NULL, g_hBrInput = NULL;

bool g_loggedIn = false;
std::string g_currentUser, g_currentChat, g_serverHost;
int g_serverPort = 8888;
const UINT WM_SERVER_MSG = WM_USER + 1;

struct ChatUser { std::string name, lastMsg, lastTime; bool online = true; };
struct ChatMsg { std::string sender, text, time; bool isSelf = false; };

std::vector<ChatUser> g_allUsers, g_filteredUsers;
std::vector<ChatMsg> g_messages;
NetworkClient g_net;

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

void drawRoundRect(HDC dc, RECT rc, int rad, COLORREF fill) {
    if (rad <= 0) {
        HBRUSH br = CreateSolidBrush(fill);
        FillRect(dc, &rc, br);
        DeleteObject(br);
        return;
    }
    HRGN rgn = CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, rad, rad);
    HBRUSH br = CreateSolidBrush(fill);
    FillRgn(dc, rgn, br);
    DeleteObject(rgn);
    DeleteObject(br);
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
    int L = 14;
    int avY = rc.top + (rc.bottom - rc.top) / 2 - 18;
    drawEllipse2(dc, L + 18, avY + 18, 18, 18, RGB(55, 60, 75));
    wchar_t init[2] = { 0 };
    if (!u.name.empty()) {
        init[0] = u.name[0];
        if (init[0] >= L'a' && init[0] <= L'z') init[0] = (wchar_t)(init[0] - 32);
    }
    HFONT oldF = (HFONT)SelectObject(dc, g_fontBold);
    SetTextColor(dc, CLR_TEXT);
    SetBkMode(dc, TRANSPARENT);
    RECT ir = { L, avY, L + 36, avY + 36 };
    DrawText(dc, init, 1, &ir, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    drawEllipse2(dc, L + 30, avY + 32, 5, 5, u.online ? CLR_ONLINE : CLR_OFFLINE);
    RECT nr = rc;
    nr.left = L + 48; nr.top = rc.top + 10; nr.right = rc.right - 12; nr.bottom = rc.top + 26;
    SetTextColor(dc, CLR_TEXT);
    SelectObject(dc, g_fontBold);
    DrawText(dc, s2w(u.name).c_str(), -1, &nr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_WORD_ELL);
    RECT mr = rc;
    mr.left = L + 48; mr.top = rc.top + 28; mr.right = rc.right - 40; mr.bottom = rc.bottom - 8;
    SetTextColor(dc, CLR_TEXT_SEC);
    SelectObject(dc, g_fontSec);
    std::wstring lm = s2w(u.lastMsg);
    if (lm.size() > 35) lm = lm.substr(0, 35) + L"...";
    DrawText(dc, lm.c_str(), -1, &mr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_WORD_ELL);
    if (!u.lastTime.empty()) {
        RECT tr = rc;
        tr.left = rc.right - 46; tr.top = rc.top + 10; tr.right = rc.right - 12; tr.bottom = rc.top + 26;
        SelectObject(dc, g_fontTiny);
        SetTextColor(dc, CLR_TEXT_SEC);
        DrawText(dc, s2w(u.lastTime).c_str(), -1, &tr, DT_RIGHT | DT_TOP | DT_SINGLELINE);
    }
    SelectObject(dc, oldF);
}

void drawMsgView(HDC dc, RECT rc) {
    HBRUSH brBg = CreateSolidBrush(CLR_CHAT_BG);
    FillRect(dc, &rc, brBg);
    DeleteObject(brBg);
    if (g_messages.empty()) {
        SetBkMode(dc, TRANSPARENT);
        HFONT oldF = (HFONT)SelectObject(dc, g_fontSec);
        SetTextColor(dc, CLR_TEXT_SEC);
        RECT trc = rc;
        DrawText(dc, L"\u0412\u044b\u0431\u0435\u0440\u0438\u0442\u0435 \u0447\u0430\u0442", -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldF);
        return;
    }
    HFONT oldF = (HFONT)SelectObject(dc, g_fontMain);
    int y = rc.bottom - 8;
    const int GAP = 4;
    for (int i = (int)g_messages.size() - 1; i >= 0 && y > rc.top + 8; i--) {
        ChatMsg& m = g_messages[i];
        wchar_t wtext[1024], wtime[16];
        int tlen = (int)m.text.length();
        if (tlen > 500) tlen = 500;
        size_t _r;
        mbstowcs_s(&_r, wtext, tlen + 1, m.text.c_str(), tlen);
        wcsncpy_s(wtime, 16, s2w(m.time).c_str(), 15);
        RECT trc0 = { 0, 0, MSG_MAX_W - MSG_BUBBLE_PAD_X * 2, 2000 };
        DrawText(dc, wtext, -1, &trc0, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
        int textH = trc0.bottom - trc0.top;
        int textW = trc0.right - trc0.left;
        SIZE ts;
        GetTextExtentPoint32(dc, wtime, (int)wcslen(wtime), &ts);
        int timeW = ts.cx + 6;
        int bubbleW = min(textW + MSG_BUBBLE_PAD_X * 2 + timeW + 8, MSG_MAX_W);
        int bubbleH = max(textH + MSG_BUBBLE_PAD_Y * 2 + 4, 40);
        RECT brc = rc;
        brc.bottom = y;
        brc.top = y - bubbleH;
        if (m.isSelf) {
            brc.left = rc.right - bubbleW - 12;
            brc.right = rc.right - 12;
        }
        else {
            brc.left = rc.left + 12;
            brc.right = rc.left + bubbleW + 12;
        }
        COLORREF bubbleFill = m.isSelf ? CLR_BUBBLE_SELF : CLR_BUBBLE_OTHER;
        drawRoundRect(dc, brc, MSG_BUBBLE_RAD, bubbleFill);
        RECT txtR = brc;
        txtR.left += MSG_BUBBLE_PAD_X;
        txtR.top += MSG_BUBBLE_PAD_Y;
        txtR.right -= MSG_BUBBLE_PAD_X + timeW + 4;
        txtR.bottom -= MSG_BUBBLE_PAD_Y;
        SetTextColor(dc, CLR_TEXT_BUBBLE);
        DrawText(dc, wtext, -1, &txtR, DT_LEFT | DT_TOP | DT_WORDBREAK);
        RECT tmR = brc;
        tmR.left = tmR.right - timeW - MSG_BUBBLE_PAD_X;
        tmR.top = tmR.bottom - MSG_BUBBLE_PAD_Y - ts.cy - 2;
        SetTextColor(dc, RGB(140, 155, 175));
        SelectObject(dc, g_fontTiny);
        DrawText(dc, wtime, -1, &tmR, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE);
        SelectObject(dc, g_fontMain);
        y = brc.top - GAP;
    }
    SelectObject(dc, oldF);
}

void drawChatHeader(HWND hWnd, HDC dc) {
    RECT rc;
    GetClientRect(hWnd, &rc);
    HBRUSH br = CreateSolidBrush(CLR_HEADER);
    FillRect(dc, &rc, br);
    DeleteObject(br);
    RECT dr = rc;
    dr.top = dr.bottom - 1;
    HBRUSH brd = CreateSolidBrush(CLR_DIVIDER);
    FillRect(dc, &dr, brd);
    DeleteObject(brd);
    if (g_currentChat.empty()) {
        HFONT oldF = (HFONT)SelectObject(dc, g_fontSec);
        SetTextColor(dc, CLR_TEXT_SEC);
        SetBkMode(dc, TRANSPARENT);
        RECT trc = rc;
        DrawText(dc, L"\u0412\u044b\u0431\u0435\u0440\u0438\u0442\u0435 \u043a\u043e\u043d\u0442\u0430\u043a\u0442", -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldF);
        return;
    }
    int L = 14;
    int avY = (rc.bottom + rc.top) / 2 - 18;
    drawEllipse2(dc, L + 18, avY + 18, 18, 18, RGB(55, 60, 75));
    wchar_t init[2] = { 0 };
    if (!g_currentChat.empty()) {
        init[0] = g_currentChat[0];
        if (init[0] >= L'a' && init[0] <= L'z') init[0] = (wchar_t)(init[0] - 32);
    }
    HFONT oldF = (HFONT)SelectObject(dc, g_fontBold);
    SetTextColor(dc, CLR_TEXT);
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
    SetTextColor(dc, CLR_TEXT);
    SelectObject(dc, g_fontBold);
    DrawText(dc, s2w(g_currentChat).c_str(), -1, &nr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_WORD_ELL);
    RECT sr = rc;
    sr.left = L + 46; sr.top = rc.top + 28; sr.right = rc.right - 12; sr.bottom = rc.bottom - 6;
    SelectObject(dc, g_fontSec);
    SetTextColor(dc, isOn ? CLR_ONLINE : CLR_TEXT_SEC);
    DrawText(dc, isOn ? L"\u043e\u043d\u043b\u0430\u0439\u043d" : L"\u0431\u044b\u043b(\u0430) \u043d\u0435\u0434\u0430\u0432\u043d\u043e", -1, &sr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_WORD_ELL);
    SelectObject(dc, oldF);
}

struct SrvMsg { std::string raw; SrvMsg(const std::string& s) : raw(s) {} };

void onSrvMsg(const std::string& raw) {
    SrvMsg* m = new SrvMsg(raw);
    PostMessage(g_hMain, WM_SERVER_MSG, 0, (LPARAM)m);
}

void handleSrvMsg(const std::string& raw) {
    auto parts = Protocol::split(raw, Protocol::DELIMITER);
    if (parts.empty()) return;
    Protocol::MessageType t = Protocol::stringToType(parts[0]);
    if (t == Protocol::USER_LIST) {
        g_allUsers.clear();
        for (size_t i = 1; i < parts.size(); i++) {
            if (!parts[i].empty() && parts[i] != g_currentUser) {
                ChatUser u;
                u.name = parts[i];
                u.online = true;
                u.lastMsg = "";
                u.lastTime = "";
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
        g_messages.clear();
        for (size_t i = 1; i + 3 < parts.size(); i += 4) {
            ChatMsg m;
            m.sender = parts[i];
            m.text = parts[i + 2];
            m.time = (i + 3 < parts.size()) ? parts[i + 3] : curTime();
            m.isSelf = (m.sender == g_currentUser);
            g_messages.push_back(m);
        }
        InvalidateRect(g_hMsgView, NULL, FALSE);
    }
    else if (t == Protocol::NEW_MESSAGE) {
        if (parts.size() >= 4) {
            ChatMsg m;
            m.sender = parts[1];
            m.text = parts[3];
            m.time = curTime();
            m.isSelf = (m.sender == g_currentUser);
            g_messages.push_back(m);
            for (size_t i = 0; i < g_allUsers.size(); i++) {
                if (g_allUsers[i].name == m.sender) {
                    g_allUsers[i].lastMsg = m.text;
                    g_allUsers[i].lastTime = m.time;
                    break;
                }
            }
            g_filteredUsers = g_allUsers;
            InvalidateRect(g_hMsgView, NULL, FALSE);
            InvalidateRect(g_hChatList, NULL, FALSE);
        }
    }
    else if (t == Protocol::RESPONSE_ERROR) {
        if (parts.size() >= 2)
            MessageBox(g_hMain, s2w(parts[1]).c_str(), L"\u041e\u0448\u0438\u0431\u043a\u0430", MB_OK | MB_ICONERROR);
    }
}

void reqUsers() {
    if (g_net.isConnected()) {
        std::string r = Protocol::createMessage(Protocol::GET_USERS, std::vector<std::string>());
        g_net.sendMessage(r);
    }
}

void reqHist(const std::string& who) {
    if (g_net.isConnected()) {
        std::vector<std::string> args;
        args.push_back(g_currentUser);
        args.push_back(who);
        std::string r = Protocol::createMessage(Protocol::GET_MESSAGES, args);
        g_net.sendMessage(r);
    }
}

void sendMsg(const std::string& text) {
    if (!g_loggedIn || g_currentChat.empty() || text.empty()) return;
    std::vector<std::string> args;
    args.push_back(g_currentUser);
    args.push_back(g_currentChat);
    args.push_back(text);
    std::string r = Protocol::createMessage(Protocol::SEND_MSG, args);
    if (g_net.sendMessage(r)) {
        ChatMsg m;
        m.sender = g_currentUser;
        m.text = text;
        m.time = curTime();
        m.isSelf = true;
        g_messages.push_back(m);
        for (size_t i = 0; i < g_allUsers.size(); i++) {
            if (g_allUsers[i].name == g_currentChat) {
                g_allUsers[i].lastMsg = text;
                g_allUsers[i].lastTime = m.time;
                break;
            }
        }
        InvalidateRect(g_hMsgView, NULL, FALSE);
        InvalidateRect(g_hChatList, NULL, FALSE);
    }
}

struct LoginData { std::string host; int port; std::string user, pass; bool doReg; };
LoginData g_ld;


INT_PTR CALLBACK LoginDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) {
        SetDlgItemText(hDlg, IDC_DLG_HOST, L"127.0.0.1");
        SetDlgItemText(hDlg, IDC_DLG_PORT, L"8888");
        SetFocus(GetDlgItem(hDlg, IDC_DLG_USER));
        return 0;
    }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDCANCEL) { EndDialog(hDlg, IDCANCEL); return 1; }
        if (id == IDC_DLG_LOGIN_BTN || id == IDC_DLG_REGISTER) {
            wchar_t b[512];
            GetDlgItemText(hDlg, IDC_DLG_HOST, b, 512); g_ld.host = w2s(b);
            GetDlgItemText(hDlg, IDC_DLG_PORT, b, 512); g_ld.port = _wtoi(b);
            if (g_ld.port <= 0) g_ld.port = 8888;
            GetDlgItemText(hDlg, IDC_DLG_USER, b, 512); g_ld.user = w2s(b);
            GetDlgItemText(hDlg, IDC_DLG_PASS, b, 512); g_ld.pass = w2s(b);
            trimStr(g_ld.user); trimStr(g_ld.pass);
            if (g_ld.user.empty()) { MessageBox(hDlg, L"\u0412\u0432\u0435\u0434\u0438\u0442\u0435 \u043b\u043e\u0433\u0438\u043d", L"\u041e\u0448\u0438\u0431\u043a\u0430", MB_OK); return 1; }
            if (g_ld.pass.empty()) { MessageBox(hDlg, L"\u0412\u0432\u0435\u0434\u0438\u0442\u0435 \u043f\u0430\u0440\u043e\u043b\u044c", L"\u041e\u0448\u0438\u0431\u043a\u0430", MB_OK); return 1; }
            g_ld.doReg = (id == IDC_DLG_REGISTER);
            EndDialog(hDlg, IDOK);
            return 1;
        }
    }
    return 0;
}

void doConnect(HWND hParent) {
    g_ld = {};
    INT_PTR r = DialogBoxParam(g_hInst, MAKEINTRESOURCE(102), hParent, LoginDlgProc, 0);
    if (r != IDOK || g_ld.host.empty()) return;
    g_serverHost = g_ld.host;
    g_serverPort = g_ld.port;
    setStatus("Подключение к " + g_serverHost + "...");
    if (g_net.isConnected()) g_net.disconnect();
    if (!g_net.connectToServer(g_ld.host, g_ld.port)) {
        MessageBox(hParent, L"\u041d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u043f\u043e\u0434\u043a\u043b\u044e\u0447\u0438\u0442\u044c\u0441\u044f.\n\u041f\u0440\u043e\u0432\u0435\u0440\u044c\u0442\u0435 \u0430\u0434\u0440\u0435\u0441 \u0438 \u043f\u043e\u0440\u0442.", L"\u041e\u0448\u0438\u0431\u043a\u0430", MB_OK | MB_ICONERROR);
        setStatus("\u041d\u0435 \u043f\u043e\u0434\u043a\u043b\u044e\u0447\u0435\u043d\u043e");
        return;
    }
    g_net.setOnMessage(onSrvMsg);
    setStatus("\u0410\u0432\u0442\u043e\u0440\u0438\u0437\u0430\u0446\u0438\u044f...");
    Sleep(100);
    if (g_ld.doReg) {
        std::vector<std::string> a; a.push_back(g_ld.user); a.push_back(g_ld.pass);
        g_net.sendMessage(Protocol::createMessage(Protocol::REGISTER, a));
        Sleep(250);
    }
    std::vector<std::string> a2; a2.push_back(g_ld.user); a2.push_back(g_ld.pass);
    g_net.sendMessage(Protocol::createMessage(Protocol::LOGIN, a2));
    Sleep(250);
    g_currentUser = g_ld.user;
    g_loggedIn = true;
    std::wstring ttl = L"Messenger - " + s2w(g_currentUser);
    SetWindowText(hParent, ttl.c_str());
    wchar_t sb[128];
    swprintf(sb, 128, L"\u041f\u043e\u0434\u043a\u043b\u044e\u0447\u0435\u043d\u043e: %S:%d", g_serverHost.c_str(), g_serverPort);
    setStatus(w2s(sb));
    EnableWindow(g_hMsgInput, TRUE);
    EnableWindow(g_hSendBtn, TRUE);
    reqUsers();
}
INT_PTR CALLBACK DeployDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) {
        // Показываем информацию о развертывании
        HWND hLog = GetDlgItem(hDlg, IDC_SSH_LOG);
        std::wstring info =
            L"Для запуска сервера используйте один из способов:\r\n\r\n"
            L"1. WSL (Windows Subsystem for Linux):\r\n"
            L"   • PowerShell от админа: wsl --install\r\n"
            L"   • Перезагрузка\r\n"
            L"   • wsl\r\n"
            L"   • cd ~/Server && ./install.sh\r\n\r\n"
            L"2. VPS/облако:\r\n"
            L"   • Скопируйте Server/ на сервер\r\n"
            L"   • Запустите ./install.sh\r\n\r\n"
            L"3. Docker:\r\n"
            L"   • docker build -t msg-srv .\r\n"
            L"   • docker run -p 8888:8888 msg-srv\r\n\r\n"
            L"Подробнее: Server/README_SERVER.md\r\n";
        SetWindowText(hLog, info.c_str());
        SetFocus(GetDlgItem(hDlg, IDCANCEL));
        return 1;
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return 1;
        }
    }
    return 0;
}

void showDeploy(HWND hParent) {
    DialogBoxParam(g_hInst, MAKEINTRESOURCE(103), hParent, DeployDlgProc, 0);
}

LRESULT CALLBACK SidebarProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_COMMAND) {}
    if (m == WM_CTLCOLORLISTBOX) {
        HDC dc = (HDC)wp;
        SetTextColor(dc, CLR_TEXT);
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
        if (id == ID_DEPLOY_BTN) { showDeploy(g_hMain); return 0; }
        if (id == ID_CHAT_LIST && ev == LBN_SELCHANGE) {
            int idx = (int)SendMessage(g_hChatList, LB_GETCURSEL, 0, 0);
            if (idx != LB_ERR && idx >= 0 && idx < (int)g_filteredUsers.size()) {
                g_currentChat = g_filteredUsers[idx].name;
                g_messages.clear();
                InvalidateRect(g_hMsgView, NULL, FALSE);
                InvalidateRect(g_hChatHeader, NULL, FALSE);
                if (g_loggedIn) reqHist(g_currentChat);
            }
            return 0;
        }
        if (id == ID_SIDEBAR_SEARCH && ev == EN_CHANGE) {
            wchar_t b[256];
            GetWindowText(g_hSearchBox, b, 256);
            std::string flt = w2s(b);
            trimStr(flt);
            g_filteredUsers.clear();
            if (flt.empty()) {
                g_filteredUsers = g_allUsers;
            }
            else {
                for (size_t i = 0; i < g_allUsers.size(); i++) {
                    std::string nm = g_allUsers[i].name;
                    std::transform(nm.begin(), nm.end(), nm.begin(), (int(*)(int))std::tolower);
                    std::string f2 = flt;
                    std::transform(f2.begin(), f2.end(), f2.begin(), (int(*)(int))std::tolower);
                    if (nm.find(f2) != std::string::npos) g_filteredUsers.push_back(g_allUsers[i]);
                }
            }
            SendMessage(g_hChatList, LB_RESETCONTENT, 0, 0);
            for (size_t i = 0; i < g_filteredUsers.size(); i++)
                SendMessage(g_hChatList, LB_ADDSTRING, 0, (LPARAM)s2w(g_filteredUsers[i].name).c_str());
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
        RECT rc;
        GetClientRect(h, &rc);
        drawMsgView(dc, rc);
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_SIZE) { InvalidateRect(h, NULL, FALSE); return 0; }
    return DefWindowProc(h, m, wp, lp);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_SERVER_MSG) {
        SrvMsg* msg = (SrvMsg*)lp;
        if (msg) { handleSrvMsg(msg->raw); delete msg; }
        return 0;
    }
    if (m == WM_CTLCOLORSTATIC) {
        HDC dc = (HDC)wp;
        HWND ctrl = (HWND)lp;
        if (ctrl == g_hStatusTxt) {
            SetTextColor(dc, CLR_TEXT_SEC);
            SetBkColor(dc, CLR_STATUSBAR);
            return (LRESULT)g_hBrBg;
        }
        SetTextColor(dc, CLR_TEXT);
        SetBkColor(dc, CLR_BG);
        return (LRESULT)g_hBrBg;
    }
    if (m == WM_CTLCOLOREDIT) {
        HDC dc = (HDC)wp;
        SetTextColor(dc, CLR_TEXT);
        SetBkColor(dc, (HWND)lp == g_hSearchBox ? CLR_SEARCH_BG : CLR_INPUT_BG);
        return (LRESULT)g_hBrInput;
    }
    if (m == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == ID_CONNECT_BTN) { doConnect(h); return 0; }
        if (id == ID_DEPLOY_BTN) { showDeploy(h); return 0; }
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
        RECT rc;
        GetClientRect(h, &rc);
        int W = rc.right, H = rc.bottom;
        int sbH = H - 28;
        int iY = sbH - INPUT_AREA_H;
        MoveWindow(g_hSidebar, 0, 0, SIDEBAR_W, sbH, TRUE);
        MoveWindow(g_hChatHeader, SIDEBAR_W, 0, W - SIDEBAR_W, CHAT_HEADER_H, TRUE);
        MoveWindow(g_hMsgView, SIDEBAR_W, CHAT_HEADER_H, W - SIDEBAR_W, sbH - CHAT_HEADER_H - INPUT_AREA_H, TRUE);
        MoveWindow(g_hMsgInput, SIDEBAR_W, iY, W - SIDEBAR_W - SEND_BTN_W - 4, INPUT_AREA_H - 4, TRUE);
        MoveWindow(g_hSendBtn, SIDEBAR_W + W - SIDEBAR_W - SEND_BTN_W, iY, SEND_BTN_W, INPUT_AREA_H - 4, TRUE);
        MoveWindow(g_hStatusTxt, 0, H - 28, W, 28, TRUE);
        return 0;
    }
    if (m == WM_DESTROY) {
        g_net.disconnect();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}

int APIENTRY wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int show) {
    g_hInst = hi;
    g_hBrBg = CreateSolidBrush(CLR_BG);
    g_hBrSidebar = CreateSolidBrush(CLR_SIDEBAR);
    g_hBrInput = CreateSolidBrush(CLR_INPUT_BG);
    g_fontMain = CreateFont(FONT_MAIN_SZ, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontSec = CreateFont(FONT_SEC_SZ, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontTiny = CreateFont(FONT_TINY_SZ, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontBold = CreateFont(FONT_MAIN_SZ, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontBtn = CreateFont(FONT_SEC_SZ, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_hBrBg;
    wc.lpszClassName = L"MessengerApp";
    RegisterClassEx(&wc);
    WNDCLASSEX wcS = {};
    wcS.cbSize = sizeof(WNDCLASSEX);
    wcS.style = CS_HREDRAW | CS_VREDRAW;
    wcS.lpfnWndProc = SidebarProc;
    wcS.hInstance = hi;
    wcS.hbrBackground = g_hBrSidebar;
    wcS.lpszClassName = L"MessengerSidebar";
    wcS.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassEx(&wcS);
    WNDCLASSEX wcH = {};
    wcH.cbSize = sizeof(WNDCLASSEX);
    wcH.style = CS_HREDRAW | CS_VREDRAW;
    wcH.lpfnWndProc = ChatHeaderProc;
    wcH.hInstance = hi;
    wcH.hbrBackground = CreateSolidBrush(CLR_HEADER);
    wcH.lpszClassName = L"MessengerChatHeader";
    RegisterClassEx(&wcH);
    WNDCLASSEX wcM = {};
    wcM.cbSize = sizeof(WNDCLASSEX);
    wcM.style = CS_HREDRAW | CS_VREDRAW;
    wcM.lpfnWndProc = MsgViewProc;
    wcM.hInstance = hi;
    wcM.hbrBackground = CreateSolidBrush(CLR_CHAT_BG);
    wcM.lpszClassName = L"MessengerMsgView";
    RegisterClassEx(&wcM);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = min(1100, sw);
    int wh = min(700, sh);
    g_hMain = CreateWindowEx(0, L"MessengerApp", L"Messenger", WS_OVERLAPPEDWINDOW,
        (sw - ww) / 2, (sh - wh) / 2, ww, wh, NULL, NULL, hi, NULL);
    int tY = 50;
    g_hSidebar = CreateWindowEx(0, L"MessengerSidebar", L"", WS_CHILD | WS_VISIBLE,
        0, 0, SIDEBAR_W, wh, g_hMain, NULL, hi, NULL);
    g_hSearchBox = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        10, 10, SIDEBAR_W - 20, 30, g_hSidebar, (HMENU)ID_SIDEBAR_SEARCH, hi, NULL);
    SendMessage(g_hSearchBox, WM_SETFONT, (WPARAM)g_fontSec, TRUE);
    g_hConnectBtn = CreateWindow(L"BUTTON", L"\u041f\u043e\u0434\u043a\u043b\u044e\u0447\u0438\u0442\u044c\u0441\u044f", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, tY, SIDEBAR_W - 20, 36, g_hSidebar, (HMENU)ID_CONNECT_BTN, hi, NULL);
    SendMessage(g_hConnectBtn, WM_SETFONT, (WPARAM)g_fontBtn, TRUE);
    g_hDeployBtn = CreateWindow(L"BUTTON", L"\u0420\u0430\u0437\u0432\u0435\u0440\u043d\u0443\u0442\u044c \u0441\u0435\u0440\u0432\u0435\u0440", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, tY + 42, SIDEBAR_W - 20, 36, g_hSidebar, (HMENU)ID_DEPLOY_BTN, hi, NULL);
    SendMessage(g_hDeployBtn, WM_SETFONT, (WPARAM)g_fontBtn, TRUE);
    g_hChatList = CreateWindow(L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
        0, tY + 84, SIDEBAR_W, wh - 150, g_hSidebar, (HMENU)ID_CHAT_LIST, hi, NULL);
    SendMessage(g_hChatList, WM_SETFONT, (WPARAM)g_fontMain, TRUE);
    int cX = SIDEBAR_W, cW = ww - SIDEBAR_W, cH = wh - 28, mY = cH - INPUT_AREA_H;
    g_hChatHeader = CreateWindowEx(0, L"MessengerChatHeader", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        cX, 0, cW, CHAT_HEADER_H, g_hMain, NULL, hi, NULL);
    g_hMsgView = CreateWindowEx(0, L"MessengerMsgView", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        cX, CHAT_HEADER_H, cW, cH - CHAT_HEADER_H - INPUT_AREA_H, g_hMain, NULL, hi, NULL);
    g_hMsgInput = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_BORDER | ES_AUTOHSCROLL | ES_MULTILINE,
        cX, mY, cW - SEND_BTN_W - 4, INPUT_AREA_H - 4, g_hMain, (HMENU)ID_MSG_INPUT, hi, NULL);
    SendMessage(g_hMsgInput, WM_SETFONT, (WPARAM)g_fontMain, TRUE);
    HFONT hFontArrow = CreateFont(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
    g_hSendBtn = CreateWindow(L"BUTTON", L" ", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
        cX + cW - SEND_BTN_W, mY, SEND_BTN_W, INPUT_AREA_H - 4, g_hMain, (HMENU)ID_SEND_BTN, hi, NULL);
    SendMessage(g_hSendBtn, WM_SETFONT, (WPARAM)hFontArrow, TRUE);
    DeleteObject(hFontArrow);
    g_hStatusTxt = CreateWindow(L"STATIC", L"\u041d\u0435 \u043f\u043e\u0434\u043a\u043b\u044e\u0447\u0435\u043d\u043e", WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, wh - 28, ww, 28, g_hMain, NULL, hi, NULL);
    SendMessage(g_hStatusTxt, WM_SETFONT, (WPARAM)g_fontTiny, TRUE);
    EnableWindow(g_hMsgInput, FALSE);
    EnableWindow(g_hSendBtn, FALSE);
    ShowWindow(g_hMain, show);
    UpdateWindow(g_hMain);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.hwnd == g_hMsgInput && msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            SendMessage(g_hMain, WM_COMMAND, ID_SEND_BTN, 0);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    DeleteObject(g_hBrBg);
    DeleteObject(g_hBrSidebar);
    DeleteObject(g_hBrInput);
    DeleteObject(g_fontMain);
    DeleteObject(g_fontSec);
    DeleteObject(g_fontTiny);
    DeleteObject(g_fontBold);
    DeleteObject(g_fontBtn);
    return (int)msg.wParam;
}
