#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

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
    DIAMONDS,       // 方片
    HEARTS,         // 红桃
    SPADES,         // 黑桃
    JOKER_SMALL,    // 小王
    JOKER_BIG       // 大王
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
} Player;

// 牌堆结构体
typedef struct {
    Card deck[54];
    int top;
} Deck;

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

void clearLastPlayedText();
void buildPlayedTextFromSelection(const Player* player, int selected[], int count);

void clearLastPlayedText() {
    lastPlayedText[0] = '\0';
}

void buildPlayedTextFromSelection(const Player* player, int selected[], int count) {
    clearLastPlayedText();
    for (int i = 0; i < count; i++) {
        if (i > 0) strcat(lastPlayedText, " ");
        strcat(lastPlayedText, player->hand[selected[i]].name);
    }
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
    deck->deck[index++] = (Card){
        .name = "小王",
        .suit = JOKER_SMALL,
        .point = POINT_SMALL_JOKER
    };
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
    for (int cardIdx = 0; cardIdx < 51; cardIdx++) {
        int playerIndex = cardIdx % 3;
        int targetSlot = players[playerIndex].cardCount;
        //targetSlot表示这张新牌应该插入到手牌数组的哪个位置（下标）
        players[playerIndex].hand[targetSlot] = deck->deck[cardIdx];
        players[playerIndex].cardCount++;
    }
    printf("发牌完成，每人获得 %d 张牌。\n", 17);
}

// 排序
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

// 打印我的所有手牌
void printPlayerHand(const Player* player) {
    printf("%s [%s] 的手牌 (%d张):\n",
        player->name,
        player->isLandlord ? "地主" : "农民",
        player->cardCount);
    for (int i = 0; i < player->cardCount; i++) {
        printf("[%d]%s\t", i, player->hand[i].name);
    }
    printf("\n");

}
// 让玩家选择一张牌打出（返回牌的索引，-1 表示过牌）
int getPlayerChoice(const Player* player, int lastPoint) {
    while (1) {
        printf("轮到你出牌！输入牌的编号（0-%d），或输入 -1 表示过牌: ", player->cardCount - 1);
        int choice;
        scanf("%d", &choice);

        if (choice == -1) {
            return -1;  // 过牌
        }

        if (choice >= 0 && choice < player->cardCount) {
            // 检查是否大于上家的牌
            if (lastPoint != -1 && player->hand[choice].point <= lastPoint) {
                printf("你出的牌太小了！必须大于上家的牌。\n");
                continue;
            }
            return choice;
        }
        else {
            printf("输入无效！请输入 0 到 %d 之间的数字。\n", player->cardCount - 1);
        }
    }
}


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

    // 先判断王炸
    if (selectedCount == 2) {
        if ((points[0] == POINT_SMALL_JOKER && points[1] == POINT_BIG_JOKER)) {
            play.type = ROCKET;
            play.point = POINT_BIG_JOKER;
            play.length = 2;
            return play;
        }
    }

    //再判断对子
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
    // 必须是三张 + 单张，不能是四张一样
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
        // 形如 AAA+BB
        if (points[0] == points[2] && points[3] == points[4]) {
            play.type = TRIPLE_PAIR;
            play.point = points[0];
            play.length = 5;
            return play;
        }
        // 形如 AA+BBB
        if (points[0] == points[1] && points[2] == points[4]) {
            play.type = TRIPLE_PAIR;
            play.point = points[2];
            play.length = 5;
            return play;
        }
    }
    // 炸弹（四张相同）
    if (selectedCount == 4 && points[0] == points[3]) {
        play.type = BOMB;
        play.point = points[0];
        play.length = 4;
        return play;
    }

    // 顺子（5张及以上，连续，3~A）
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

    // 其他情况：非法牌型
    play.type = PASS;
    return play;
}
// 判断当前出的牌是否能打过上一家
bool canPlayBeat(const Play* current, const Play* last) {
    if (last->type == PASS) {
        return true;  // 上家没出牌（或过牌），任何合法牌都能出
    }

    // 1. 王炸最大，能打任何牌
    if (current->type == ROCKET) {
        return true;
    }
    if (last->type == ROCKET) {
        return false;  // 上家是王炸，你不是，打不过
    }

    // 2. 炸弹可以打除王炸外的任何牌
    if (current->type == BOMB && last->type != ROCKET) {
        // 但要比较点数（炸弹之间比点数）
        if (last->type == BOMB) {
            return current->point > last->point;
        }
        return true;  // 打非炸弹牌
    }
    if (last->type == BOMB && current->type != BOMB && current->type != ROCKET) {
        return false; // 上家是炸弹，你不是，打不过
    }

    // 3. 同类型比较
    if (current->type == last->type) {
        if (current->type == STRAIGHT) {
            // 顺子：长度必须相同，且起始点数更高
            if (current->length != last->length) {
                return false;
            }
            return current->point > last->point;
        }
        else {
            // 单张、对子、炸弹（已处理）等：比点数
            return current->point > last->point;
        }
    }

    // 其他情况：类型不同，不能打
    return false;
}

