/*
 * UNO 服务端游戏逻辑
 * ==================
 * 包含服务端专用的游戏流程逻辑（出牌/抽牌处理、断线跳过、消息解析）。
 * 核心规则引擎使用 rule.h/rule.cpp 的共享实现。
 *
 * 关键特性：
 * - nextPlayer()：沿循环双向链表 O(1) 找到下一个在线玩家
 * - notifyCurrentTurn()：跳过空槽，找到下一个在线玩家
 * - processMessage()：解析客户端 JOIN/PLAY/DRAW 等命令
 * - handlePlay()/handleDraw()：执行出牌/抽牌并广播结果
 */

#include "server.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <stdexcept>

// 全局服务端状态（定义于 server.cpp）
extern ServerGameState g_game;
extern std::string g_messageBuffer;

// ===== 辅助函数：字符串 ↔ RuleColor 转换（供协议解析使用）=====
static RuleColor stringToRuleColor(const std::string& s) {
    if (s == "Red")    return RuleColor::Red;
    if (s == "Yellow") return RuleColor::Yellow;
    if (s == "Green")  return RuleColor::Green;
    if (s == "Blue")   return RuleColor::Blue;
    if (s == "Wild")   return RuleColor::Wild;
    return RuleColor::None;
}

/* ========== 牌堆 / 初始发牌 ========== */

void reshuffleDrawPile() {
    if (g_game.discardPile.size() <= 1) return;
    RuleCard top = g_game.discardPile.back();
    g_game.discardPile.pop_back();
    g_game.drawPile.insert(g_game.drawPile.end(),
        g_game.discardPile.begin(), g_game.discardPile.end());
    g_game.discardPile.clear();
    g_game.discardPile.push_back(top);
    ruleShuffleDeck(g_game.drawPile);
    serverBroadcast("MSG|Draw pile reshuffled");
}

void dealInitialCards() {
    for (size_t i = 0; i < g_game.players.size(); ++i) {
        for (int j = 0; j < 7; ++j) {
            if (g_game.drawPile.empty()) reshuffleDrawPile();
            if (g_game.drawPile.empty()) return;
            g_game.players[i].hand.push_back(g_game.drawPile.back());
            g_game.drawPile.pop_back();
        }
    }
}

void initDiscardPile() {
    while (!g_game.drawPile.empty()) {
        RuleCard top = g_game.drawPile.back();
        g_game.drawPile.pop_back();
        g_game.discardPile.push_back(top);
        if (!ruleIsWildCard(top)) {
            g_game.currentColor = top.color;
            break;
        }
    }
}

void drawCardsFor(int playerId, int count) {
    for (int i = 0; i < count; ++i) {
        if (g_game.drawPile.empty()) reshuffleDrawPile();
        if (g_game.drawPile.empty()) return;
        g_game.players[playerId].hand.push_back(g_game.drawPile.back());
        g_game.drawPile.pop_back();
    }
}

/* ========== 广播函数 ========== */

void broadcastHands() {
    // 向每个在线玩家发送他自己手牌的完整列表
    for (size_t i = 0; i < g_game.players.size(); ++i) {
        if (!g_game.players[i].connected) continue;
        std::string handMsg = "HAND|";
        for (size_t j = 0; j < g_game.players[i].hand.size(); ++j) {
            if (j > 0) handMsg += "|";
            handMsg += ruleCardToString(g_game.players[i].hand[j]);
        }
        printf("[HAND] player %zu: %zu cards\n", i, g_game.players[i].hand.size());
        serverSendTo((int)i, handMsg);
    }
    // 向每个在线玩家广播对手的手牌数量
    for (size_t i = 0; i < g_game.players.size(); ++i) {
        if (!g_game.players[i].connected) continue;
        for (size_t j = 0; j < g_game.players.size(); ++j) {
            if (i == j || !g_game.players[j].connected) continue;
            std::string oppMsg = "OPPONENT_HAND|" +
                std::to_string((int)j) + "|" +
                std::to_string((int)g_game.players[j].hand.size());
            printf("[OPP] send to %zu: player %zu has %zu cards\n",
                   i, j, g_game.players[j].hand.size());
            serverSendTo((int)i, oppMsg);
        }
    }
}

