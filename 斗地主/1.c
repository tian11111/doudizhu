#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

// 出牌类型
typedef enum {
    PASS = 0,         // 过牌
    SINGLE,           // 单张
    PAIR,             // 对子
    TRIPLE,           // 三张
    TRIPLE_ONE,       // 三带一
    TRIPLE_PAIR,      // 三带二
    STRAIGHT,         // 顺子
    BOMB,             // 炸弹（四张相同）
    ROCKET            // 王炸（大小王）
} PlayType;

// 队伍枚举（新增：地主队/农民队）
typedef enum {
    TEAM_LANDLORD,    // 地主队
    TEAM_FARMER       // 农民队
} TeamType;

// 表示一次出牌行为
typedef struct {
    PlayType type;
    int point;            // 主点数（如对子是5，顺子是起始点）
    int length;           // 长度（顺子用）
    int cardIndices[20];  // 选中的手牌索引
    int cardCount;        // 实际出几张牌
} Play;

// 牌种类枚举
typedef enum {
    CLUBS,      // 梅花
    DIAMONDS,   // 方片
    HEARTS,     // 红桃
    SPADES,     // 黑桃
    JOKER_SMALL,// 小王
    JOKER_BIG   // 大王
} Suit;

// 点数枚举
typedef enum {
    POINT_3 = 0, POINT_4, POINT_5, POINT_6, POINT_7,
    POINT_8, POINT_9, POINT_10, POINT_J, POINT_Q,
    POINT_K, POINT_A, POINT_2,
    POINT_SMALL_JOKER, POINT_BIG_JOKER
} Point;

typedef struct {
    Suit suit;      // 花色
    Point point;    // 点数
    char name[8];   // 显示名称
} Card;

typedef struct {
    char name[20];              // 玩家名字
    Card hand[20];              // 手牌数组
    int cardCount;              // 当前手中牌的数量
    bool isLandlord;            // 是否是地主
    TeamType team;              // 所属队伍（新增）
} Player;

// 牌堆结构体
typedef struct {
    Card deck[54];
    int top;
} Deck;

// 全局变量
Deck gameDeck;
Player players[3];
Play lastPlay;
int lastPlayer;
int passCount;
int gameRound;
int currentPlayer;
bool gameOver;
char lastPlayedText[256];
int landlordIndex = -1;
char buffer[4096];
bool landlordRobbed = false;    // 抢地主是否完成（新增）
Card landlordCards[3];          // 地主底牌（新增）

// 函数声明
void clearLastPlayedText();
void buildPlayedTextFromSelection(const Player* player, int selected[], int count);
void reset_all();
void initializeDeck(Deck* deck);
void shuffleDeck(Deck* deck);
void dealCards(Deck* deck, Player players[]);
void sortHandByPoint(Player* player);
Play analyzePlay(const Player* player, int selected[], int selectedCount);
bool canPlayBeat(const Play* current, const Play* last);
int check_play_valid(int selected[], int count);
int game_play_by_player(int playerIdx, int selected[], int count);
int game_play(int selected[], int count);
int game_pass_by_player(int playerIdx);
int game_pass();
const char* game_get_state_json();
int game_ai_step();
void game_init();
void game_auto_run();

// 新增函数声明
void rob_landlord(int playerIdx);       // 玩家抢地主
void ai_rob_landlord(int playerIdx);    // AI抢地主逻辑
void assign_landlord_cards();           // 分配地主底牌
int get_best_play(int playerIdx);       // 智能出牌逻辑（核心）
bool check_team_win();                  // 检查队伍是否获胜
void update_player_team();              // 更新玩家队伍归属

// 重置所有游戏状态
void reset_all() {
    // 清空三个玩家的所有数据
    for (int i = 0; i < 3; i++) {
        players[i].cardCount = 0;
        players[i].isLandlord = false;
        players[i].team = TEAM_FARMER;  // 默认农民队
        memset(players[i].hand, 0, sizeof(players[i].hand));
        memset(players[i].name, 0, sizeof(players[i].name));
    }
    // 清空牌堆和全局状态
    memset(&gameDeck, 0, sizeof(Deck));
    memset(&lastPlay, 0, sizeof(Play));
    lastPlayer = -1;
    passCount = 0;
    gameRound = 1;
    currentPlayer = 0;
    gameOver = false;
    landlordIndex = -1;
    landlordRobbed = false;
    memset(landlordCards, 0, sizeof(landlordCards));
    clearLastPlayedText();
    memset(buffer, 0, sizeof(buffer));
}

