/*
 * UNO Game - 客户端主程序
 * EasyX 图形库，支持单人 AI 和多人网络对战。
 * 单人模式：本地 GameState，AI 自动出牌。
 * 多人模式：通过 network 模块连接 TCP 服务端。
 * 协议：命令|参数1|参数2|... 以 \n 结尾。
 */

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <stdlib.h>
#include <conio.h>
#include <windows.h>
#include <graphics.h>
#include "music.h"
#include "picture.h"
#pragma comment(lib,"winmm.lib")
#include <graphics.h>
#include "ai.h"
#include "network.h"
#include "Tool.h"

extern bool networkConnect(const std::wstring& address);
extern bool networkSend(const std::string& message);
extern bool networkRecv(std::string& message, int timeoutMs);
extern void networkDisconnect();
extern bool networkIsConnected();
extern std::string networkLastError();

#ifdef UNICODE
inline std::wstring s2w(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring ws(n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &ws[0], n);
    return ws;
}
#else
inline const std::string& s2w(const std::string& s) { return s; }
#endif

// ===== 卡牌与布局常量 =====
const int CARD_W = 60;
const int CARD_H = 85;
const int PLAYER_CARD_W = 72;
const int PLAYER_CARD_H = 100;
const int MAX_CARD_GAP = 82;
const int HAND_Y = 440;
const int DISCARD_X = 310;
const int DISCARD_Y = 150;
const int DRAW_X = 430;
const int DRAW_Y = 150;
const int STATUS_Y = 390;

LPCTSTR g_backgroundPath = _T("Assets/Picture/bg.jpg");
IMAGE cardImages[5][15];
IMAGE cardBack;

void loadCardImages();
void drawPlayerHand(const vector<Card>& hand);
void drawOpponents(const GameState& game, int myIndex);
int hitTestCard(int mx, int my, int handSize);
bool isInRect(int mx, int my, int rx, int ry, int rw, int rh);
Color showColorPicker();
void setSmoothFont(int height, LPCTSTR face);

/*
 * Button 类 —— 通用按钮控件
 * 支持三种状态：正常 / 悬停 / 按下
 * 由鼠标消息驱动状态更新，自动绘制圆角矩形 + 居中文字。
 * EasyX 本身没有自带按钮，手动实现。
 */
class Button {
private:
    int x, y, w, h;
    LPCTSTR text;
    COLORREF normalColor, hoverColor, pressColor, textColor;
    bool hovering, pressing, enabled;
    int fontSize;
public:
    Button(int _x, int _y, int _w, int _h, LPCTSTR _text,
        COLORREF _normal = RGB(0, 160, 255),
        COLORREF _hover = RGB(70, 200, 255),
        COLORREF _press = RGB(0, 100, 200))
        : x(_x), y(_y), w(_w), h(_h), text(_text),
        normalColor(_normal), hoverColor(_hover), pressColor(_press),
        textColor(WHITE), hovering(false), pressing(false), enabled(true), fontSize(25) {}

    void draw() {
        COLORREF drawColor, drawTextColor;
        if (!enabled) {
            drawColor = RGB(150, 150, 150);
            drawTextColor = RGB(100, 100, 100);
        } else if (pressing) {
            drawColor = pressColor;
            drawTextColor = textColor;
        } else if (hovering) {
            drawColor = hoverColor;
            drawTextColor = textColor;
        } else {
            drawColor = normalColor;
            drawTextColor = textColor;
        }
        setfillcolor(drawColor);
        fillroundrect(x, y, x + w, y + h, 8, 8);
        setlinecolor(RGB(0, 100, 200));
        setlinestyle(PS_SOLID, 2);
        roundrect(x, y, x + w, y + h, 8, 8);
        setbkmode(TRANSPARENT);
        settextcolor(drawTextColor);
        setSmoothFont(fontSize, _T("SimSun"));

        int tx = x + (w - textwidth(text)) / 2;
        int ty = y + (h - textheight(text)) / 2;
        outtextxy(tx, ty, text);
    }

    void handleMessage(ExMessage msg) {
        if (!enabled) return;
        bool inside = (msg.x >= x && msg.x <= x + w && msg.y >= y && msg.y <= y + h);
        if (msg.message == WM_MOUSEMOVE) hovering = inside;
        else if (msg.message == WM_LBUTTONDOWN && inside) pressing = true;
        else if (msg.message == WM_LBUTTONUP) pressing = false;
    }

    bool isClicked(ExMessage msg) {
        if (!enabled) return false;
        return (msg.message == WM_LBUTTONUP &&
                msg.x >= x && msg.x <= x + w && msg.y >= y && msg.y <= y + h);
    }

