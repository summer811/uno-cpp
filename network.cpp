#include "network.h"
#include <cstdio>

static SOCKET g_socket = INVALID_SOCKET;
static bool g_connected = false;
static std::string g_lastError;
static std::string g_recvBuffer;

bool networkConnect(const std::wstring& address) {
    networkDisconnect();

    size_t colonPos = address.find(L':');
    if (colonPos == std::wstring::npos) {
        g_lastError = "address format error, must be IP:PORT";
        return false;
    }

    std::wstring ipStr = address.substr(0, colonPos);
    std::wstring portStr = address.substr(colonPos + 1);
    int port = 0;
    try { port = std::stoi(portStr); }
    catch (...) {
        g_lastError = "invalid port number";
        return false;
    }

    if (port <= 0 || port > 65535) {
        g_lastError = "port out of range (1-65535)";
        return false;
    }

    WSADATA wsaData;
    int wsaErr = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaErr != 0) {
        g_lastError = "WSAStartup failed";
        return false;
    }

    g_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_socket == INVALID_SOCKET) {
        g_lastError = "socket creation failed";
        WSACleanup();
        return false;
    }

    char ipBuf[64] = {0};
    WideCharToMultiByte(CP_ACP, 0, ipStr.c_str(), -1, ipBuf, 64, nullptr, nullptr);

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ipBuf);

    if (addr.sin_addr.s_addr == INADDR_NONE) {
        hostent* host = gethostbyname(ipBuf);
        if (host == nullptr) {
            g_lastError = std::string("cannot resolve host: ") + ipBuf;
            closesocket(g_socket);
            g_socket = INVALID_SOCKET;
            WSACleanup();
            return false;
        }
        memcpy(&addr.sin_addr, host->h_addr_list[0], host->h_length);
    }

    if (connect(g_socket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        g_lastError = std::string("connect failed: ") + std::to_string(WSAGetLastError());
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    u_long mode = 1;
    ioctlsocket(g_socket, FIONBIO, &mode);

    g_connected = true;
    g_recvBuffer.clear();
    return true;
}

bool networkSend(const std::string& message) {
    if (!g_connected || g_socket == INVALID_SOCKET) {
        g_lastError = "not connected";
        return false;
    }
    std::string full = message + "\n";
    int sent = send(g_socket, full.c_str(), (int)full.size(), 0);
    if (sent == SOCKET_ERROR) {
        g_lastError = std::string("send failed: ") + std::to_string(WSAGetLastError());
        networkDisconnect();
        return false;
    }
    return true;
}

bool networkRecv(std::string& message, int timeoutMs) {
    if (!g_connected || g_socket == INVALID_SOCKET) {
        g_lastError = "not connected";
        return false;
    }

    size_t nlPos = g_recvBuffer.find('\n');
    if (nlPos != std::string::npos) {
        message = g_recvBuffer.substr(0, nlPos);
        g_recvBuffer.erase(0, nlPos + 1);
        return true;
    }

    char buf[4096];
    int r = recv(g_socket, buf, 4096, 0);

    if (r == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            if (timeoutMs == 0) return false;
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(g_socket, &readSet);
            timeval tv;
            if (timeoutMs < 0) { tv.tv_sec = 5; tv.tv_usec = 0; }
            else { tv.tv_sec = timeoutMs / 1000; tv.tv_usec = (timeoutMs % 1000) * 1000; }
            int sel = select((int)g_socket + 1, &readSet, nullptr, nullptr, &tv);
            if (sel <= 0) return false;
            r = recv(g_socket, buf, 4096, 0);
            if (r <= 0) { g_lastError = "connection closed"; networkDisconnect(); return false; }
        } else {
            g_lastError = std::string("recv failed: ") + std::to_string(err);
            networkDisconnect();
            return false;
        }
    } else if (r == 0) {
        g_lastError = "server closed connection";
        networkDisconnect();
        return false;
    }

    g_recvBuffer.append(buf, r);
    nlPos = g_recvBuffer.find('\n');
    if (nlPos != std::string::npos) {
        message = g_recvBuffer.substr(0, nlPos);
        g_recvBuffer.erase(0, nlPos + 1);
        return true;
    }
    return false;
}

void networkDisconnect() {
    if (g_socket != INVALID_SOCKET) { closesocket(g_socket); g_socket = INVALID_SOCKET; }
    g_connected = false;
    g_recvBuffer.clear();
    WSACleanup();
}

bool networkIsConnected() { return g_connected && g_socket != INVALID_SOCKET; }
std::string networkLastError() { return g_lastError; }
