#include <string.h>

#include "ash/base/arena.h"
#include "ash/fb/scrollback.h"
#include "ash_test.h"

static uint8_t tag_of(uint64_t seq)
{
    return (uint8_t)('A' + (seq % 26));
}

static void put(ash_scrollback *sb, size_t n, uint8_t tag)
{
    ash_cell line[64];
    memset(line, 0, sizeof line);
    for (size_t i = 0; i < n; i++) {
        line[i].bytes[0] = tag;
        line[i].len = 1;
        line[i].width = 1;
    }
    ash_sb_append(sb, line, n);
}

static void check_intact(ash_scrollback *sb)
{
    for (uint64_t s = ash_sb_oldest(sb); s < ash_sb_newest(sb); s++) {
        const ash_cell *cells = NULL;
        size_t n = 0;
        ASH_CHECK(ash_sb_line_at(sb, s, &cells, &n));
        ASH_CHECK(n > 0 && cells != NULL && cells[0].bytes[0] == tag_of(s));
    }
}

int main(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "sb", 1u << 16) == ASH_OK);

    ash_scrollback sb;
    ash_sb_init(&sb, &a, 20 * sizeof(ash_cell), 8);
    ASH_CHECK(ash_sb_count(&sb) == 0);
    ASH_CHECK(ash_sb_oldest(&sb) == 0);
    ASH_CHECK(ash_sb_newest(&sb) == 0);
    ASH_CHECK(ash_sb_is_following(&sb));

    for (uint64_t s = 0; s < 3; s++)
        put(&sb, 4, tag_of(s));
    ASH_CHECK(ash_sb_count(&sb) == 3);
    ASH_CHECK(ash_sb_oldest(&sb) == 0);
    ASH_CHECK(ash_sb_newest(&sb) == 3);

    const ash_cell *cells = NULL;
    size_t n = 0;
    ASH_CHECK(ash_sb_line_at(&sb, 1, &cells, &n));
    ASH_CHECK(n == 4 && cells[0].bytes[0] == tag_of(1));
    ASH_CHECK(!ash_sb_line_at(&sb, 3, &cells, &n));

    ASH_CHECK(ash_sb_view_top(&sb, 2) == 1);
    ASH_CHECK(ash_sb_view_top(&sb, 10) == 0);

    ash_sb_scroll_to(&sb, 0);
    ASH_CHECK(!ash_sb_is_following(&sb));
    ASH_CHECK(ash_sb_view_top(&sb, 2) == 0);
    ash_sb_scroll_by(&sb, 1, 2);
    ASH_CHECK(ash_sb_view_top(&sb, 2) == 1);
    ASH_CHECK(ash_sb_is_following(&sb));
    ash_sb_follow(&sb);
    ASH_CHECK(ash_sb_is_following(&sb));
    ASH_CHECK(ash_sb_view_top(&sb, 2) == 1);

    ash_sb_scroll_by(&sb, -1, 2);
    ASH_CHECK(!ash_sb_is_following(&sb));
    ASH_CHECK(ash_sb_view_top(&sb, 2) == 0);
    ash_sb_scroll_by(&sb, 1, 2);
    ash_sb_scroll_by(&sb, 1, 2);
    ASH_CHECK(ash_sb_is_following(&sb));
    ASH_CHECK(ash_sb_view_top(&sb, 2) == 1);

    for (uint64_t s = 3; s < 12; s++)
        put(&sb, 4, tag_of(s));
    ASH_CHECK(ash_sb_newest(&sb) == 12);
    ASH_CHECK(ash_sb_count(&sb) == 5);
    ASH_CHECK(ash_sb_oldest(&sb) == 7);
    check_intact(&sb);
    ASH_CHECK(!ash_sb_line_at(&sb, 6, &cells, &n));

    uint64_t before = ash_sb_oldest(&sb);
    ash_sb_scroll_to(&sb, before);
    for (uint64_t s = 12; s < 30; s++)
        put(&sb, (size_t)(1 + (s % 3)), tag_of(s));
    check_intact(&sb);
    ASH_CHECK(ash_sb_view_top(&sb, 4) == ash_sb_oldest(&sb));

    ash_scrollback lim;
    ash_sb_init(&lim, &a, 4096 * sizeof(ash_cell), 4);
    for (uint64_t s = 0; s < 20; s++)
        put(&lim, 1, tag_of(s));
    ASH_CHECK(ash_sb_count(&lim) == 4);
    ASH_CHECK(ash_sb_oldest(&lim) == 16);
    check_intact(&lim);

    put(&lim, 0, 0);
    ASH_CHECK(ash_sb_line_at(&lim, 20, &cells, &n));
    ASH_CHECK(n == 0);

    ash_scrollback wr;
    ash_sb_init(&wr, &a, 4096 * sizeof(ash_cell), 8);
    ash_cell one[1];
    memset(one, 0, sizeof one);
    one[0].bytes[0] = 'x';
    one[0].len = 1;
    one[0].width = 1;
    ash_sb_append_wrapped(&wr, one, 1, 0);
    ash_sb_append_wrapped(&wr, one, 1, 1);
    ash_sb_append(&wr, one, 1);
    ASH_CHECK(ash_sb_cont(&wr, 0) == 0);
    ASH_CHECK(ash_sb_cont(&wr, 1) == 1);
    ASH_CHECK(ash_sb_cont(&wr, 2) == 0);
    ASH_CHECK(ash_sb_cont(&wr, 99) == 0);

    ash_arena_destroy(&a);
    return ash_test_done();
}