// 初始化牌堆
void initializeDeck(Deck* deck) {
    int index = 0;
    const char* suitSymbols[] = { "梅", "方", "红", "黑" };
    const char* pointNames[] = { "3","4","5","6","7","8","9","10","J","Q","K","A","2" };

    for (int suit = CLUBS; suit <= SPADES; suit++) {
        for (int pointVal = 0; pointVal < 13; pointVal++) {
            deck->deck[index].suit = (Suit)suit;
            deck->deck[index].point = (Point)pointVal;
            sprintf(deck->deck[index].name, "%s%s", pointNames[pointVal], suitSymbols[suit]);
            index++;
        }
    }
    // 小王
    deck->deck[index++] = (Card){
        .name = "小王",
        .suit = JOKER_SMALL,
        .point = POINT_SMALL_JOKER
    };
    // 大王
    deck->deck[index++] = (Card){
        .name = "大王",
        .suit = JOKER_BIG,
        .point = POINT_BIG_JOKER
    };
}

// 洗牌
void shuffleDeck(Deck* deck) {
    for (int i = 54 - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        Card temp = deck->deck[i];
        deck->deck[i] = deck->deck[j];
        deck->deck[j] = temp;
    }
}

// 发牌（新增：预留3张地主底牌）
void dealCards(Deck* deck, Player players[]) {
    // 先给3个玩家发17张牌
    for (int cardIdx = 0; cardIdx < 51; cardIdx++) {
        int playerIndex = cardIdx % 3;
        int targetSlot = players[playerIndex].cardCount;
        players[playerIndex].hand[targetSlot] = deck->deck[cardIdx];
        players[playerIndex].cardCount++;
    }
    // 保存3张地主底牌
    for (int i = 0; i < 3; i++) {
        landlordCards[i] = deck->deck[51 + i];
    }
    printf("发牌完成，预留3张地主底牌！\n");
}

// 手牌排序
void sortHandByPoint(Player* player) {
    for (int i = 0; i < player->cardCount - 1; i++) {
        for (int j = 0; j < player->cardCount - i - 1; j++) {
            if (player->hand[j].point > player->hand[j + 1].point) {
                Card temp = player->hand[j];
                player->hand[j] = player->hand[j + 1];
                player->hand[j + 1] = temp;
            }
        }
    }
}

// 清空出牌文本
void clearLastPlayedText() {
    lastPlayedText[0] = '\0';
}

// 构建出牌文本描述
void buildPlayedTextFromSelection(const Player* player, int selected[], int count) {
    clearLastPlayedText();
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            strcat(lastPlayedText, " ");
        }
        strcat(lastPlayedText, player->hand[selected[i]].name);
    }
}

// 分析牌型
Play analyzePlay(const Player* player, int selected[], int selectedCount) {
    Play play = { 0 };
    play.cardCount = selectedCount;

    if (selectedCount == 0) {
        play.type = PASS;
        return play;
    }

    // 提取点数并排序
    int points[20];
    for (int i = 0; i < selectedCount; i++) {
        points[i] = player->hand[selected[i]].point;
    }
    // 排序点数（从小到大）
    for (int i = 0; i < selectedCount - 1; i++) {
        for (int j = 0; j < selectedCount - i - 1; j++) {
            if (points[j] > points[j + 1]) {
                int temp = points[j];
                points[j] = points[j + 1];
                points[j + 1] = temp;
            }
        }
    }

    // 复制索引
    for (int i = 0; i < selectedCount; i++) {
        play.cardIndices[i] = selected[i];
    }

    // 王炸
    if (selectedCount == 2) {
        if ((points[0] == POINT_SMALL_JOKER && points[1] == POINT_BIG_JOKER)) {
            play.type = ROCKET;
            play.point = POINT_BIG_JOKER;
            play.length = 2;
            return play;
        }
    }

    // 对子
    if (selectedCount == 2 && points[0] == points[1]) {
        play.type = PAIR;
        play.point = points[0];
        play.length = 2;
        return play;
    }

    // 单张
    if (selectedCount == 1) {
        play.type = SINGLE;
        play.point = points[0];
        play.length = 1;
        return play;
    }

    // 三张
    if (selectedCount == 3 && points[0] == points[2]) {
        play.type = TRIPLE;
        play.point = points[0];
        play.length = 3;
        return play;
    }

    // 三带一
    if (selectedCount == 4) {
        if ((points[0] == points[2] && points[2] != points[3]) ||
            (points[1] == points[3] && points[0] != points[1])) {
            play.type = TRIPLE_ONE;
            play.point = (points[0] == points[2]) ? points[0] : points[1];
            play.length = 4;
            return play;
        }
    }

    // 三带二
    if (selectedCount == 5) {
        if (points[0] == points[2] && points[3] == points[4]) {
            play.type = TRIPLE_PAIR;
            play.point = points[0];
            play.length = 5;
            return play;
        }
        if (points[0] == points[1] && points[2] == points[4]) {
            play.type = TRIPLE_PAIR;
            play.point = points[2];
            play.length = 5;
            return play;
        }
    }

    // 炸弹
    if (selectedCount == 4 && points[0] == points[3]) {
        play.type = BOMB;
        play.point = points[0];
        play.length = 4;
        return play;
    }

    // 顺子
    if (selectedCount >= 5) {
        bool isStraight = true;
        for (int i = 0; i < selectedCount - 1; i++) {
            if (points[i + 1] != points[i] + 1) {
                isStraight = false;
                break;
            }
        }
        if (isStraight && points[0] >= POINT_3 && points[selectedCount - 1] <= POINT_A) {
            play.type = STRAIGHT;
            play.point = points[0];
            play.length = selectedCount;
            return play;
        }
    }

    // 非法牌型
    play.type = PASS;
    return play;
}

