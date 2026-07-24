#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/edit/diffview.h"
#include "ash_test.h"

#ifndef ASH_GOLDEN_DIR
#define ASH_GOLDEN_DIR "."
#endif

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
        fprintf(stderr, "--- golden %s ---\n%.*s\n--- got ---\n%.*s\n", name,
                (int)gn, gold, (int)snap->len, (char *)snap->data);
}

static void test_compute(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "d", 1u << 16) == ASH_OK);

    ash_diff d;
    ash_diff_compute(&a, "a\nb\nc", 5, "a\nB\nc", 5, &d);
    ASH_CHECK(d.count == 4);
    ASH_CHECK(d.additions == 1 && d.deletions == 1);
    ASH_CHECK(d.lines[0].op == ASH_DIFF_EQ);
    ASH_CHECK(d.lines[1].op == ASH_DIFF_DEL && d.lines[1].len == 1 &&
              d.lines[1].text[0] == 'b');
    ASH_CHECK(d.lines[2].op == ASH_DIFF_ADD && d.lines[2].text[0] == 'B');
    ASH_CHECK(d.lines[3].op == ASH_DIFF_EQ);
    ASH_CHECK(d.lines[1].old_no == 2 && d.lines[2].new_no == 2);

    ash_arena_destroy(&a);
}

static void test_propose(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "d", 1u << 16) == ASH_OK);

    ash_diffview dv;
    ash_diffview_init(&dv, &a, "x.c");
    ash_edit_spec ed = { "bar", 3, "qux", 3 };
    const char *err = NULL;
    int ok = ash_diffview_propose(&dv, "foo\nbar\nbaz", 11, &ed, 1, &err);
    ASH_CHECK(ok && err == NULL);
    ASH_CHECK(dv.diff.additions == 1 && dv.diff.deletions == 1);

    ash_edit_spec bad = { "nope", 4, "x", 1 };
    ok = ash_diffview_propose(&dv, "foo\nbar", 7, &bad, 1, &err);
    ASH_CHECK(!ok && err != NULL);

    ash_arena_destroy(&a);
}

static void test_golden(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "d", 1u << 16) == ASH_OK);

    ash_style ctx = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_style add = { ash_rgb(0x20, 0xa0, 0x20), ASH_RGBA_DEFAULT, 0 };
    ash_style del = { ash_rgb(0xd0, 0x30, 0x30), ASH_RGBA_DEFAULT, 0 };
    ash_style gut = { ash_rgb(0x80, 0x80, 0x80), ASH_RGBA_DEFAULT, 0 };
    ash_style hdr = { ash_rgb(0xff, 0xff, 0xff), ash_rgb(0x20, 0x40, 0x80), 0 };
    ash_style hint = { ash_rgb(0xc0, 0xc0, 0xc0), ASH_RGBA_DEFAULT,
                       ASH_ATTR_BOLD };
    ash_diffview_theme theme = { ctx, add, del, gut, hdr, hint };

    ash_fb fb;
    ash_fb_init(&fb, &a, ctx);

    ash_diffview dv;
    ash_diffview_init(&dv, &a, "hello.c");
    ash_diffview_set(&dv,
                     "one\ntwo\nthree\nfour\nfive", 23,
                     "one\nTWO\nthree\nfour\nfive", 23);
    ash_fb_begin(&fb, 24, 8);
    ash_diffview_render(&dv, &fb, (ash_rect){ 0, 0, 24, 8 }, &theme);
    ash_buf snap;
    ash_buf_init(&snap, &a);
    ash_fb_snapshot(&fb, &snap);
    check_golden("diff_basic", &snap);

    ash_arena_destroy(&a);
}

int main(void)
{
    test_compute();
    test_propose();
    test_golden();
    return ash_test_done();
}
