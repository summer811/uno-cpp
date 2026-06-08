/*
 * rul.cpp — UNO 规则测试程序
 * ============================
 * 独立测试程序，使用共享的 rule.h/rule.cpp 规则引擎。
 * 编译：g++ -o rul.exe rul.cpp rule.cpp
 * 或在 Visual Studio 中作为独立项目编译。
 */
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdlib>
#include <conio.h>

#include "rule.h"

using namespace std;

// ===== 交互式测试 =====

RuleGameState createTestGame(const vector<string>& playerNames) {
    if (playerNames.size() < 2)
        throw invalid_argument("UNO needs at least 2 players.");

    RuleGameState game;
    for (const string& name : playerNames)
        game.players.push_back({name, {}});

    game.drawPile = ruleCreateUnoDeck();
    ruleShuffleDeck(game.drawPile);

    for (int round = 0; round < 7; ++round)
        for (size_t i = 0; i < game.players.size(); ++i)
            ruleDrawCards(game, (int)i, 1);

    while (!game.drawPile.empty()) {
        RuleCard first = game.drawPile.back();
        game.drawPile.pop_back();
        game.discardPile.push_back(first);

        if (!ruleIsWildCard(first)) {
            game.currentColor = first.color;
            break;
        }
    }

    if (game.discardPile.empty())
        throw runtime_error("Cannot start game without a discard card.");

    return game;
}

void printHand(const RulePlayer& player) {
    cout << player.name << "'s hand:" << endl;
    for (size_t i = 0; i < player.hand.size(); ++i)
        cout << "  [" << i << "] " << ruleCardToString(player.hand[i]) << endl;
}

int main() {
    RuleGameState game = createTestGame({"PlayerA", "PlayerB", "PlayerC"});

    cout << "Top card: " << ruleCardToString(ruleTopDiscard(game)) << endl;
    cout << "Current color: " << ruleColorToString(game.currentColor) << endl;
    cout << "Current player: " << game.players[game.currentPlayer].name << endl;

    printHand(game.players[game.currentPlayer]);

    cout << endl << "Playable cards:" << endl;
    RuleCard top = ruleTopDiscard(game);
    const vector<RuleCard>& hand = game.players[game.currentPlayer].hand;
    for (size_t i = 0; i < hand.size(); ++i) {
        if (ruleCanPlay(hand[i], top, game.currentColor))
            cout << "  [" << i << "] " << ruleCardToString(hand[i]) << endl;
    }

    cout << "\nPress any key to exit...";
    _getch();
    return 0;
}
