#define _CRT_SECURE_NO_WARNINGS
#include "qwen_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} MemoryBuffer;

static void set_error(char* errbuf, size_t errbuf_size, const char* msg) {
    if (!errbuf || errbuf_size == 0) return;
    snprintf(errbuf, errbuf_size, "%s", msg ? msg : "unknown error");
}

static int append_char(char* dst, size_t dst_size, size_t* pos, char ch) {
    if (!dst || !pos || *pos + 1 >= dst_size) return 0;
    dst[(*pos)++] = ch;
    dst[*pos] = '\0';
    return 1;
}

static int json_escape_to_buffer(const char* src, char* dst, size_t dst_size) {
    size_t pos = 0;
    size_t i;

    if (!src || !dst || dst_size == 0) return 0;
    dst[0] = '\0';

    for (i = 0; src[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)src[i];
        switch (c) {
            case '"':
                if (!append_char(dst, dst_size, &pos, '\\') || !append_char(dst, dst_size, &pos, '"')) return 0;
                break;
            case '\\':
                if (!append_char(dst, dst_size, &pos, '\\') || !append_char(dst, dst_size, &pos, '\\')) return 0;
                break;
            case '\b':
                if (!append_char(dst, dst_size, &pos, '\\') || !append_char(dst, dst_size, &pos, 'b')) return 0;
                break;
            case '\f':
                if (!append_char(dst, dst_size, &pos, '\\') || !append_char(dst, dst_size, &pos, 'f')) return 0;
                break;
            case '\n':
                if (!append_char(dst, dst_size, &pos, '\\') || !append_char(dst, dst_size, &pos, 'n')) return 0;
                break;
            case '\r':
                if (!append_char(dst, dst_size, &pos, '\\') || !append_char(dst, dst_size, &pos, 'r')) return 0;
                break;
            case '\t':
                if (!append_char(dst, dst_size, &pos, '\\') || !append_char(dst, dst_size, &pos, 't')) return 0;
                break;
            default:
                if (c < 0x20) {
                    if (pos + 6 >= dst_size) return 0;
                    snprintf(dst + pos, dst_size - pos, "\\u%04x", c);
                    pos += 6;
                } else {
                    if (!append_char(dst, dst_size, &pos, (char)c)) return 0;
                }
                break;
        }
    }
    return 1;
}

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    MemoryBuffer* mem = (MemoryBuffer*)userp;
    char* ptr;

    if (!mem) return 0;
    if (mem->len + realsize + 1 > mem->cap) {
        size_t new_cap = (mem->cap == 0) ? (realsize + 1024) : (mem->cap * 2 + realsize);
        ptr = (char*)realloc(mem->data, new_cap);
        if (!ptr) return 0;
        mem->data = ptr;
        mem->cap = new_cap;
    }

    memcpy(&(mem->data[mem->len]), contents, realsize);
    mem->len += realsize;
    mem->data[mem->len] = '\0';
    return realsize;
}

static const char* find_json_string_value_after(const char* json, const char* key) {
    const char* p;
    size_t key_len;
    if (!json || !key) return NULL;
    p = strstr(json, key);
    if (!p) return NULL;
    key_len = strlen(key);
    p += key_len;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    if (*p != '"') return NULL;
    return p + 1;
}

static int json_unescape_string(const char* src, char* dst, size_t dst_size) {
    size_t pos = 0;
    const char* p = src;
    if (!src || !dst || dst_size == 0) return 0;

    while (*p) {
        if (*p == '"') {
            dst[pos] = '\0';
            return 1;
        }
        if (pos + 1 >= dst_size) return 0;

        if (*p == '\\') {
            p++;
            if (*p == '\0') return 0;
            switch (*p) {
                case '"': dst[pos++] = '"'; break;
                case '\\': dst[pos++] = '\\'; break;
                case '/': dst[pos++] = '/'; break;
                case 'b': dst[pos++] = '\b'; break;
                case 'f': dst[pos++] = '\f'; break;
                case 'n': dst[pos++] = '\n'; break;
                case 'r': dst[pos++] = '\r'; break;
                case 't': dst[pos++] = '\t'; break;
                case 'u': {
                    /* 简化处理：\uXXXX 先原样跳过或降级为 '?' */
                    int i;
                    dst[pos++] = '?';
                    for (i = 0; i < 4 && p[1] != '\0'; ++i) p++;
                    break;
                }
                default: dst[pos++] = *p; break;
            }
            p++;
            continue;
        }

        dst[pos++] = *p++;
    }

    return 0;
}

