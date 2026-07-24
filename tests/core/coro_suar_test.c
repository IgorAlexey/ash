#include "ash/core/coro.h"
#include "ash/base/arena.h"

static ash_co co;
static int *escaped;

static ash_status body(ash_co *c, void *ud)
{
    (void)ud;
    ash_co_yield(c, ASH_WAIT_SSE);
    return ASH_OK;
}

__attribute__((noinline))
static void scheduler_frame(void)
{
    volatile int local = 0x5a5a;
    escaped = (int *)&local;
    ash_co_resume(&co);
    ash_co_resume(&co);
    (void)local;
}

int main(void)
{
    ash_arena a;
    if (ash_arena_create(&a, "s", 1u << 20) != ASH_OK)
        return 2;
    void *stk = ash_arena_alloc(&a, 256u * 1024u, 16);
    if (ash_co_create(&co, stk, 256u * 1024u, body, NULL) != ASH_OK)
        return 2;
    scheduler_frame();
    volatile int v = *escaped;
    (void)v;
    ash_arena_destroy(&a);
    return 0;
}
