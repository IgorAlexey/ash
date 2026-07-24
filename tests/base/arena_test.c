#include <stdint.h>

#include "ash/base/arena.h"
#include "ash_test.h"

int main(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "test", 4096) == ASH_OK);

    int *x = ash_new(&a, int);
    *x = 7;
    ASH_CHECK(((uintptr_t)x % _Alignof(int)) == 0);

    ash_arena grow;
    ASH_CHECK(ash_arena_create(&grow, "grow", 64) == ASH_OK);
    int *arr = ash_array(&grow, int, 100);
    for (int i = 0; i < 100; i++)
        arr[i] = i;
    ASH_CHECK(arr[0] == 0);
    ASH_CHECK(arr[99] == 99);
    ash_arena_destroy(&grow);

    ash_arena_mark m = ash_arena_mark_get(&a);
    void *p1 = ash_arena_alloc(&a, 32, 16);
    ASH_CHECK(((uintptr_t)p1 % 16) == 0);
    ash_arena_rewind(&a, m);
    void *p2 = ash_arena_alloc(&a, 32, 16);
    ASH_CHECK(p1 == p2);

    ash_arena_reset(&a);
    void *p3 = ash_arena_alloc(&a, 8, 8);
    ASH_CHECK(p3 != NULL);
    ash_arena_destroy(&a);

    ash_arena orphan;
    ASH_CHECK(ash_arena_create(&orphan, "orphan", 64) == ASH_OK);
    void *big1 = ash_arena_alloc(&orphan, 50, 1);
    void *small = ash_arena_alloc(&orphan, 40, 1);
    ASH_CHECK(big1 != small);
    ash_arena_reset(&orphan);
    void *big2 = ash_arena_alloc(&orphan, 200, 1);
    ASH_CHECK(big2 != NULL);
    ash_arena_destroy(&orphan);

    ash_mem mem;
    ASH_CHECK(ash_mem_create(&mem) == ASH_OK);
    char *s = ash_array(&mem.session, char, 16);
    s[0] = 'z';
    ASH_CHECK(s[0] == 'z');
    ash_mem_destroy(&mem);

    return ash_test_done();
}
