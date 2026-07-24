#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ash/app/queue.h"
#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/fb/fb.h"
#include "ash_test.h"

#ifndef ASH_GOLDEN_DIR
#define ASH_GOLDEN_DIR "."
#endif

static void test_fifo(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "q", 1u << 16) == ASH_OK);
    ash_queue q;
    ash_queue_init(&q, &a);

    ASH_CHECK(ash_queue_count(&q) == 0);
    ash_queue_push(&q, "one", 3);
    ash_queue_push(&q, "two", 3);
    ash_queue_push(&q, "three", 5);
    ASH_CHECK(ash_queue_count(&q) == 3);

    size_t n = 0;
    const char *s = ash_queue_at(&q, 0, &n);
    ASH_CHECK(n == 3 && memcmp(s, "one", 3) == 0);
    s = ash_queue_at(&q, 2, &n);
    ASH_CHECK(n == 5 && memcmp(s, "three", 5) == 0);

    const char *t = NULL;
    ASH_CHECK(ash_queue_pop(&q, &t, &n) && n == 3 && memcmp(t, "one", 3) == 0);
    ASH_CHECK(ash_queue_pop(&q, &t, &n) && n == 3 && memcmp(t, "two", 3) == 0);
    ASH_CHECK(ash_queue_count(&q) == 1);
    ASH_CHECK(ash_queue_pop(&q, &t, &n) && n == 5 && memcmp(t, "three", 5) == 0);
    ASH_CHECK(ash_queue_count(&q) == 0);
    ASH_CHECK(!ash_queue_pop(&q, &t, &n));

    ash_arena_destroy(&a);
}

static void test_compact(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "q", 1u << 16) == ASH_OK);
    ash_queue q;
    ash_queue_init(&q, &a);

    for (int r = 0; r < 4; r++) {
        char buf[16];
        for (int i = 0; i < 20; i++) {
            int nn = snprintf(buf, sizeof buf, "m%d_%d", r, i);
            ash_queue_push(&q, buf, (size_t)nn);
        }
        ASH_CHECK(ash_queue_count(&q) == 20);
        for (int i = 0; i < 20; i++) {
            const char *t = NULL;
            size_t n = 0;
            ASH_CHECK(ash_queue_pop(&q, &t, &n));
            int nn = snprintf(buf, sizeof buf, "m%d_%d", r, i);
            ASH_CHECK(n == (size_t)nn && memcmp(t, buf, n) == 0);
        }
        ASH_CHECK(ash_queue_count(&q) == 0);
    }

    ash_queue_push(&q, "alive", 5);
    ash_queue_clear(&q);
    ASH_CHECK(ash_queue_count(&q) == 0);

    ash_arena_destroy(&a);
}

static void check_golden(const char *name, const ash_buf *snap)
{
    char path[512];
    int n = snprintf(path, sizeof path, "%s/%s.golden", ASH_GOLDEN_DIR, name);
    ASH_CHECK(n > 0 && (size_t)n < sizeof path);
    if (getenv("ASH_GOLDEN_UPDATE")) {
        FILE *f = fopen(path, "wb");
        ASH_CHECK(f != NULL);
        if (f) {
            fwrite(snap->data, 1, snap->len, f);
            fclose(f);
        }
        return;
    }
    FILE *f = fopen(path, "rb");
    ASH_CHECK(f != NULL);
    if (!f)
        return;
    static unsigned char gold[65536];
    size_t gn = fread(gold, 1, sizeof gold, f);
    fclose(f);
    int ok = gn == snap->len && memcmp(gold, snap->data, gn) == 0;
    ASH_CHECK(ok);
    if (!ok)
        fprintf(stderr, "golden %s mismatch (%zu vs %zu)\n", name, gn, snap->len);
}

static void golden_case(ash_arena *a, const char *name, ash_rgba fg)
{
    ash_queue q;
    ash_queue_init(&q, a);
    ash_queue_push(&q, "first queued message", 20);
    ash_queue_push(&q, "second one is quite a bit longer than the bar", 45);
    ash_queue_push(&q, "third", 5);

    ash_style deco = { fg, ASH_RGBA_DEFAULT, ASH_ATTR_NONE };
    ash_style text = { fg, ASH_RGBA_DEFAULT, ASH_ATTR_CONTENT };

    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_fb fb;
    ash_fb_init(&fb, a, def);
    ash_fb_begin(&fb, 24, 3);
    ash_rect r = { 0, 0, 24, 3 };
    int rows = ash_queue_render(&q, &fb, r, text, deco);
    ASH_CHECK(rows == 3);

    ash_buf snap;
    ash_buf_init(&snap, a);
    ash_fb_snapshot(&fb, &snap);
    check_golden(name, &snap);
}

static void test_golden(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "g", 1u << 20) == ASH_OK);
    golden_case(&a, "queued_dark", ash_rgb(150, 150, 150));
    golden_case(&a, "queued_light", ash_rgb(110, 110, 110));
    ash_arena_destroy(&a);
}

static void test_content_attr(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "c", 1u << 16) == ASH_OK);
    ash_queue q;
    ash_queue_init(&q, &a);
    ash_queue_push(&q, "hi", 2);

    ash_style deco = { ash_rgb(150, 150, 150), ASH_RGBA_DEFAULT, ASH_ATTR_NONE };
    ash_style text = { ash_rgb(150, 150, 150), ASH_RGBA_DEFAULT, ASH_ATTR_CONTENT };
    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_fb fb;
    ash_fb_init(&fb, &a, def);
    ash_fb_begin(&fb, 12, 1);
    ash_rect r = { 0, 0, 12, 1 };
    ash_queue_render(&q, &fb, r, text, deco);

    const ash_cell *row = fb.buffers[fb.frame & 1u];
    ASH_CHECK(row != NULL);
    ASH_CHECK((row[0].attr & ASH_ATTR_CONTENT) == 0);
    ASH_CHECK((row[1].attr & ASH_ATTR_CONTENT) == 0);
    ASH_CHECK((row[2].attr & ASH_ATTR_CONTENT) != 0);
    ASH_CHECK(row[2].bytes[0] == 'h');
    ASH_CHECK((row[3].attr & ASH_ATTR_CONTENT) != 0);
    ASH_CHECK(row[3].bytes[0] == 'i');

    ash_arena_destroy(&a);
}

int main(void)
{
    test_fifo();
    test_compact();
    test_content_attr();
    test_golden();
    return ash_test_done();
}
