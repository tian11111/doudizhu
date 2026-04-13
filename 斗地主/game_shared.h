#ifndef GAME_SHARED_H
#define GAME_SHARED_H

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
    DOUBLE_STRAIGHT,  // 连对
    AIRCRAFT,         // 飞机（连续三张）
    AIRCRAFT_ONE,     // 飞机带单张
    AIRCRAFT_PAIR,    // 飞机带对子
    BOMB,             // 炸弹（四张相同）
    ROCKET            // 王炸（大小王）
} PlayType;


// 队伍枚举（地主队/农民队）
typedef enum {
    TEAM_LANDLORD,    // 地主队
    TEAM_FARMER       // 农民队
} TeamType;

// AI 难度
typedef enum {
    DIFF_EASY = 0,
    DIFF_NORMAL,
    DIFF_HARD
} Difficulty;

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

extern Deck gameDeck;
extern Player players[3];
extern Play lastPlay;
extern int lastPlayer;
extern int passCount;
extern int gameRound;
extern int currentPlayer;
extern bool gameOver;
extern char lastPlayedText[256];
extern int landlordIndex;
extern char buffer[4096];
extern bool landlordRobbed;
extern Card landlordCards[3];
extern Difficulty ai_difficulty;

void clearLastPlayedText(void);
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
int game_pass(void);
const char* game_get_state_json(void);
void set_ai_difficulty(int level);
void game_init(void);
void game_auto_run(void);
void rob_landlord(int playerIdx);
void assign_landlord_cards(void);
bool check_team_win(void);
void update_player_team(void);

#endif