// 判断当前牌能否打过上家
bool canPlayBeat(const Play* current, const Play* last) {
    if (last->type == PASS) {
        return true;
    }

    // 王炸最大
    if (current->type == ROCKET) {
        return true;
    }
    if (last->type == ROCKET) {
        return false;
    }

    // 炸弹打非王炸
    if (current->type == BOMB && last->type != ROCKET) {
        if (last->type == BOMB) {
            return current->point > last->point;
        }
        return true;
    }
    if (last->type == BOMB && current->type != BOMB && current->type != ROCKET) {
        return false;
    }

    // 同类型比较
    if (current->type == last->type) {
        if (current->type == STRAIGHT) {
            if (current->length != last->length) {
                return false;
            }
            return current->point > last->point;
        }
        else {
            return current->point > last->point;
        }
    }

    return false;
}

// 检查出牌是否合法
int check_play_valid(int selected[], int count) {
    if (count <= 0) return 0;

    Play current_play = analyzePlay(&players[currentPlayer], selected, count);
    if (current_play.type == PASS) {
        return 0;
    }

    if (!canPlayBeat(&current_play, &lastPlay)) {
        return 0;
    }

    return 1;
}

int game_play_by_player(int playerIdx, int selected[], int count) {
    if (!landlordRobbed) return 0;
    if (gameOver) return 0;
    if (playerIdx != currentPlayer) return 0;

    Player* player = &players[playerIdx];
    Play currentPlay = analyzePlay(player, selected, count);

    if (currentPlay.type == PASS) return 0;
    if (!canPlayBeat(&currentPlay, &lastPlay)) return 0;

    buildPlayedTextFromSelection(player, selected, count);

    for (int k = count - 1; k >= 0; k--) {
        int idx = selected[k];
        if (idx < 0 || idx >= player->cardCount) return 0;

        for (int j = idx; j < player->cardCount - 1; j++) {
            player->hand[j] = player->hand[j + 1];
        }
        player->cardCount--;
    }

    lastPlay = currentPlay;
    lastPlayer = playerIdx;
    passCount = 0;
    currentPlayer = (playerIdx + 1) % 3;

    if (check_team_win()) {
        gameOver = true;
        printf("game over! %s team wins!\n", player->team == TEAM_LANDLORD ? "landlord" : "farmer");
    }

    gameRound++;
    return 1;
}

