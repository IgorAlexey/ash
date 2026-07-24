#include "ash/core/coro.h"
#include "ash/base/arena.h"
#include "ash_test.h"

static ash_status body(ash_co *co, void *ud)
{
    int *counter = ud;

    ash_co_yield(co, ASH_WAIT_INPUT);
    (*counter)++;
    if (co->canceled)
        return ASH_ERR_INTERRUPTED;

    ash_co_yield(co, ASH_WAIT_SSE);
    (*counter)++;
    if (co->canceled)
        return ASH_ERR_INTERRUPTED;

    ash_co_yield(co, ASH_WAIT_SSE);
    (*counter)++;
    return ASH_OK;
}

int main(void)
{
    ash_arena stack_arena;
    ASH_CHECK(ash_arena_create(&stack_arena, "stack", 1u << 20) == ASH_OK);

    size_t ssz = 256u * 1024u;
    void *stk = ash_arena_alloc(&stack_arena, ssz, 16);
    int counter = 0;
    ash_co co;
    ASH_CHECK(ash_co_create(&co, stk, ssz, body, &counter) == ASH_OK);

    ASH_CHECK(ash_co_resume(&co) == ASH_WAIT_INPUT);
    ASH_CHECK(ash_co_resume(&co) == ASH_WAIT_SSE);
    ASH_CHECK(counter == 1);
    ASH_CHECK(ash_co_resume(&co) == ASH_WAIT_SSE);
    ASH_CHECK(counter == 2);
    ASH_CHECK(ash_co_resume(&co) == ASH_WAIT_NONE);
    ASH_CHECK(co.state == ASH_CO_DEAD);
    ASH_CHECK(counter == 3);
    ASH_CHECK(co.status == ASH_OK);

    void *stk2 = ash_arena_alloc(&stack_arena, ssz, 16);
    int c2 = 0;
    ash_co co2;
    ASH_CHECK(ash_co_create(&co2, stk2, ssz, body, &c2) == ASH_OK);
    ASH_CHECK(ash_co_resume(&co2) == ASH_WAIT_INPUT);
    ash_co_cancel(&co2);
    ASH_CHECK(ash_co_resume(&co2) == ASH_WAIT_NONE);
    ASH_CHECK(co2.state == ASH_CO_DEAD);
    ASH_CHECK(co2.status == ASH_ERR_INTERRUPTED);

    ash_arena_destroy(&stack_arena);
    return ash_test_done();
}
