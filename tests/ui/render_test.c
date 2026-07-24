#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/ui/textarea.h"
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
    if (!ok)
        fprintf(stderr, "--- golden %s ---\n%.*s\n--- got ---\n%.*s\n",
                name, (int)gn, gold, (int)snap->len, (char *)snap->data);
}

static void ins(ash_textarea *ta, const char *s)
{
    ash_textarea_insert(ta, s, strlen(s));
}

static void snap(ash_fb *fb, ash_arena *a, const char *name)
{
    ash_buf b;
    ash_buf_init(&b, a);
    ash_fb_snapshot(fb, &b);
    check_golden(name, &b);
}

int main(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "render", 1u << 16) == ASH_OK);

    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_fb fb;
    ash_fb_init(&fb, &a, def);

    ash_textarea ta;

    ash_textarea_init(&ta, &a, 8, 0);
    ins(&ta, "the quick brown fox");
    ash_fb_begin(&fb, 8, 5);
    ash_textarea_render(&ta, &fb, (ash_rect){ 0, 0, 8, 5 }, def);
    snap(&fb, &a, "wrap");

    ash_textarea_init(&ta, &a, 12, 0);
    ins(&ta, "hello world");
    ash_textarea_apply(&ta, (ash_key){ ASH_EC_DOC_HOME, NULL, 0 });
    for (int i = 0; i < 6; i++)
        ash_textarea_apply(&ta, (ash_key){ ASH_EC_RIGHT, NULL, 0 });
    ash_fb_begin(&fb, 12, 2);
    ash_textarea_render(&ta, &fb, (ash_rect){ 0, 0, 12, 2 }, def);
    snap(&fb, &a, "cursor_mid");

    ash_textarea_init(&ta, &a, 6, 3);
    ins(&ta, "l1\nl2\nl3\nl4\nl5\nl6");
    ash_fb_begin(&fb, 6, 3);
    ash_textarea_render(&ta, &fb, (ash_rect){ 0, 0, 6, 3 }, def);
    snap(&fb, &a, "grow_scroll");

    ash_style l_text = { ash_rgb(30, 30, 30), ash_rgb(250, 250, 250), 0 };
    ash_style l_border = { ash_rgb(128, 128, 128), ash_rgb(250, 250, 250), 0 };
    ash_style d_text = { ash_rgb(220, 220, 220), ash_rgb(20, 20, 20), 0 };
    ash_style d_border = { ash_rgb(128, 128, 128), ash_rgb(20, 20, 20), 0 };

    ash_fb light;
    ash_fb_init(&light, &a, (ash_style){ ash_rgb(30, 30, 30),
                                         ash_rgb(250, 250, 250), 0 });
    ash_fb dark;
    ash_fb_init(&dark, &a, (ash_style){ ash_rgb(220, 220, 220),
                                        ash_rgb(20, 20, 20), 0 });

    ash_buf scratch;
    ash_buf_init(&scratch, &a);
    ash_rect bar = { 0, 0, 20, 5 };

    ash_textarea_init(&ta, &a, 18, 0);
    ins(&ta, "hello world");
    ash_fb_begin(&light, 20, 5);
    ash_input_bar_render(&ta, &light, bar, l_text, l_border, &scratch);
    snap(&light, &a, "bar_light");

    ash_textarea_init(&ta, &a, 18, 0);
    ins(&ta, "hello world");
    ash_fb_begin(&dark, 20, 5);
    ash_input_bar_render(&ta, &dark, bar, d_text, d_border, &scratch);
    snap(&dark, &a, "bar_dark");

    ash_textarea_init(&ta, &a, 18, 0);
    ins(&ta, "l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9");
    ash_textarea_apply(&ta, (ash_key){ ASH_EC_DOC_HOME, NULL, 0 });
    for (int i = 0; i < 4; i++)
        ash_textarea_apply(&ta, (ash_key){ ASH_EC_DOWN, NULL, 0 });
    ash_fb_begin(&light, 20, 5);
    ash_input_bar_render(&ta, &light, bar, l_text, l_border, &scratch);
    snap(&light, &a, "bar_light_scroll");

    ash_textarea_init(&ta, &a, 18, 0);
    ins(&ta, "l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9");
    ash_textarea_apply(&ta, (ash_key){ ASH_EC_DOC_HOME, NULL, 0 });
    for (int i = 0; i < 4; i++)
        ash_textarea_apply(&ta, (ash_key){ ASH_EC_DOWN, NULL, 0 });
    ash_fb_begin(&dark, 20, 5);
    ash_input_bar_render(&ta, &dark, bar, d_text, d_border, &scratch);
    snap(&dark, &a, "bar_dark_scroll");

    ash_arena_destroy(&a);
    return ash_test_done();
}