    void setEnabled(bool e) { enabled = e; }
    bool isEnabled() { return enabled; }
    void setText(LPCTSTR t) { text = t; }
    LPCTSTR getText() { return text; }
    void setPosition(int _x, int _y) { x = _x; y = _y; }
    void setFontSize(int s) { fontSize = s; }
};

/*
 * setSmoothFont —— 设置带 ClearType 抗锯齿的字体
 * 使用 EasyX 的 LOGFONT 结构体 + settextstyle 实现。
 * 参数 face 传字体名称（如 SimSun/SimHei）。
 */
void setSmoothFont(int height, LPCTSTR face) {
    LOGFONT lf = { 0 };
    lf.lfHeight = height;
    lf.lfWeight = FW_BOLD;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfCharSet = GB2312_CHARSET;
    _tcscpy_s(lf.lfFaceName, face);
    settextstyle(&lf);
}

void openMenu();

/*
 * openSetting —— 设置界面
 * 左栏：音量控制（6 档：静音/20%/40%/60%/80%/100%）
 * 右栏：背景切换（3 套背景图）
 * 点击音量按钮调用 music::adjustCurrentVolume 实时调整。
 * 点击背景按钮修改全局 g_backgroundPath。
 */
void openSetting() {
    IMAGE bg; loadimage(&bg, g_backgroundPath, 800, 600);
    Button vol0(150,100,200,30,_T("静音"));
    Button vol20(150,150,200,30,_T("20%"));
    Button vol40(150,200,200,30,_T("40%"));
    Button vol60(150,250,200,30,_T("60%"));
    Button vol80(150,300,200,30,_T("80%"));
    Button vol100(150,350,200,30,_T("100%"));
    Button exitSetting(300,450,200,30,_T("返回"));
    Button bgp1(450,175,200,30,_T("背景1"));
    Button bgp2(450,225,200,30,_T("背景2"));
    Button bgp3(450,275,200,30,_T("背景3"));
    ExMessage msg; bool running = true;
    while (running) {
        while (peekmessage(&msg, EM_MOUSE)) {
            vol0.handleMessage(msg); vol20.handleMessage(msg); vol40.handleMessage(msg);
            vol60.handleMessage(msg); vol80.handleMessage(msg); vol100.handleMessage(msg);
            bgp1.handleMessage(msg); bgp2.handleMessage(msg); bgp3.handleMessage(msg);
            exitSetting.handleMessage(msg);
            if (vol0.isClicked(msg)) { music::adjustCurrentVolume(0); outtextxy(350,500,_T("已静音")); FlushBatchDraw();Sleep(500); }
            if (vol20.isClicked(msg)) { music::adjustCurrentVolume(200); outtextxy(350,500,_T("20%")); FlushBatchDraw();Sleep(500); }
            if (vol40.isClicked(msg)) { music::adjustCurrentVolume(400); outtextxy(350,500,_T("40%")); FlushBatchDraw();Sleep(500); }
            if (vol60.isClicked(msg)) { music::adjustCurrentVolume(600); outtextxy(350,500,_T("60%")); FlushBatchDraw();Sleep(500); }
            if (vol80.isClicked(msg)) { music::adjustCurrentVolume(800); outtextxy(350,500,_T("80%")); FlushBatchDraw();Sleep(500); }
            if (vol100.isClicked(msg)) { music::adjustCurrentVolume(1000); outtextxy(350,500,_T("100%")); FlushBatchDraw();Sleep(500); }
            if (bgp1.isClicked(msg)) { g_backgroundPath=_T("Assets/Picture/bg.jpg"); loadimage(&bg,g_backgroundPath,800,600); outtextxy(350,500,_T("背景1")); FlushBatchDraw();Sleep(500); }
            if (bgp2.isClicked(msg)) { g_backgroundPath=_T("Assets/Picture/bg2.jpg"); loadimage(&bg,g_backgroundPath,800,600); outtextxy(350,500,_T("背景2")); FlushBatchDraw();Sleep(500); }
            if (bgp3.isClicked(msg)) { g_backgroundPath=_T("Assets/Picture/bg3.jpg"); loadimage(&bg,g_backgroundPath,800,600); outtextxy(350,500,_T("背景3")); FlushBatchDraw();Sleep(500); }
            if (exitSetting.isClicked(msg)) running = false;
        }
        cleardevice(); putimage(0,0,&bg);
        settextcolor(RGB(255,255,255)); setSmoothFont(35,_T("SimHei")); outtextxy(155,50,_T("音量"));
        setSmoothFont(35,_T("SimHei")); outtextxy(460,50,_T("背景"));
        vol0.draw(); vol20.draw(); vol40.draw(); vol60.draw(); vol80.draw(); vol100.draw();
        bgp1.draw(); bgp2.draw(); bgp3.draw(); exitSetting.draw();
        FlushBatchDraw(); Sleep(16);
    }
}

