#include <string.h>

#include "ash/ai/sse.h"
#include "ash/base/arena.h"
#include "ash_test.h"

static void ignore(void *ud, const ash_sse_event *ev)
{
    (void)ud;
    (void)ev;
}

int main(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "data-cap", 4096) == ASH_OK);
    ash_sse_parser p;
    ash_sse_init(&p, &a);

    const char *line = "data: xxxxxxxx\n";
    size_t linelen = strlen(line);
    ash_status st = ASH_OK;
    int fed = 0;
    while (st == ASH_OK && fed < 100000) {
        st = ash_sse_feed(&p, line, linelen, ignore, NULL);
        fed++;
    }
    ASH_CHECK(st == ASH_ERR_PROTOCOL);
    ASH_CHECK(ash_sse_feed(&p, "z", 1, ignore, NULL) == ASH_ERR_PROTOCOL);
    ash_arena_destroy(&a);

    ash_arena b;
    ASH_CHECK(ash_arena_create(&b, "line-cap", 4096) == ASH_OK);
    ash_sse_parser q;
    ash_sse_init(&q, &b);
    char big[ASH_SSE_MAX_LINE + 8];
    memset(big, 'x', sizeof big);
    ASH_CHECK(ash_sse_feed(&q, big, sizeof big, ignore, NULL) == ASH_ERR_PROTOCOL);
    ASH_CHECK(ash_sse_feed(&q, "z", 1, ignore, NULL) == ASH_ERR_PROTOCOL);
    ash_arena_destroy(&b);

    return ash_test_done();
}
