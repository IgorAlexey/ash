#include "ash/ui/menubar.h"

#include <string.h>

static ash_style bar_style(void)
{
    ash_style s = { ash_rgb(20, 20, 20), ash_rgb(200, 200, 200), 0 };
    return s;
}

static ash_style bar_sel_style(void)
{
    ash_style s = { ash_rgb(255, 255, 255), ash_rgb(0, 120, 0), 0 };
    return s;
}

static ash_style menu_style(void)
{
    ash_style s = { ash_rgb(20, 20, 20), ash_rgb(220, 220, 220), 0 };
    return s;
}

static ash_style menu_sel_style(void)
{
    ash_style s = { ash_rgb(255, 255, 255), ash_rgb(0, 120, 0), 0 };
    return s;
}

void ash_menubar_init(ash_menubar *mb, const ash_menu *menus, int nmenus)
{
    mb->menus = menus;
    mb->nmenus = nmenus;
    mb->open = -1;
    mb->hi = 0;
}

static int disp_width(const char *p, size_t len)
{
    int w = 0;
    size_t i = 0;
    while (i < len) {
        uint32_t cp;
        size_t a = ash_utf8_decode(p + i, len - i, &cp);
        if (a == 0)
            break;
        int cw = ash_char_width(cp);
        if (cw > 0)
            w += cw;
        i += a;
    }
    return w;
}

static int str_width(const char *s)
{
    return s ? disp_width(s, strlen(s)) : 0;
}

static int title_box_w(const ash_menu *m)
{
    return str_width(m->title) + 2;
}

static int menu_x(const ash_menubar *mb, int idx)
{
    int x = 0;
    for (int i = 0; i < idx; i++)
        x += title_box_w(&mb->menus[i]);
    return x;
}

static int item_content_w(const ash_menu_item *it)
{
    if (it->separator)
        return 0;
    int w = 1 + str_width(it->label) + 1;
    if (it->checked >= 0)
        w += 2;
    if (it->shortcut)
        w += 2 + str_width(it->shortcut);
    return w;
}

static int dropdown_geom(const ash_menubar *mb, int w, int *bx, int *by,
                         int *bw, int *bh)
{
    const ash_menu *m = &mb->menus[mb->open];
    int content = 0;
    for (int i = 0; i < m->nitems; i++) {
        int iw = item_content_w(&m->items[i]);
        if (iw > content)
            content = iw;
    }
    int box_w = content + 2;
    if (box_w > w)
        box_w = w;
    int x = menu_x(mb, mb->open);
    if (x + box_w > w)
        x = w - box_w;
    if (x < 0)
        x = 0;
    *bx = x;
    *by = 1;
    *bw = box_w;
    *bh = m->nitems + 2;
    return content;
}

static int upper(uint32_t k)
{
    return k >= 'a' && k <= 'z' ? (int)(k - 32) : (int)k;
}

static int first_item(const ash_menu *m)
{
    for (int i = 0; i < m->nitems; i++)
        if (!m->items[i].separator)
            return i;
    return 0;
}

static int step_item(const ash_menu *m, int from, int dir)
{
    if (m->nitems == 0)
        return 0;
    int i = from;
    for (int n = 0; n < m->nitems; n++) {
        i = (i + dir + m->nitems) % m->nitems;
        if (!m->items[i].separator)
            return i;
    }
    return from;
}

static ash_menubar_event none(void)
{
    ash_menubar_event e = { -1, -1 };
    return e;
}