/*
 * loadCardImages —— 从 Assets/CardImg/ 加载所有卡牌图片
 * 图片命名规则：{颜色}_{点数}.png
 *   颜色：red / yellow / green / blue / black（万能牌）
 *   点数：0-9 / Skip / Reverse / Draw Two / Wild / Wild Draw Four
 * 所有牌加载到全局 cardImages[5][15] 二维缓存中。
 * cardBack 单独存储牌背图片。
 */
void loadCardImages() {
    LPCTSTR cn[] = {_T("red"), _T("yellow"), _T("green"), _T("blue"), _T("black")};
    TCHAR p[256];
    for (int c = 0; c < 4; c++) {
        for (int n = 0; n <= 9; n++) { _stprintf_s(p,_countof(p),_T("Assets/CardImg/%s_%d.png"),cn[c],n); loadimage(&cardImages[c][n],p,CARD_W,CARD_H); }
        _stprintf_s(p,_countof(p),_T("Assets/CardImg/%s_Skip.png"),cn[c]); loadimage(&cardImages[c][10],p,CARD_W,CARD_H);
        _stprintf_s(p,_countof(p),_T("Assets/CardImg/%s_Reverse.png"),cn[c]); loadimage(&cardImages[c][11],p,CARD_W,CARD_H);
        _stprintf_s(p,_countof(p),_T("Assets/CardImg/%s_Draw Two.png"),cn[c]); loadimage(&cardImages[c][12],p,CARD_W,CARD_H);
    }
    loadimage(&cardImages[4][13],_T("Assets/CardImg/black_Wild.png"),CARD_W,CARD_H);
    loadimage(&cardImages[4][14],_T("Assets/CardImg/black_Wild Draw Four.png"),CARD_W,CARD_H);
    loadimage(&cardBack,_T("Assets/CardImg/card_back.png"),CARD_W,CARD_H);
}

/*
 * drawPlayerHand —— 在屏幕底部绘制玩家手牌
 * 动态计算牌间距 gap，保证所有手牌在 780px 内显示。
 * 手牌少时 gap=68px（牌间留 8px 空隙），多时自动压缩。
 * 整行水平居中，使用带 alpha 透明的 putimage_alpha。
 */
void drawPlayerHand(const vector<Card>& hand) {
    if (hand.empty()) return;
    int n = (int)hand.size();
    int gap = (n <= 1) ? PLAYER_CARD_W : min(MAX_CARD_GAP, (780 - PLAYER_CARD_W) / (n - 1));
    int tw = PLAYER_CARD_W + (n - 1) * gap;
    int sx = (800 - tw) / 2;
    for (int i = 0; i < n; i++)
        Tool::putimage_alpha(sx + i * gap, HAND_Y, PLAYER_CARD_W, PLAYER_CARD_H,
                            &cardImages[(int)hand[i].color][(int)hand[i].rank]);
}

/*
 * drawOpponents —— 绘制对手信息
 * 三名对手分别显示在顶部左(30)、中(300)、右(580)三个位置。
 * 每个对手显示：名字（当前轮到的蓝色高亮）、牌背叠放、手牌数量。
 * 参数 myIndex 用于跳过玩家自己的绘制。
 */
void drawOpponents(const GameState& game, int myIndex) {
    int xp[] = {30, 300, 580};
    int oi = 0;
    for (int i = 0; i < (int)game.players.size(); i++) {
        if (i == myIndex) continue;
        int x = xp[oi];
        bool isActive = (i == game.currentPlayer);
        settextcolor(isActive ? RGB(0, 140, 255) : WHITE);
        setSmoothFont(16, _T("SimSun"));
        string nm = (isActive ? "> " : "  ") + game.players[i].name;
        outtextxy(x, 10, s2w(nm).c_str());
        int cnt = (int)game.players[i].hand.size();
        for (int j = 0; j < cnt; j++) Tool::putimage_alpha(x + j * 12, 30, &cardBack);
        int lastX = x + max(0, cnt - 1) * 12 + CARD_W + 4;
        setSmoothFont(14, _T("SimSun"));
        outtextxy(lastX, 30 + CARD_H - 20, s2w("x" + to_string(cnt)).c_str());
        oi++;
    }
}

int hitTestCard(int mx, int my, int handSize) {
    if (my < HAND_Y || my > HAND_Y + PLAYER_CARD_H) return -1;
    if (handSize <= 0) return -1;
    int gap = (handSize <= 1) ? PLAYER_CARD_W : min(MAX_CARD_GAP, (780 - PLAYER_CARD_W) / (handSize - 1));
    int tw = PLAYER_CARD_W + (handSize - 1) * gap; int sx = (800 - tw) / 2;
    for (int i = handSize - 1; i >= 0; i--) { int x = sx + i * gap; if (mx >= x && mx <= x + PLAYER_CARD_W) return i; }
    return -1;
}

