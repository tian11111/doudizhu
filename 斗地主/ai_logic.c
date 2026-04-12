#include "ai_logic.h"
#include "game_shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 智能出牌逻辑
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

// AI抢地主逻辑
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