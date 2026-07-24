#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/edit/editor.h"
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

static int text_is(ash_editor *ed, ash_arena *a, const char *s)
{
    ash_buf out;
    ash_buf_init(&out, a);
    ash_editor_text(ed, &out);
    size_t n = strlen(s);
    return out.len == n && (n == 0 || memcmp(out.data, s, n) == 0);
}

static void apply(ash_editor *ed, ash_editcmd c)
{
    ash_key k = { c, NULL, 0 };
    ash_editor_apply(ed, k);
}

static void test_edits(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "e", 1u << 16) == ASH_OK);
    ash_editor ed;
    ash_editor_init(&ed, &a);

    ash_editor_set_text(&ed, "hello world", 11);
    apply(&ed, ASH_EC_DOC_END);
    ASH_CHECK(ash_editor_cursor(&ed) == 11);
    apply(&ed, ASH_EC_BACKSPACE_WORD);
    ASH_CHECK(text_is(&ed, &a, "hello "));

    ash_editor_set_text(&ed, "abcdef", 6);
    ash_editor_set_cursor(&ed, 1, 0);
    ash_editor_set_cursor(&ed, 4, 1);
    size_t s, e;
    ASH_CHECK(ash_editor_selection(&ed, &s, &e) && s == 1 && e == 4);
    ash_key ins = { ASH_EC_INSERT, "X", 1 };
    ash_editor_apply(&ed, ins);
    ASH_CHECK(text_is(&ed, &a, "aXef"));
    ASH_CHECK(ash_editor_cursor(&ed) == 2);

    ash_arena_destroy(&a);
}

static void test_updown(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "e", 1u << 16) == ASH_OK);
    ash_editor ed;
    ash_editor_init(&ed, &a);

    ash_editor_set_text(&ed, "abc\nde\nfghi", 11);
    apply(&ed, ASH_EC_END);
    ASH_CHECK(ash_editor_cursor(&ed) == 3);
    apply(&ed, ASH_EC_DOWN);
    ASH_CHECK(ash_editor_cursor(&ed) == 6);
    apply(&ed, ASH_EC_DOWN);
    ASH_CHECK(ash_editor_cursor(&ed) == 10);
    apply(&ed, ASH_EC_UP);
    ASH_CHECK(ash_editor_cursor(&ed) == 6);

    int line = -1, col = -1;
    ash_editor_line_col(&ed, 6, &line, &col);
    ASH_CHECK(line == 1 && col == 2);

    ash_arena_destroy(&a);
}

static void test_hit(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "e", 1u << 16) == ASH_OK);
    ash_editor ed;
    ash_editor_init(&ed, &a);
    ash_editor_set_text(&ed, "abc\ndefg\nhi", 11);

    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_fb fb;
    ash_fb_init(&fb, &a, def);
    ash_fb_begin(&fb, 20, 5);
    ash_editor_render(&ed, &fb, (ash_rect){ 0, 0, 20, 5 }, def, def);

    size_t off = 0;
    int gw = ed.gutter_w;
    ASH_CHECK(ash_editor_hit(&ed, gw + 2, 1, &off) && off == 6);
    ASH_CHECK(ash_editor_hit(&ed, gw + 0, 2, &off) && off == 9);

    ash_arena_destroy(&a);
}

static void snap(ash_fb *fb, ash_arena *a, const char *name)
{
    ash_buf b;
    ash_buf_init(&b, a);
    ash_fb_snapshot(fb, &b);
    check_golden(name, &b);
}

static void test_golden(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "e", 1u << 16) == ASH_OK);
    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_style sel = { ash_rgb(0x10, 0x10, 0x10), ash_rgb(0x40, 0x80, 0xf0), 0 };
    ash_fb fb;
    ash_fb_init(&fb, &a, def);

    ash_editor ed;
    ash_editor_init(&ed, &a);
    ash_editor_set_text(&ed, "line one\nline two\nthree", 23);
    ash_fb_begin(&fb, 20, 5);
    ash_editor_render(&ed, &fb, (ash_rect){ 0, 0, 20, 5 }, def, sel);
    snap(&fb, &a, "editor_basic");

    ash_editor_init(&ed, &a);
    ash_editor_set_text(&ed, "a1\na2\na3\na4\na5\na6\na7", 20);
    ash_editor_set_cursor(&ed, 20, 0);
    ash_fb_begin(&fb, 12, 3);
    ash_editor_render(&ed, &fb, (ash_rect){ 0, 0, 12, 3 }, def, sel);
    snap(&fb, &a, "editor_scroll");

    ash_editor_init(&ed, &a);
    ash_editor_set_text(&ed, "select me\nand this", 18);
    ash_editor_set_cursor(&ed, 3, 0);
    ash_editor_set_cursor(&ed, 13, 1);
    ash_fb_begin(&fb, 16, 3);
    ash_editor_render(&ed, &fb, (ash_rect){ 0, 0, 16, 3 }, def, sel);
    snap(&fb, &a, "editor_sel");

    ash_arena_destroy(&a);
}

int main(void)
{
    test_edits();
    test_updown();
    test_hit();
    test_golden();
    return ash_test_done();
}
