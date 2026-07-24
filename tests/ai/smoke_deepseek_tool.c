#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ash/ai/http.h"
#include "ash/ai/provider.h"
#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/base/poison.h"

struct sink {
    ash_buf buf;
};

static void on_text(void *ud, const char *text, size_t n)
{
    struct sink *s = ud;
    if (n == 0)
        return;
    ash_buf_append(&s->buf, text, n);
    fwrite(text, 1, n, stdout);
    fflush(stdout);
}

static ash_status run_turn(ash_arena *a, const ash_provider_cfg *cfg,
                           const ash_msg *msgs, size_t nmsgs, struct sink *s,
                           char *stop, size_t cap, const char **tid,
                           const char **tname, const char **targs,
                           int *tcount, ash_ai_usage *usage)
{
    ash_provider_stream *ps = NULL;
    ASH_TRY(ash_provider_start(&ps, a, cfg, msgs, nmsgs, on_text, s, stop, cap));
    int running = 1;
    ash_status st = ASH_OK;
    while (running && st == ASH_OK) {
        st = ash_provider_wait(ps, -1, 1000, NULL);
        if (st == ASH_OK)
            st = ash_provider_pump(ps, &running);
    }
    ash_status fin = ash_provider_finish(ps);
    size_t alen = 0;
    if (tid != NULL)
        ASH_TRY(ash_provider_tool_at(ps, 0, tid, tname, targs, &alen));
    if (tcount != NULL)
        *tcount = ash_provider_tool_count(ps);
    if (usage != NULL)
        ASH_TRY(ash_provider_usage(ps, usage));
    ash_provider_stream_close(ps);
    return st != ASH_OK ? st : fin;
}

int main(void)
{
    const ash_provider_desc *desc = ash_provider_find("deepseek");
    const char *key = ash_provider_env_key(desc);
    if (key == NULL || key[0] == 0) {
        fprintf(stderr, "smoke: set %s\n", desc->env_key);
        return 2;
    }
    if (ash_http_global_init() != ASH_OK) {
        fprintf(stderr, "smoke: %s\n", ash_errbuf);
        return 1;
    }

    ash_arena a;
    if (ash_arena_create(&a, "smoke", 1u << 18) != ASH_OK) {
        ash_http_global_cleanup();
        return 1;
    }

    const char *tools =
        "[{\"name\":\"get_temperature\","
        "\"description\":\"Return the current temperature for a city in Celsius.\","
        "\"input_schema\":{\"type\":\"object\",\"properties\":"
        "{\"city\":{\"type\":\"string\"}},\"required\":[\"city\"]}}]";

    ash_provider_cfg cfg = {
        .provider = desc,
        .api_key = key,
        .model = desc->default_model,
        .max_tokens = 256,
        .system = "You must use the get_temperature tool to answer.",
        .tools = tools,
    };

    ash_msg turn1[] = {
        { .role = "user",
          .content = "What is the temperature in Paris right now?" },
    };
    struct sink s1 = { 0 };
    ash_buf_init(&s1.buf, &a);
    char stop1[32] = { 0 };
    const char *tid = NULL, *tname = NULL, *targs = NULL;
    int tcount = 0;
    ash_ai_usage u1 = { 0 };

    printf("turn 1 (expect tool call)\n  assistant> ");
    ash_status st = run_turn(&a, &cfg, turn1, 1, &s1, stop1, sizeof stop1,
                             &tid, &tname, &targs, &tcount, &u1);
    printf("\n  stop=%s tool_count=%d\n", stop1, tcount);
    if (st != ASH_OK) {
        fprintf(stderr, "smoke: turn 1 failed: %s\n", ash_errbuf);
        goto fail;
    }
    if (strcmp(stop1, "tool_use") != 0 || tname == NULL || tid == NULL) {
        fprintf(stderr, "smoke: model did not call a tool (stop=%s)\n", stop1);
        goto fail;
    }
    printf("  tool: id=%s name=%s args=%s\n", tid, tname, targs);

    const char *result = "{\"city\":\"Paris\",\"celsius\":21}";
    ash_msg turn2[] = {
        { .role = "user",
          .content = "What is the temperature in Paris right now?" },
        { .role = "assistant", .content = (const char *)s1.buf.data,
          .tool_id = tid, .tool_name = tname, .tool_input = targs },
        { .role = "tool", .tool_id = tid, .tool_result = result },
    };
    struct sink s2 = { 0 };
    ash_buf_init(&s2.buf, &a);
    char stop2[32] = { 0 };
    ash_ai_usage u2 = { 0 };

    printf("turn 2 (feed result, expect final text)\n  assistant> ");
    st = run_turn(&a, &cfg, turn2, 3, &s2, stop2, sizeof stop2,
                  NULL, NULL, NULL, NULL, &u2);
    printf("\n  stop=%s\n", stop2);
    if (st != ASH_OK) {
        fprintf(stderr, "smoke: turn 2 failed: %s\n", ash_errbuf);
        goto fail;
    }
    if (s2.buf.len == 0) {
        fprintf(stderr, "smoke: turn 2 produced no text\n");
        goto fail;
    }

    ash_ai_usage total = { 0 };
    ash_ai_usage_add(&total, &u1);
    ash_ai_usage_add(&total, &u2);
    double cost = ash_model_cost_usd(ash_model_find(cfg.model), &total);
    printf("usage total: input=%lld output=%lld cache_read=%lld  cost=$%.6f\n",
           (long long)total.input_tokens, (long long)total.output_tokens,
           (long long)total.cache_read_input_tokens, cost);
    printf("ROUND-TRIP OK\n");

    ash_arena_destroy(&a);
    ash_http_global_cleanup();
    return 0;

fail:
    ash_arena_destroy(&a);
    ash_http_global_cleanup();
    return 1;
}