void broadcastGameState() {
    if (g_game.discardPile.empty()) return;
    RuleCard top = g_game.discardPile.back();
    std::string stateMsg = "STATE|" +
        ruleColorToString(g_game.currentColor) + "|" +
        std::to_string(g_game.direction) + "|" +
        std::to_string(g_game.currentPlayer) + "|" +
        std::to_string((int)g_game.drawPile.size()) + "|" +
        ruleColorToString(top.color) + "|" +
        ruleRankToString(top.rank);
    serverBroadcast(stateMsg);
}

void notifyCurrentTurn() {
    if (g_game.connectedCount == 0) return;

    // 如果当前玩家断线了，沿链表进到下一个在线的（链表保证一次到达）
    if (!g_game.players[g_game.currentPlayer].connected)
        g_game.currentPlayer = nextPlayer(g_game.currentPlayer, g_game.direction);

    std::string turnMsg = "YOUR_TURN|30000";
    serverSendTo(g_game.currentPlayer, turnMsg);
    serverBroadcast("ACTION|" +
        std::to_string(g_game.currentPlayer) + "|TURN|" +
        g_game.players[g_game.currentPlayer].name);
}

/* ========== 工具函数 ========== */

/*
 * rebuildPlayerList — 根据 players[].connected 重建循环双向链表
 * 每次玩家连接/断线后调用，保证 nextPlayer() 能 O(1) 找到下一个在线玩家。
 */
void rebuildPlayerList() {
    // 重置（自环保证断线时 nextPlayer 有安全返回值）
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        g_game.players[i].nextSlot = i;
        g_game.players[i].prevSlot = i;
    }

    int slots[MAX_PLAYERS];
    g_game.connectedCount = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i)
        if (g_game.players[i].connected)
            slots[g_game.connectedCount++] = i;

    if (g_game.connectedCount <= 1) return;  // 0 或 1 人时自环即可

    // 串成循环双向链表（按 slot 顺序排列）
    for (int i = 0; i < g_game.connectedCount; ++i) {
        int cur = slots[i];
        int nxt = slots[(i + 1) % g_game.connectedCount];
        int prv = slots[(i - 1 + g_game.connectedCount) % g_game.connectedCount];
        g_game.players[cur].nextSlot = nxt;
        g_game.players[cur].prevSlot = prv;
    }
}

/* 
 * nextPlayer — 沿链表走一步
 * dir > 0 正向 (next), dir < 0 反向 (prev)
 * O(1) 查找，不再扫描数组
 */
int nextPlayer(int from, int dir) {
    if (from < 0 || from >= MAX_PLAYERS) {
        for (int i = 0; i < MAX_PLAYERS; ++i)
            if (g_game.players[i].connected) return i;
        return 0;
    }
    return (dir > 0) ? g_game.players[from].nextSlot
                     : g_game.players[from].prevSlot;
}

/* ========== 卡牌效果 / 流程 ========== */