int game_play(int selected[], int count) {
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

    if (players[0].cardCount == 0) {
        gameOver = true;
    }
    gameRound++;
    return 1;
}

int game_pass() {
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

char buffer[4096];

const char* game_get_state_json() {
    int offset = 0;

    offset += sprintf(buffer + offset, "{");

    // 我的手牌
    offset += sprintf(buffer + offset, "\"hand\":[");
    for (int i = 0; i < players[0].cardCount; i++) {
        if (i > 0) offset += sprintf(buffer + offset, ",");
        offset += sprintf(buffer + offset, "\"%s\"", players[0].hand[i].name);
    }
    offset += sprintf(buffer + offset, "],");

    // 基本状态
    offset += sprintf(buffer + offset, "\"currentPlayer\":%d,", currentPlayer);
    offset += sprintf(buffer + offset, "\"gameRound\":%d,", gameRound);
    offset += sprintf(buffer + offset, "\"lastPlayer\":%d,", lastPlayer);
    offset += sprintf(buffer + offset, "\"gameOver\":%s,", gameOver ? "true" : "false");

    // 各玩家剩余牌数
    offset += sprintf(buffer + offset, "\"myCardCount\":%d,", players[0].cardCount);
    offset += sprintf(buffer + offset, "\"ai1CardCount\":%d,", players[1].cardCount);
    offset += sprintf(buffer + offset, "\"ai2CardCount\":%d,", players[2].cardCount);

    // 上一手牌型和具体牌面
    offset += sprintf(buffer + offset, "\"lastPlayType\":%d,", lastPlay.type);
    offset += sprintf(buffer + offset, "\"lastPlayedText\":\"%s\"", lastPlayedText);
    offset += sprintf(buffer + offset, "\"landlordIndex\":%d", landlordIndex);
    offset += sprintf(buffer + offset, "}");    

    return buffer;
}

int game_ai_step() {
    int i = currentPlayer;
    if (i == 0) return 0;   // 轮到人类时，AI不动
    if (gameOver) return 0;

    bool found = false;
    Play currentPlay = { 0 };
    int selected[20];
    int selectedCount = 0;
    bool isPass = false;

    // 简单 AI：只找最小合法单张
    if (lastPlay.type == PASS || lastPlay.type == SINGLE) {
        for (int idx = 0; idx < players[i].cardCount; idx++) {
            if (lastPlay.type == PASS || players[i].hand[idx].point > lastPlay.point) {
                selected[0] = idx;
                selectedCount = 1;
                currentPlay = analyzePlay(&players[i], selected, 1);
                if (currentPlay.type == SINGLE) {
                    found = true;
                    break;
                }
            }
        }
    }
        // AI 出对子
    if (!found && lastPlay.type == PAIR) {
        for (int idx = 0; idx < players[i].cardCount - 1; idx++) {
            if (players[i].hand[idx].point == players[i].hand[idx + 1].point &&
                players[i].hand[idx].point > lastPlay.point) {
                selected[0] = idx;
                selected[1] = idx + 1;
                selectedCount = 2;
                currentPlay = analyzePlay(&players[i], selected, 2);
                if (currentPlay.type == PAIR) {
                    found = true;
                    break;
                }
            }
        }
    }

    // AI 出三张
    if (!found && lastPlay.type == TRIPLE) {
        for (int idx = 0; idx < players[i].cardCount - 2; idx++) {
            if (players[i].hand[idx].point == players[i].hand[idx + 2].point &&
                players[i].hand[idx].point > lastPlay.point) {
                selected[0] = idx;
                selected[1] = idx + 1;
                selected[2] = idx + 2;
                selectedCount = 3;
                currentPlay = analyzePlay(&players[i], selected, 3);
                if (currentPlay.type == TRIPLE) {
                    found = true;
                    break;
                }
            }
        }
    }
    // AI 出三带一
    if (!found && lastPlay.type == TRIPLE_ONE) {
        for (int idx = 0; idx < players[i].cardCount - 2; idx++) {
             if (players[i].hand[idx].point == players[i].hand[idx + 2].point &&players[i].hand[idx].point > lastPlay.point) {

                // 找一个单张
                for (int k = 0; k < players[i].cardCount; k++) {
                    if (k != idx && k != idx+1 && k != idx+2) {
                        selected[0] = idx;
                        selected[1] = idx + 1;
                        selected[2] = idx + 2;
                        selected[3] = k;
                        selectedCount = 4;

                        currentPlay = analyzePlay(&players[i], selected, 4);
                        if (currentPlay.type == TRIPLE_ONE) {
                            found = true;
                            break;
                        }
                    }
                }
            }
            if (found) break;
        }
    }   
    if (!found) {
        isPass = true;
    }

    if (!isPass) {
        buildPlayedTextFromSelection(&players[i], selected, selectedCount);

        lastPlay = currentPlay;
        lastPlayer = i;
        passCount = 0;

        for (int k = selectedCount - 1; k >= 0; k--) {
            int idx = selected[k];
            for (int j = idx; j < players[i].cardCount - 1; j++) {
                players[i].hand[j] = players[i].hand[j + 1];
            }
            players[i].cardCount--;
        }

        if (players[i].cardCount == 0) {
            gameOver = true;
        }
    }
else {
    strcpy(lastPlayedText, "过牌");

    passCount++;

    if (passCount >= 2) {
        lastPlay.type = PASS;
        lastPlayer = -1;
        passCount = 0;
    }
}

    // 推进到下一个玩家
    currentPlayer = (i + 1) % 3;
    gameRound++;
    return 1;
}
void game_init() {
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

    dealCards(&gameDeck, players);

    int landlordIdx = rand() % 3;
    landlordIndex = landlordIdx;
    players[landlordIdx].isLandlord = true;
    printf("\n【地主】: %s\n", players[landlordIdx].name);

    for (int i = 0; i < 3; i++) {
        sortHandByPoint(&players[i]);
    }

    passCount = 0;
    lastPlayer = -1;
    lastPlay.type = PASS;
    gameRound = 1;
    currentPlayer = landlordIdx;
    gameOver = false;
    clearLastPlayedText();
}

void game_auto_run() {
    while (!gameOver && currentPlayer != 0) {
        game_ai_step();
    }
}

void clearLastPlayedText() {
    lastPlayedText[0] = '\0';
}


void buildPlayedTextFromSelection(const Player* player, int selected[], int count) {
    clearLastPlayedText();

    for (int i = 0; i < count; i++) {
        if (i > 0) {
            strcat(lastPlayedText, " ");
        }
        strcat(lastPlayedText, player->hand[selected[i]].name);
    }
}


int main() {
    srand((unsigned)time(NULL));
    return 0;
}