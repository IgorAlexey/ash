#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/ui/dialog.h"
#include "ash/ui/tui.h"
#include "ash_test.h"

#ifndef ASH_GOLDEN_DIR
#define ASH_GOLDEN_DIR "."
#endif

static ash_arena *g_a;
static ash_fb    *g_fb;

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

static void snap(const char *name)
{
    ash_buf b;
    ash_buf_init(&b, g_a);
    ash_fb_snapshot(g_fb, &b);
    check_golden(name, &b);
}

typedef void (*draw_fn)(ash_ctx *);

static void run(const char *name, int w, int h, draw_fn draw)
{
    ash_tui tui;
    ASH_CHECK(ash_tui_init(&tui) == ASH_OK);
    const ash_input_event *e = NULL;
    int guard = 0;
    do {
        ash_ctx *c = ash_tui_begin(&tui, w, h, e);
        draw(c);
        ash_tui_end(c);
    } while (ash_tui_settling(&tui) && ++guard < 16);

    ash_fb_begin(g_fb, w, h);
    ash_tui_render(&tui, g_fb);
    snap(name);
    ash_tui_destroy(&tui);
}

static void d_confirm(ash_ctx *c)
{
    (void)ash_dialog_confirm(c, "Confirm", "Discard changes?", "Yes", "No");
}

static ash_textarea g_prompt_ta;

static void d_prompt(ash_ctx *c)
{
    (void)ash_dialog_prompt(c, "Rename", "New name:", &g_prompt_ta, "OK",
                            "Cancel");
}

static const char *const cands[] = {
    "src/ui/tui.c", "src/ui/menubar.c", "src/term/input.c", "README.md",
    "Makefile",
};
enum { NCAND = (int)(sizeof cands / sizeof cands[0]) };

static void type(ash_picker *pk, const char *s)
{
    for (size_t i = 0; s[i]; i++) {
        ash_input_event e = { ASH_EV_TEXT, 0, 0, s + i, 1, 0, 0, 0, 0 };
        (void)ash_picker_handle(pk, &e);
    }
}

int main(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "dialog-test", 1u << 18) == ASH_OK);
    g_a = &a;

    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_fb fb;
    ash_fb_init(&fb, &a, def);
    g_fb = &fb;

    run("dialog_confirm", 30, 10, d_confirm);

    ash_textarea_init(&g_prompt_ta, &a, 16, 1);
    ash_textarea_insert(&g_prompt_ta, "main.c", 6);
    run("dialog_prompt", 30, 10, d_prompt);

    ash_arena scratch;
    ASH_CHECK(ash_arena_create(&scratch, "picker-scratch", 1u << 20) == ASH_OK);
    ash_arena qa;
    ASH_CHECK(ash_arena_create(&qa, "picker-query", 1u << 12) == ASH_OK);

    ash_picker pk;
    ash_picker_init(&pk, &scratch, &qa, cands, NCAND, 26);

    ash_rect rect = { 2, 1, 30, 10 };
    ash_fb_begin(g_fb, 34, 12);
    ash_picker_render(&pk, g_fb, rect);
    snap("picker_empty");

    type(&pk, "men");
    ASH_CHECK(pk.sel == 0);
    ash_fb_begin(g_fb, 34, 12);
    ash_picker_render(&pk, g_fb, rect);
    snap("picker_men");

    ash_input_event enter = { ASH_EV_KEY, 13, 0, NULL, 0, 0, 0, 0, 0 };
    int sel = ash_picker_handle(&pk, &enter);
    ASH_CHECK(sel == 1);

    ash_input_event esc = { ASH_EV_KEY, 27, 0, NULL, 0, 0, 0, 0, 0 };
    ASH_CHECK(ash_picker_handle(&pk, &esc) == ASH_PICKER_CANCEL);

    ash_picker pk2;
    ash_picker_init(&pk2, &scratch, &qa, cands, NCAND, 26);
    type(&pk2, "tui");
    int sel2 = ash_picker_handle(&pk2, &enter);
    ASH_CHECK(sel2 == 0);

    ash_picker pk3;
    ash_picker_init(&pk3, &scratch, &qa, cands, NCAND, 26);
    type(&pk3, "zzzz");
    ASH_CHECK(ash_picker_handle(&pk3, &enter) == ASH_PICKER_NONE);

    ash_arena_destroy(&qa);
    ash_arena_destroy(&scratch);
    ash_arena_destroy(&a);
    return ash_test_done();
}
