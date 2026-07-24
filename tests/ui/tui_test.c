#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
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

static void snap(const char *name)
{
    ash_buf b;
    ash_buf_init(&b, g_a);
    ash_fb_snapshot(g_fb, &b);
    check_golden(name, &b);
}

typedef void (*draw_fn)(ash_ctx *);

static void run_seq(const char *name, int w, int h,
                    const ash_input_event *const *evs, int nev, draw_fn draw)
{
    ash_tui tui;
    ASH_CHECK(ash_tui_init(&tui) == ASH_OK);

    for (int f = 0; f < nev; f++) {
        const ash_input_event *e = evs[f];
        int guard = 0;
        do {
            ash_ctx *c = ash_tui_begin(&tui, w, h, e);
            draw(c);
            ash_tui_end(c);
            e = NULL;
        } while (ash_tui_settling(&tui) && ++guard < 16);
    }

    ash_fb_begin(g_fb, w, h);
    ash_tui_render(&tui, g_fb);
    snap(name);

    ash_tui_destroy(&tui);
}

static void run(const char *name, int w, int h, const ash_input_event *ev,
                draw_fn draw)
{
    const ash_input_event *evs[1] = { ev };
    run_seq(name, w, h, evs, 1, draw);
}

static void d_label(ash_ctx *c)
{
    ash_label(c, "lbl", "Hello");
}

static void d_styled(ash_ctx *c)
{
    ash_styled_label_begin(c, "s");
    ash_styled_label_text(c, "ab");
    ash_styled_label_fg(c, ash_rgb(220, 40, 40));
    ash_styled_label_text(c, "cd");
    ash_styled_label_end(c);
}

static void d_button(ash_ctx *c)
{
    (void)ash_button(c, "b", "Save", ash_button_default());
}

static void d_button_focus(ash_ctx *c)
{
    (void)ash_button(c, "b", "Save", ash_button_default());
    ash_steal_focus(c);
}

static void d_accel(ash_ctx *c)
{
    ash_button_style s = ash_button_default();
    s.accel = 'S';
    (void)ash_button(c, "b", "Save", s);
}

static int g_checked;

static void d_checkbox(ash_ctx *c)
{
    (void)ash_checkbox(c, "chk", "Wrap", &g_checked);
}

static void d_list(ash_ctx *c)
{
    ash_list_begin(c, "list");
    (void)ash_list_item(c, 1, "alpha");
    (void)ash_list_item(c, 0, "beta");
    (void)ash_list_item(c, 0, "gamma");
    ash_list_end(c);
    ash_focus_on_first_present(c);
}

static void d_border(ash_ctx *c)
{
    ash_block_begin(c, "box");
    ash_attr_border(c);
    ash_attr_padding(c, (ash_padding){ 1, 0, 1, 0 });
    ash_label(c, "lbl", "hi");
    ash_block_end(c);
}

static void d_modal(ash_ctx *c)
{
    ash_label(c, "bg", "background");
    ash_modal_begin(c, "m", "Menu");
    ash_label(c, "l", "Pick one:");
    (void)ash_button(c, "ok", "OK", ash_button_default());
    (void)ash_modal_end(c);
}

static void d_scroll(ash_ctx *c)
{
    ash_scrollarea_begin(c, "sc", 0, 4);
    for (int i = 0; i < 8; i++) {
        char t[16];
        snprintf(t, sizeof t, "line%d", i);
        ash_label(c, "row", t);
    }
    ash_scrollarea_end(c);
    ash_focus_on_first_present(c);
}

static ash_textarea g_ta;

static void d_textarea(ash_ctx *c)
{
    ash_textarea_widget(c, "ta", &g_ta);
    ash_steal_focus(c);
}

int main(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "tui-test", 1u << 16) == ASH_OK);
    g_a = &a;

    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_fb fb;
    ash_fb_init(&fb, &a, def);
    g_fb = &fb;

    ash_input_event down = { ASH_EV_KEY, ASH_KEY_DOWN, 0, NULL, 0, 0, 0, 0, 0 };
    ash_input_event end = { ASH_EV_KEY, ASH_KEY_END, 0, NULL, 0, 0, 0, 0, 0 };

    run("label", 12, 1, NULL, d_label);
    run("styled", 12, 1, NULL, d_styled);
    run("button", 12, 1, NULL, d_button);
    run("button_focus", 12, 1, NULL, d_button_focus);
    run("accel", 12, 1, NULL, d_accel);

    g_checked = 0;
    run("checkbox", 12, 1, NULL, d_checkbox);
    g_checked = 1;
    run("checkbox_on", 12, 1, NULL, d_checkbox);

    run("list", 12, 3, NULL, d_list);
    const ash_input_event *list_seq[2] = { NULL, &down };
    run_seq("list_down", 12, 3, list_seq, 2, d_list);

    run("border", 12, 3, NULL, d_border);
    run("modal", 20, 8, NULL, d_modal);

    run("scroll", 12, 4, NULL, d_scroll);
    const ash_input_event *scroll_seq[2] = { NULL, &end };
    run_seq("scroll_end", 12, 4, scroll_seq, 2, d_scroll);

    ash_textarea_init(&g_ta, &a, 12, 0);
    ash_textarea_insert(&g_ta, "hi there world", 14);
    run("textarea", 12, 3, NULL, d_textarea);

    ash_arena_destroy(&a);
    return ash_test_done();
}
