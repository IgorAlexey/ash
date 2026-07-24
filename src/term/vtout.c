#include "ash/term/vtout.h"

#include "ash/fb/cell.h"

void ash_vt_u32(ash_buf *b, uint32_t v)
{
    unsigned char tmp[10];
    int n = 0;
    do {
        tmp[n++] = (unsigned char)('0' + (v % 10u));
        v /= 10u;
    } while (v);
    while (n > 0)
        ash_buf_append_byte(b, tmp[--n]);
}

void ash_vt_reset(ash_buf *b)
{
    ash_buf_append_cstr(b, "\x1b[m");
}

void ash_vt_cup(ash_buf *b, int row, int col)
{
    ash_buf_append_cstr(b, "\x1b[");
    ash_vt_u32(b, (uint32_t)row);
    ash_buf_append_byte(b, ';');
    ash_vt_u32(b, (uint32_t)col);
    ash_buf_append_byte(b, 'H');
}

void ash_vt_color(ash_buf *b, int fg, ash_rgba c)
{
    if (ash_rgba_a(c) == 0) {
        ash_buf_append_cstr(b, fg ? "\x1b[39m" : "\x1b[49m");
        return;
    }
    ash_buf_append_cstr(b, fg ? "\x1b[38;2;" : "\x1b[48;2;");
    ash_vt_u32(b, ash_rgba_r(c));
    ash_buf_append_byte(b, ';');
    ash_vt_u32(b, ash_rgba_g(c));
    ash_buf_append_byte(b, ';');
    ash_vt_u32(b, ash_rgba_b(c));
    ash_buf_append_byte(b, 'm');
}

static void sgr(ash_buf *b, uint32_t code)
{
    ash_buf_append_cstr(b, "\x1b[");
    ash_vt_u32(b, code);
    ash_buf_append_byte(b, 'm');
}

void ash_vt_attr_diff(ash_buf *b, uint16_t old, uint16_t neu)
{
    static const struct {
        uint16_t bit;
        uint32_t on;
        uint32_t off;
    } map[] = {
        { ASH_ATTR_BOLD, 1, 22 },
        { ASH_ATTR_ITALIC, 3, 23 },
        { ASH_ATTR_UNDERLINE, 4, 24 },
        { ASH_ATTR_REVERSE, 7, 27 },
        { ASH_ATTR_STRIKETHROUGH, 9, 29 },
    };
    uint16_t diff = old ^ neu;
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (!(diff & map[i].bit))
            continue;
        sgr(b, (neu & map[i].bit) ? map[i].on : map[i].off);
    }
}

void ash_vt_cursor_show(ash_buf *b, int row, int col, int style)
{
    ash_vt_cup(b, row, col);
    ash_buf_append_cstr(b, "\x1b[");
    ash_vt_u32(b, (uint32_t)style);
    ash_buf_append_cstr(b, " q\x1b[?25h");
}

void ash_vt_cursor_hide(ash_buf *b)
{
    ash_buf_append_cstr(b, "\x1b[?25l");
}
