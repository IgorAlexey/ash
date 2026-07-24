#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/ui/menubar.h"
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

static void snap(ash_menubar *mb, int w, int h, const char *name)
{
    ash_fb_begin(g_fb, w, h);
    ash_menubar_render(mb, g_fb, w);
    ash_buf b;
    ash_buf_init(&b, g_a);
    ash_fb_snapshot(g_fb, &b);
    check_golden(name, &b);
}

static const ash_menu_item file_items[] = {
    { "New", 'N', "Ctrl+N", -1, 0 },
    { "Open", 'O', "Ctrl+O", -1, 0 },
    { "Save", 'S', "Ctrl+S", -1, 0 },
    { NULL, 0, NULL, 0, 1 },
    { "Exit", 'X', "Ctrl+Q", -1, 0 },
};

static const ash_menu_item view_items[] = {
    { "Word Wrap", 'W', "Alt+Z", 1, 0 },
    { "Line Numbers", 'L', NULL, 0, 0 },
};

static const ash_menu_item help_items[] = {
    { "About", 'A', NULL, -1, 0 },
};

static const ash_menu menus[] = {
    { "File", 'F', file_items, (int)(sizeof file_items / sizeof file_items[0]) },
    { "View", 'V', view_items, (int)(sizeof view_items / sizeof view_items[0]) },
    { "Help", 'H', help_items, (int)(sizeof help_items / sizeof help_items[0]) },
};

enum { NMENUS = (int)(sizeof menus / sizeof menus[0]) };

static ash_input_event key_ev(uint32_t k, uint32_t mods)
{
    ash_input_event e = { ASH_EV_KEY, k, mods, NULL, 0, 0, 0, 0, 0 };
    return e;
}

static ash_input_event mouse_ev(int x, int y, uint8_t btn, uint8_t action)
{
    ash_input_event e = { ASH_EV_MOUSE, 0, 0, NULL, 0,
                          (int16_t)x, (int16_t)y, btn, action };
    return e;
}

int main(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "menubar-test", 1u << 16) == ASH_OK);
    g_a = &a;

    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_fb fb;
    ash_fb_init(&fb, &a, def);
    g_fb = &fb;

    ash_menubar mb;
    ash_menubar_init(&mb, menus, NMENUS);

    snap(&mb, 40, 1, "menubar_closed");

    ash_input_event alt_f = key_ev('f', ASH_MOD_ALT);
    ash_menubar_event ev = ash_menubar_handle(&mb, &alt_f, 40);
    ASH_CHECK(ev.menu == -1 && mb.open == 0 && mb.hi == 0);
    snap(&mb, 40, 10, "menubar_file_open");

    ash_input_event down = key_ev(ASH_KEY_DOWN, 0);
    ash_menubar_handle(&mb, &down, 40);
    ash_menubar_handle(&mb, &down, 40);
    ASH_CHECK(mb.hi == 2);
    snap(&mb, 40, 10, "menubar_file_hi2");

    ash_input_event down2 = key_ev(ASH_KEY_DOWN, 0);
    ash_menubar_handle(&mb, &down2, 40);
    ASH_CHECK(mb.hi == 4);

    ash_input_event enter = key_ev(13, 0);
    ev = ash_menubar_handle(&mb, &enter, 40);
    ASH_CHECK(ev.menu == 0 && ev.item == 4 && mb.open == -1);

    ash_input_event alt_v = key_ev('v', ASH_MOD_ALT);
    ash_menubar_handle(&mb, &alt_v, 40);
    ASH_CHECK(mb.open == 1);
    snap(&mb, 40, 10, "menubar_view_open");

    ash_input_event right = key_ev(ASH_KEY_RIGHT, 0);
    ash_menubar_handle(&mb, &right, 40);
    ASH_CHECK(mb.open == 2);

    ash_input_event esc = key_ev(27, 0);
    ash_menubar_handle(&mb, &esc, 40);
    ASH_CHECK(mb.open == -1);

    ash_input_event click_view = mouse_ev(7, 0, ASH_MB_LEFT, ASH_MOUSE_PRESS);
    ev = ash_menubar_handle(&mb, &click_view, 40);
    ASH_CHECK(mb.open == 1 && ev.menu == -1);

    ash_input_event click_item = mouse_ev(10, 3, ASH_MB_LEFT, ASH_MOUSE_PRESS);
    ev = ash_menubar_handle(&mb, &click_item, 40);
    ASH_CHECK(ev.menu == 1 && ev.item == 1 && mb.open == -1);

    ash_menubar_handle(&mb, &click_view, 40);
    ASH_CHECK(mb.open == 1);
    ash_input_event click_off = mouse_ev(30, 5, ASH_MB_LEFT, ASH_MOUSE_PRESS);
    ash_menubar_handle(&mb, &click_off, 40);
    ASH_CHECK(mb.open == -1);

    ash_arena_destroy(&a);
    return ash_test_done();
}
