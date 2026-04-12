#define _CRT_SECURE_NO_WARNINGS
#include "game_shared.h"
#include "ai_logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif



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
bool landlordRobbed = false;    // 抢地主是否完成
Card landlordCards[3];          // 地主底牌

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

// 发牌
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
        
        // 计算并更新积分
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

int game_pass() {
    return game_pass_by_player(0);
}

// 获取游戏状态JSON
const char* game_get_state_json() {
    int offset = 0;

    offset += sprintf(buffer + offset, "{");

    // 基础状态
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

// 抢地主功能（玩家调用）
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



// 分配地主底牌
void assign_landlord_cards() {
    if (landlordIndex == -1) return;

    // 把底牌加入地主手牌
    for (int i = 0; i < 3; i++) {
        players[landlordIndex].hand[players[landlordIndex].cardCount++] = landlordCards[i];
    }
    // 重新排序地主手牌
    sortHandByPoint(&players[landlordIndex]);
}

// 更新玩家队伍归属
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
// 游戏初始化
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
    return 0;
}
