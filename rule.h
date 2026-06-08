/*
 * rule.h — 共享的 UNO 规则类型和函数声明
 * ========================================
 * 同时被客户端（UNO-cpp.cpp）和服务端（server/server.cpp）使用。
 * 服务端不再维护自己的 ServerColor/ServerRank 等类型。
 *
 * 与 ai.h 的关系：ai.h 包含 AI／机器人专属逻辑（bot 相关函数），
 * rule.h 只有纯规则引擎，两者互补。
 */

#ifndef UNO_RULE_H
#define UNO_RULE_H

#include <string>
#include <vector>
#include <stdexcept>

// ===== 卡牌类型枚举 =====

enum class RuleColor {
    Red,
    Yellow,
    Green,
    Blue,
    Wild,
    None
};

enum class RuleRank {
    Num0,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    Skip,
    Reverse,
    DrawTwo,
    Wild,
    WildDrawFour
};

// ===== 数据结构 =====

struct RuleCard {
    RuleColor color;
    RuleRank  rank;
};

struct RulePlayer {
    std::string name;
    std::vector<RuleCard> hand;
};

struct RuleGameState {
    std::vector<RulePlayer> players;
    std::vector<RuleCard>   drawPile;
    std::vector<RuleCard>   discardPile;
    RuleColor currentColor = RuleColor::None;
    int       currentPlayer = 0;
    int       direction = 1;  // 1 = clockwise, -1 = counter-clockwise
};

// ===== 规则引擎函数 =====

bool        ruleIsWildCard(const RuleCard& card);
bool        ruleIsNumberCard(const RuleCard& card);
bool        ruleIsNormalColor(RuleColor color);
std::string ruleColorToString(RuleColor color);
std::string ruleRankToString(RuleRank rank);
std::string ruleCardToString(const RuleCard& card);
int         ruleRankValue(RuleRank rank);

RuleCard    ruleTopDiscard(const RuleGameState& game);
bool        ruleCanPlay(const RuleCard& card, const RuleCard& topCard, RuleColor currentColor);
int         ruleNextPlayerIndex(const RuleGameState& game, int step = 1);

void        ruleDrawCards(RuleGameState& game, int playerIndex, int count);

// 仅用于 shuffle / 创建牌堆
std::vector<RuleCard> ruleCreateUnoDeck();
void        ruleShuffleDeck(std::vector<RuleCard>& deck);
void        ruleReshuffleDiscardIntoDrawPile(RuleGameState& game);

#endif // UNO_RULE_H
