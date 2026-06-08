#define SKIP_MAIN
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "ai.h"

bool isWildCard(const Card& card) {
    return card.rank == Rank::Wild || card.rank == Rank::WildDrawFour;
}

bool isNumberCard(const Card& card) {
    return card.rank >= Rank::Num0 && card.rank <= Rank::Num9;
}

string colorToString(Color color) {
    switch (color) {
        case Color::Red: return "Red";
        case Color::Yellow: return "Yellow";
        case Color::Green: return "Green";
        case Color::Blue: return "Blue";
        case Color::Wild: return "Wild";
        case Color::None: return "None";
    }
    return "Unknown";
}

string rankToString(Rank rank) {
    switch (rank) {
        case Rank::Num0: return "0";
        case Rank::Num1: return "1";
        case Rank::Num2: return "2";
        case Rank::Num3: return "3";
        case Rank::Num4: return "4";
        case Rank::Num5: return "5";
        case Rank::Num6: return "6";
        case Rank::Num7: return "7";
        case Rank::Num8: return "8";
        case Rank::Num9: return "9";
        case Rank::Skip: return "Skip";
        case Rank::Reverse: return "Reverse";
        case Rank::DrawTwo: return "DrawTwo";
        case Rank::Wild: return "Wild";
        case Rank::WildDrawFour: return "WildDrawFour";
    }
    return "Unknown";
}

string cardToString(const Card& card) {
    if (isWildCard(card)) {
        return rankToString(card.rank);
    }
    return colorToString(card.color) + " " + rankToString(card.rank);
}

int rankValue(Rank rank) {
    switch (rank) {
        case Rank::Num0: return 0;
        case Rank::Num1: return 1;
        case Rank::Num2: return 2;
        case Rank::Num3: return 3;
        case Rank::Num4: return 4;
        case Rank::Num5: return 5;
        case Rank::Num6: return 6;
        case Rank::Num7: return 7;
        case Rank::Num8: return 8;
        case Rank::Num9: return 9;
        case Rank::Skip: return 20;
        case Rank::Reverse: return 20;
        case Rank::DrawTwo: return 20;
        case Rank::Wild: return 50;
        case Rank::WildDrawFour: return 50;
    }
    return 0;
}

bool isNormalColor(Color color) {
    return color == Color::Red || color == Color::Yellow ||
           color == Color::Green || color == Color::Blue;
}

Card topDiscard(const GameState& game) {
    if (game.discardPile.empty()) {
        throw runtime_error("Discard pile is empty.");
    }
    return game.discardPile.back();
}

bool canPlay(const Card& card, const Card& topCard, Color currentColor) {
    if (isWildCard(card)) {
        return true;
    }

    if (currentColor != Color::None && card.color == currentColor) {
        return true;
    }

    if (card.color == topCard.color) {
        return true;
    }

    if (card.rank == topCard.rank) {
        return true;
    }

    return false;
}

int nextPlayerIndex(const GameState& game, int step) {
    int n = static_cast<int>(game.players.size());
    if (n == 0) {
        throw runtime_error("No players in game.");
    }

    int move = game.direction * step;
    int next = (game.currentPlayer + move) % n;
    if (next < 0) {
        next += n;
    }
    return next;
}

void reshuffleDiscardIntoDrawPile(GameState& game) {
    if (game.discardPile.size() <= 1) {
        return;
    }

    Card top = game.discardPile.back();
    game.discardPile.pop_back();

    game.drawPile.insert(game.drawPile.end(), game.discardPile.begin(), game.discardPile.end());
    game.discardPile.clear();
    game.discardPile.push_back(top);

    random_device rd;
    mt19937 gen(rd());
    shuffle(game.drawPile.begin(), game.drawPile.end(), gen);
}

void drawCards(GameState& game, int playerIndex, int count) {
    if (playerIndex < 0 || playerIndex >= static_cast<int>(game.players.size())) {
        throw out_of_range("Invalid player index.");
    }

    for (int i = 0; i < count; ++i) {
        if (game.drawPile.empty()) {
            reshuffleDiscardIntoDrawPile(game);
        }
        if (game.drawPile.empty()) {
            return;
        }

        game.players[playerIndex].hand.push_back(game.drawPile.back());
        game.drawPile.pop_back();
    }
}

void moveToNextPlayer(GameState& game, int step) {
    game.currentPlayer = nextPlayerIndex(game, step);
}

int countColorInHand(const vector<Card>& hand, Color color) {
    int count = 0;
    for (const Card& card : hand) {
        if (card.color == color) {
            ++count;
        }
    }
    return count;
}