void applyCardEffect(const RuleCard& card, RuleColor chosenColor) {
    if (ruleIsWildCard(card))
        g_game.currentColor = chosenColor;
    else
        g_game.currentColor = card.color;

    int cp = g_game.currentPlayer;

    if (card.rank == RuleRank::Skip) {
        // 跳过下一个人 → 沿链表走两步
        cp = nextPlayer(cp, g_game.direction);
        cp = nextPlayer(cp, g_game.direction);
    } else if (card.rank == RuleRank::Reverse) {
        g_game.direction *= -1;
        cp = nextPlayer(cp, g_game.direction);
        if (g_game.connectedCount == 2)  // 两人局中 Reverse 等价于 Skip
            cp = nextPlayer(cp, g_game.direction);
    } else if (card.rank == RuleRank::DrawTwo) {
        int target = nextPlayer(cp, g_game.direction);  // 被罚者
        drawCardsFor(target, 2);
        cp = nextPlayer(cp, g_game.direction);           // 跳过他
        cp = nextPlayer(cp, g_game.direction);
    } else if (card.rank == RuleRank::WildDrawFour) {
        int target = nextPlayer(cp, g_game.direction);
        drawCardsFor(target, 4);
        cp = nextPlayer(cp, g_game.direction);
        cp = nextPlayer(cp, g_game.direction);
    } else {
        cp = nextPlayer(cp, g_game.direction);
    }
    g_game.currentPlayer = cp;
}

bool handlePlay(int playerId, int handIndex, const std::string& colorStr) {
    if (playerId != g_game.currentPlayer) return false;
    if (handIndex < 0 || handIndex >= (int)g_game.players[playerId].hand.size()) return false;

    RuleCard card = g_game.players[playerId].hand[handIndex];
    RuleCard top = g_game.discardPile.empty()
        ? RuleCard{RuleColor::None, RuleRank::Num0}
        : g_game.discardPile.back();
    if (!ruleCanPlay(card, top, g_game.currentColor)) return false;

    g_game.players[playerId].hand.erase(g_game.players[playerId].hand.begin() + handIndex);
    g_game.discardPile.push_back(card);

    RuleColor chosenColor = g_game.currentColor;
    if (ruleIsWildCard(card)) {
        chosenColor = stringToRuleColor(colorStr);
        if (chosenColor == RuleColor::Wild || chosenColor == RuleColor::None)
            chosenColor = RuleColor::Red;
    }
    applyCardEffect(card, chosenColor);

    std::string actionMsg = "ACTION|" + std::to_string(playerId) + "|PLAY|" +
        ruleCardToString(card);
    if (ruleIsWildCard(card))
        actionMsg += "|" + ruleColorToString(chosenColor);
    serverBroadcast(actionMsg);
    g_game.turnCount++;

    if (g_game.players[playerId].hand.empty()) {
        serverBroadcast("WINNER|" + std::to_string(playerId) + "|" +
                        g_game.players[playerId].name);
        broadcastGameState();
        broadcastHands();
        // 延迟一小段时间后重置房间，让客户端收到 WINNER 消息
        Sleep(200);
        resetToLobby();
        return true;
    }
    broadcastGameState();
    broadcastHands();
    notifyCurrentTurn();
    return true;
}

bool handleDraw(int playerId) {
    if (playerId != g_game.currentPlayer) return false;
    drawCardsFor(playerId, 1);

    std::string actionMsg = "ACTION|" + std::to_string(playerId) + "|DRAW";
    serverBroadcast(actionMsg);

    if (!g_game.players[playerId].hand.empty()) {
        RuleCard drawn = g_game.players[playerId].hand.back();
        RuleCard top = g_game.discardPile.empty()
            ? RuleCard{RuleColor::None, RuleRank::Num0}
            : g_game.discardPile.back();
        if (ruleCanPlay(drawn, top, g_game.currentColor)) {
            serverSendTo(playerId, "MSG|Drew " + ruleCardToString(drawn) + ", click to play");
        } else {
            g_game.currentPlayer = nextPlayer(g_game.currentPlayer, g_game.direction);
            g_game.turnCount++;
            serverSendTo(playerId, "MSG|Drawn card not playable, turn passes");
        }
    }
    broadcastGameState();
    broadcastHands();
    notifyCurrentTurn();
    return true;
}

int playerIdBySocket(SOCKET s) {
    for (size_t i = 0; i < g_game.players.size(); ++i)
        if (g_game.players[i].socket == s) return (int)i;
    return -1;
}

/* ========== 消息解析 ========== */

