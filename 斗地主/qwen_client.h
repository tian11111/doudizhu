#ifndef QWEN_CLIENT_H
#define QWEN_CLIENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* api_key;      /* 建议从环境变量读取，不要硬编码 */
    const char* base_url;     /* 例如：https://dashscope.aliyuncs.com/compatible-mode/v1 */
    const char* model;        /* 例如：qwen-plus */
    long timeout_seconds;     /* 例如：20 */
} QwenConfig;

/*
 * 统一调用入口。
 * prompt_json_hint 建议写明“请返回 JSON”之类提示，便于后续结构化输出。
 * out_text 用于接收模型返回的正文内容。
 * 返回 0 表示成功，非 0 表示失败。
 */
int qwen_chat_completion(
    const QwenConfig* cfg,
    const char* system_prompt,
    const char* user_prompt,
    int require_json_object,
    char* out_text,
    size_t out_text_size,
    char* errbuf,
    size_t errbuf_size
);

/*
 * 针对你的斗地主项目：生成“出牌建议解释”。
 * game_state_json 可以直接传 game_get_state_json() 的结果。
 * analysis_json 传你本地算法算出来的候选牌/评分/风险标签。
 */
int qwen_explain_move(
    const QwenConfig* cfg,
    const char* game_state_json,
    const char* analysis_json,
    char* out_text,
    size_t out_text_size,
    char* errbuf,
    size_t errbuf_size
);

/*
 * 针对你的斗地主项目：生成“赛后复盘”。
 * replay_json 建议传结构化关键事件，而不是整局原始日志硬塞进去。
 */
int qwen_review_game(
    const QwenConfig* cfg,
    const char* replay_json,
    char* out_text,
    size_t out_text_size,
    char* errbuf,
    size_t errbuf_size
);

#ifdef __cplusplus
}
#endif

#endif /* QWEN_CLIENT_H */
