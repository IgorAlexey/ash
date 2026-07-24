#ifndef ASH_TEST_H
#define ASH_TEST_H

#include <stdio.h>
#include <string.h>

static int ash_test_fails;
static int ash_test_count;

static inline void ash_test_check(int ok, const char *expr,
                                  const char *file, int line)
{
    ash_test_count++;
    if (!ok) {
        ash_test_fails++;
        fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
    }
}

static inline int ash_test_done(void)
{
    if (ash_test_fails) {
        fprintf(stderr, "%d/%d checks failed\n", ash_test_fails, ash_test_count);
        return 1;
    }
    printf("ok: %d checks\n", ash_test_count);
    return 0;
}

#define ASH_CHECK(cond)       ash_test_check(!!(cond), #cond, __FILE__, __LINE__)
#define ASH_CHECK_STREQ(a, b) ASH_CHECK(strcmp((a), (b)) == 0)

#endif
