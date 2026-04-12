#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qwen_client.h"

/*
 * 这是接入示例，不直接依赖你整份游戏源码。
 * 你后面可以把 game_get_state_json()、本地分析 JSON、复盘 JSON 接进来。
 */

int main(void) {
    const char* api_key = getenv("DASHSCOPE_API_KEY");
    char result[8192];
    char errbuf[1024];

    QwenConfig cfg = {
        api_key,
        "https://dashscope.aliyuncs.com/compatible-mode/v1",
        "qwen-plus",
        20
    };

    const char* game_state_json =
        "{"
        "\"currentPlayer\":0,"
        "\"landlordIndex\":1,"
        "\"myCardCount\":5,"
        "\"ai1CardCount\":2,"
        "\"ai2CardCount\":7,"
        "\"lastPlayedText\":\"对Q\""
        "}";

    const char* analysis_json =
        "{"
        "\"recommended_action\":\"pair_9\","
        "\"alternatives\":[\"single_3\",\"bomb_7777\"],"
        "\"risk\":\"中\","
        "\"style\":\"保守\","
        "\"reason_features\":["
            "\"preserve bomb for endgame\""
            ",\"avoid breaking straight structure\""
            ",\"maintain response flexibility\""
        "]"
        "}";

    if (!api_key || api_key[0] == '\0') {
        printf("请先设置环境变量 DASHSCOPE_API_KEY\n");
        return 1;
    }

    if (qwen_explain_move(&cfg, game_state_json, analysis_json, result, sizeof(result), errbuf, sizeof(errbuf)) != 0) {
        printf("调用失败：%s\n", errbuf);
        return 2;
    }

    printf("模型返回：\n%s\n", result);
    return 0;
}
