#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ash/ai/http.h"
#include "ash/ai/provider.h"
#include "ash/base/arena.h"
#include "ash/base/poison.h"

static void on_text(void *ud, const char *text, size_t n)
{
    (void)ud;
    fwrite(text, 1, n, stdout);
    fflush(stdout);
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
    if (ash_arena_create(&a, "smoke", 1u << 16) != ASH_OK) {
        ash_http_global_cleanup();
        return 1;
    }

    ash_provider_cfg cfg = {
        .provider = desc,
        .api_key = key,
        .model = desc->default_model,
        .max_tokens = 64,
        .system = "Answer in exactly one short sentence.",
    };
    ash_msg msgs[] = {
        { .role = "user",
          .content = "Name one primary color in a single short sentence." },
    };

    char stop[32] = { 0 };
    ash_provider_stream *ps = NULL;
    ash_status st = ash_provider_start(&ps, &a, &cfg, msgs, 1, on_text, NULL,
                                       stop, sizeof stop);
    if (st != ASH_OK) {
        fprintf(stderr, "smoke: start failed: %s\n", ash_errbuf);
        ash_arena_destroy(&a);
        ash_http_global_cleanup();
        return 1;
    }

    fputs("deepseek> ", stdout);
    int running = 1;
    while (running && st == ASH_OK) {
        st = ash_provider_wait(ps, -1, 1000, NULL);
        if (st == ASH_OK)
            st = ash_provider_pump(ps, &running);
    }
    fputc('\n', stdout);

    ash_status fin = ash_provider_finish(ps);
    ash_ai_usage u = { 0 };
    ash_status ust = ash_provider_usage(ps, &u);
    ash_provider_stream_close(ps);

    if (st != ASH_OK || fin != ASH_OK) {
        fprintf(stderr, "smoke: turn failed: %s\n", ash_errbuf);
        ash_arena_destroy(&a);
        ash_http_global_cleanup();
        return 1;
    }

    printf("stop=%s\n", stop);
    if (ust == ASH_OK) {
        const ash_model_info *mi = ash_model_find(cfg.model);
        double cost = ash_model_cost_usd(mi, &u);
        printf("usage: input=%lld output=%lld cache_read=%lld  cost=$%.6f\n",
               (long long)u.input_tokens, (long long)u.output_tokens,
               (long long)u.cache_read_input_tokens, cost);
    }

    ash_arena_destroy(&a);
    ash_http_global_cleanup();
    return 0;
}
