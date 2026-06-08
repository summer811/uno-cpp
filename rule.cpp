/*
 * rule.cpp — 共享的 UNO 规则引擎实现
 * ====================================
 * 实现 rule.h 中声明的所有函数。
 * 同时被客户端和服务器使用，消除重复规则代码。
 */

#include "rule.h"
#include <algorithm>
#include <random>
#include <stdexcept>

// ===== 工具函数 =====

bool ruleIsWildCard(const RuleCard& card) {
    return card.rank == RuleRank::Wild || card.rank == RuleRank::WildDrawFour;
}

bool ruleIsNumberCard(const RuleCard& card) {
    int r = static_cast<int>(card.rank);
    return r >= static_cast<int>(RuleRank::Num0) &&
           r <= static_cast<int>(RuleRank::Num9);
}

bool ruleIsNormalColor(RuleColor color) {
    return color == RuleColor::Red || color == RuleColor::Yellow ||
           color == RuleColor::Green || color == RuleColor::Blue;
}

std::string ruleColorToString(RuleColor color) {
    switch (color) {
        case RuleColor::Red:    return "Red";
        case RuleColor::Yellow: return "Yellow";
        case RuleColor::Green:  return "Green";
        case RuleColor::Blue:   return "Blue";
        case RuleColor::Wild:   return "Wild";
        case RuleColor::None:   return "None";
    }
    return "Unknown";
}

std::string ruleRankToString(RuleRank rank) {
    switch (rank) {
        case RuleRank::Num0:  return "0";
        case RuleRank::Num1:  return "1";
        case RuleRank::Num2:  return "2";
        case RuleRank::Num3:  return "3";
        case RuleRank::Num4:  return "4";
        case RuleRank::Num5:  return "5";
        case RuleRank::Num6:  return "6";
        case RuleRank::Num7:  return "7";
        case RuleRank::Num8:  return "8";
        case RuleRank::Num9:  return "9";
        case RuleRank::Skip:        return "Skip";
        case RuleRank::Reverse:     return "Reverse";
        case RuleRank::DrawTwo:     return "DrawTwo";
        case RuleRank::Wild:        return "Wild";
        case RuleRank::WildDrawFour: return "WildDrawFour";
    }
    return "Unknown";
}

std::string ruleCardToString(const RuleCard& card) {
    if (ruleIsWildCard(card))
        return ruleRankToString(card.rank);
    return ruleColorToString(card.color) + "_" + ruleRankToString(card.rank);
}

int ruleRankValue(RuleRank rank) {
    switch (rank) {
        case RuleRank::Num0:  return 0;
        case RuleRank::Num1:  return 1;
        case RuleRank::Num2:  return 2;
        case RuleRank::Num3:  return 3;
        case RuleRank::Num4:  return 4;
        case RuleRank::Num5:  return 5;
        case RuleRank::Num6:  return 6;
        case RuleRank::Num7:  return 7;
        case RuleRank::Num8:  return 8;
        case RuleRank::Num9:  return 9;
        case RuleRank::Skip:        return 20;
        case RuleRank::Reverse:     return 20;
        case RuleRank::DrawTwo:     return 20;
        case RuleRank::Wild:        return 50;
        case RuleRank::WildDrawFour: return 50;
    }
    return 0;
}

// ===== 牌堆操作 =====

std::vector<RuleCard> ruleCreateUnoDeck() {
    std::vector<RuleCard> deck;
    std::vector<RuleColor> colors = {
        RuleColor::Red, RuleColor::Yellow,
        RuleColor::Green, RuleColor::Blue
    };

    for (RuleColor color : colors) {
        deck.push_back({color, RuleRank::Num0});
        for (int repeat = 0; repeat < 2; ++repeat) {
            deck.push_back({color, RuleRank::Num1});
            deck.push_back({color, RuleRank::Num2});
            deck.push_back({color, RuleRank::Num3});
            deck.push_back({color, RuleRank::Num4});
            deck.push_back({color, RuleRank::Num5});
            deck.push_back({color, RuleRank::Num6});
            deck.push_back({color, RuleRank::Num7});
            deck.push_back({color, RuleRank::Num8});
            deck.push_back({color, RuleRank::Num9});
            deck.push_back({color, RuleRank::Skip});
            deck.push_back({color, RuleRank::Reverse});
            deck.push_back({color, RuleRank::DrawTwo});
        }
    }

    for (int i = 0; i < 4; ++i) {
        deck.push_back({RuleColor::Wild, RuleRank::Wild});
        deck.push_back({RuleColor::Wild, RuleRank::WildDrawFour});
    }

    return deck;
}

void ruleShuffleDeck(std::vector<RuleCard>& deck) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(deck.begin(), deck.end(), gen);
}

void ruleReshuffleDiscardIntoDrawPile(RuleGameState& game) {
    if (game.discardPile.size() <= 1) return;

    RuleCard top = game.discardPile.back();
    game.discardPile.pop_back();

    game.drawPile.insert(game.drawPile.end(),
        game.discardPile.begin(), game.discardPile.end());
    game.discardPile.clear();
    game.discardPile.push_back(top);

    ruleShuffleDeck(game.drawPile);
}

// ===== 规则判断 =====

RuleCard ruleTopDiscard(const RuleGameState& game) {
    if (game.discardPile.empty())
        throw std::runtime_error("Discard pile is empty.");
    return game.discardPile.back();
}

bool ruleCanPlay(const RuleCard& card, const RuleCard& topCard, RuleColor currentColor) {
    if (ruleIsWildCard(card)) return true;
    if (currentColor != RuleColor::None && card.color == currentColor) return true;
    if (card.color == topCard.color) return true;
    if (card.rank == topCard.rank) return true;
    return false;
}

int ruleNextPlayerIndex(const RuleGameState& game, int step) {
    int n = static_cast<int>(game.players.size());
    if (n == 0)
        throw std::runtime_error("No players in game.");

    int move = game.direction * step;
    int next = (game.currentPlayer + move) % n;
    if (next < 0) next += n;
    return next;
}

void ruleDrawCards(RuleGameState& game, int playerIndex, int count) {
    if (playerIndex < 0 || playerIndex >= static_cast<int>(game.players.size()))
        throw std::out_of_range("Invalid player index.");

    for (int i = 0; i < count; ++i) {
        if (game.drawPile.empty())
            ruleReshuffleDiscardIntoDrawPile(game);
        if (game.drawPile.empty()) return;

        game.players[playerIndex].hand.push_back(game.drawPile.back());
        game.drawPile.pop_back();
    }
}