void processMessage(int playerId, const std::string& msg) {
    std::istringstream ss(msg);
    std::string cmd;
    std::getline(ss, cmd, '|');

    if (cmd == "JOIN") {
        std::string name;
        std::getline(ss, name);
        if (!name.empty()) {
            g_game.players[playerId].name = name;
            g_game.players[playerId].isReady = true;
            std::string listMsg = "PLAYER_LIST";
            for (size_t i = 0; i < g_game.players.size(); ++i)
                if (g_game.players[i].connected && !g_game.players[i].name.empty())
                    listMsg += "|" + std::to_string((int)i) + ":" + g_game.players[i].name;
            serverBroadcast(listMsg);

            int readyCount = 0;
            for (size_t i = 0; i < g_game.players.size(); ++i)
                if (g_game.players[i].connected && !g_game.players[i].name.empty())
                    readyCount++;
            printf("[%s] joined (%d/%d)\n", name.c_str(), readyCount, (int)g_game.players.size());

            // 4 人全到且全部命名 → 自动开始
            if (!g_game.gameStarted && g_game.connectedCount == MAX_PLAYERS && readyCount == MAX_PLAYERS) {
                serverBroadcast("MSG|All players ready, game starting!");
                startGame();
            }
        }
    } else if (cmd == "READY") {
        g_game.players[playerId].isReady = true;
    } else if (cmd == "LEAVE") {
        g_game.players[playerId].connected = false;
        rebuildPlayerList();
        serverBroadcast("MSG|" + g_game.players[playerId].name + " left the game");
        printf("[%s] left\n", g_game.players[playerId].name.c_str());
    } else if (cmd == "PLAY") {
        std::string idxStr, colorStr;
        std::getline(ss, idxStr, '|');
        std::getline(ss, colorStr);
        int idx = std::stoi(idxStr);
        handlePlay(playerId, idx, colorStr);
    } else if (cmd == "DRAW") {
        handleDraw(playerId);
    } else if (cmd == "UNO") {
        serverBroadcast("MSG|" + g_game.players[playerId].name + " yelled UNO!");
    }
}

/* ========== 游戏生命周期 ========== */

void startGame() {
    g_game.gameStarted = true;
    int count = g_game.connectedCount;
    printf("Game started, %d players\n", count);

    g_game.drawPile = ruleCreateUnoDeck();
    ruleShuffleDeck(g_game.drawPile);
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (!g_game.players[i].connected) continue;
        g_game.players[i].hand.clear();
        for (int j = 0; j < 7; ++j) {
            if (g_game.drawPile.empty()) reshuffleDrawPile();
            if (g_game.drawPile.empty()) return;
            g_game.players[i].hand.push_back(g_game.drawPile.back());
            g_game.drawPile.pop_back();
        }
    }
    initDiscardPile();
    g_game.currentPlayer = 0; g_game.direction = 1; g_game.turnCount = 0;
    serverBroadcast("GAME_START");
    broadcastGameState(); broadcastHands(); notifyCurrentTurn();
}

void resetToLobby() {
    // 通知在线玩家房间即将重置
    if (g_game.gameStarted)
        serverBroadcast("MSG|Game over — room resetting...");
    else
        serverBroadcast("MSG|Room resetting...");

    // 断开所有玩家
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (g_game.players[i].socket != INVALID_SOCKET) {
            closesocket(g_game.players[i].socket);
            g_game.players[i].socket = INVALID_SOCKET;
        }
        g_game.players[i].connected = false;
        g_game.players[i].name = "";
        g_game.players[i].hand.clear();
        g_game.players[i].isReady = false;
    }

    // 重置游戏状态
    g_game.drawPile.clear();
    g_game.discardPile.clear();
    g_game.currentColor = RuleColor::None;
    g_game.currentPlayer = 0;
    g_game.direction = 1;
    g_game.gameStarted = false;
    g_game.turnCount = 0;

    rebuildPlayerList();
    printf("[!] Room reset — waiting for players...\n");
}
