/*
 * UNO 服务端主程序
 * ================
 * 基于 select 模型的 TCP 服务端，支持最多 4 人同时连接。
 *
 * 工作流程：
 * 1. 创建监听 socket，绑定 8888 端口
 * 2. select() 轮询监听 socket 和已有客户端
 * 3. 新连接 → 分配空闲 slot（4 个），发送 WELCOME
 * 4. 客户端消息 → processMessage() 处理
 * 5. 管理命令从后台线程读取，互斥锁同步
 *
 * 玩家槽位：固定 4 个 (slot 0-3)，每个 slot 可以单独连接/断开。
 * 循环双向链表（nextSlot/prevSlot）确保轮次只经过在线的玩家。
 */

#include "server.h"
#include <iostream>
#include <cstdio>
#include <thread>
#include <mutex>
#include <queue>
#include <string>
#include <sstream>
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

ServerGameState g_game;
std::string g_messageBuffer;
// 其他 .cpp 通过 "extern ServerGameState g_game;" 引用
static std::queue<std::string> g_adminQueue;
static std::mutex g_adminMutex;
static bool g_serverRunning = true;

void serverSendTo(int clientId, const std::string& message) {
    if (clientId < 0 || clientId >= (int)g_game.players.size()) return;
    if (!g_game.players[clientId].connected) return;
    std::string full = message + "\n";
    send(g_game.players[clientId].socket, full.c_str(), (int)full.size(), 0);
}

void serverBroadcast(const std::string& message, int excludeId) {
    for (size_t i = 0; i < g_game.players.size(); ++i) {
        if ((int)i == excludeId) continue;
        if (!g_game.players[i].connected) continue;
        serverSendTo((int)i, message);
    }
    printf("[B] %s\n", message.c_str());
}

bool recvLine(SOCKET s, std::string& line) {
    char buf[1]; line.clear(); int timeout = 0;
    while (timeout < 200) {
        int r = recv(s, buf, 1, 0);
        if (r == SOCKET_ERROR) return false;
        if (r == 0) return false;
        if (buf[0] == '\n') return true;
        if (buf[0] == '\r') continue;
        line += buf[0]; timeout++;
    }
    return true;
}

/*
 * adminInputThread —— 后台管理命令线程
 * 独立线程运行，读取标准输入（getchar），
 * 将输入压入 g_adminQueue，主循环轮询处理。
 * 支持命令：op start / op kick / op list / op quit
 */
void adminInputThread() {
    printf("Commands: op start | op kick name | op list | op quit | op shutdown\n");
    std::string line;
    while (g_serverRunning) {
        line.clear(); int c;
        while ((c = getchar()) != '\n' && c != EOF)
            if (c != '\r') line += (char)c;
        if (line.empty()) continue;
        { std::lock_guard<std::mutex> lock(g_adminMutex); g_adminQueue.push(line); }
        if (line == "op quit") break;
    }
}

/*
 * processAdminCommand —— 执行管理命令
 * op start [2-3人]：检查在线人数 ≥ MIN_PLAYERS，发牌、初始化、广播 GAME_START
 * op kick name：查找玩家，发 ERROR|Kicked 并断开 socket
 * op list：打印在线玩家列表
 * op quit：重置房间（踢所有人、清状态、等待下一局）
 * op shutdown：关闭服务端
 */
void processAdminCommand(const std::string& cmd) {
    std::istringstream ss(cmd);
    std::string op, arg; ss >> op >> arg;
    if (op != "op") return;

    if (arg == "start") {
        if (g_game.gameStarted) { printf("Already started\n"); return; }
        if (g_game.connectedCount < MIN_PLAYERS) {
            printf("Need %d+ players (have %d)\n", MIN_PLAYERS, g_game.connectedCount);
            return;
        }
        serverBroadcast("MSG|Admin starts the game!");
        startGame();

    } else if (arg == "kick") {
        std::string name; std::getline(ss, name);
        while (!name.empty() && name[0] == ' ') name.erase(0, 1);
        if (name.empty()) { printf("Usage: op kick name\n"); return; }
        for (size_t i = 0; i < g_game.players.size(); ++i) {
            if (g_game.players[i].connected && g_game.players[i].name == name) {
                serverSendTo((int)i, "ERROR|Kicked");
                closesocket(g_game.players[i].socket);
                g_game.players[i].connected = false;
                g_game.players[i].socket = INVALID_SOCKET;
                rebuildPlayerList();
                serverBroadcast("MSG|" + name + " kicked");
                printf("Kicked: %s\n", name.c_str());
                return;
            }
        }
        printf("Not found: %s\n", name.c_str());

    } else if (arg == "list") {
        printf("--- Players ---\n");
        for (size_t i = 0; i < g_game.players.size(); ++i)
            if (g_game.players[i].connected)
                printf(" [%d] %s (ready=%d)\n", (int)i,
                       g_game.players[i].name.c_str(),
                       g_game.players[i].isReady ? 1 : 0);

    } else if (arg == "quit") {
        printf("[!] Resetting room...\n");
        resetToLobby();
    } else if (arg == "shutdown") {
        printf("[!] Server shutting down...\n");
        g_serverRunning = false;
    }
}

