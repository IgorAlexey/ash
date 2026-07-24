#include <string.h>

#include "ash/base/arena.h"
#include "ash/edit/gapbuf.h"
#include "ash/edit/measure.h"
#include "ash_test.h"

static void load(ash_gapbuf *b, const char *s)
{
    ash_gapbuf_set(b, s, strlen(s));
}

static void test_lines(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "m", 1u << 16) == ASH_OK);
    ash_gapbuf b;
    ash_gapbuf_init(&b, &a);
    load(&b, "abc\ndef\nghi");

    ASH_CHECK(ash_measure_line_start(&b, 5) == 4);
    ASH_CHECK(ash_measure_line_end(&b, 5) == 7);
    ASH_CHECK(ash_measure_line_index(&b, 0) == 0);
    ASH_CHECK(ash_measure_line_index(&b, 5) == 1);
    ASH_CHECK(ash_measure_line_index(&b, 9) == 2);
    ASH_CHECK(ash_measure_line_at(&b, 0) == 0);
    ASH_CHECK(ash_measure_line_at(&b, 1) == 4);
    ASH_CHECK(ash_measure_line_at(&b, 2) == 8);
    ASH_CHECK(ash_measure_line_at(&b, 9) == 11);

    ash_arena_destroy(&a);
}

static void test_grapheme(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "m", 1u << 16) == ASH_OK);
    ash_gapbuf b;
    ash_gapbuf_init(&b, &a);

    load(&b, "a\xcc\x81\x62");
    ash_grapheme g;
    ASH_CHECK(ash_measure_next(&b, 0, 0, 4, &g));
    ASH_CHECK(g.offset == 0 && g.next == 3 && g.width == 1);
    ASH_CHECK(ash_measure_next(&b, 3, 0, 4, &g));
    ASH_CHECK(g.next == 4 && g.width == 1);
    ASH_CHECK(ash_measure_prev(&b, 4) == 3);
    ASH_CHECK(ash_measure_prev(&b, 3) == 0);

    load(&b, "\xe4\xb8\xad X");
    ASH_CHECK(ash_measure_next(&b, 0, 0, 4, &g));
    ASH_CHECK(g.next == 3 && g.width == 2);
    ASH_CHECK(ash_measure_col(&b, 0, 3, 4) == 2);

    ash_arena_destroy(&a);
}

static void test_tabs_cols(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "m", 1u << 16) == ASH_OK);
    ash_gapbuf b;
    ash_gapbuf_init(&b, &a);

    load(&b, "a\tb");
    ash_grapheme g;
    ASH_CHECK(ash_measure_next(&b, 1, 1, 4, &g));
    ASH_CHECK(g.first_cp == '\t' && g.width == 3);
    ASH_CHECK(ash_measure_col(&b, 0, 2, 4) == 4);
    ASH_CHECK(ash_measure_col(&b, 0, 3, 4) == 5);

    load(&b, "abc");
    int oc = -1;
    size_t off = ash_measure_col_to_offset(&b, 0, 2, 4, &oc);
    ASH_CHECK(off == 2 && oc == 2);
    off = ash_measure_col_to_offset(&b, 0, 10, 4, &oc);
    ASH_CHECK(off == 3 && oc == 3);

    ash_arena_destroy(&a);
}

int main(void)
{
    test_lines();
    test_grapheme();
    test_tabs_cols();
    return ash_test_done();
}