bool isInRect(int mx, int my, int rx, int ry, int rw, int rh) {
    return mx >= rx && mx <= rx + rw && my >= ry && my <= ry + rh;
}

/*
 * showColorPicker —— 出万能牌时的颜色选择器
 * 在当前画面之上叠加四个颜色方块（红/黄/绿/蓝）。
 * 等待玩家点击后返回所选 Color 枚举值。
 * 本函数不清屏，调用者负责在返回后重新绘制完整帧。
 */
Color showColorPicker() {
    const int bw = 70, bh = 50, sx = 170, sy = 270, gap = 20;
    COLORREF cc[] = {RED, YELLOW, GREEN, BLUE};
    Color ec[] = {Color::Red, Color::Yellow, Color::Green, Color::Blue};
    settextcolor(WHITE); setSmoothFont(22, _T("SimHei")); outtextxy(320, 220, _T("选颜色:"));
    for (int i = 0; i < 4; i++) { setfillcolor(cc[i]); solidrectangle(sx + i * (bw + gap), sy, sx + i * (bw + gap) + bw, sy + bh); }
    FlushBatchDraw();
    while (true) {
        ExMessage m;
        while (peekmessage(&m, EM_MOUSE)) {
            if (m.message == WM_LBUTTONUP)
                for (int i = 0; i < 4; i++) { int bx = sx + i * (bw + gap); if (isInRect(m.x, m.y, bx, sy, bw, bh)) return ec[i]; }
        }
        Sleep(16);
    }
}

/*
 * startGame —— 单人游戏主循环
 * 1. 初始化：加载背景和卡牌、创建 GameState（1人+3AI）
 * 2. 背景音乐状态机：
 *    - welcome 播 30 秒 → 切 normal/normal2 轮替
 *    - 玩家手牌<=2时切 exciting
 *    - 游戏结束播 win/lose
 * 3. 游戏循环：
 *    - AI 回合：botTakeTurn 自动决策
 *    - 玩家回合：等待鼠标点击（出牌/抽牌）
 *    - 支持万能牌颜色选择器
 */