Color chooseBestColorForBot(const vector<Card>& hand) {
    vector<Color> colors = {Color::Red, Color::Yellow, Color::Green, Color::Blue};
    Color bestColor = Color::Red;
    int bestCount = -1;

    for (Color color : colors) {
        int count = countColorInHand(hand, color);
        if (count > bestCount) {
            bestCount = count;
            bestColor = color;
        }
    }

    return bestColor;
}

int botCardScore(const Card& card, int botHandSize) {
    if (card.rank == Rank::WildDrawFour) {
        return botHandSize <= 3 ? 100 : 30;
    }
    if (card.rank == Rank::DrawTwo) {
        return 90;
    }
    if (card.rank == Rank::Skip) {
        return 80;
    }
    if (card.rank == Rank::Reverse) {
        return 70;
    }
    if (card.rank == Rank::Wild) {
        return botHandSize <= 3 ? 60 : 20;
    }
    if (isNumberCard(card)) {
        return 10 + rankValue(card.rank);
    }
    return 0;
}

int chooseBotCardIndex(const GameState& game, int botIndex) {
    if (botIndex < 0 || botIndex >= static_cast<int>(game.players.size())) {
        return -1;
    }

    const vector<Card>& hand = game.players[botIndex].hand;
    Card top = topDiscard(game);
    int bestIndex = -1;
    int bestScore = -1;

    for (int i = 0; i < static_cast<int>(hand.size()); ++i) {
        if (!canPlay(hand[i], top, game.currentColor)) {
            continue;
        }

        int score = botCardScore(hand[i], static_cast<int>(hand.size()));
        if (score > bestScore) {
            bestScore = score;
            bestIndex = i;
        }
    }

    return bestIndex;
}

bool botTakeTurn(GameState& game, int botIndex) {
    if (botIndex != game.currentPlayer) {
        return false;
    }

    int cardIndex = chooseBotCardIndex(game, botIndex);
    if (cardIndex == -1) {
        cout << game.players[botIndex].name << " has no playable card and draws 1 card." << endl;
        drawCards(game, botIndex, 1);

        int lastIndex = static_cast<int>(game.players[botIndex].hand.size()) - 1;
        if (lastIndex >= 0 &&
            canPlay(game.players[botIndex].hand[lastIndex], topDiscard(game), game.currentColor)) {
            cardIndex = lastIndex;
        } else {
            moveToNextPlayer(game, 1);
            return true;
        }
    }

    Card chosenCard = game.players[botIndex].hand[cardIndex];
    Color chosenColor = Color::None;
    if (isWildCard(chosenCard)) {
        chosenColor = chooseBestColorForBot(game.players[botIndex].hand);
    }

    cout << game.players[botIndex].name << " plays " << cardToString(chosenCard);
    if (isWildCard(chosenCard)) {
        cout << " and chooses " << colorToString(chosenColor);
    }
    cout << "." << endl;

    return playCard(game, botIndex, cardIndex, chosenColor);
}

void applyCardEffect(GameState& game, const Card& card, Color chosenColor) {
    if (isWildCard(card)) {
        if (!isNormalColor(chosenColor)) {
            throw invalid_argument("Wild card must choose Red, Yellow, Green, or Blue.");
        }
        game.currentColor = chosenColor;
    } else {
        game.currentColor = card.color;
    }

    if (card.rank == Rank::Skip) {
        moveToNextPlayer(game, 2);
    } else if (card.rank == Rank::Reverse) {
        game.direction *= -1;
        if (game.players.size() == 2) {
            moveToNextPlayer(game, 2);
        } else {
            moveToNextPlayer(game, 1);
        }
    } else if (card.rank == Rank::DrawTwo) {
        int target = nextPlayerIndex(game, 1);
        drawCards(game, target, 2);
        moveToNextPlayer(game, 2);
    } else if (card.rank == Rank::WildDrawFour) {
        int target = nextPlayerIndex(game, 1);
        drawCards(game, target, 4);
        moveToNextPlayer(game, 2);
    } else {
        moveToNextPlayer(game, 1);
    }
}

bool playCard(GameState& game, int playerIndex, int handIndex, Color chosenColor) {
    if (playerIndex != game.currentPlayer) {
        return false;
    }
    if (playerIndex < 0 || playerIndex >= static_cast<int>(game.players.size())) {
        return false;
    }

    vector<Card>& hand = game.players[playerIndex].hand;
    if (handIndex < 0 || handIndex >= static_cast<int>(hand.size())) {
        return false;
    }

    Card card = hand[handIndex];
    Card top = topDiscard(game);
    if (!canPlay(card, top, game.currentColor)) {
        return false;
    }

    hand.erase(hand.begin() + handIndex);
    game.discardPile.push_back(card);
    applyCardEffect(game, card, chosenColor);
    return true;
}