// 玩家出牌
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int game_play(int selected[], int count) {
    return game_play_by_player(0, selected, count);

    // 抢地主未完成时不能出牌
    if (!landlordRobbed) {
        return 0;
    }

    Play currentPlay = analyzePlay(&players[0], selected, count);

    if (currentPlay.type == PASS) return 0;
    if (!canPlayBeat(&currentPlay, &lastPlay)) return 0;

    buildPlayedTextFromSelection(&players[0], selected, count);

    // 删除手牌
    for (int k = count - 1; k >= 0; k--) {
        int idx = selected[k];
        for (int j = idx; j < players[0].cardCount - 1; j++) {
            players[0].hand[j] = players[0].hand[j + 1];
        }
        players[0].cardCount--;
    }

    lastPlay = currentPlay;
    lastPlayer = 0;
    passCount = 0;
    currentPlayer = 1;

    // 检查队伍是否获胜
    if (check_team_win()) {
        gameOver = true;
        printf("游戏结束！%s队获胜！\n", players[0].team == TEAM_LANDLORD ? "地主" : "农民");
    }

    gameRound++;
    return 1;
}

// 过牌
#if 0
int game_pass_by_player(int playerIdx) {
    if (!landlordRobbed) return 0;
    if (gameOver) return 0;
    if (playerIdx != currentPlayer) return 0;

    if (lastPlay.type == PASS) return 0;

    strcpy(lastPlayedText, "杩囩墝");

    passCount++;
    lastPlayer = playerIdx;
    currentPlayer = (playerIdx + 1) % 3;

    if (passCount >= 2) {
        lastPlay.type = PASS;
        lastPlayer = -1;
        passCount = 0;
    }

    gameRound++;
    return 1;
}
#endif