void startGame() {
    IMAGE bg; loadimage(&bg, g_backgroundPath, 800, 600);
    loadCardImages(); welcome.playMusic();
    GameState game = createGame({"你", "AI-1", "AI-2", "AI-3"});
    for (int i = 1; i < 4; i++) game.players[i].isBot = true;
    string msg = "游戏开始!";
    bool welcomeDone = false; DWORD musicTimer = GetTickCount(); int nextAction = 5000;
    bool useNormal2 = false; bool inExciting = false;
    BeginBatchDraw();

    while (!hasWinner(game)) {
        DWORD now = GetTickCount(); int curHand = (int)game.players[0].hand.size();
        if (!welcomeDone) { if (now - musicTimer >= 30000) { normal.playMusic(); welcomeDone = true; musicTimer = now; nextAction = 30000; } }
        else if (curHand <= 2) { if (!inExciting) { exciting.playMusic(); inExciting = true; } }
        else { if (inExciting) { inExciting = false; useNormal2 ? normal2.playMusic() : normal.playMusic(); musicTimer = now; nextAction = 30000; } if (now - musicTimer >= nextAction) { if (useNormal2) { normal.playMusic(); useNormal2 = false; } else { normal2.playMusic(); useNormal2 = true; } musicTimer = now; } }

        cleardevice(); putimage(0, 0, &bg);
        if (!game.discardPile.empty()) { Card top = topDiscard(game); Tool::putimage_alpha(DISCARD_X, DISCARD_Y, &cardImages[(int)top.color][(int)top.rank]); }
        Tool::putimage_alpha(DRAW_X, DRAW_Y, &cardBack);
        settextcolor(WHITE); setSmoothFont(14, _T("SimSun"));
        outtextxy(DRAW_X, DRAW_Y + CARD_H + 5, s2w("牌堆:" + to_string(game.drawPile.size())).c_str());
        if (game.currentColor >= Color::Red && game.currentColor <= Color::Blue) { COLORREF cm[] = {RED, YELLOW, GREEN, BLUE}; setfillcolor(cm[(int)game.currentColor]); solidcircle(370, 255, 18); }
        settextcolor(WHITE); setSmoothFont(14, _T("SimSun")); outtextxy(355, 278, _T("颜色"));
        drawOpponents(game, 0); drawPlayerHand(game.players[0].hand);
        settextcolor(RGB(255, 215, 0)); setSmoothFont(20, _T("SimHei"));
        outtextxy(400 - textwidth(s2w(msg).c_str()) / 2, STATUS_Y - 20, s2w(msg).c_str());
        FlushBatchDraw();

        if (game.players[game.currentPlayer].isBot) {
            int ai = game.currentPlayer; msg = game.players[ai].name + " 思考中..."; FlushBatchDraw(); Sleep(600);
            botTakeTurn(game, ai);
            Card top = topDiscard(game); msg = game.players[ai].name + " 出了 " + cardToString(top);
            if (isWildCard(top)) msg += " 选" + colorToString(game.currentColor);
            FlushBatchDraw(); Sleep(600);
        } else {
            msg = "轮到你了!"; FlushBatchDraw();
            bool acted = false;
            while (!acted && !hasWinner(game)) {
                ExMessage em;
                while (peekmessage(&em, EM_MOUSE)) {
                    if (em.message != WM_LBUTTONUP) continue;
                    int hit = hitTestCard(em.x, em.y, (int)game.players[0].hand.size());
                    if (hit >= 0) { Card& card = game.players[0].hand[hit]; if (canPlay(card, topDiscard(game), game.currentColor)) { Color ch = Color::None; if (isWildCard(card)) ch = showColorPicker(); if (playCard(game, 0, hit, ch)) { msg = "出了 " + cardToString(card); if (isWildCard(card)) msg += " 选" + colorToString(ch); acted = true; } } else { msg = "不能出!"; } break; }
                    if (isInRect(em.x, em.y, DRAW_X, DRAW_Y, CARD_W, CARD_H)) { drawCards(game, 0, 1); msg = "抽了一张"; if (!game.players[0].hand.empty()) { Card& dn = game.players[0].hand.back(); if (canPlay(dn, topDiscard(game), game.currentColor)) msg += "(可出)"; else { moveToNextPlayer(game, 1); acted = true; } } break; }
                }
                cleardevice(); putimage(0, 0, &bg);
                if (!game.discardPile.empty()) { Card top = topDiscard(game); Tool::putimage_alpha(DISCARD_X, DISCARD_Y, &cardImages[(int)top.color][(int)top.rank]); }
                Tool::putimage_alpha(DRAW_X, DRAW_Y, &cardBack);
                if (game.currentColor >= Color::Red && game.currentColor <= Color::Blue) { COLORREF cm[] = {RED,YELLOW,GREEN,BLUE}; setfillcolor(cm[(int)game.currentColor]); solidcircle(370,255,18); }
                drawOpponents(game, 0); drawPlayerHand(game.players[0].hand);
                settextcolor(RGB(255,215,0)); setSmoothFont(20,_T("SimHei"));
                outtextxy(400 - textwidth(s2w(msg).c_str()) / 2, STATUS_Y - 20, s2w(msg).c_str());
                FlushBatchDraw(); Sleep(16);
            }
        }
    }

    int wi = winnerIndex(game); if (wi == 0) win.playMusic(); else lose.playMusic();
    cleardevice(); putimage(0, 0, &bg);
    settextcolor(RGB(255,215,0)); setSmoothFont(40, _T("SimHei"));
    string ems = game.players[wi].name + " 获胜!";
    outtextxy(400 - textwidth(s2w(ems).c_str()) / 2, 230, s2w(ems).c_str());
    Button btnBack(300,340,200,50,_T("返回"));
    btnBack.draw(); FlushBatchDraw();
    ExMessage flush; while (peekmessage(&flush, EM_MOUSE));
    bool back = false;
    while (!back) {
        ExMessage em;
        while (peekmessage(&em, EM_MOUSE)) { btnBack.handleMessage(em); if (btnBack.isClicked(em)) back = true; }
        cleardevice(); putimage(0,0,&bg);
        settextcolor(RGB(255,215,0)); setSmoothFont(40,_T("SimHei"));
        outtextxy(400 - textwidth(s2w(ems).c_str())/2, 230, s2w(ems).c_str());
        btnBack.draw(); FlushBatchDraw(); Sleep(16);
    }
    EndBatchDraw();
}

/*
 * multipleGame —— 多人游戏主逻辑
 * 1. 通过 InputBox 获取服务端地址 → networkConnect
 * 2. 发送 JOIN 消息加入游戏，在等待房显示玩家列表
 * 3. 管理员在服务端输入 op start 后开始游戏
 * 4. 接收服务端消息并更新本地 GameState 镜像：
 *    - STATE：更新当前颜色、方向、当前玩家、弃牌堆顶牌
 *    - HAND：更新玩家手牌
 *    - YOUR_TURN：允许鼠标交互（出牌/抽牌）
 *    - ACTION：显示其他玩家的操作
 *    - WINNER：游戏结束，播 win/lose 音效
 * 5. ESC 退出 / 断连自动返回主菜单
 * 6. 背景音乐状态机与单人模式相同
 */