static ash_menubar_event handle_mouse(ash_menubar *mb, const ash_input_event *ev,
                                      int w)
{
    if (ev->maction != ASH_MOUSE_PRESS || ev->mbutton != ASH_MB_LEFT)
        return none();
    int mx = ev->mx;
    int my = ev->my;

    if (my == 0) {
        for (int i = 0; i < mb->nmenus; i++) {
            int x0 = menu_x(mb, i);
            int x1 = x0 + title_box_w(&mb->menus[i]);
            if (mx >= x0 && mx < x1) {
                if (mb->open == i) {
                    mb->open = -1;
                } else {
                    mb->open = i;
                    mb->hi = first_item(&mb->menus[i]);
                }
                return none();
            }
        }
        mb->open = -1;
        return none();
    }

    if (mb->open < 0)
        return none();

    int bx, by, bw, bh;
    dropdown_geom(mb, w, &bx, &by, &bw, &bh);
    const ash_menu *m = &mb->menus[mb->open];
    if (mx >= bx && mx < bx + bw && my >= by + 1 && my < by + 1 + m->nitems) {
        int item = my - (by + 1);
        if (!m->items[item].separator) {
            ash_menubar_event e = { mb->open, item };
            mb->open = -1;
            return e;
        }
        return none();
    }
    mb->open = -1;
    return none();
}

static ash_menubar_event handle_key(ash_menubar *mb, const ash_input_event *ev)
{
    int key = upper(ev->key);
    int alt = (ev->mods & ASH_MOD_ALT) != 0;

    if (mb->open < 0) {
        if (alt) {
            for (int i = 0; i < mb->nmenus; i++)
                if (upper(mb->menus[i].accel) == key) {
                    mb->open = i;
                    mb->hi = first_item(&mb->menus[i]);
                    return none();
                }
        }
        return none();
    }

    const ash_menu *m = &mb->menus[mb->open];
    switch (ev->key) {
    case 27:
        mb->open = -1;
        return none();
    case ASH_KEY_LEFT:
        mb->open = (mb->open - 1 + mb->nmenus) % mb->nmenus;
        mb->hi = first_item(&mb->menus[mb->open]);
        return none();
    case ASH_KEY_RIGHT:
        mb->open = (mb->open + 1) % mb->nmenus;
        mb->hi = first_item(&mb->menus[mb->open]);
        return none();
    case ASH_KEY_UP:
        mb->hi = step_item(m, mb->hi, -1);
        return none();
    case ASH_KEY_DOWN:
        mb->hi = step_item(m, mb->hi, 1);
        return none();
    case 13:
        if (mb->hi >= 0 && mb->hi < m->nitems && !m->items[mb->hi].separator) {
            ash_menubar_event e = { mb->open, mb->hi };
            mb->open = -1;
            return e;
        }
        return none();
    default:
        break;
    }

    for (int i = 0; i < m->nitems; i++)
        if (!m->items[i].separator && upper(m->items[i].accel) == key) {
            ash_menubar_event e = { mb->open, i };
            mb->open = -1;
            return e;
        }
    if (alt) {
        for (int i = 0; i < mb->nmenus; i++)
            if (upper(mb->menus[i].accel) == key) {
                mb->open = i;
                mb->hi = first_item(&mb->menus[i]);
                return none();
            }
    }
    return none();
}

ash_menubar_event ash_menubar_handle(ash_menubar *mb, const ash_input_event *ev,
                                     int w)
{
    if (mb->nmenus == 0 || ev == NULL)
        return none();
    if (ev->kind == ASH_EV_MOUSE)
        return handle_mouse(mb, ev, w);
    if (ev->kind == ASH_EV_KEY)
        return handle_key(mb, ev);
    return none();
}

static int put_run(ash_fb *fb, int x, int y, ash_style st, const char *s, size_t len)
{
    ash_fb_put_text(fb, x, y, st, s, len);
    return x + disp_width(s, len);
}