static int extract_first_content_text(const char* response_json, char* out_text, size_t out_text_size) {
    const char* p;
    if (!response_json || !out_text || out_text_size == 0) return 0;

    /* 优先找 choices[0].message.content */
    p = strstr(response_json, "\"message\"");
    if (p) {
        p = strstr(p, "\"content\"");
        if (p) {
            p = find_json_string_value_after(p, "\"content\"");
            if (p && json_unescape_string(p, out_text, out_text_size)) {
                return 1;
            }
        }
    }

    /* 兜底：全局找第一个 content */
    p = strstr(response_json, "\"content\"");
    if (p) {
        p = find_json_string_value_after(p, "\"content\"");
        if (p && json_unescape_string(p, out_text, out_text_size)) {
            return 1;
        }
    }

    return 0;
}

int qwen_chat_completion(
    const QwenConfig* cfg,
    const char* system_prompt,
    const char* user_prompt,
    int require_json_object,
    char* out_text,
    size_t out_text_size,
    char* errbuf,
    size_t errbuf_size
) {
    CURL* curl;
    CURLcode res;
    long http_code = 0;
    struct curl_slist* headers = NULL;
    MemoryBuffer response = {0};
    char url[512];
    char auth_header[512];
    char* req_body = NULL;
    size_t req_cap = 65536;
    char* esc_system = NULL;
    char* esc_user = NULL;
    size_t sys_cap;
    size_t usr_cap;

    if (!cfg || !cfg->api_key || !cfg->base_url || !cfg->model || !out_text || out_text_size == 0) {
        set_error(errbuf, errbuf_size, "invalid qwen config or output buffer");
        return -1;
    }

    out_text[0] = '\0';
    if (errbuf && errbuf_size > 0) errbuf[0] = '\0';

    sys_cap = (system_prompt ? strlen(system_prompt) : 0) * 6 + 64;
    usr_cap = (user_prompt ? strlen(user_prompt) : 0) * 6 + 64;
    esc_system = (char*)malloc(sys_cap);
    esc_user = (char*)malloc(usr_cap);
    req_body = (char*)malloc(req_cap);

    if (!esc_system || !esc_user || !req_body) {
        set_error(errbuf, errbuf_size, "memory allocation failed");
        free(esc_system);
        free(esc_user);
        free(req_body);
        return -2;
    }

    if (!json_escape_to_buffer(system_prompt ? system_prompt : "", esc_system, sys_cap) ||
        !json_escape_to_buffer(user_prompt ? user_prompt : "", esc_user, usr_cap)) {
        set_error(errbuf, errbuf_size, "prompt too long or JSON escape failed");
        free(esc_system);
        free(esc_user);
        free(req_body);
        return -3;
    }

    snprintf(url, sizeof(url), "%s/chat/completions", cfg->base_url);
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", cfg->api_key);

    if (require_json_object) {
        snprintf(
            req_body,
            req_cap,
            "{"
            "\"model\":\"%s\"," 
            "\"messages\":["
                "{\"role\":\"system\",\"content\":\"%s\"},"
                "{\"role\":\"user\",\"content\":\"%s\"}"
            "],"
            "\"temperature\":0.3,"
            "\"response_format\":{\"type\":\"json_object\"}"
            "}",
            cfg->model,
            esc_system,
            esc_user
        );
    } else {
        snprintf(
            req_body,
            req_cap,
            "{"
            "\"model\":\"%s\"," 
            "\"messages\":["
                "{\"role\":\"system\",\"content\":\"%s\"},"
                "{\"role\":\"user\",\"content\":\"%s\"}"
            "],"
            "\"temperature\":0.4"
            "}",
            cfg->model,
            esc_system,
            esc_user
        );
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl) {
        set_error(errbuf, errbuf_size, "curl_easy_init failed");
        free(esc_system);
        free(esc_user);
        free(req_body);
        curl_global_cleanup();
        return -4;
    }

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req_body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(req_body));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, cfg->timeout_seconds > 0 ? cfg->timeout_seconds : 20L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "doudizhu-qwen-client/1.0");

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        set_error(errbuf, errbuf_size, curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        free(esc_system);
        free(esc_user);
        free(req_body);
        free(response.data);
        return -5;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code < 200 || http_code >= 300) {
        if (response.data && response.len > 0) {
            snprintf(errbuf, errbuf_size, "HTTP %ld: %.300s", http_code, response.data);
        } else {
            snprintf(errbuf, errbuf_size, "HTTP %ld", http_code);
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        free(esc_system);
        free(esc_user);
        free(req_body);
        free(response.data);
        return -6;
    }

    if (!response.data || !extract_first_content_text(response.data, out_text, out_text_size)) {
        if (response.data && response.len > 0) {
            snprintf(errbuf, errbuf_size, "failed to parse model response: %.300s", response.data);
        } else {
            set_error(errbuf, errbuf_size, "empty response body");
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        free(esc_system);
        free(esc_user);
        free(req_body);
        free(response.data);
        return -7;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    free(esc_system);
    free(esc_user);
    free(req_body);
    free(response.data);
    return 0;
}

int qwen_explain_move(
    const QwenConfig* cfg,
    const char* game_state_json,
    const char* analysis_json,
    char* out_text,
    size_t out_text_size,
    char* errbuf,
    size_t errbuf_size
) {
    char user_prompt[16384];
    const char* system_prompt =
        "你是斗地主AI解说模块。"
        "你不负责重新制定规则，只根据给定的牌局状态和本地算法分析结果做解释。"
        "请输出 JSON，字段必须包含：recommended_explanation、why_not_bomb、style、risk。";

    snprintf(
        user_prompt,
        sizeof(user_prompt),
        "请根据以下输入，输出 JSON。不要输出 markdown。"
        "\n[game_state_json]\n%s"
        "\n[analysis_json]\n%s"
        "\n要求："
        "\n1. recommended_explanation：解释为什么推荐这手牌；"
        "\n2. why_not_bomb：如果当前不建议炸弹，说明原因；如果建议炸弹，也要解释；"
        "\n3. style：只能是 保守 / 均衡 / 进攻 三选一；"
        "\n4. risk：只能是 低 / 中 / 高 三选一；",
        game_state_json ? game_state_json : "{}",
        analysis_json ? analysis_json : "{}"
    );

    return qwen_chat_completion(
        cfg,
        system_prompt,
        user_prompt,
        1,
        out_text,
        out_text_size,
        errbuf,
        errbuf_size
    );
}

int qwen_review_game(
    const QwenConfig* cfg,
    const char* replay_json,
    char* out_text,
    size_t out_text_size,
    char* errbuf,
    size_t errbuf_size
) {
    char user_prompt[16384];
    const char* system_prompt =
        "你是斗地主赛后复盘模块。"
        "你只根据输入的结构化事件做总结，不要捏造不存在的牌。"
        "请输出 JSON，字段必须包含：turning_points、missed_best_play、high_risk_ops、win_reason_summary。";

    snprintf(
        user_prompt,
        sizeof(user_prompt),
        "请根据以下复盘数据输出 JSON，不要输出 markdown。"
        "\n[replay_json]\n%s"
        "\n要求："
        "\n1. turning_points：数组，列出关键转折点；"
        "\n2. missed_best_play：数组，列出错失更优出牌；"
        "\n3. high_risk_ops：数组，列出高风险操作；"
        "\n4. win_reason_summary：字符串，总结胜负主因。",
        replay_json ? replay_json : "{}"
    );

    return qwen_chat_completion(
        cfg,
        system_prompt,
        user_prompt,
        1,
        out_text,
        out_text_size,
        errbuf,
        errbuf_size
    );
}