void multipleGame() {
    wchar_t addr[100] = L"";
    InputBox(addr, 100, L"服务器地址:", L"多人连接", L"127.0.0.1:8888");
    if (wcslen(addr) == 0) return;

    if (!networkConnect(addr)) {
        MessageBox(GetHWnd(), _T("连接失败!"), _T("错误"), MB_OK | MB_ICONERROR);
        return;
    }

    wchar_t playerName[50] = L"";
    InputBox(playerName, 50, L"你的名字:", L"多人游戏", L"玩家");
    char nameBuf[100] = {0};
    if (wcslen(playerName) > 0)
        WideCharToMultiByte(CP_ACP, 0, playerName, -1, nameBuf, 100, nullptr, nullptr);
    else strcpy_s(nameBuf, "玩家");

    networkSend(string("JOIN|") + nameBuf);

    GameState localGame;
    localGame.currentColor = Color::None;
    localGame.direction = 1;
    localGame.currentPlayer = 0;
    int myPlayerId = -1;
    bool gameStarted = false, myTurn = false, acted = false;
    vector<string> playerNames(4, "?");
    string statusMsg = "等待服务端...";
    localGame.players.resize(4);

    bool welcomeDone = false, useNormal2 = false, inExciting = false;
    DWORD musicTimer = GetTickCount();
    int nextAction = 5000;

    IMAGE bg; loadimage(&bg, g_backgroundPath, 800, 600);
    loadCardImages(); BeginBatchDraw();

    while (networkIsConnected()) {
        // BGM update
        { DWORD now = GetTickCount();
          if (gameStarted) {
              int curHand = (int)localGame.players[myPlayerId >= 0 ? myPlayerId : 0].hand.size();
              if (!welcomeDone) {
                  if (now - musicTimer >= 30000) { normal.playMusic(); welcomeDone = true; musicTimer = now; nextAction = 30000; }
              } else if (curHand <= 2) {
                  if (!inExciting) { exciting.playMusic(); inExciting = true; }
              } else {
                  if (inExciting) { inExciting = false; useNormal2 ? normal2.playMusic() : normal.playMusic(); musicTimer = now; nextAction = 30000; }
                  if (now - musicTimer >= nextAction) { if (useNormal2) { normal.playMusic(); useNormal2 = false; } else { normal2.playMusic(); useNormal2 = true; } musicTimer = now; }
              }
          }
        }

        // Network messages
        string msg;
        while (networkRecv(msg, 0)) {
            size_t p = msg.find('|');
            string cmd = (p == string::npos) ? msg : msg.substr(0, p);
            string rest = (p == string::npos) ? "" : msg.substr(p + 1);

            if (cmd == "WELCOME") { myPlayerId = stoi(rest); }
            else if (cmd == "GAME_START") { gameStarted = true; statusMsg = "游戏开始!"; localGame.players.resize(4); welcome.playMusic(); welcomeDone = false; musicTimer = GetTickCount(); }
            else if (cmd == "PLAYER_LIST") { std::istringstream ss(rest); string e; while (getline(ss, e, '|')) { size_t c = e.find(':'); if (c != string::npos) { int pid = stoi(e.substr(0,c)); if (pid < 4) playerNames[pid] = e.substr(c+1); } } }
            else if (cmd == "STATE") {
                std::istringstream ss(rest); string sc,sd,scp,sz,stc,sr;
                getline(ss, sc, '|'); getline(ss, sd, '|'); getline(ss, scp, '|');
                getline(ss, sz, '|'); getline(ss, stc, '|'); getline(ss, sr);
                if (sc == "Red") localGame.currentColor = Color::Red;
                else if (sc == "Yellow") localGame.currentColor = Color::Yellow;
                else if (sc == "Green") localGame.currentColor = Color::Green;
                else if (sc == "Blue") localGame.currentColor = Color::Blue;
                else localGame.currentColor = Color::None;
                localGame.direction = stoi(sd); localGame.currentPlayer = stoi(scp);
                Card t;
                if (stc == "Red") t.color = Color::Red; else if (stc == "Yellow") t.color = Color::Yellow;
                else if (stc == "Green") t.color = Color::Green; else if (stc == "Blue") t.color = Color::Blue;
                else t.color = Color::Wild;
                string rm[] = {"0","1","2","3","4","5","6","7","8","9","Skip","Reverse","DrawTwo","Wild","WildDrawFour"};
                Rank rv[] = {Rank::Num0,Rank::Num1,Rank::Num2,Rank::Num3,Rank::Num4,Rank::Num5,Rank::Num6,Rank::Num7,Rank::Num8,Rank::Num9,Rank::Skip,Rank::Reverse,Rank::DrawTwo,Rank::Wild,Rank::WildDrawFour};
                for (int ri = 0; ri < 15; ri++) if (sr == rm[ri]) t.rank = rv[ri];
                if (localGame.discardPile.empty()) localGame.discardPile.push_back(t);
                else { Card o = localGame.discardPile.back(); if (o.color != t.color || o.rank != t.rank) localGame.discardPile.push_back(t); }
            }
            else if (cmd == "HAND") {
                int hidx = (myPlayerId >= 0) ? myPlayerId : 0;
                printf("[CLIENT] HAND received -> slot %d\n", hidx);
                localGame.players[hidx].hand.clear(); std::istringstream ss(rest); string cs;
                string rm2[] = {"0","1","2","3","4","5","6","7","8","9","Skip","Reverse","DrawTwo","Wild","WildDrawFour"};
                Rank rv2[] = {Rank::Num0,Rank::Num1,Rank::Num2,Rank::Num3,Rank::Num4,Rank::Num5,Rank::Num6,Rank::Num7,Rank::Num8,Rank::Num9,Rank::Skip,Rank::Reverse,Rank::DrawTwo,Rank::Wild,Rank::WildDrawFour};
                while (getline(ss, cs, '|')) { size_t u = cs.find('_'); if (u == string::npos) continue; string cl = cs.substr(0,u), rk = cs.substr(u+1); Card c; if (cl=="Red") c.color=Color::Red; else if (cl=="Yellow") c.color=Color::Yellow; else if (cl=="Green") c.color=Color::Green; else if (cl=="Blue") c.color=Color::Blue; else c.color=Color::Wild; for (int ri=0;ri<15;ri++) if (rk==rm2[ri]) c.rank=rv2[ri]; localGame.players[hidx].hand.push_back(c); }
            }
            else if (cmd == "YOUR_TURN") {
                myTurn = true; acted = false; statusMsg = "轮到你了!";
                Card top = localGame.discardPile.empty() ? Card{Color::None,Rank::Num0} : localGame.discardPile.back();
                int hidx = (myPlayerId >= 0) ? myPlayerId : 0;
                bool hp = false; for (Card& c : localGame.players[hidx].hand) if (canPlay(c, top, localGame.currentColor)) { hp = true; break; }
                if (!hp) { networkSend("DRAW"); acted = true; myTurn = false; statusMsg = "没牌出,自动抽牌..."; }
            }
            else if (cmd == "ACTION") { std::istringstream ss(rest); string sid,at,ac,ac2; getline(ss,sid,'|'); getline(ss,at,'|'); getline(ss,ac,'|'); getline(ss,ac2); int ap=stoi(sid); if(at=="TURN") statusMsg=playerNames[ap]+" 的回合"; else if(at=="PLAY"){statusMsg=playerNames[ap]+" 出了 "+ac;if(!ac2.empty())statusMsg+=" "+ac2;} else if(at=="DRAW") statusMsg=playerNames[ap]+" 抽牌"; }
            else if (cmd == "OPPONENT_HAND") { std::istringstream ss(rest); string sid,sn; getline(ss,sid,'|'); getline(ss,sn); int oid=stoi(sid),cnt=stoi(sn); printf("[CLIENT] OPP_HAND: oid=%d cnt=%d myId=%d\n",oid,cnt,myPlayerId); if(oid>=0&&oid<4&&oid!=myPlayerId) { localGame.players[oid].hand.resize(cnt); printf("[CLIENT] -> players[%d].hand.size()=%zu\n",oid,localGame.players[oid].hand.size()); } }
            else if (cmd == "WINNER") { std::istringstream ss(rest); string wid,wn; getline(ss,wid,'|'); getline(ss,wn); std::wstring wW(wn.begin(),wn.end()); if(myPlayerId==stoi(wid)) win.playMusic(); else lose.playMusic(); MessageBox(GetHWnd(),(wW+L" 获胜!").c_str(),_T("游戏结束"),MB_OK); gameStarted=false; break; }
            else if (cmd == "ERROR") { if(rest=="Kicked") { networkDisconnect(); break; } statusMsg="错误:"+rest; }
            else if (cmd == "MSG") { statusMsg = rest; }
        }

        if (!networkIsConnected()) break;

        // Mouse input
        if (gameStarted && myTurn && !acted) {
            ExMessage em;
            while (peekmessage(&em, EM_MOUSE)) {
                if (em.message != WM_LBUTTONUP) continue;
                int hidx = (myPlayerId >= 0) ? myPlayerId : 0;
                int hit = hitTestCard(em.x, em.y, (int)localGame.players[hidx].hand.size());
                if (hit >= 0) {
                    Card& card = localGame.players[hidx].hand[hit];
                    if (isWildCard(card)) {
                        Color ch = showColorPicker(); string cs;
                        if (ch == Color::Red) cs = "Red"; else if (ch == Color::Yellow) cs = "Yellow";
                        else if (ch == Color::Green) cs = "Green"; else cs = "Blue";
                        networkSend("PLAY|" + to_string(hit) + "|" + cs);
                    } else { networkSend("PLAY|" + to_string(hit) + "|None"); }
                    acted = true; myTurn = false; break;
                }
                if (isInRect(em.x, em.y, DRAW_X, DRAW_Y, CARD_W, CARD_H)) { networkSend("DRAW"); acted = true; myTurn = false; break; }
            }
        }

        // Render
        cleardevice(); putimage(0, 0, &bg);
        if (gameStarted) {
            if (!localGame.discardPile.empty()) { Card top = localGame.discardPile.back(); Tool::putimage_alpha(DISCARD_X,DISCARD_Y,&cardImages[(int)top.color][(int)top.rank]); }
            Tool::putimage_alpha(DRAW_X,DRAW_Y,&cardBack);
            if (localGame.currentColor >= Color::Red && localGame.currentColor <= Color::Blue) { COLORREF cm[]={RED,YELLOW,GREEN,BLUE}; setfillcolor(cm[(int)localGame.currentColor]); solidcircle(370,255,18); }
            settextcolor(WHITE); setSmoothFont(12,_T("SimSun")); outtextxy(355,278,_T("颜色"));
            drawOpponents(localGame, myPlayerId); drawPlayerHand(localGame.players[myPlayerId >= 0 ? myPlayerId : 0].hand);
            settextcolor(RGB(255,215,0)); setSmoothFont(18,_T("SimHei"));
            outtextxy(400 - textwidth(s2w(statusMsg).c_str())/2, STATUS_Y-20, s2w(statusMsg).c_str());
        } else {
            settextcolor(RGB(255,255,255)); setSmoothFont(24,_T("SimHei")); outtextxy(200,220,_T("等待服务端开始..."));
            setSmoothFont(16,_T("SimSun")); outtextxy(250,270,_T("管理员: op start 开始游戏"));
            int y = 300;
            for (int i = 0; i < (int)playerNames.size() && i < 4; ++i) {
                if (!playerNames[i].empty()) { outtextxy(250,y,s2w("玩家"+to_string(i)+": "+playerNames[i]).c_str()); y += 25; }
            }
            setSmoothFont(12,_T("SimSun")); outtextxy(250,y+10,_T("ESC 退出"));
        }
        FlushBatchDraw(); Sleep(16);
        if (_kbhit() && _getch() == 27) break;
    }

    EndBatchDraw(); networkDisconnect();
}

