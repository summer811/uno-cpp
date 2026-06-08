/*
 * UNO Server 服务端核心头文件
 * =========================
 * 定义服务端专用的网络/玩家连接类型和函数声明。
 * 卡牌类型和规则引擎使用 ../rule.h 的共享定义。
 *
 * === 通信协议（TCP 文本协议，\n 分隔） ===
 * 客户端 → 服务端：
 *   JOIN|玩家名      加入游戏
 *   PLAY|手牌索引|颜色  出牌（万能牌时指定: Red/Yellow/Green/Blue）
 *   DRAW            抽牌
 *   UNO             喊 UNO
 *   LEAVE           离开
 *
 * 服务端 → 客户端：
 *   WELCOME|ID         分配玩家 ID
 *   PLAYER_LIST|ID:名|.. 当前玩家列表
 *   GAME_START         游戏开始
 *   STATE|颜色|方向|当前玩家|牌堆数|顶牌颜色|顶牌点数
 *   HAND|卡1|卡2|...   发手牌（颜色_点数格式）
 *   YOUR_TURN|超时ms   轮到该客户端出牌
 *   ACTION|ID|类型|...  其他玩家操作通知
 *   WINNER|ID|名字     宣布胜利者
 *   ERROR|描述         错误（含踢出）
 *   MSG|描述           普通消息
 *   OPPONENT_HAND|ID|数 对手手牌数量
 *
 * 管理命令（在服务端控制台输入）：
 *   op start              开始游戏
 *   op kick 玩家名        踢出玩家
 *   op list               列出在线玩家
 *   op quit               关闭服务端
 */

#ifndef UNO_SERVER_H
#define UNO_SERVER_H

#include <string>
#include <vector>
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
#include <windows.h>

#include "../rule.h"

const int SERVER_PORT = 8888;
const int MAX_PLAYERS = 4;
const int MIN_PLAYERS = 2;

// ===== 玩家连接信息 =====
struct ClientInfo {
    SOCKET socket;
    std::string name;
    std::vector<RuleCard> hand;
    int id;
    bool isReady;
    bool connected;
};

// ===== 服务端游戏状态 =====
// 使用 rule.h 的 RuleCard / RuleColor / RuleRank 等类型
struct ServerGameState {
    std::vector<ClientInfo> players;
    std::vector<RuleCard>   drawPile;
    std::vector<RuleCard>   discardPile;
    RuleColor currentColor = RuleColor::None;
    int       currentPlayer = 0;
    int       direction = 1;
    bool      gameStarted = false;
    int       turnCount = 0;
};

// ===== 网络通信 =====
void serverSendTo(int clientId, const std::string& message);
void serverBroadcast(const std::string& message, int excludeId = -1);
void processMessage(int playerId, const std::string& msg);

// ===== 游戏流程（服务端专用，含跳过断线玩家逻辑）=====
int  nextConnectedPlayer(int from, int dir);
void broadcastGameState();
void broadcastHands();
void notifyCurrentTurn();
int  playerIdBySocket(SOCKET s);

// ===== 游戏操作（服务端专用）=====
void reshuffleDrawPile();
void dealInitialCards();
void initDiscardPile();
void drawCardsFor(int playerId, int count);
void applyCardEffect(const RuleCard& card, RuleColor chosenColor);
bool handlePlay(int playerId, int handIndex, const std::string& colorStr);
bool handleDraw(int playerId);

extern ServerGameState g_game;
extern std::string g_messageBuffer;

#endif // UNO_SERVER_H
