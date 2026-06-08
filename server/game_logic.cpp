/*
 * UNO 服务端游戏逻辑
 * ==================
 * 包含服务端专用的游戏流程逻辑（出牌/抽牌处理、断线跳过、消息解析）。
 * 核心规则引擎使用 rule.h/rule.cpp 的共享实现。
 *
 * 关键特性：
 * - nextConnectedPlayer()：跳过断开连接的玩家槽位
 * - notifyCurrentTurn()：跳过空槽，找到下一个在线玩家
 * - processMessage()：解析客户端 JOIN/PLAY/DRAW 等命令
 * - handlePlay()/handleDraw()：执行出牌/抽牌并广播结果
 */

#include "server.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <stdexcept>

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
    // 跳过空槽（!connected 的 slot）
    while (g_game.currentPlayer >= 0 && g_game.currentPlayer < (int)g_game.players.size()
           && !g_game.players[g_game.currentPlayer].connected) {
        g_game.currentPlayer = nextConnectedPlayer(g_game.currentPlayer, g_game.direction);
    }
    std::string turnMsg = "YOUR_TURN|30000";
    if (g_game.currentPlayer >= 0 && g_game.currentPlayer < (int)g_game.players.size()) {
        if (g_game.players[g_game.currentPlayer].connected)
            serverSendTo(g_game.currentPlayer, turnMsg);
        serverBroadcast("ACTION|" +
            std::to_string(g_game.currentPlayer) + "|TURN|" +
            g_game.players[g_game.currentPlayer].name);
    }
}

/* ========== 工具函数 ========== */

int nextConnectedPlayer(int from, int dir) {
    int n = (int)g_game.players.size();
    for (int s = 1; s <= n; ++s) {
        int idx = (from + dir * s + n) % n;
        if (g_game.players[idx].connected) return idx;
    }
    return from;
}

/* ========== 卡牌效果 / 流程 ========== */

void applyCardEffect(const RuleCard& card, RuleColor chosenColor) {
    int n = (int)g_game.players.size();
    if (ruleIsWildCard(card))
        g_game.currentColor = chosenColor;
    else
        g_game.currentColor = card.color;

    int cp = g_game.currentPlayer;

    if (card.rank == RuleRank::Skip) {
        cp = nextConnectedPlayer(cp, g_game.direction);
        cp = nextConnectedPlayer(cp, g_game.direction);
    } else if (card.rank == RuleRank::Reverse) {
        g_game.direction *= -1;
        cp = nextConnectedPlayer(cp, g_game.direction);
        if (n == 2) cp = nextConnectedPlayer(cp, g_game.direction);
    } else if (card.rank == RuleRank::DrawTwo) {
        int target = nextConnectedPlayer(cp, g_game.direction);
        drawCardsFor(target, 2);
        cp = nextConnectedPlayer(cp, g_game.direction);
        cp = nextConnectedPlayer(cp, g_game.direction);
    } else if (card.rank == RuleRank::WildDrawFour) {
        int target = nextConnectedPlayer(cp, g_game.direction);
        drawCardsFor(target, 4);
        cp = nextConnectedPlayer(cp, g_game.direction);
        cp = nextConnectedPlayer(cp, g_game.direction);
    } else {
        cp = nextConnectedPlayer(cp, g_game.direction);
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
            g_game.currentPlayer = nextConnectedPlayer(g_game.currentPlayer, g_game.direction);
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
        }
    } else if (cmd == "READY") {
        g_game.players[playerId].isReady = true;
    } else if (cmd == "LEAVE") {
        g_game.players[playerId].connected = false;
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