int main() {
    printf("=== UNO Server ===\n");
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        { printf("WSAStartup failed\n"); return 1; }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET)
        { printf("socket() failed\n"); WSACleanup(); return 1; }

    int opt = 1; setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    sockaddr_in addr; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(SERVER_PORT);
    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
        { printf("bind() failed\n"); closesocket(listenSock); WSACleanup(); return 1; }
    if (listen(listenSock, MAX_PLAYERS) == SOCKET_ERROR)
        { printf("listen() failed\n"); closesocket(listenSock); WSACleanup(); return 1; }
    printf("Server listening on port %d\n", SERVER_PORT);

    g_game.players.resize(MAX_PLAYERS);
    g_game.currentColor = RuleColor::None; g_game.currentPlayer = 0;
    g_game.direction = 1; g_game.gameStarted = false; g_game.turnCount = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        g_game.players[i].socket = INVALID_SOCKET;
        g_game.players[i].connected = false;
        g_game.players[i].isReady = false;
        g_game.players[i].id = i;
    }
    rebuildPlayerList();

    std::thread adminThread(adminInputThread);
    adminThread.detach();

    fd_set readSet;
    while (g_serverRunning) {
        {
            std::lock_guard<std::mutex> lock(g_adminMutex);
            while (!g_adminQueue.empty())
                { processAdminCommand(g_adminQueue.front()); g_adminQueue.pop(); }
        }

        FD_ZERO(&readSet); FD_SET(listenSock, &readSet);
        SOCKET maxSock = listenSock;
        for (int i = 0; i < MAX_PLAYERS; ++i)
            if (g_game.players[i].connected && g_game.players[i].socket != INVALID_SOCKET) {
                FD_SET(g_game.players[i].socket, &readSet);
                if (g_game.players[i].socket > maxSock) maxSock = g_game.players[i].socket;
            }

        timeval tv = {0, 200000};
        int result = select((int)maxSock + 1, &readSet, nullptr, nullptr, &tv);
        if (result == SOCKET_ERROR) { printf("select error\n"); break; }
        if (result == 0) continue;

        //玩家连接处理
        if (FD_ISSET(listenSock, &readSet)) {
            sockaddr_in clientAddr; int addrLen = sizeof(clientAddr);
            SOCKET clientSock = accept(listenSock, (sockaddr*)&clientAddr, &addrLen);
            if (clientSock != INVALID_SOCKET) {
                int slot = -1;
                for (int i = 0; i < MAX_PLAYERS; ++i)
                    if (!g_game.players[i].connected) { slot = i; break; }
                if (slot == -1) {
                    send(clientSock, "ERROR|Server full\n", 18, 0);
                    closesocket(clientSock);
                } else {
                    g_game.players[slot].socket = clientSock;
                    g_game.players[slot].connected = true;
                    g_game.players[slot].name = ""; g_game.players[slot].hand.clear();
                    g_game.players[slot].id = slot;
                    rebuildPlayerList();
                    printf("[+] Player %d (%s)\n", slot, inet_ntoa(clientAddr.sin_addr));
                    serverSendTo(slot, "WELCOME|" + std::to_string(slot));
                    // 4 人满且都发了名字 → 自动开始（详见 JOIN 处理）
                    if (!g_game.gameStarted && g_game.connectedCount == MAX_PLAYERS)
                        printf("[!] All slots filled, waiting for names...\n");
                }
            }
        }

        for (int i = 0; i < MAX_PLAYERS; ++i) {
            if (!g_game.players[i].connected || g_game.players[i].socket == INVALID_SOCKET) continue;
            if (!FD_ISSET(g_game.players[i].socket, &readSet)) continue;
            std::string line;

            //玩家离线处理
            if (!recvLine(g_game.players[i].socket, line)) {
                if (g_game.gameStarted) serverBroadcast("MSG|" + g_game.players[i].name + " disconnected");
                g_game.players[i].connected = false;
                closesocket(g_game.players[i].socket);
                g_game.players[i].socket = INVALID_SOCKET;
                rebuildPlayerList();
                printf("[-] Player %d disconnected\n", i);
                continue;
            }
            if (!line.empty()) { printf("  [%d] %s\n", i, line.c_str()); processMessage(i, line); }
        }
    }

    for (int i = 0; i < MAX_PLAYERS; ++i)
        if (g_game.players[i].socket != INVALID_SOCKET) closesocket(g_game.players[i].socket);
    closesocket(listenSock); WSACleanup();
    return 0;
}
