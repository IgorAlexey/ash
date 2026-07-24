#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/fb/fb.h"
#include "ash_test.h"

static int has(const ash_buf *b, const char *needle)
{
    return memmem(b->data, b->len, needle, strlen(needle)) != NULL;
}

int main(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "fb", 1u << 16) == ASH_OK);

    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_style red = { ash_rgb(0xe7, 0x4c, 0x3c), ASH_RGBA_DEFAULT, ASH_ATTR_BOLD };

    ash_fb fb;
    ash_fb_init(&fb, &a, def);

    ash_buf out;

    ash_fb_begin(&fb, 10, 3);
    ash_fb_put_text(&fb, 0, 0, red, "Hi", 2);
    ash_buf_init(&out, &a);
    ash_fb_flip(&fb, &out);
    ASH_CHECK(out.len > 0);
    ASH_CHECK(has(&out, "Hi"));

    ash_fb_begin(&fb, 10, 3);
    ash_fb_put_text(&fb, 0, 0, red, "Hi", 2);
    ash_buf_init(&out, &a);
    ash_fb_flip(&fb, &out);
    ASH_CHECK(out.len == 0);

    ash_fb_begin(&fb, 10, 3);
    ash_fb_put_text(&fb, 0, 0, red, "Hi", 2);
    ash_fb_put_text(&fb, 5, 1, def, "X", 1);
    ash_buf_init(&out, &a);
    ash_fb_flip(&fb, &out);
    ASH_CHECK(out.len > 0);
    ASH_CHECK(out.len < 40);
    ASH_CHECK(has(&out, "\x1b[2;6H"));
    ASH_CHECK(has(&out, "X"));
    ASH_CHECK(!has(&out, "\x1b[1;"));
    ASH_CHECK(!has(&out, "\x1b[3;"));

    ash_fb_begin(&fb, 10, 3);
    ash_fb_clip_push(&fb, (ash_rect){ 2, 0, 3, 3 });
    ash_fb_put_text(&fb, 0, 0, def, "ABCDEF", 6);
    ash_fb_clip_pop(&fb);
    ash_buf snap;
    ash_buf_init(&snap, &a);
    ash_fb_snapshot(&fb, &snap);
    ASH_CHECK(memmem(snap.data, snap.len, "|  CDE     |", 12) != NULL);

    ash_buf_init(&out, &a);
    ash_fb_flip(&fb, &out);

    ash_fb_begin(&fb, 20, 3);
    ash_buf_init(&out, &a);
    ash_fb_flip(&fb, &out);
    ASH_CHECK(out.len > 0);

    ash_fb_begin(&fb, 20, 3);
    ash_fb_fill_rect(&fb, (ash_rect){ 0, 0, 20, 3 }, def, ' ');
    ash_fb_set_cursor(&fb, 4, 1, ASH_CURSOR_BAR);
    ash_buf_init(&out, &a);
    ash_fb_flip(&fb, &out);
    ASH_CHECK(has(&out, "\x1b[2;5H"));
    ASH_CHECK(has(&out, "\x1b[?25h"));

    ash_arena_destroy(&a);
    return ash_test_done();
}
