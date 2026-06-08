#ifndef AI_H
#define AI_H

#include <vector>
#include <string>

using namespace std;

// ===== 枚举和结构体定义 =====

enum class Color {
    Red,
    Yellow,
    Green,
    Blue,
    Wild,
    None
};

enum class Rank {
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

struct Card {
    Color color;
    Rank rank;
};

struct Player {
    string name;
    vector<Card> hand;
    bool isBot = false;
};

struct GameState {
    vector<Player> players;
    vector<Card> drawPile;
    vector<Card> discardPile;
    Color currentColor = Color::None;
    int currentPlayer = 0;
    int direction = 1; // 1 means clockwise, -1 means counterclockwise.
};

// ===== 函数声明 =====

bool isWildCard(const Card& card);
bool isNumberCard(const Card& card);
string colorToString(Color color);
string rankToString(Rank rank);
string cardToString(const Card& card);
int rankValue(Rank rank);
bool isNormalColor(Color color);
Card topDiscard(const GameState& game);
bool canPlay(const Card& card, const Card& topCard, Color currentColor);
int nextPlayerIndex(const GameState& game, int step = 1);
void reshuffleDiscardIntoDrawPile(GameState& game);
void drawCards(GameState& game, int playerIndex, int count);
void moveToNextPlayer(GameState& game, int step = 1);
int countColorInHand(const vector<Card>& hand, Color color);
Color chooseBestColorForBot(const vector<Card>& hand);
int botCardScore(const Card& card, int botHandSize);
int chooseBotCardIndex(const GameState& game, int botIndex);
bool botTakeTurn(GameState& game, int botIndex);
void applyCardEffect(GameState& game, const Card& card, Color chosenColor = Color::None);
bool playCard(GameState& game, int playerIndex, int handIndex, Color chosenColor = Color::None);
bool needCallUno(const GameState& game, int playerIndex);
bool hasWinner(const GameState& game);
int winnerIndex(const GameState& game);
vector<Card> createUnoDeck();
void shuffleDeck(vector<Card>& deck);
GameState createGame(const vector<string>& playerNames);
void printHand(const Player& player);

#endif