int game_pass_by_player(int playerIdx) {
    if (!landlordRobbed) return 0;
    if (gameOver) return 0;
    if (playerIdx != currentPlayer) return 0;

    if (lastPlay.type == PASS) return 0;

    strcpy(lastPlayedText, "pass");

    passCount++;
    lastPlayer = playerIdx;
    currentPlayer = (playerIdx + 1) % 3;

    if (passCount >= 2) {
        lastPlay.type = PASS;
        lastPlayer = -1;
        passCount = 0;
    }

    gameRound++;
    return 1;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int game_pass() {
    return game_pass_by_player(0);

    if (!landlordRobbed) {
        return 0;
    }

    if (lastPlay.type == PASS) return 0;

    strcpy(lastPlayedText, "过牌");

    passCount++;
    lastPlayer = 0;
    currentPlayer = 1;

    if (passCount >= 2) {
        lastPlay.type = PASS;
        lastPlayer = -1;
        passCount = 0;
    }
    gameRound++;
    return 1;
}

// 获取游戏状态JSON
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
const char* game_get_state_json() {
    int offset = 0;

    offset += sprintf(buffer + offset, "{");

    // 基础状态（新增抢地主状态）
    offset += sprintf(buffer + offset, "\"landlordRobbed\":%s,", landlordRobbed ? "true" : "false");
    offset += sprintf(buffer + offset, "\"landlordIndex\":%d,", landlordIndex);

    // 地主底牌
    offset += sprintf(buffer + offset, "\"landlordCards\":[");
    for (int i = 0; i < 3; i++) {
        if (i > 0) offset += sprintf(buffer + offset, ",");
        offset += sprintf(buffer + offset, "\"%s\"", landlordCards[i].name);
    }
    offset += sprintf(buffer + offset, "],");

    // 我的手牌
    offset += sprintf(buffer + offset, "\"hand\":[");
    for (int i = 0; i < players[0].cardCount; i++) {
        if (i > 0) offset += sprintf(buffer + offset, ",");
        offset += sprintf(buffer + offset, "\"%s\"", players[0].hand[i].name);
    }
    offset += sprintf(buffer + offset, "],");

    // 游戏流程状态
    offset += sprintf(buffer + offset, "\"currentPlayer\":%d,", currentPlayer);
    offset += sprintf(buffer + offset, "\"gameRound\":%d,", gameRound);
    offset += sprintf(buffer + offset, "\"lastPlayer\":%d,", lastPlayer);
    offset += sprintf(buffer + offset, "\"gameOver\":%s,", gameOver ? "true" : "false");

    // 各玩家剩余牌数
    offset += sprintf(buffer + offset, "\"myCardCount\":%d,", players[0].cardCount);
    offset += sprintf(buffer + offset, "\"ai1CardCount\":%d,", players[1].cardCount);
    offset += sprintf(buffer + offset, "\"ai2CardCount\":%d,", players[2].cardCount);

    // 上一手牌信息
    offset += sprintf(buffer + offset, "\"lastPlayType\":%d,", lastPlay.type);
    offset += sprintf(buffer + offset, "\"lastPlayedText\":\"%s\"", lastPlayedText);
    offset += sprintf(buffer + offset, "}");

    return buffer;
}

// 新增：抢地主功能（玩家调用）
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rob_landlord(int playerIdx) {
    if (landlordRobbed) return;

    // 设置地主
    landlordIndex = playerIdx;
    players[playerIdx].isLandlord = true;
    // 分配地主底牌
    assign_landlord_cards();
    // 更新队伍
    update_player_team();
    // 抢地主完成
    landlordRobbed = true;
    // 地主先出牌
    currentPlayer = landlordIndex;
    printf("%s 抢地主成功！获得底牌：%s %s %s\n", players[playerIdx].name,
        landlordCards[0].name, landlordCards[1].name, landlordCards[2].name);
}

// 新增：AI抢地主逻辑
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ai_rob_landlord(int playerIdx) {
    if (landlordRobbed) return;

    // AI抢地主概率：手牌有炸弹/王炸则80%抢，否则20%
    int bombCount = 0;
    bool hasRocket = false;
    int points[20];
    for (int i = 0; i < players[playerIdx].cardCount; i++) {
        points[i] = players[playerIdx].hand[i].point;
    }
    // 排序点数
    sortHandByPoint(&players[playerIdx]);
    // 检查炸弹/王炸
    for (int i = 0; i < players[playerIdx].cardCount - 3; i++) {
        if (points[i] == points[i + 1] && points[i + 1] == points[i + 2] && points[i + 2] == points[i + 3]) {
            bombCount++;
        }
    }
    if (points[players[playerIdx].cardCount - 1] == POINT_BIG_JOKER &&
        points[players[playerIdx].cardCount - 2] == POINT_SMALL_JOKER) {
        hasRocket = true;
    }

    // 随机决定是否抢
    int randVal = rand() % 100;
    if ((bombCount > 0 || hasRocket) && randVal < 80) {
        rob_landlord(playerIdx);
    }
    else if (randVal < 20) {
        rob_landlord(playerIdx);
    }
    else {
        printf("%s 放弃抢地主\n", players[playerIdx].name);
        // 轮到下一个AI
        int nextAI = (playerIdx + 1) % 3;
        if (nextAI != 0) { // 跳过玩家
            ai_rob_landlord(nextAI);
        }
        else {
            // 所有AI都不抢，随机分配给一个AI
            int randAI = 1 + rand() % 2;
            rob_landlord(randAI);
        }
    }
}

// 新增：分配地主底牌
void assign_landlord_cards() {
    if (landlordIndex == -1) return;

    // 把底牌加入地主手牌
    for (int i = 0; i < 3; i++) {
        players[landlordIndex].hand[players[landlordIndex].cardCount++] = landlordCards[i];
    }
    // 重新排序地主手牌
    sortHandByPoint(&players[landlordIndex]);
}

// 新增：更新玩家队伍归属
void update_player_team() {
    for (int i = 0; i < 3; i++) {
        if (players[i].isLandlord) {
            players[i].team = TEAM_LANDLORD;
        }
        else {
            players[i].team = TEAM_FARMER;
        }
    }
}

// 新增：智能出牌逻辑（核心）
int get_best_play(int playerIdx) {
    Player* player = &players[playerIdx];
    // 1. 无上家出牌时：优先出小牌（单张3、对子33等）
    if (lastPlay.type == PASS) {
        // 找最小的单张
        for (int i = 0; i < player->cardCount; i++) {
            int selected[1] = { i };
            Play play = analyzePlay(player, selected, 1);
            if (play.type == SINGLE && play.point <= POINT_5) {
                // 出这张牌
                int selectedArr[1] = { i };
                game_play_by_player(playerIdx, selectedArr, 1);
                return 1;
            }
        }
        // 无小单张则出最小对子
        for (int i = 0; i < player->cardCount - 1; i++) {
            if (player->hand[i].point == player->hand[i + 1].point) {
                int selected[2] = { i, i + 1 };
                Play play = analyzePlay(player, selected, 2);
                if (play.type == PAIR && play.point <= POINT_5) {
                    game_play_by_player(playerIdx, selected, 2);
                    return 1;
                }
            }
        }
        // 否则出任意合法牌
        int selected[1] = { 0 };
        game_play_by_player(playerIdx, selected, 1);
        return 1;
    }

    // 2. 有上家出牌时：优先用最小的牌压制
    Play bestPlay = { 0 };
    int bestSelected[20] = { 0 };
    int bestCount = 0;
    bool found = false;

    // 遍历所有可能的出牌组合
    // 单张压制
    if (lastPlay.type == SINGLE) {
        for (int i = 0; i < player->cardCount; i++) {
            int selected[1] = { i };
            Play play = analyzePlay(player, selected, 1);
            if (play.type == SINGLE && play.point > lastPlay.point) {
                if (!found || play.point < bestPlay.point) {
                    bestPlay = play;
                    bestSelected[0] = i;
                    bestCount = 1;
                    found = true;
                }
            }
        }
    }
    // 对子压制
    else if (lastPlay.type == PAIR) {
        for (int i = 0; i < player->cardCount - 1; i++) {
            if (player->hand[i].point == player->hand[i + 1].point) {
                int selected[2] = { i, i + 1 };
                Play play = analyzePlay(player, selected, 2);
                if (play.type == PAIR && play.point > lastPlay.point) {
                    if (!found || play.point < bestPlay.point) {
                        bestPlay = play;
                        bestSelected[0] = i;
                        bestSelected[1] = i + 1;
                        bestCount = 2;
                        found = true;
                    }
                }
            }
        }
    }
    // 炸弹压制（非王炸）
    else if (lastPlay.type != ROCKET) {
        for (int i = 0; i < player->cardCount - 3; i++) {
            if (player->hand[i].point == player->hand[i + 1].point &&
                player->hand[i + 1].point == player->hand[i + 2].point &&
                player->hand[i + 2].point == player->hand[i + 3].point) {
                int selected[4] = { i, i + 1, i + 2, i + 3 };
                Play play = analyzePlay(player, selected, 4);
                if (play.type == BOMB) {
                    bestPlay = play;
                    memcpy(bestSelected, selected, sizeof(selected));
                    bestCount = 4;
                    found = true;
                    break; // 炸弹直接出
                }
            }
        }
    }

    // 找到最优牌则出，否则过牌
    if (found) {
        game_play_by_player(playerIdx, bestSelected, bestCount);
        return 1;
    }
    else {
        game_pass_by_player(playerIdx);
        return 0;
    }
}

// 新增：检查队伍是否获胜
bool check_team_win() {
    // 检查地主队
    if (players[landlordIndex].cardCount == 0) {
        return true;
    }
    // 检查农民队
    for (int i = 0; i < 3; i++) {
        if (!players[i].isLandlord && players[i].cardCount == 0) {
            return true;
        }
    }
    return false;
}

// AI出牌（优化：调用智能出牌逻辑）
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int game_ai_step() {
    int i = currentPlayer;
    if (i == 0 || gameOver || !landlordRobbed) return 0;

    // 调用智能出牌逻辑
    get_best_play(i);

    // 检查队伍是否获胜
    if (check_team_win()) {
        gameOver = true;
        printf("游戏结束！%s队获胜！\n", players[i].team == TEAM_LANDLORD ? "地主" : "农民");
    }

    // 切换玩家
    return 1;
}

// 游戏初始化
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void game_init() {
    reset_all();

    srand((unsigned)time(NULL));

    gameDeck.top = 0;
    initializeDeck(&gameDeck);
    shuffleDeck(&gameDeck);

    strcpy(players[0].name, "我");
    strcpy(players[1].name, "电脑1");
    strcpy(players[2].name, "电脑2");

    for (int i = 0; i < 3; i++) {
        players[i].cardCount = 0;
        players[i].isLandlord = false;
    }

    // 发牌
    dealCards(&gameDeck, players);

    // 手牌排序
    for (int i = 0; i < 3; i++) {
        sortHandByPoint(&players[i]);
    }

    // 初始化游戏流程（抢地主阶段）
    passCount = 0;
    lastPlayer = -1;
    lastPlay.type = PASS;
    gameRound = 1;
    currentPlayer = 0; // 玩家先决定是否抢地主
    gameOver = false;
    landlordRobbed = false;
    clearLastPlayedText();

    printf("发牌完成！请玩家决定是否抢地主！\n");
}

// AI自动运行
void game_auto_run() {
    while (!gameOver && currentPlayer != 0) {
        game_ai_step();
    }
}

// 主函数
int main() {
    srand((unsigned)time(NULL));
    game_init();

    // 测试抢地主流程（玩家抢地主）
    // rob_landlord(0); // 玩家抢地主
    // 或AI抢地主
    // ai_rob_landlord(1);

    return 0;
}
