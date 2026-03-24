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
    DOUBLE_STRAIGHT,  // 连对
    AIRCRAFT,         // 飞机（连续三张）
    AIRCRAFT_ONE,     // 飞机带单张
    AIRCRAFT_PAIR,    // 飞机带对子
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
    int score;                  // 积分（新增）
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
void reset_all(bool keepScore);
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
void reset_all(bool keepScore) {
    // 清空三个玩家的所有数据
    for (int i = 0; i < 3; i++) {
        players[i].cardCount = 0;
        players[i].isLandlord = false;
        players[i].team = TEAM_FARMER;  // 默认农民队
        if (!keepScore) {
            players[i].score = 0;       // 只有当 keepScore 为 false 时才清零积分
        }
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

    // 连对：至少 3 对，不能包含 2 和王
    if (selectedCount >= 6 && selectedCount % 2 == 0) {
        bool isDoubleStraight = true;

        for (int i = 0; i < selectedCount; i += 2) {
            if (points[i] != points[i + 1]) {
                isDoubleStraight = false;
                break;
            }
            if (points[i] >= POINT_2) {   // 连对不能到 2 / 王
                isDoubleStraight = false;
                break;
            }
            if (i >= 2 && points[i] != points[i - 2] + 1) {
                isDoubleStraight = false;
                break;
            }
        }

        if (isDoubleStraight) {
            play.type = DOUBLE_STRAIGHT;
            play.point = points[0];
            play.length = selectedCount;   // 这里 length 存总牌数
            return play;
        }
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

    // 飞机（纯三张连续）：至少 2 个连续三张，不能包含 2 和王
    if (selectedCount >= 6 && selectedCount % 3 == 0) {
        int tripleCount = selectedCount / 3;
        bool isAircraft = true;

        for (int i = 0; i < tripleCount; i++) {
            int baseIdx = i * 3;
            // 检查是否是三张相同的
            if (points[baseIdx] != points[baseIdx + 1] || 
                points[baseIdx + 1] != points[baseIdx + 2]) {
                isAircraft = false;
                break;
            }
            // 检查是否包含 2 或王
            if (points[baseIdx] >= POINT_2) {
                isAircraft = false;
                break;
            }
            // 检查是否连续
            if (i > 0 && points[baseIdx] != points[baseIdx - 3] + 1) {
                isAircraft = false;
                break;
            }
        }

        if (isAircraft) {
            play.type = AIRCRAFT;
            play.point = points[0];
            play.length = selectedCount;
            return play;
        }
    }

    // 飞机带单张：至少 2 个连续三张 + 相同数量的单张
    if (selectedCount >= 8 && selectedCount % 4 == 0) {
        int tripleCount = selectedCount / 4;  // 三张的组数（总牌数 = 4n）
        // 先排序，让三张在前，单张在后
        int aircraftPoints[20];
        int extraCards[20];
        int extraCount = 0;
        
        // 提取所有可能的三张
        int foundTriples = 0;
        for (int i = 0; i < selectedCount && foundTriples < tripleCount; ) {
            if (i + 2 < selectedCount && 
                points[i] == points[i + 1] && 
                points[i + 1] == points[i + 2] &&
                points[i] < POINT_2) {
                aircraftPoints[foundTriples] = points[i];
                foundTriples++;
                i += 3;
            } else {
                extraCards[extraCount++] = points[i];
                i++;
            }
        }
        
        // 检查是否找到足够的三张且它们连续
        bool isAircraftWithSingle = (foundTriples == tripleCount);
        if (isAircraftWithSingle) {
            for (int i = 1; i < foundTriples; i++) {
                if (aircraftPoints[i] != aircraftPoints[i - 1] + 1) {
                    isAircraftWithSingle = false;
                    break;
                }
            }
        }
        
        if (isAircraftWithSingle && extraCount == tripleCount) {
            play.type = AIRCRAFT_ONE;
            play.point = aircraftPoints[0];
            play.length = selectedCount;
            return play;
        }
    }

    // 飞机带对子：至少 2 个连续三张 + 相同数量的对子
    if (selectedCount >= 10 && selectedCount % 5 == 0) {
        int tripleCount = selectedCount / 5;  // 三张的组数（总牌数 = 5n）
        // 提取所有可能的三张和对子
        int aircraftPoints[20];
        int pairPoints[20];
        int foundTriples = 0;
        int foundPairs = 0;
        
        // 统计每个点数的出现次数
        int countMap[15] = {0};
        for (int i = 0; i < selectedCount; i++) {
            countMap[points[i]]++;
        }
        
        // 找出所有可用的三张（点数<2）
        for (int p = 0; p < POINT_2; p++) {
            if (countMap[p] >= 3 && foundTriples < tripleCount) {
                aircraftPoints[foundTriples++] = p;
                countMap[p] -= 3;
            }
        }
        
        // 检查三张是否连续
        bool isAircraftWithPair = (foundTriples == tripleCount);
        if (isAircraftWithPair) {
            for (int i = 1; i < foundTriples; i++) {
                if (aircraftPoints[i] != aircraftPoints[i - 1] + 1) {
                    isAircraftWithPair = false;
                    break;
                }
            }
        }
        
        // 找出所有可用的对子
        if (isAircraftWithPair) {
            for (int p = 0; p < 15 && foundPairs < tripleCount; p++) {
                if (countMap[p] >= 2) {
                    pairPoints[foundPairs++] = p;
                    countMap[p] -= 2;
                }
            }
        }
        
        if (isAircraftWithPair && foundPairs == tripleCount) {
            play.type = AIRCRAFT_PAIR;
            play.point = aircraftPoints[0];
            play.length = selectedCount;
            return play;
        }
    }

    // 非法牌型
    play.type = PASS;
    return play;
}

bool check_team_win() {
    if (landlordIndex < 0) return false;

    // 地主出完牌，地主队赢
    if (players[landlordIndex].cardCount == 0) {
        return true;
    }

    // 任意一个农民出完牌，农民队赢
    for (int i = 0; i < 3; i++) {
        if (!players[i].isLandlord && players[i].cardCount == 0) {
            return true;
        }
    }

    return false;
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
        if (current->type == STRAIGHT || current->type == DOUBLE_STRAIGHT || 
            current->type == AIRCRAFT || current->type == AIRCRAFT_ONE || 
            current->type == AIRCRAFT_PAIR) {
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
        
        // 计算并更新积分（新增）
        if (player->team == TEAM_LANDLORD) {
            // 地主队获胜
            players[landlordIndex].score += 2;
            for (int i = 0; i < 3; i++) {
                if (!players[i].isLandlord) {
                    players[i].score -= 1;
                }
            }
        } else {
            // 农民队获胜
            for (int i = 0; i < 3; i++) {
                if (!players[i].isLandlord) {
                    players[i].score += 1;
                } else {
                    players[i].score -= 2;
                }
            }
        }
        // 打印积分信息
        printf("积分更新：\n");
        for (int i = 0; i < 3; i++) {
            printf("%s: %d\n", players[i].name, players[i].score);
        }
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

    strcpy(lastPlayedText, "过牌");

    passCount++;
    lastPlayer = playerIdx;
    currentPlayer = (playerIdx + 1) % 3;

    if (passCount >= 2) {
        lastPlay.type = PASS;
        lastPlayer = -1;
        passCount = 0;
        clearLastPlayedText();  // 清空出牌文本，避免显示问题
    }

    gameRound++;
    return 1;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int game_pass() {
    return game_pass_by_player(0);
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

    // AI1 手牌（结算或调试可用）
    offset += sprintf(buffer + offset, "\"ai1Hand\":[");
    for (int i = 0; i < players[1].cardCount; i++) {
        if (i > 0) offset += sprintf(buffer + offset, ",");
        offset += sprintf(buffer + offset, "\"%s\"", players[1].hand[i].name);
    }
    offset += sprintf(buffer + offset, "],");

    // AI2 手牌
    offset += sprintf(buffer + offset, "\"ai2Hand\":[");
    for (int i = 0; i < players[2].cardCount; i++) {
        if (i > 0) offset += sprintf(buffer + offset, ",");
        offset += sprintf(buffer + offset, "\"%s\"", players[2].hand[i].name);
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
    
    // 各玩家积分（新增）
    offset += sprintf(buffer + offset, "\"myScore\":%d,", players[0].score);
    offset += sprintf(buffer + offset, "\"ai1Score\":%d,", players[1].score);
    offset += sprintf(buffer + offset, "\"ai2Score\":%d,", players[2].score);

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
    int myTeam = players[playerIdx].team;
    int mateIdx = -1;

    // 找队友（只有农民才有队友）
    if (myTeam == TEAM_FARMER) {
        for (int i = 0; i < 3; i++) {
            if (i != playerIdx && players[i].team == myTeam) {
                mateIdx = i;
                break;
            }
        }
    }

    int selfRemain = player->cardCount;
    int landlordRemain = (landlordIndex >= 0) ? players[landlordIndex].cardCount : 99;
    int nextIdx = (playerIdx + 1) % 3;
    int prevIdx = lastPlayer;

    // --- 小工具：统计手牌信息 ---
    int pointCount[15] = { 0 };
    bool hasSmallJoker = false, hasBigJoker = false;
    for (int i = 0; i < player->cardCount; i++) {
        int p = player->hand[i].point;
        pointCount[p]++;
        if (p == POINT_SMALL_JOKER) hasSmallJoker = true;
        if (p == POINT_BIG_JOKER) hasBigJoker = true;
    }

    // 是否有王炸
    bool hasRocket = hasSmallJoker && hasBigJoker;

    // 候选解
    Play bestPlay = { 0 };
    int bestSelected[20] = { 0 };
    int bestCount = 0;
    int bestScore = -1000000000;
    bool found = false;

    // --- 评分函数思想 ---
    // 分高：能快走完、保留大牌结构、关键时刻压制对手
    // 分低：乱拆对子/三张、浪费炸弹、压队友
    // 这玩意不是什么 AlphaGo，就是比“瞎出”多想两步。

    // 提交候选
    #define TRY_CANDIDATE(selArr, selCount) do { \
        int __selected[20] = {0}; \
        for (int __k = 0; __k < (selCount); __k++) __selected[__k] = (selArr)[__k]; \
        Play __play = analyzePlay(player, __selected, (selCount)); \
        if (__play.type == PASS) break; \
        if (!canPlayBeat(&__play, &lastPlay)) break; \
        int __score = 0; \
        int __remainAfter = player->cardCount - (selCount); \
        int __samePointCount = pointCount[__play.point]; \
        bool __isBomb = (__play.type == BOMB); \
        bool __isRocket = (__play.type == ROCKET); \
        \
        /* 1. 能直接出完，优先级拉满 */ \
        if (__remainAfter == 0) __score += 100000; \
        if (__remainAfter == 1) __score += 1500; \
        if (__remainAfter == 2) __score += 800; \
        \
        /* 2. 先手时尽量出小牌，不乱交大牌 */ \
        if (lastPlay.type == PASS) { \
            __score -= __play.point * 14; \
            if (__play.type == SINGLE) __score += 180; \
            if (__play.type == PAIR) __score += 120; \
            if (__play.type == TRIPLE) __score += 60; \
            if (__play.type == STRAIGHT) __score += 220 + __play.length * 12; \
        } \
        \
        /* 3. 跟牌时倾向“刚好压住”，别拿大炮打蚊子 */ \
        if (lastPlay.type != PASS) { \
            if (__play.type == lastPlay.type) { \
                __score -= (__play.point - lastPlay.point) * 18; \
            } \
            if (__play.type == STRAIGHT && lastPlay.type == STRAIGHT) { \
                __score -= (__play.point - lastPlay.point) * 10; \
            } \
        } \
        \
        /* 4. 拆结构要扣分：单张拆对子/三张，对子拆三张 */ \
        if (__play.type == SINGLE) { \
            if (__samePointCount >= 2) __score -= 140; \
            if (__samePointCount >= 3) __score -= 240; \
            if (__play.point >= POINT_A) __score -= 120; \
            if (__play.point >= POINT_2) __score -= 240; \
        } \
        if (__play.type == PAIR) { \
            if (__samePointCount >= 3) __score -= 150; \
            if (__play.point >= POINT_A) __score -= 80; \
            if (__play.point >= POINT_2) __score -= 160; \
        } \
        if (__play.type == TRIPLE || __play.type == TRIPLE_ONE || __play.type == TRIPLE_PAIR) { \
            if (__play.point >= POINT_A) __score -= 60; \
            if (__play.point >= POINT_2) __score -= 120; \
        } \
        \
        /* 5. 炸弹/王炸默认很贵，除非局势紧急 */ \
        if (__isBomb) __score -= 900; \
        if (__isRocket) __score -= 1300; \
        \
        /* 6. 积分策略：分低时更激进，分高时更保守 */ \
        if (player->score < 0) { \
            if (__isBomb) __score += 260; \
            if (__isRocket) __score += 320; \
            if (__remainAfter <= 2) __score += 200; \
        } else if (player->score > 3) { \
            if (__isBomb) __score -= 220; \
            if (__isRocket) __score -= 260; \
        } \
        \
        /* 7. 地主 / 农民不同思路 */ \
        if (myTeam == TEAM_LANDLORD) { \
            /* 地主要控节奏，农民快走时必须盯防 */ \
            int farmerMin = 99; \
            for (int __i = 0; __i < 3; __i++) { \
                if (!players[__i].isLandlord && players[__i].cardCount < farmerMin) farmerMin = players[__i].cardCount; \
            } \
            if (farmerMin <= 2 && lastPlay.type != PASS) { \
                __score += 600; \
                if (__play.type == BOMB) __score += 180; \
                if (__play.type == ROCKET) __score += 220; \
            } \
        } else { \
            /* 农民：队友出的牌，能不压就别犯病 */ \
            if (prevIdx != -1 && players[prevIdx].team == myTeam && lastPlay.type != PASS) { \
                __score -= 1200; \
                if (players[prevIdx].cardCount <= 2) __score -= 2400; \
            } \
            /* 对地主要更狠一点 */ \
            if (prevIdx == landlordIndex && lastPlay.type != PASS) { \
                __score += 260; \
                if (players[landlordIndex].cardCount <= 2) __score += 700; \
                if (__isBomb) __score += 120; \
            } \
        } \
        \
        /* 8. 下家危险时要卡牌 */ \
        if (lastPlay.type != PASS && nextIdx == landlordIndex && players[nextIdx].cardCount <= 2) { \
            __score += 260; \
        } \
        if (lastPlay.type != PASS && players[nextIdx].team != myTeam && players[nextIdx].cardCount <= 2) { \
            __score += 420; \
        } \
        \
        /* 9. 手牌很少时，允许打大一点，争取收尾 */ \
        if (selfRemain <= 4) { \
            __score += __play.cardCount * 120; \
            if (__play.type == BOMB) __score += 160; \
            if (__play.type == ROCKET) __score += 200; \
        } \
        \
        if (!found || __score > bestScore) { \
            found = true; \
            bestScore = __score; \
            bestPlay = __play; \
            bestCount = (selCount); \
            for (int __k = 0; __k < (selCount); __k++) bestSelected[__k] = __selected[__k]; \
        } \
    } while (0)

    // ========== 枚举候选 ==========
    // 先手：不需要跟 lastPlay，广一点枚举
    if (lastPlay.type == PASS) {
        // 单张
        for (int i = 0; i < player->cardCount; i++) {
            int sel[1] = { i };
            TRY_CANDIDATE(sel, 1);
        }

        // 对子
        for (int i = 0; i < player->cardCount - 1; i++) {
            if (player->hand[i].point == player->hand[i + 1].point) {
                int sel[2] = { i, i + 1 };
                TRY_CANDIDATE(sel, 2);
            }
        }

        // 三张
        for (int i = 0; i < player->cardCount - 2; i++) {
            if (player->hand[i].point == player->hand[i + 2].point) {
                int sel[3] = { i, i + 1, i + 2 };
                TRY_CANDIDATE(sel, 3);
            }
        }

        // 三带一
        for (int i = 0; i < player->cardCount - 2; i++) {
            if (player->hand[i].point == player->hand[i + 2].point) {
                for (int k = 0; k < player->cardCount; k++) {
                    if (k < i || k > i + 2) {
                        int sel[4] = { i, i + 1, i + 2, k };
                        TRY_CANDIDATE(sel, 4);
                    }
                }
            }
        }

        // 三带二
        for (int i = 0; i < player->cardCount - 2; i++) {
            if (player->hand[i].point == player->hand[i + 2].point) {
                for (int j = 0; j < player->cardCount - 1; j++) {
                    if ((j < i || j > i + 2) &&
                        (j + 1 < i || j + 1 > i + 2) &&
                        player->hand[j].point == player->hand[j + 1].point &&
                        player->hand[j].point != player->hand[i].point) {
                        int sel[5] = { i, i + 1, i + 2, j, j + 1 };
                        TRY_CANDIDATE(sel, 5);
                    }
                }
            }
        }

        // 顺子（长度 5~8，够用了，再长容易把自己出傻）
        for (int len = 5; len <= 8; len++) {
            if (len > player->cardCount) break;
            for (int i = 0; i <= player->cardCount - len; i++) {
                int sel[20];
                for (int k = 0; k < len; k++) sel[k] = i + k;
                TRY_CANDIDATE(sel, len);
            }
        }

        // 连对（长度 6~12，按偶数）
        for (int len = 6; len <= 12; len += 2) {
            if (len > player->cardCount) break;
            for (int i = 0; i <= player->cardCount - len; i++) {
                int sel[20];
                for (int k = 0; k < len; k++) sel[k] = i + k;
                TRY_CANDIDATE(sel, len);
            }
        }

        // 飞机（纯三张，长度 6~15）
        for (int tripleCount = 2; tripleCount <= 5; tripleCount++) {
            int len = tripleCount * 3;
            if (len > player->cardCount) break;
            for (int i = 0; i <= player->cardCount - len; i++) {
                int sel[20];
                for (int k = 0; k < len; k++) sel[k] = i + k;
                TRY_CANDIDATE(sel, len);
            }
        }

        // 飞机带单张（至少 2 个三张 + 等量单张）
        for (int tripleCount = 2; tripleCount <= 4; tripleCount++) {
            int totalLen = tripleCount * 4;  // 每个三张带一个单张
            if (totalLen > player->cardCount) break;
            
            // 枚举所有可能的三张组合
            for (int start = 0; start <= player->cardCount - totalLen; start++) {
                // 检查是否构成连续的三张
                bool valid = true;
                for (int t = 0; t < tripleCount; t++) {
                    int baseIdx = start + t * 4;
                    if (player->hand[baseIdx].point != player->hand[baseIdx + 1].point ||
                        player->hand[baseIdx + 1].point != player->hand[baseIdx + 2].point ||
                        player->hand[baseIdx].point >= POINT_2) {
                        valid = false;
                        break;
                    }
                    if (t > 0 && player->hand[start + t * 4].point != 
                             player->hand[start + (t-1) * 4].point + 1) {
                        valid = false;
                        break;
                    }
                }
                if (valid) {
                    int sel[20];
                    for (int k = 0; k < totalLen; k++) sel[k] = start + k;
                    TRY_CANDIDATE(sel, totalLen);
                }
            }
        }

        // 飞机带对子（至少 2 个三张 + 等量对子）
        for (int tripleCount = 2; tripleCount <= 3; tripleCount++) {
            int totalLen = tripleCount * 5;  // 每个三张带一个对子
            if (totalLen > player->cardCount) break;
            
            // 简化处理：枚举起始位置
            for (int start = 0; start <= player->cardCount - totalLen; start++) {
                bool valid = true;
                // 检查前三个是否为连续三张
                for (int t = 0; t < tripleCount; t++) {
                    int baseIdx = start + t * 5;
                    if (player->hand[baseIdx].point != player->hand[baseIdx + 1].point ||
                        player->hand[baseIdx + 1].point != player->hand[baseIdx + 2].point ||
                        player->hand[baseIdx].point >= POINT_2) {
                        valid = false;
                        break;
                    }
                    if (t > 0 && player->hand[start + t * 5].point != 
                             player->hand[start + (t-1) * 5].point + 1) {
                        valid = false;
                        break;
                    }
                }
                // 检查剩余的是否为对子
                if (valid) {
                    for (int p = 0; p < tripleCount; p++) {
                        int pairIdx = start + tripleCount * 3 + p * 2;
                        if (pairIdx + 1 >= player->cardCount ||
                            player->hand[pairIdx].point != player->hand[pairIdx + 1].point) {
                            valid = false;
                            break;
                        }
                    }
                }
                if (valid) {
                    int sel[20];
                    for (int k = 0; k < totalLen; k++) sel[k] = start + k;
                    TRY_CANDIDATE(sel, totalLen);
                }
            }
        }

        // 炸弹
        for (int i = 0; i < player->cardCount - 3; i++) {
            if (player->hand[i].point == player->hand[i + 3].point) {
                int sel[4] = { i, i + 1, i + 2, i + 3 };
                TRY_CANDIDATE(sel, 4);
            }
        }

        // 王炸
        if (hasRocket) {
            int sj = -1, bj = -1;
            for (int i = 0; i < player->cardCount; i++) {
                if (player->hand[i].point == POINT_SMALL_JOKER) sj = i;
                if (player->hand[i].point == POINT_BIG_JOKER) bj = i;
            }
            if (sj != -1 && bj != -1) {
                int sel[2] = { sj, bj };
                TRY_CANDIDATE(sel, 2);
            }
        }
    }
    else {
        // 跟牌：只枚举能接住的类型 + 炸弹/王炸

        if (lastPlay.type == SINGLE) {
            for (int i = 0; i < player->cardCount; i++) {
                int sel[1] = { i };
                TRY_CANDIDATE(sel, 1);
            }
        }
        else if (lastPlay.type == PAIR) {
            for (int i = 0; i < player->cardCount - 1; i++) {
                if (player->hand[i].point == player->hand[i + 1].point) {
                    int sel[2] = { i, i + 1 };
                    TRY_CANDIDATE(sel, 2);
                }
            }
        }
        else if (lastPlay.type == TRIPLE) {
            for (int i = 0; i < player->cardCount - 2; i++) {
                if (player->hand[i].point == player->hand[i + 2].point) {
                    int sel[3] = { i, i + 1, i + 2 };
                    TRY_CANDIDATE(sel, 3);
                }
            }
        }
        else if (lastPlay.type == TRIPLE_ONE) {
            for (int i = 0; i < player->cardCount - 2; i++) {
                if (player->hand[i].point == player->hand[i + 2].point) {
                    for (int k = 0; k < player->cardCount; k++) {
                        if (k < i || k > i + 2) {
                            int sel[4] = { i, i + 1, i + 2, k };
                            TRY_CANDIDATE(sel, 4);
                        }
                    }
                }
            }
        }
        else if (lastPlay.type == TRIPLE_PAIR) {
            for (int i = 0; i < player->cardCount - 2; i++) {
                if (player->hand[i].point == player->hand[i + 2].point) {
                    for (int j = 0; j < player->cardCount - 1; j++) {
                        if ((j < i || j > i + 2) &&
                            (j + 1 < i || j + 1 > i + 2) &&
                            player->hand[j].point == player->hand[j + 1].point &&
                            player->hand[j].point != player->hand[i].point) {
                            int sel[5] = { i, i + 1, i + 2, j, j + 1 };
                            TRY_CANDIDATE(sel, 5);
                        }
                    }
                }
            }
        }
        else if (lastPlay.type == STRAIGHT) {
            int len = lastPlay.length;
            if (len >= 5 && len <= player->cardCount) {
                for (int i = 0; i <= player->cardCount - len; i++) {
                    int sel[20];
                    for (int k = 0; k < len; k++) sel[k] = i + k;
                    TRY_CANDIDATE(sel, len);
                }
            }
        }
        else if (lastPlay.type == DOUBLE_STRAIGHT) {
            int len = lastPlay.length;
            if (len >= 6 && len <= player->cardCount && len % 2 == 0) {
                for (int i = 0; i <= player->cardCount - len; i++) {
                    int sel[20];
                    for (int k = 0; k < len; k++) sel[k] = i + k;
                    TRY_CANDIDATE(sel, len);
                }
            }
        }
        else if (lastPlay.type == AIRCRAFT) {
            int len = lastPlay.length;
            if (len >= 6 && len <= player->cardCount && len % 3 == 0) {
                for (int i = 0; i <= player->cardCount - len; i++) {
                    int sel[20];
                    for (int k = 0; k < len; k++) sel[k] = i + k;
                    TRY_CANDIDATE(sel, len);
                }
            }
        }
        else if (lastPlay.type == AIRCRAFT_ONE) {
            int len = lastPlay.length;
            if (len >= 8 && len <= player->cardCount && len % 4 == 0) {
                for (int i = 0; i <= player->cardCount - len; i++) {
                    int sel[20];
                    for (int k = 0; k < len; k++) sel[k] = i + k;
                    TRY_CANDIDATE(sel, len);
                }
            }
        }
        else if (lastPlay.type == AIRCRAFT_PAIR) {
            int len = lastPlay.length;
            if (len >= 10 && len <= player->cardCount && len % 5 == 0) {
                for (int i = 0; i <= player->cardCount - len; i++) {
                    int sel[20];
                    for (int k = 0; k < len; k++) sel[k] = i + k;
                    TRY_CANDIDATE(sel, len);
                }
            }
        }

        // 炸弹永远是兜底
        if (lastPlay.type != ROCKET) {
            for (int i = 0; i < player->cardCount - 3; i++) {
                if (player->hand[i].point == player->hand[i + 3].point) {
                    int sel[4] = { i, i + 1, i + 2, i + 3 };
                    TRY_CANDIDATE(sel, 4);
                }
            }
        }

        // 王炸最后兜底
        if (lastPlay.type != ROCKET && hasRocket) {
            int sj = -1, bj = -1;
            for (int i = 0; i < player->cardCount; i++) {
                if (player->hand[i].point == POINT_SMALL_JOKER) sj = i;
                if (player->hand[i].point == POINT_BIG_JOKER) bj = i;
            }
            if (sj != -1 && bj != -1) {
                int sel[2] = { sj, bj };
                TRY_CANDIDATE(sel, 2);
            }
        }
    }

    #undef TRY_CANDIDATE

    // 农民时：队友出了牌，默认尽量不压，除非真危险（优化版）
    if (lastPlay.type != PASS &&
        myTeam == TEAM_FARMER &&
        prevIdx != -1 &&
        players[prevIdx].team == myTeam &&
        found) {

        bool shouldForcePass = false;

        // 只有在队友牌权很稳、地主不危险、自己也不适合接时才让
        if (players[prevIdx].cardCount > 2 &&
            (landlordIndex < 0 || players[landlordIndex].cardCount > 3) &&
            player->cardCount > 4) {
            shouldForcePass = true;
        }

        if (shouldForcePass) {
            game_pass_by_player(playerIdx);
            return 0;
        }
    }

    if (found) {
        game_play_by_player(playerIdx, bestSelected, bestCount);
        return 1;
    }

    game_pass_by_player(playerIdx);
    return 0;
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
        
        // 计算并更新积分（新增）
        if (players[i].team == TEAM_LANDLORD) {
            // 地主队获胜
            players[landlordIndex].score += 2;
            for (int j = 0; j < 3; j++) {
                if (!players[j].isLandlord) {
                    players[j].score -= 1;
                }
            }
        } else {
            // 农民队获胜
            for (int j = 0; j < 3; j++) {
                if (!players[j].isLandlord) {
                    players[j].score += 1;
                } else {
                    players[j].score -= 2;
                }
            }
        }
        // 打印积分信息
        printf("积分更新：\n");
        for (int j = 0; j < 3; j++) {
            printf("%s: %d\n", players[j].name, players[j].score);
        }
    }

    // 切换玩家
    return 1;
}

// 游戏初始化
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void game_init() {
    reset_all(true);  // 保留积分

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