static void put_accel(ash_fb *fb, int x, int y, ash_style base, uint32_t accel,
                      const char *text)
{
    size_t len = strlen(text);
    size_t off = len;
    if (accel >= 'A' && accel <= 'Z') {
        for (size_t i = 0; i < len; i++) {
            uint8_t ch = (uint8_t)text[i];
            if ((uint32_t)ch == accel) {
                off = i;
                break;
            }
            if ((uint32_t)(ch & ~0x20u) == accel && off == len)
                off = i;
        }
    }
    if (off >= len) {
        put_run(fb, x, y, base, text, len);
        return;
    }
    int cx = put_run(fb, x, y, base, text, off);
    ash_style u = base;
    u.attr = (uint16_t)(u.attr | ASH_ATTR_UNDERLINE);
    cx = put_run(fb, cx, y, u, text + off, 1);
    put_run(fb, cx, y, base, text + off + 1, len - off - 1);
}

static void draw_border(ash_fb *fb, int x, int y, int w, int h, ash_style st)
{
    if (w < 2 || h < 2)
        return;
    int x1 = x + w - 1;
    int y1 = y + h - 1;
    ash_fb_put_text(fb, x, y, st, "\xE2\x94\x8C", 3);
    ash_fb_put_text(fb, x1, y, st, "\xE2\x94\x90", 3);
    ash_fb_put_text(fb, x, y1, st, "\xE2\x94\x94", 3);
    ash_fb_put_text(fb, x1, y1, st, "\xE2\x94\x98", 3);
    for (int i = x + 1; i < x1; i++) {
        ash_fb_put_text(fb, i, y, st, "\xE2\x94\x80", 3);
        ash_fb_put_text(fb, i, y1, st, "\xE2\x94\x80", 3);
    }
    for (int j = y + 1; j < y1; j++) {
        ash_fb_put_text(fb, x, j, st, "\xE2\x94\x82", 3);
        ash_fb_put_text(fb, x1, j, st, "\xE2\x94\x82", 3);
    }
}

static void draw_item(ash_fb *fb, const ash_menu_item *it, int bx, int y, int bw,
                      int sel)
{
    ash_style base = sel ? menu_sel_style() : menu_style();
    ash_rect row = { bx + 1, y, bw - 2, 1 };
    ash_fb_fill_rect(fb, row, base, ' ');

    if (it->separator) {
        for (int i = bx + 1; i < bx + bw - 1; i++)
            ash_fb_put_text(fb, i, y, base, "\xE2\x94\x80", 3);
        return;
    }

    int cx = bx + 2;
    if (it->checked >= 0)
        cx = put_run(fb, cx, y, base, it->checked ? "* " : "  ", 2);
    put_accel(fb, cx, y, base, it->accel, it->label);

    if (it->shortcut) {
        int sw = str_width(it->shortcut);
        int sx = bx + bw - 2 - sw;
        put_run(fb, sx, y, base, it->shortcut, strlen(it->shortcut));
    }
}

void ash_menubar_render(const ash_menubar *mb, ash_fb *fb, int w)
{
    ash_style bar = bar_style();
    ash_rect barrow = { 0, 0, w, 1 };
    ash_fb_fill_rect(fb, barrow, bar, ' ');

    for (int i = 0; i < mb->nmenus; i++) {
        int x = menu_x(mb, i);
        ash_style st = (mb->open == i) ? bar_sel_style() : bar;
        ash_rect box = { x, 0, title_box_w(&mb->menus[i]), 1 };
        ash_fb_fill_rect(fb, box, st, ' ');
        put_accel(fb, x + 1, 0, st, mb->menus[i].accel, mb->menus[i].title);
    }

    if (mb->open < 0)
        return;

    int bx, by, bw, bh;
    dropdown_geom(mb, w, &bx, &by, &bw, &bh);
    const ash_menu *m = &mb->menus[mb->open];
    ash_style mst = menu_style();
    ash_rect fill = { bx, by, bw, bh };
    ash_fb_fill_rect(fb, fill, mst, ' ');
    draw_border(fb, bx, by, bw, bh, mst);
    for (int i = 0; i < m->nitems; i++)
        draw_item(fb, &m->items[i], bx, by + 1 + i, bw, i == mb->hi);
}
