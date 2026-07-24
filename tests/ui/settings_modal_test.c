#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/ui/settings_modal.h"
#include "ash/ui/tui.h"
#include "ash_test.h"

#ifndef ASH_GOLDEN_DIR
#define ASH_GOLDEN_DIR "."
#endif

enum { W = 44, H = 14 };

static ash_arena          g_a;
static ash_fb             g_fb;
static ash_tui            g_tui;
static ash_settings_modal g_m;

static const char *const provider_opts[] = { "anthropic", "deepseek", "openai" };
static const char *const think_opts[] = { "off", "low", "high" };

static ash_sm_field g_fields[] = {
    { "Provider", "anthropic", ASH_SM_ENUM, provider_opts, 3 },
    { "Model", "m1", ASH_SM_TEXT, NULL, 0 },
    { "Thinking", "off", ASH_SM_ENUM, think_opts, 3 },
};

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

static void frame(const ash_input_event *ev)
{
    int guard = 0;
    do {
        ash_ctx *c = ash_tui_begin(&g_tui, W, H, ev);
        ash_settings_modal_draw(c, &g_m);
        ash_tui_end(c);
        ev = NULL;
    } while (ash_tui_settling(&g_tui) && ++guard < 32);
}

static void snap(const char *name)
{
    ash_fb_begin(&g_fb, W, H);
    ash_tui_render(&g_tui, &g_fb);
    ash_buf b;
    ash_buf_init(&b, &g_a);
    ash_fb_snapshot(&g_fb, &b);
    check_golden(name, &b);
}

int main(void)
{
    ASH_CHECK(ash_arena_create(&g_a, "settings-modal", 1u << 18) == ASH_OK);
    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_fb_init(&g_fb, &g_a, def);
    ASH_CHECK(ash_tui_init(&g_tui) == ASH_OK);
    ash_settings_modal_init(&g_m, g_fields, 3, &g_a);

    ash_input_event down = { .kind = ASH_EV_KEY, .key = ASH_KEY_DOWN };
    ash_input_event enter = { .kind = ASH_EV_KEY, .key = ASH_TK_ENTER };
    ash_input_event esc = { .kind = ASH_EV_KEY, .key = ASH_TK_ESCAPE };
    ash_input_event typ = { .kind = ASH_EV_TEXT, .text = "x", .len = 1 };

    frame(NULL);
    snap("settings");

    frame(&enter);
    ASH_CHECK(g_m.commit == 1 && g_m.commit_index == 0);
    ASH_CHECK(strcmp(g_m.commit_value, "deepseek") == 0);
    g_m.commit = 0;

    frame(&down);
    frame(&enter);
    ASH_CHECK(g_m.editing == 1 && g_m.edit_index == 1);
    snap("settings_edit");

    frame(&typ);
    frame(&enter);
    ASH_CHECK(g_m.editing == 0 && g_m.commit == 1 && g_m.commit_index == 1);
    ASH_CHECK(strcmp(g_m.commit_value, "m1x") == 0);
    g_m.commit = 0;

    frame(&down);
    frame(&enter);
    ASH_CHECK(g_m.editing == 1);
    frame(&esc);
    ASH_CHECK(g_m.editing == 0 && g_m.commit == 0 && g_m.closed == 0);

    frame(&esc);
    ASH_CHECK(g_m.closed == 1);

    ash_tui_destroy(&g_tui);
    ash_arena_destroy(&g_a);
    return ash_test_done();
}