bool needCallUno(const GameState& game, int playerIndex) {
    if (playerIndex < 0 || playerIndex >= static_cast<int>(game.players.size())) {
        return false;
    }
    return game.players[playerIndex].hand.size() == 1;
}

bool hasWinner(const GameState& game) {
    for (const Player& player : game.players) {
        if (player.hand.empty()) {
            return true;
        }
    }
    return false;
}

int winnerIndex(const GameState& game) {
    for (int i = 0; i < static_cast<int>(game.players.size()); ++i) {
        if (game.players[i].hand.empty()) {
            return i;
        }
    }
    return -1;
}

vector<Card> createUnoDeck() {
    vector<Card> deck;
    vector<Color> colors = {Color::Red, Color::Yellow, Color::Green, Color::Blue};

    for (Color color : colors) {
        deck.push_back({color, Rank::Num0});

        for (int repeat = 0; repeat < 2; ++repeat) {
            deck.push_back({color, Rank::Num1});
            deck.push_back({color, Rank::Num2});
            deck.push_back({color, Rank::Num3});
            deck.push_back({color, Rank::Num4});
            deck.push_back({color, Rank::Num5});
            deck.push_back({color, Rank::Num6});
            deck.push_back({color, Rank::Num7});
            deck.push_back({color, Rank::Num8});
            deck.push_back({color, Rank::Num9});
            deck.push_back({color, Rank::Skip});
            deck.push_back({color, Rank::Reverse});
            deck.push_back({color, Rank::DrawTwo});
        }
    }

    for (int i = 0; i < 4; ++i) {
        deck.push_back({Color::Wild, Rank::Wild});
        deck.push_back({Color::Wild, Rank::WildDrawFour});
    }

    return deck;
}

void shuffleDeck(vector<Card>& deck) {
    random_device rd;
    mt19937 gen(rd());
    shuffle(deck.begin(), deck.end(), gen);
}

GameState createGame(const vector<string>& playerNames) {
    if (playerNames.size() < 2) {
        throw invalid_argument("UNO needs at least 2 players.");
    }

    GameState game;
    for (const string& name : playerNames) {
        game.players.push_back({name, {}, false});
    }

    game.drawPile = createUnoDeck();
    shuffleDeck(game.drawPile);

    for (int round = 0; round < 7; ++round) {
        for (int i = 0; i < static_cast<int>(game.players.size()); ++i) {
            drawCards(game, i, 1);
        }
    }

    while (!game.drawPile.empty()) {
        Card first = game.drawPile.back();
        game.drawPile.pop_back();
        game.discardPile.push_back(first);

        if (!isWildCard(first)) {
            game.currentColor = first.color;
            break;
        }
    }

    if (game.discardPile.empty()) {
        throw runtime_error("Cannot start game without a discard card.");
    }

    return game;
}

void printHand(const Player& player) {
    cout << player.name << "'s hand:" << endl;
    for (int i = 0; i < static_cast<int>(player.hand.size()); ++i) {
        cout << "  [" << i << "] " << cardToString(player.hand[i]) << endl;
    }
}

#ifndef SKIP_MAIN
int main() {
    GameState game = createGame({"Human", "Robot"});
    game.players[1].isBot = true;

    cout << "Top card: " << cardToString(topDiscard(game)) << endl;
    cout << "Current color: " << colorToString(game.currentColor) << endl;
    cout << "Current player: " << game.players[game.currentPlayer].name << endl;

    printHand(game.players[game.currentPlayer]);

    cout << endl << "Playable cards:" << endl;
    Card top = topDiscard(game);
    const vector<Card>& hand = game.players[game.currentPlayer].hand;
    for (int i = 0; i < static_cast<int>(hand.size()); ++i) {
        if (canPlay(hand[i], top, game.currentColor)) {
            cout << "  [" << i << "] " << cardToString(hand[i]) << endl;
        }
    }

    cout << endl << "Bot algorithm demo:" << endl;
    game.currentPlayer = 1;
    cout << "Top card: " << cardToString(topDiscard(game)) << endl;
    cout << "Current color: " << colorToString(game.currentColor) << endl;
    printHand(game.players[game.currentPlayer]);
    botTakeTurn(game, game.currentPlayer);

    return 0;
}
#endif