/*
 * openMenu —— 主菜单
 * 四个按钮：单人游戏 / 多人游戏 / 设置 / 退出游戏
 * 每个按钮点击后打开对应界面，返回后刷新背景。
 * 程序入口 main() 直接调用 openMenu()。
 */
void openMenu() {
    initgraph(800, 600);
    IMAGE bg; loadimage(&bg, g_backgroundPath, 800, 600);
    BeginBatchDraw();
    Button sp(300,140,200,60,_T("单人游戏"));
    Button mp(300,220,200,60,_T("多人游戏"));
    Button st(300,300,200,60,_T("设置"));
    Button ex(300,380,200,60,_T("退出游戏"));
    ExMessage msg; bool running = true;
    while (running) {
        while (peekmessage(&msg, EM_MOUSE)) {
            sp.handleMessage(msg); mp.handleMessage(msg); st.handleMessage(msg); ex.handleMessage(msg);
            if (sp.isClicked(msg)) { startGame(); loadimage(&bg,g_backgroundPath,800,600); FlushBatchDraw(); Sleep(500); }
            if (mp.isClicked(msg)) { multipleGame(); loadimage(&bg,g_backgroundPath,800,600); FlushBatchDraw(); Sleep(500); }
            if (st.isClicked(msg)) { openSetting(); loadimage(&bg,g_backgroundPath,800,600); FlushBatchDraw(); Sleep(500); }
            if (ex.isClicked(msg)) running = false;
        }
        cleardevice(); putimage(0,0,&bg); sp.draw(); mp.draw(); st.draw(); ex.draw();
        settextcolor(RGB(255,255,255)); setSmoothFont(35,_T("SimHei")); outtextxy(320,50,_T("UNO Game"));
        FlushBatchDraw(); Sleep(16);
    }
    EndBatchDraw(); closegraph();
}

int main() { openMenu(); return 0; }
