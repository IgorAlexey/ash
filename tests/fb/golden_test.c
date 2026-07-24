#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/fb/fb.h"
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
    if (!f) {
        fprintf(stderr, "missing golden %s (rerun with ASH_GOLDEN_UPDATE=1)\n", path);
        return;
    }
    static unsigned char gold[65536];
    size_t gn = fread(gold, 1, sizeof gold, f);
    fclose(f);

    int ok = gn == snap->len && memcmp(gold, snap->data, gn) == 0;
    ASH_CHECK(ok);
    if (!ok) {
        fprintf(stderr, "--- golden %s ---\n%.*s\n--- got ---\n%.*s\n",
                name, (int)gn, gold, (int)snap->len, (char *)snap->data);
    }
}

int main(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "golden", 1u << 16) == ASH_OK);

    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_style red = { ash_rgb(0xe7, 0x4c, 0x3c), ASH_RGBA_DEFAULT, ASH_ATTR_BOLD };
    ash_style barbg = { ash_rgb(0xff, 0xff, 0xff), ash_rgb(0x20, 0x60, 0xc0),
                        ASH_ATTR_UNDERLINE };

    ash_fb fb;
    ash_fb_init(&fb, &a, def);

    ash_buf snap;

    ash_fb_begin(&fb, 16, 4);
    ash_fb_put_text(&fb, 0, 0, red, "Hello", 5);
    ash_fb_put_text(&fb, 0, 1, def, "world", 5);
    ash_fb_fill_rect(&fb, (ash_rect){ 0, 3, 16, 1 }, barbg, ' ');
    ash_fb_put_text(&fb, 1, 3, barbg, "status", 6);
    ash_fb_set_cursor(&fb, 5, 0, ASH_CURSOR_BAR);
    ash_buf_init(&snap, &a);
    ash_fb_snapshot(&fb, &snap);
    check_golden("basic", &snap);

    ash_fb_begin(&fb, 10, 2);
    ash_fb_put_text(&fb, 0, 0, def, "a\xcc\x81z", 4);
    ash_fb_put_text(&fb, 0, 1, red, "\xe4\xb8\xad X", 5);
    ash_buf_init(&snap, &a);
    ash_fb_snapshot(&fb, &snap);
    check_golden("unicode", &snap);

    ash_fb_begin(&fb, 10, 2);
    ash_fb_put_text(&fb, 0, 0, def, "e\xcc\x81", 3);
    ash_fb_put_text(&fb, 3, 0, red,
                    "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9", 11);
    ash_fb_put_text(&fb, 0, 1, def, "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8", 8);
    ash_fb_put_text(&fb, 3, 1, red, "\xe4\xb8\xad", 3);
    ash_buf_init(&snap, &a);
    ash_fb_snapshot(&fb, &snap);
    check_golden("clusters", &snap);

    ash_style rev = { ash_rgb(0x10, 0x10, 0x10), ash_rgb(0xd0, 0xd0, 0xd0),
                      ASH_ATTR_REVERSE | ASH_ATTR_STRIKETHROUGH };
    ash_fb_begin(&fb, 12, 2);
    ash_fb_put_text(&fb, 0, 0, red, "bold", 4);
    ash_fb_put_text(&fb, 5, 0, rev, "rev", 3);
    ash_fb_put_text(&fb, 0, 1, def, "plain", 5);
    ash_buf_init(&snap, &a);
    ash_fb_snapshot(&fb, &snap);
    check_golden("styles", &snap);

    ash_arena_destroy(&a);
    return ash_test_done();
}
